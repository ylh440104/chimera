#include <limits.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 16384
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../third_party/stb/stb_image.h"
#pragma GCC diagnostic pop

enum { CG_SPACE = 1, CG_PROVIDER, CG_IMAGE, CG_CONTEXT, CG_COLOR };
#define CG_BYTES_LIMIT ((size_t)256 * 1024 * 1024)

typedef struct cg_object {
    struct cg_object *next;
    size_t references;
    int kind;
} cg_object;

typedef struct { cg_object object; int model; size_t components; } cg_space;
typedef struct {
    unsigned version;
    const void *(*get_pointer)(void *);
    void (*release_pointer)(void *, const void *);
    size_t (*get_bytes)(void *, void *, int64_t, size_t);
    void (*release_info)(void *);
} cg_direct_callbacks;
typedef struct {
    cg_object object;
    void *info;
    const unsigned char *data;
    size_t size;
    void (*release_data)(void *, const void *, size_t);
    cg_direct_callbacks callbacks;
    int direct, acquired, owned;
} cg_provider;
typedef struct {
    cg_object object;
    size_t width, height, bits_component, bits_pixel, stride;
    uint32_t bitmap;
    cg_space *space;
    cg_provider *provider;
    unsigned char *pixels;
    int interpolate, intent, owned;
    void *release_info;
    void (*release_data)(void *, void *);
} cg_bitmap;
typedef struct { cg_object object; cg_space *space; double values[5]; } cg_color;

static cg_object *cg_objects;

static cg_object *cg_find(const void *pointer, int kind)
{
    for (cg_object *object = cg_objects; object; object = object->next)
        if (object == pointer && (!kind || object->kind == kind)) return object;
    return NULL;
}

static void *cg_alloc(size_t size, int kind)
{
    cg_object *object = calloc(1, size);
    if (!object) return NULL;
    object->references = 1;
    object->kind = kind;
    object->next = cg_objects;
    cg_objects = object;
    return object;
}

static void *cg_retain(void *pointer)
{
    cg_object *object = cg_find(pointer, 0);
    if (!object || object->references == SIZE_MAX) return NULL;
    object->references++;
    return object;
}

static void cg_release(void *pointer)
{
    cg_object *object = cg_find(pointer, 0);
    if (!object || --object->references) return;
    cg_object **link = &cg_objects;
    while (*link != object) link = &(*link)->next;
    *link = object->next;
    if (object->kind == CG_PROVIDER) {
        cg_provider *provider = (cg_provider *)object;
        if (provider->direct) {
            if (provider->acquired && provider->callbacks.release_pointer)
                provider->callbacks.release_pointer(provider->info, provider->data);
            if (provider->callbacks.release_info) provider->callbacks.release_info(provider->info);
        } else if (provider->release_data) provider->release_data(provider->info, provider->data, provider->size);
        if (provider->owned) free((void *)provider->data);
    } else if (object->kind == CG_IMAGE || object->kind == CG_CONTEXT) {
        cg_bitmap *bitmap = (cg_bitmap *)object;
        cg_release(bitmap->provider);
        cg_release(bitmap->space);
        if (bitmap->release_data) bitmap->release_data(bitmap->release_info, bitmap->pixels);
        else if (bitmap->owned) free(bitmap->pixels);
    } else if (object->kind == CG_COLOR) {
        cg_release(((cg_color *)object)->space);
    }
    free(object);
}

static int cg_reset_body(void *context)
{
    (void)context;
    while (cg_objects) {
        cg_objects->references = 1;
        cg_release(cg_objects);
    }
    return 0;
}

void chm_graphics_reset(chm_session *session)
{
    chm_session_callback(session, cg_reset_body, NULL);
}

static void *cg_space_new(int model)
{
    cg_space *space = cg_alloc(sizeof(*space), CG_SPACE);
    if (space) { space->model = model; space->components = model == 1 ? 3 : 1; }
    return space;
}

static void *CGColorSpaceCreateDeviceRGB_impl(void) { return cg_space_new(1); }
static void *CGColorSpaceCreateDeviceGray_impl(void) { return cg_space_new(0); }
static int CGColorSpaceGetModel_impl(void *pointer)
{
    cg_space *space = (cg_space *)cg_find(pointer, CG_SPACE);
    return space ? space->model : -1;
}
static size_t CGColorSpaceGetNumberOfComponents_impl(void *pointer)
{
    cg_space *space = (cg_space *)cg_find(pointer, CG_SPACE);
    return space ? space->components : 0;
}

static void *CGColorCreate_impl(void *pointer, const double *components)
{
    cg_space *space = (cg_space *)cg_find(pointer, CG_SPACE);
    if (!space || !components) return NULL;
    cg_color *color = cg_alloc(sizeof(*color), CG_COLOR);
    if (!color) return NULL;
    color->space = cg_retain(space);
    memcpy(color->values, components, (space->components + 1) * sizeof(double));
    return color;
}
static const double *CGColorGetComponents_impl(void *pointer)
{
    cg_color *color = (cg_color *)cg_find(pointer, CG_COLOR);
    return color ? color->values : NULL;
}
static double CGColorGetAlpha_impl(void *pointer)
{
    cg_color *color = (cg_color *)cg_find(pointer, CG_COLOR);
    return color ? color->values[color->space->components] : 0;
}
static size_t CGColorGetNumberOfComponents_impl(void *pointer)
{
    cg_color *color = (cg_color *)cg_find(pointer, CG_COLOR);
    return color ? color->space->components + 1 : 0;
}

static void *CGDataProviderCreateWithData_impl(void *info, const void *data, size_t size,
                                               void (*release_data)(void *, const void *, size_t))
{
    if ((!data && size) || size > CG_BYTES_LIMIT) return NULL;
    cg_provider *provider = cg_alloc(sizeof(*provider), CG_PROVIDER);
    if (!provider) return NULL;
    provider->info = info;
    provider->data = data;
    provider->size = size;
    provider->release_data = release_data;
    return provider;
}

static void *CGDataProviderCreateWithCFData_impl(void *data)
{
    const void *bytes;
    size_t length;
    if (!chm_foundation_data_bytes(data, &bytes, &length) || length > CG_BYTES_LIMIT) return NULL;
    unsigned char *copy = malloc(length ? length : 1);
    if (!copy) return NULL;
    if (length) memcpy(copy, bytes, length);
    cg_provider *provider = CGDataProviderCreateWithData_impl(NULL, copy, length, NULL);
    if (!provider) { free(copy); return NULL; }
    provider->owned = 1;
    return provider;
}

static void *CGDataProviderCreateDirect_impl(void *info, int64_t size, const cg_direct_callbacks *callbacks)
{
    if (size < 0 || (uint64_t)size > CG_BYTES_LIMIT || !callbacks || callbacks->version ||
        (!callbacks->get_pointer && !callbacks->get_bytes) || (callbacks->release_pointer && !callbacks->get_pointer)) return NULL;
    cg_provider *provider = cg_alloc(sizeof(*provider), CG_PROVIDER);
    if (!provider) return NULL;
    provider->info = info;
    provider->size = (size_t)size;
    provider->callbacks = *callbacks;
    provider->direct = 1;
    return provider;
}

static int cg_provider_read(cg_provider *provider, void *destination, size_t offset, size_t length)
{
    if (!provider || offset > provider->size || length > provider->size - offset) return 0;
    if (!length) return 1;
    if (provider->direct && !provider->acquired && provider->callbacks.get_pointer) {
        provider->data = provider->callbacks.get_pointer(provider->info);
        provider->acquired = provider->data != NULL;
    }
    if (provider->data) { memcpy(destination, provider->data + offset, length); return 1; }
    if (!provider->direct || !provider->callbacks.get_bytes) return 0;
    size_t done = 0;
    while (done < length) {
        size_t n = provider->callbacks.get_bytes(provider->info, (unsigned char *)destination + done, (int64_t)(offset + done), length - done);
        if (!n || n > length - done) return 0;
        done += n;
    }
    return 1;
}

static void *CGDataProviderCopyData_impl(void *pointer)
{
    cg_provider *provider = (cg_provider *)cg_find(pointer, CG_PROVIDER);
    if (!provider) return NULL;
    unsigned char *bytes = malloc(provider->size ? provider->size : 1);
    if (!bytes) return NULL;
    void *data = cg_provider_read(provider, bytes, 0, provider->size) ? chm_foundation_data(bytes, provider->size) : NULL;
    free(bytes);
    return data;
}

static size_t cg_pixel_size(cg_space *space, size_t bits, uint32_t bitmap)
{
    unsigned alpha = bitmap & 31, order = bitmap & 0x7000;
    if (!space || bits != 8 || (bitmap & ~UINT32_C(0x701f))) return 0;
    if (space->model == 1) {
        if (!alpha) return order ? 0 : 3;
        if (alpha <= 6 && (order == 0 || order == 0x2000 || order == 0x4000)) return 4;
    } else if (space->model == 0 && !order) {
        if (!alpha) return 1;
        if (alpha <= 6) return 2;
    }
    return 0;
}

static cg_bitmap *cg_bitmap_new(int kind, size_t width, size_t height, size_t bits, size_t stride, void *space_pointer, uint32_t bitmap)
{
    cg_space *space = (cg_space *)cg_find(space_pointer, CG_SPACE);
    size_t pixel = cg_pixel_size(space, bits, bitmap);
    if (!pixel || !width || !height || width > 16384 || height > 16384) return NULL;
    if (!stride) stride = width * pixel;
    if (stride < width * pixel || stride > CG_BYTES_LIMIT / height) return NULL;
    cg_bitmap *out = cg_alloc(sizeof(*out), kind);
    if (!out) return NULL;
    out->width = width; out->height = height; out->bits_component = bits;
    out->bits_pixel = pixel * 8; out->stride = stride; out->bitmap = bitmap;
    out->space = cg_retain(space);
    return out;
}

static void *CGImageCreate_impl(size_t width, size_t height, size_t bits_component, size_t bits_pixel,
                                size_t stride, void *space, uint32_t bitmap, void *provider_pointer,
                                const double *decode, _Bool interpolate, int intent)
{
    cg_provider *provider = (cg_provider *)cg_find(provider_pointer, CG_PROVIDER);
    if (!provider || decode || intent < 0 || intent > 4) return NULL;
    cg_bitmap *image = cg_bitmap_new(CG_IMAGE, width, height, bits_component, stride, space, bitmap);
    if (!image) return NULL;
    if (image->bits_pixel != bits_pixel || image->stride * height > provider->size) { cg_release(image); return NULL; }
    image->pixels = malloc(image->stride * height);
    image->owned = 1;
    if (!image->pixels || !cg_provider_read(provider, image->pixels, 0, image->stride * height)) { cg_release(image); return NULL; }
    image->provider = cg_retain(provider);
    image->interpolate = interpolate;
    image->intent = intent;
    return image;
}

void *chm_cg_image_create_stack(size_t width, size_t height, size_t bits_component, size_t bits_pixel,
                               size_t stride, void *space, uint32_t bitmap, void *provider, const unsigned char *stack)
{
    const double *decode;
    int intent;
    memcpy(&decode, stack, sizeof(decode));
    memcpy(&intent, stack + 12, sizeof(intent));
    return CGImageCreate_impl(width, height, bits_component, bits_pixel, stride, space, bitmap, provider, decode, stack[8] != 0, intent);
}

#if defined(__aarch64__)
extern void *chm_cg_image_create_entry(void);
#else
#define chm_cg_image_create_entry CGImageCreate_impl
#endif

static void *cg_decode_image(void *pointer, const double *decode, _Bool interpolate, int intent, int png)
{
    cg_provider *provider = (cg_provider *)cg_find(pointer, CG_PROVIDER);
    if (!provider || decode || provider->size > INT_MAX || provider->size < 8 || intent < 0 || intent > 4) return NULL;
    unsigned char *bytes = malloc(provider->size);
    if (!bytes) return NULL;
    if (!cg_provider_read(provider, bytes, 0, provider->size) ||
        (png ? memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) != 0 : bytes[0] != 0xff || bytes[1] != 0xd8)) { free(bytes); return NULL; }
    int width, height, channels;
    if (!stbi_info_from_memory(bytes, (int)provider->size, &width, &height, &channels) || width <= 0 || height <= 0 ||
        (size_t)width > CG_BYTES_LIMIT / 4 / (size_t)height) { free(bytes); return NULL; }
    unsigned char *pixels = stbi_load_from_memory(bytes, (int)provider->size, &width, &height, &channels, 4);
    free(bytes);
    if (!pixels) return NULL;
    void *space = CGColorSpaceCreateDeviceRGB_impl();
    cg_bitmap *image = cg_bitmap_new(CG_IMAGE, (size_t)width, (size_t)height, 8, (size_t)width * 4, space, 3);
    cg_release(space);
    if (!image) { stbi_image_free(pixels); return NULL; }
    image->pixels = pixels; image->owned = 1; image->interpolate = interpolate; image->intent = intent;
    image->provider = cg_retain(provider);
    return image;
}

static void *CGImageCreateWithPNGDataProvider_impl(void *provider, const double *decode, _Bool interpolate, int intent)
{
    return cg_decode_image(provider, decode, interpolate, intent, 1);
}
static void *CGImageCreateWithJPEGDataProvider_impl(void *provider, const double *decode, _Bool interpolate, int intent)
{
    return cg_decode_image(provider, decode, interpolate, intent, 0);
}

static void *CGBitmapContextCreateWithData_impl(void *data, size_t width, size_t height, size_t bits, size_t stride,
                                              void *space, uint32_t bitmap, void (*release_data)(void *, void *), void *info)
{
    cg_bitmap *context = cg_bitmap_new(CG_CONTEXT, width, height, bits, stride, space, bitmap);
    if (!context) return NULL;
    context->pixels = data ? data : calloc(height, context->stride);
    context->owned = data == NULL;
    if (!context->pixels) { cg_release(context); return NULL; }
    context->release_data = release_data;
    context->release_info = info;
    return context;
}
static void *CGBitmapContextCreate_impl(void *data, size_t width, size_t height, size_t bits, size_t stride, void *space, uint32_t bitmap)
{
    return CGBitmapContextCreateWithData_impl(data, width, height, bits, stride, space, bitmap, NULL, NULL);
}

static void *cg_bitmap_copy(cg_bitmap *source)
{
    if (!source) return NULL;
    cg_bitmap *image = cg_bitmap_new(CG_IMAGE, source->width, source->height, source->bits_component, source->stride, source->space, source->bitmap);
    if (!image) return NULL;
    image->pixels = malloc(source->stride * source->height);
    image->owned = 1;
    if (!image->pixels) { cg_release(image); return NULL; }
    memcpy(image->pixels, source->pixels, source->stride * source->height);
    image->interpolate = source->interpolate; image->intent = source->intent;
    image->provider = cg_retain(source->provider);
    return image;
}
static void *CGBitmapContextCreateImage_impl(void *pointer) { return cg_bitmap_copy((cg_bitmap *)cg_find(pointer, CG_CONTEXT)); }
static void *CGImageCreateCopy_impl(void *pointer) { return cg_bitmap_copy((cg_bitmap *)cg_find(pointer, CG_IMAGE)); }
static void *CGBitmapContextGetData_impl(void *pointer)
{
    cg_bitmap *context = (cg_bitmap *)cg_find(pointer, CG_CONTEXT);
    return context ? context->pixels : NULL;
}

static cg_bitmap *cg_bitmap_find(void *pointer)
{
    cg_object *object = cg_find(pointer, 0);
    return object && (object->kind == CG_IMAGE || object->kind == CG_CONTEXT) ? (cg_bitmap *)object : NULL;
}
static size_t cg_width(void *pointer) { cg_bitmap *b = cg_bitmap_find(pointer); return b ? b->width : 0; }
static size_t cg_height(void *pointer) { cg_bitmap *b = cg_bitmap_find(pointer); return b ? b->height : 0; }
static size_t cg_bits_component(void *pointer) { cg_bitmap *b = cg_bitmap_find(pointer); return b ? b->bits_component : 0; }
static size_t cg_bits_pixel(void *pointer) { cg_bitmap *b = cg_bitmap_find(pointer); return b ? b->bits_pixel : 0; }
static size_t cg_stride(void *pointer) { cg_bitmap *b = cg_bitmap_find(pointer); return b ? b->stride : 0; }
static uint32_t cg_bitmap_info(void *pointer) { cg_bitmap *b = cg_bitmap_find(pointer); return b ? b->bitmap : 0; }
static unsigned cg_alpha_info(void *pointer) { return cg_bitmap_info(pointer) & 31; }
static void *cg_color_space(void *pointer) { cg_bitmap *b = cg_bitmap_find(pointer); return b ? b->space : NULL; }
static _Bool CGImageGetShouldInterpolate_impl(void *pointer) { cg_bitmap *b = (cg_bitmap *)cg_find(pointer, CG_IMAGE); return b && b->interpolate; }
static int CGImageGetRenderingIntent_impl(void *pointer) { cg_bitmap *b = (cg_bitmap *)cg_find(pointer, CG_IMAGE); return b ? b->intent : 0; }

static void cg_pixel_read(cg_bitmap *bitmap, size_t x, size_t y, double rgba[4])
{
    size_t count = bitmap->bits_pixel / 8;
    const unsigned char *p = bitmap->pixels + y * bitmap->stride + x * count;
    unsigned alpha = bitmap->bitmap & 31;
    unsigned char ordered[4];
    for (size_t i = 0; i < count; i++) ordered[i] = p[(bitmap->bitmap & 0x7000) == 0x2000 ? count - i - 1 : i];
    int first = alpha == 2 || alpha == 4 || alpha == 6;
    rgba[3] = alpha >= 1 && alpha <= 4 ? ordered[first ? 0 : count - 1] / 255.0 : 1;
    for (size_t i = 0; i < 3; i++) rgba[i] = ordered[first + (bitmap->space->model == 1 ? i : 0)] / 255.0;
    if (alpha != 1 && alpha != 2) for (int i = 0; i < 3; i++) rgba[i] *= rgba[3];
}

static unsigned char cg_byte(double value)
{
    return (unsigned char)(fmin(1, fmax(0, value)) * 255 + 0.5);
}

static void cg_pixel_write(cg_bitmap *bitmap, size_t x, size_t y, const double rgba[4])
{
    size_t count = bitmap->bits_pixel / 8;
    unsigned char *p = bitmap->pixels + y * bitmap->stride + x * count;
    unsigned alpha = bitmap->bitmap & 31;
    int first = alpha == 2 || alpha == 4 || alpha == 6;
    double divisor = alpha == 1 || alpha == 2 || rgba[3] <= 0 ? 1 : rgba[3];
    unsigned char ordered[4] = {255, 255, 255, 255};
    if (bitmap->space->model == 1) for (size_t i = 0; i < 3; i++) ordered[first + i] = cg_byte(rgba[i] / divisor);
    else ordered[first] = cg_byte((rgba[0] * 0.2126 + rgba[1] * 0.7152 + rgba[2] * 0.0722) / divisor);
    if (alpha >= 1 && alpha <= 4) ordered[first ? 0 : count - 1] = cg_byte(rgba[3]);
    for (size_t i = 0; i < count; i++) p[(bitmap->bitmap & 0x7000) == 0x2000 ? count - i - 1 : i] = ordered[i];
}

static void CGContextDrawImage_impl(void *context_pointer, cg_rect rect, void *image_pointer)
{
    cg_bitmap *context = (cg_bitmap *)cg_find(context_pointer, CG_CONTEXT);
    cg_bitmap *image = (cg_bitmap *)cg_find(image_pointer, CG_IMAGE);
    if (!context || !image || !isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.w) || !isfinite(rect.h)) return;
    if (rect.w < 0) { rect.x += rect.w; rect.w = -rect.w; }
    if (rect.h < 0) { rect.y += rect.h; rect.h = -rect.h; }
    if (rect.w <= 0 || rect.h <= 0 || !isfinite(rect.x + rect.w) || !isfinite(rect.y + rect.h)) return;
    size_t left = (size_t)fmin(context->width, fmax(0, ceil(rect.x - 0.5)));
    size_t right = (size_t)fmin(context->width, fmax(0, ceil(rect.x + rect.w - 0.5)));
    size_t bottom = (size_t)fmin(context->height, fmax(0, ceil(rect.y - 0.5)));
    size_t top = (size_t)fmin(context->height, fmax(0, ceil(rect.y + rect.h - 0.5)));
    for (size_t y = bottom; y < top; y++) for (size_t x = left; x < right; x++) {
        double u = (x + 0.5 - rect.x) / rect.w * image->width - 0.5;
        double v = (1 - (y + 0.5 - rect.y) / rect.h) * image->height - 0.5;
        double source[4] = {0}, destination[4];
        if (image->interpolate) {
            double fx = floor(u), fy = floor(v), dx = u - fx, dy = v - fy;
            for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
                size_t sx = (size_t)fmin(image->width - 1, fmax(0, fx + i));
                size_t sy = (size_t)fmin(image->height - 1, fmax(0, fy + j));
                double sample[4], weight = (i ? dx : 1 - dx) * (j ? dy : 1 - dy);
                cg_pixel_read(image, sx, sy, sample);
                for (int k = 0; k < 4; k++) source[k] += sample[k] * weight;
            }
        } else {
            size_t sx = (size_t)fmin(image->width - 1, fmax(0, floor(u + 0.5)));
            size_t sy = (size_t)fmin(image->height - 1, fmax(0, floor(v + 0.5)));
            cg_pixel_read(image, sx, sy, source);
        }
        size_t row = context->height - 1 - y;
        cg_pixel_read(context, x, row, destination);
        for (int k = 0; k < 3; k++) destination[k] = source[k] + destination[k] * (1 - source[3]);
        destination[3] = source[3] + destination[3] * (1 - source[3]);
        cg_pixel_write(context, x, row, destination);
    }
}

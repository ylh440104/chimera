typedef unsigned long Size;
typedef struct { double x, y, w, h; } Rect;
typedef struct {
    unsigned version;
    const void *(*get_pointer)(void *);
    void (*release_pointer)(void *, const void *);
    Size (*get_bytes)(void *, void *, long long, Size);
    void (*release_info)(void *);
} Direct;
typedef struct {
    long version;
    const void *(*retain)(void *, const void *);
    void (*release)(void *, const void *);
    void *(*description)(const void *);
    unsigned char (*equal)(const void *, const void *);
} ArrayCallbacks;
extern const ArrayCallbacks kCFTypeArrayCallBacks;
extern void *CFDataCreate(void *, const unsigned char *, long);
extern long CFDataGetLength(void *);
extern const unsigned char *CFDataGetBytePtr(void *);
extern void *CFRetain(void *);
extern void CFRelease(void *);
extern unsigned char CFEqual(void *, void *);
extern Size CFHash(void *);
extern void *CFArrayCreate(void *, const void **, long, const ArrayCallbacks *);
extern long CFArrayGetCount(void *);
extern const void *CFArrayGetValueAtIndex(void *, long);
extern void *CGColorSpaceCreateDeviceRGB(void);
extern void *CGColorSpaceCreateDeviceGray(void);
extern int CGColorSpaceGetModel(void *);
extern Size CGColorSpaceGetNumberOfComponents(void *);
extern void CGColorSpaceRelease(void *);
extern void *CGColorCreate(void *, const double *);
extern double CGColorGetAlpha(void *);
extern const double *CGColorGetComponents(void *);
extern Size CGColorGetNumberOfComponents(void *);
extern void CGColorRelease(void *);
extern void *CGDataProviderCreateWithData(void *, const void *, Size, void (*)(void *, const void *, Size));
extern void *CGDataProviderCreateDirect(void *, long long, const Direct *);
extern void *CGDataProviderCreateWithCFData(void *);
extern void *CGDataProviderCopyData(void *);
extern void CGDataProviderRelease(void *);
extern void *CGImageCreate(Size, Size, Size, Size, Size, void *, unsigned, void *, const double *, _Bool, int);
extern void *CGImageCreateCopy(void *);
extern void *CGImageCreateWithPNGDataProvider(void *, const double *, _Bool, int);
extern void *CGImageCreateWithJPEGDataProvider(void *, const double *, _Bool, int);
extern Size CGImageGetWidth(void *), CGImageGetHeight(void *), CGImageGetBitsPerComponent(void *), CGImageGetBitsPerPixel(void *), CGImageGetBytesPerRow(void *);
extern unsigned CGImageGetBitmapInfo(void *);
extern void *CGImageGetColorSpace(void *);
extern _Bool CGImageGetShouldInterpolate(void *);
extern int CGImageGetRenderingIntent(void *);
extern void CGImageRelease(void *);
extern void *CGBitmapContextCreate(void *, Size, Size, Size, Size, void *, unsigned);
extern void *CGBitmapContextCreateWithData(void *, Size, Size, Size, Size, void *, unsigned, void (*)(void *, void *), void *);
extern void *CGBitmapContextCreateImage(void *);
extern unsigned char *CGBitmapContextGetData(void *);
extern Size CGBitmapContextGetBytesPerRow(void *);
extern void CGContextDrawImage(void *, Rect, void *);
extern void CGContextRelease(void *);
extern int memcmp(const void *, const void *, Size);
extern void *memcpy(void *, const void *, Size);
extern void *memset(void *, int, Size);
extern int open(const char *, int, ...), close(int);
extern long read(int, void *, Size);

static const unsigned char pixels[16] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
static int released, pointer_gets, pointer_releases, info_releases, context_releases, bad_callback;
static void release_data(void *info, const void *data, Size size)
{
    if (info != &released || data != pixels || size != 16) bad_callback++;
    released++;
}
static const void *get_pointer(void *info) { if (info != pixels) bad_callback++; pointer_gets++; return pixels; }
static void release_pointer(void *info, const void *pointer)
{
    if (info != pixels || pointer != pixels) bad_callback++;
    pointer_releases++;
}
static void release_info(void *info) { if (info != pixels) bad_callback++; info_releases++; }
static Size get_bytes(void *info, void *destination, long long position, Size count)
{
    if (info != pixels || position < 0 || position > 16 || count > 16 - (Size)position) { bad_callback++; return 0; }
    if (count > 3) count = 3;
    memcpy(destination, pixels + position, count);
    return count;
}
static void release_context(void *info, void *data) { if (info != data) bad_callback++; context_releases++; }

static int encoded(const char *path, int png)
{
    unsigned char bytes[8192];
    int fd = open(path, 0);
    if (fd < 0) return 1;
    long length = read(fd, bytes, sizeof(bytes));
    close(fd);
    if (length <= 0) return 2;
    void *data = CFDataCreate(0, bytes, length);
    void *provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    if (!provider) return 3;
    void *image = png ? CGImageCreateWithPNGDataProvider(provider, 0, 0, 2) : CGImageCreateWithJPEGDataProvider(provider, 0, 0, 2);
    if (!image || CGImageGetWidth(image) != 2 || CGImageGetHeight(image) != 2 || CGImageGetRenderingIntent(image) != 2) return 4;
    if (png ? CGImageCreateWithJPEGDataProvider(provider, 0, 0, 0) != 0 : CGImageCreateWithPNGDataProvider(provider, 0, 0, 0) != 0) return 5;
    CGDataProviderRelease(provider);
    void *rgb = CGColorSpaceCreateDeviceRGB();
    unsigned char output[16] = {0};
    void *context = CGBitmapContextCreate(output, 2, 2, 8, 8, rgb, 3);
    CGContextDrawImage(context, (Rect){0,0,2,2}, image);
    if (png) { if (memcmp(output, pixels, 16)) return 6; }
    else for (int i = 0; i < 16; i++) {
        int delta = output[i] - pixels[i];
        if (delta < -12 || delta > 12) return 7;
    }
    CGContextRelease(context); CGImageRelease(image); CGColorSpaceRelease(rgb);
    return 0;
}

int main(void)
{
    void *rgb = CGColorSpaceCreateDeviceRGB(), *gray = CGColorSpaceCreateDeviceGray();
    if (!rgb || !gray || CGColorSpaceGetModel(rgb) != 1 || CGColorSpaceGetModel(gray) != 0 || CGColorSpaceGetNumberOfComponents(rgb) != 3) return 1;
    double components[] = {0.25, 0.5, 0.75, 0.125};
    void *color = CGColorCreate(rgb, components);
    if (!color || CGColorGetAlpha(color) != 0.125 || CGColorGetNumberOfComponents(color) != 4 || memcmp(CGColorGetComponents(color), components, sizeof(components))) return 2;
    CGColorRelease(color);
    void *data = CFDataCreate(0, pixels, 16), *same = CFDataCreate(0, pixels, 16);
    if (!data || CFDataGetLength(data) != 16 || memcmp(CFDataGetBytePtr(data), pixels, 16) || !CFEqual(data, same) || CFHash(data) != CFHash(same)) return 3;
    CFRelease(same);
    const void *values[] = {data};
    void *array = CFArrayCreate(0, values, 1, &kCFTypeArrayCallBacks);
    CFRelease(data);
    if (!array || CFArrayGetCount(array) != 1 || CFDataGetLength((void *)CFArrayGetValueAtIndex(array, 0)) != 16) return 4;
    void *provider = CGDataProviderCreateWithCFData((void *)CFArrayGetValueAtIndex(array, 0));
    CFRelease(array);
    same = CGDataProviderCopyData(provider);
    if (!same || CFDataGetLength(same) != 16 || memcmp(CFDataGetBytePtr(same), pixels, 16)) return 5;
    CFRelease(same); CGDataProviderRelease(provider);
    provider = CGDataProviderCreateWithData(&released, pixels, 16, release_data);
    void *image = CGImageCreate(2, 2, 8, 32, 8, rgb, 3, provider, 0, 0, 4);
    if (!image || CGImageGetRenderingIntent(image) != 4 || CGImageGetShouldInterpolate(image) || CGImageGetWidth(image) != 2 || CGImageGetHeight(image) != 2 || CGImageGetBitsPerComponent(image) != 8 || CGImageGetBitsPerPixel(image) != 32 || CGImageGetBytesPerRow(image) != 8 || CGImageGetBitmapInfo(image) != 3 || CGImageGetColorSpace(image) != rgb) return 6;
    CGDataProviderRelease(provider);
    if (released) return 7;
    unsigned char output[24]; memset(output, 0xcc, sizeof(output));
    memset(output, 0, 8); memset(output + 12, 0, 8);
    void *context = CGBitmapContextCreate(output, 2, 2, 8, 12, rgb, 0x2002);
    if (!context || CGBitmapContextGetData(context) != output || CGBitmapContextGetBytesPerRow(context) != 12) return 8;
    CGContextDrawImage(context, (Rect){0,0,2,2}, image);
    const unsigned char expected[] = {0,0,255,255, 0,255,0,255, 0xcc,0xcc,0xcc,0xcc, 255,0,0,255, 255,255,255,255, 0xcc,0xcc,0xcc,0xcc};
    if (memcmp(output, expected, 24)) return 9;
    void *snapshot = CGBitmapContextCreateImage(context), *copy = CGImageCreateCopy(image);
    memset(output, 0, sizeof(output));
    CGContextDrawImage(context, (Rect){0,0,2,2}, snapshot);
    if (memcmp(output, expected, 8) || memcmp(output + 12, expected + 12, 8)) return 10;
    CGImageRelease(snapshot); CGImageRelease(copy); CGImageRelease(image);
    if (released != 1) return 11;
    CGContextRelease(context);
    Direct callbacks = {0, get_pointer, release_pointer, 0, release_info};
    provider = CGDataProviderCreateDirect((void *)pixels, 16, &callbacks);
    image = CGImageCreate(2, 2, 8, 32, 8, rgb, 3, provider, 0, 1, 1);
    if (!image || !CGImageGetShouldInterpolate(image) || CGImageGetRenderingIntent(image) != 1 || pointer_gets != 1) return 12;
    CGDataProviderRelease(provider); CGImageRelease(image);
    if (pointer_releases != 1 || info_releases != 1) return 13;
    callbacks = (Direct){0, 0, 0, get_bytes, release_info};
    provider = CGDataProviderCreateDirect((void *)pixels, 16, &callbacks);
    same = CGDataProviderCopyData(provider);
    if (!same || memcmp(CFDataGetBytePtr(same), pixels, 16)) return 14;
    CFRelease(same); CGDataProviderRelease(provider);
    if (info_releases != 2) return 15;
    unsigned char translucent[] = {255,0,0,128}, target[] = {0,0,255,255};
    provider = CGDataProviderCreateWithData(0, translucent, 4, 0);
    image = CGImageCreate(1, 1, 8, 32, 4, rgb, 3, provider, 0, 0, 0);
    context = CGBitmapContextCreateWithData(target, 1, 1, 8, 4, rgb, 3, release_context, target);
    CGContextDrawImage(context, (Rect){0,0,1,1}, image);
    if (memcmp(target, (unsigned char[]){128,0,127,255}, 4)) return 16;
    CGContextRelease(context); CGImageRelease(image); CGDataProviderRelease(provider);
    if (context_releases != 1 || bad_callback) return 17;
    if (CGBitmapContextCreate(0, ~0ul, 2, 8, 0, rgb, 3) || CGBitmapContextCreate(0, 2, 2, 8, 7, rgb, 3) || CGBitmapContextCreate(0, 2, 2, 16, 8, rgb, 3)) return 18;
    provider = CGDataProviderCreateWithData(0, pixels, 3, 0);
    if (CGImageCreate(2, 2, 8, 32, 8, rgb, 3, provider, 0, 0, 0) || CGImageCreateWithPNGDataProvider(provider, 0, 0, 0)) return 19;
    CGDataProviderRelease(provider);
    int rc = encoded("/var/mobile/graphics.png", 1);
    if (rc) return 30 + rc;
    rc = encoded("/var/mobile/graphics.jpg", 0);
    if (rc) return 40 + rc;
    CGColorSpaceRelease(rgb); CGColorSpaceRelease(gray);
    return 0;
}
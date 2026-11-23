typedef struct { vector_object object; double scale; } ui_image_format;
typedef struct { void *isa; ui_point size; double scale; } ui_image_renderer;
typedef struct vector_context {
    vector_object object;
    struct vector_context *previous;
    pthread_t thread;
    ui_point size;
    ui_symbol_layer *layers;
    size_t count, capacity;
    int failed;
} vector_context;

static vector_context *active_contexts;
static void invoke_one(void *block, void *argument);

static void *renderer_format(void *cls, const char *sel)
{
    (void)cls; (void)sel;
    ui_image_format *format = vector_alloc("UIGraphicsImageRendererFormat", sizeof(*format));
    if (format) format->scale = g_scale;
    return format;
}

static double format_scale(ui_image_format *format, const char *sel)
{
    (void)sel;
    return format && format->scale > 0 ? format->scale : g_scale;
}

static void format_set_scale(ui_image_format *format, const char *sel, double scale)
{
    (void)sel;
    if (isfinite(scale) && scale >= 0) format->scale = scale;
}

static void *renderer_init(ui_image_renderer *renderer, const char *sel, ui_point size, void *format)
{
    (void)sel;
    if (!isfinite(size.x) || !isfinite(size.y) || size.x <= 0 || size.y <= 0) return NULL;
    if (format && !chm_objc_is_kind(format, "UIGraphicsImageRendererFormat")) return NULL;
    renderer->size = size;
    renderer->scale = format_scale(format, NULL);
    return renderer;
}

static void *renderer_init_size(ui_image_renderer *renderer, const char *sel, ui_point size)
{
    return renderer_init(renderer, sel, size, NULL);
}

static int record_layer(vector_context *context, ui_symbol_layer layer)
{
    if (context->count == MAX_VECTOR_LAYERS) return 0;
    if (context->count == context->capacity) {
        size_t capacity = context->capacity ? context->capacity * 2 : 4;
        ui_symbol_layer *layers = realloc(context->layers, capacity * sizeof(*layers));
        if (!layers) return 0;
        context->layers = layers;
        context->capacity = capacity;
    }
    context->layers[context->count++] = layer;
    return 1;
}

static void image_draw_rect(void *self, const char *sel, ui_rect rect)
{
    (void)sel;
    ui_symbol_image *source = symbol_image(self);
    vector_context *context = active_contexts;
    while (context && !pthread_equal(context->thread, pthread_self())) context = context->previous;
    if (!context || context->failed || !source || !source->count ||
        !isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.w) || !isfinite(rect.h) || rect.w <= 0 || rect.h <= 0) return;
    ui_rect frame = {rect.x / context->size.x, rect.y / context->size.y,
                     rect.w / context->size.x, rect.h / context->size.y};
    for (size_t i = 0; i < source->count; i++) {
        ui_symbol_layer layer = source->layers[i];
        layer.rect = vector_project(frame, layer.rect);
        layer.clip = vector_clip(unit_rect, vector_project(frame, layer.clip));
        if (layer.clip.w <= 0 || layer.clip.h <= 0) continue;
        if (!record_layer(context, layer)) { context->failed = 1; break; }
    }
}

static void *renderer_image(ui_image_renderer *renderer, const char *sel, void *actions)
{
    (void)sel;
    if (!actions || renderer->size.x <= 0 || renderer->size.y <= 0) return NULL;
    vector_context *context = vector_alloc("UIGraphicsImageRendererContext", sizeof(*context));
    if (!context) return NULL;
    context->thread = pthread_self();
    context->size = renderer->size;
    context->previous = active_contexts;
    active_contexts = context;
    invoke_one(actions, context);
    vector_context **link = &active_contexts;
    while (*link != context) link = &(*link)->previous;
    *link = context->previous;
    ui_symbol_image *image = context->failed ? NULL : vector_image(context->size, renderer->scale, 0, context->layers, context->count);
    free(context->layers);
    context->layers = NULL;
    return image;
}

static void renderer_reset(void)
{
    while (active_contexts) {
        free(active_contexts->layers);
        active_contexts = active_contexts->previous;
    }
}
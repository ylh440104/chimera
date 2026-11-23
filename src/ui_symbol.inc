typedef struct {
    const char *name, *path;
    void *standard;
} ui_symbol;

static ui_symbol symbol_catalog[] = {
    {"square.grid.2x2", "M2 2H11V11H2ZM4 4V9H9V4ZM13 2H22V11H13ZM15 4V9H20V4ZM2 13H11V22H2ZM4 15V20H9V15ZM13 13H22V22H13ZM15 15V20H20V15Z", NULL},
    {"square.grid.2x2.fill", "M2 2H11V11H2ZM13 2H22V11H13ZM2 13H11V22H2ZM13 13H22V22H13Z", NULL},
    {"person.crop.circle", "M12 1A11 11 0 1 0 12 23A11 11 0 1 0 12 1ZM12 3A9 9 0 0 1 19.4 17.1Q12 10 4.6 17.1A9 9 0 0 1 12 3ZM6 18.6Q12 12.7 18 18.6Q12 23.5 6 18.6ZM12 5.5A3.5 3.5 0 1 0 12 12.5A3.5 3.5 0 1 0 12 5.5Z", NULL},
    {"person.crop.circle.fill", "M12 1A11 11 0 1 0 12 23A11 11 0 1 0 12 1ZM12 5A3.6 3.6 0 1 1 12 12.2A3.6 3.6 0 1 1 12 5ZM5.5 18.4Q12 9.5 18.5 18.4Q12 24 5.5 18.4Z", NULL},
    {"gearshape", "M9 2H15L15.7 4.6L18.2 5.9L20.7 5.4L23 9.5L21 11.5V13L23 15L20.7 19L18 18.5L15.5 20L15 22H9L8.4 19.6L6 18.2L3.3 19L1 15L3 13V11L1 9L3.4 5L6 5.8L8.4 4.5ZM12 5A7 7 0 1 1 12 19A7 7 0 1 1 12 5ZM12 8A4 4 0 1 0 12 16A4 4 0 1 0 12 8ZM12 10A2 2 0 1 1 12 14A2 2 0 1 1 12 10Z", NULL},
    {"gearshape.fill", "M9 2H15L15.7 4.6L18.2 5.9L20.7 5.4L23 9.5L21 11.5V13L23 15L20.7 19L18 18.5L15.5 20L15 22H9L8.4 19.6L6 18.2L3.3 19L1 15L3 13V11L1 9L3.4 5L6 5.8L8.4 4.5ZM12 8A4 4 0 1 1 12 16A4 4 0 1 1 12 8Z", NULL},
    {"cube.box", "M12 1L22 6.5V17.5L12 23L2 17.5V6.5ZM12 3.5L5.1 7.2L12 11L18.9 7.2ZM4 9V16.3L11 20.2V12.8ZM13 12.8V20.2L20 16.3V9Z", NULL},
    {"arrow.down.circle", "M12 1A11 11 0 1 0 12 23A11 11 0 1 0 12 1ZM12 3A9 9 0 1 1 12 21A9 9 0 1 1 12 3ZM11 6H13V14L16.6 10.5L18 12L12 18L6 12L7.4 10.5L11 14Z", NULL},
    {"lock", "M5 10H7V7A5 5 0 0 1 17 7V10H19V22H5ZM9 10H15V7A3 3 0 0 0 9 7ZM7 12V20H17V12Z", NULL},
    {"plus", "M11 3H13V11H21V13H13V21H11V13H3V11H11Z", NULL},
    {"minus", "M3 11H21V13H3Z", NULL},
    {"checkmark", "M2 12L4 10L9 15L20 4L22 6L9 19Z", NULL},
    {"chevron.left", "M16 2L18 4L10 12L18 20L16 22L6 12Z", NULL},
    {"chevron.right", "M8 2L18 12L8 22L6 20L14 12L6 4Z", NULL},
    {"xmark", "M4 6L6 4L12 10L18 4L20 6L14 12L20 18L18 20L12 14L6 20L4 18L10 12Z", NULL}
};

typedef struct {
    void *isa;
    const ui_symbol *symbol;
    ui_point size;
    long mode;
} ui_symbol_image;

static ui_symbol_image *symbol_image(void *obj)
{
    return chm_foundation_isa_is(obj, "UIImage") ? obj : NULL;
}

static void *symbol_make(const ui_symbol *symbol, ui_point size, long mode)
{
    ui_symbol_image *image = chm_foundation_alloc_obj("UIImage", sizeof(*image));
    if (image) { image->symbol = symbol; image->size = size; image->mode = mode; }
    return image;
}

static void *system_image(void *cls, const char *sel, void *name)
{
    (void)cls; (void)sel;
    const char *key = chm_foundation_cstr(name);
    for (size_t i = 0; i < sizeof(symbol_catalog) / sizeof(*symbol_catalog); i++) {
        ui_symbol *s = &symbol_catalog[i];
        if (strcmp(s->name, key)) continue;
        if (!s->standard) s->standard = symbol_make(s, (ui_point){24, 24}, 0);
        return s->standard;
    }
    return NULL;
}

static ui_point image_size(void *self, const char *sel)
{
    (void)sel;
    ui_symbol_image *image = symbol_image(self);
    return image ? image->size : (ui_point){0, 0};
}

static void *image_scaled(void *self, const char *sel, ui_point size)
{
    (void)sel;
    ui_symbol_image *image = symbol_image(self);
    if (!image || !image->symbol || !isfinite(size.x) || !isfinite(size.y) || size.x <= 0 || size.y <= 0) return NULL;
    if (image->size.x == size.x && image->size.y == size.y) return self;
    return symbol_make(image->symbol, size, image->mode);
}

static long image_mode(void *self, const char *sel)
{
    (void)sel;
    ui_symbol_image *image = symbol_image(self);
    return image ? image->mode : 0;
}

static void *image_with_mode(void *self, const char *sel, long mode)
{
    (void)sel;
    ui_symbol_image *image = symbol_image(self);
    if (!image || !image->symbol) return NULL;
    return mode == image->mode ? self : symbol_make(image->symbol, image->size, mode);
}

static void image_set(void *self, const char *sel, void *image)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v) v->image = image;
}

static void *image_get(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v ? v->image : NULL;
}

static void *image_init(void *self, const char *sel, void *image)
{
    ui_point size = image_size(image, NULL);
    void *view = v_init_frame(self, sel, n_rect(0, 0, size.x, size.y));
    if (view) image_set(view, NULL, image);
    return view;
}

static long device_idiom(void *self, const char *sel)
{
    (void)self; (void)sel;
    return fmin(g_vw, g_vh) / g_scale >= 600 ? 1 : 0;
}

static void *device_version_field(void *self, const char *sel)
{
    (void)self;
    if (!strcmp(sel, "systemName")) return chm_foundation_string("iOS");
    if (!strcmp(sel, "systemVersion")) return chm_foundation_string("16.0");
    return chm_foundation_string("Chimera");
}

static const chm_host_method image_methods[] = {
    {"UIImage", "systemImageNamed:", "@@:@", system_image, 1},
    {"UIImage", "size", "{CGSize=dd}@:", image_size, 0},
    {"UIImage", "_imageWithSize:", "@@:{CGSize=dd}", image_scaled, 0},
    {"UIImage", "renderingMode", "q@:", image_mode, 0},
    {"UIImage", "imageWithRenderingMode:", "@@:q", image_with_mode, 0},
    {"UIImageView", "initWithImage:", "@@:@", image_init, 0},
    {"UIImageView", "setImage:", "v@:@", image_set, 0},
    {"UIImageView", "image", "@@:", image_get, 0},
    {"UIDevice", "userInterfaceIdiom", "q@:", device_idiom, 0},
    {"UIDevice", "systemName", "@@:", device_version_field, 0},
    {"UIDevice", "systemVersion", "@@:", device_version_field, 0},
    {"UIDevice", "buildVersion", "@@:", device_version_field, 0}
};

static void symbols_reset(void)
{
    for (size_t i = 0; i < sizeof(symbol_catalog) / sizeof(*symbol_catalog); i++) {
        free(symbol_catalog[i].standard);
        symbol_catalog[i].standard = NULL;
    }
}
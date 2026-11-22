#include "ui_table.inc"

static void *table_init(void *self, const char *sel, ui_rect rect, long style)
{
    if (!chm_objc_is_kind(self, "UITableView")) return NULL;
    if (!v_init_frame(self, sel, rect) || !table_state(self)) return NULL;
    ui_view *v = as_view(self);
    v->clips = 1;
    v->bg = style ? 0xff1c1c1eu : 0xff000000u;
    return self;
}

static void table_set_background(void *self, const char *sel, void *object)
{
    (void)sel;
    if (!chm_objc_is_kind(self, "UITableView")) return;
    ui_view *table = as_view(self), *background = as_view(object);
    if (!table || (object && !background)) return;
    if (background && !view_add(table, background)) return;
    if (table->background_view != object) view_remove(as_view(table->background_view));
    table->background_view = object;
    if (background && table->nkids > 1) {
        memmove(table->kids + 1, table->kids, (size_t)(table->nkids - 1) * sizeof(*table->kids));
        table->kids[0] = background;
    }
    table_update(table);
    layout_dirty = 1;
}

static void *table_background(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v ? v->background_view : NULL;
}

static void table_set_source(void *self, const char *sel, void *source)
{
    (void)sel;
    ui_table *t = table_state(self);
    if (t) { t->view->data_source = source; t->dirty = 1; }
}

static void *table_source(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v ? v->data_source : NULL;
}

static void v_set_translates(void *self, const char *sel, signed char flag)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v) { v->translates = !!flag; layout_dirty = 1; }
}

static int v_translates(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->translates;
}

static void l_set_lines(void *self, const char *sel, long lines)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v && lines >= 0 && lines <= INT_MAX) { v->number_of_lines = (int)lines; layout_dirty = 1; }
}

static long l_lines(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v ? v->number_of_lines : 0;
}

static void l_set_color(void *self, const char *sel, void *object)
{
    (void)sel;
    ui_view *v = as_view(self);
    ui_color *c = as_color(object);
    if (v) v->text_color = c ? argb(c->r, c->g, c->b, c->a) : 0xffffffffu;
}

static void *l_color(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    uint32_t c = v ? v->text_color : 0;
    return color_new(((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, (c >> 24) / 255.0);
}

static void *font_size_new(void *cls, const char *sel, double size)
{
    (void)sel;
    if (cls != chm_foundation_lookup_class("UIFont") || !isfinite(size) || size <= 0 || size > 1024) return NULL;
    ui_font *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->object = chm_foundation_alloc_obj("UIFont", 0);
    if (!f->object) { free(f); return NULL; }
    f->size = size; f->next = fonts; fonts = f;
    return f->object;
}

static void *font_preferred(void *cls, const char *sel, void *style)
{
    const char *name = chm_foundation_cstr(style);
    return font_size_new(cls, sel, strstr(name, "Footnote") ? 13 : 17);
}

static double font_point_size(void *self, const char *sel)
{
    (void)sel;
    for (ui_font *f = fonts; f; f = f->next) if (f->object == self) return f->size;
    return 17;
}

static void l_set_font(void *self, const char *sel, void *font)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v && (!font || chm_objc_is_kind(font, "UIFont"))) {
        v->font = font;
        v->font_size = font_point_size(font, NULL);
        layout_dirty = 1;
    }
}

static void *l_font(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v) return NULL;
    if (!v->font) v->font = font_size_new(chm_foundation_lookup_class("UIFont"), NULL, v->font_size);
    return v->font;
}

static void *color_grouped(void *cls, const char *sel)
{
    (void)cls; (void)sel;
    return color_new(0.11, 0.11, 0.12, 1);
}

static void *color_secondary(void *cls, const char *sel)
{
    (void)cls; (void)sel;
    return color_new(0.70, 0.70, 0.74, 1);
}

static void *progress_init(void *self, const char *sel, long style)
{
    (void)style;
    if (!chm_objc_is_kind(self, "UIProgressView")) return NULL;
    return v_init_frame(self, sel, n_rect(0, 0, 0, 2));
}

static void progress_set(void *self, const char *sel, float amount)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v) v->progress = isfinite(amount) ? fmin(1, fmax(0, amount)) : 0;
}

static void progress_set_animated(void *self, const char *sel, float amount, signed char animated)
{
    (void)animated;
    progress_set(self, sel, amount);
}

static void *bar_system_item(void *self, const char *sel, long kind, void *target, const char *action)
{
    (void)sel;
    if (!chm_objc_is_kind(self, "UIBarButtonItem")) return NULL;
    ui_item *item = item_state(self);
    if (!item) return NULL;
    item->title = chm_foundation_string(kind == 4 ? "+" : kind == 0 ? "Done" : kind == 1 ? "Cancel" : "");
    item->target = target;
    item->action = action;
    return self;
}

static void navigation_set_button(void *self, const char *sel, void *button)
{
    if (!chm_objc_is_kind(self, "UINavigationItem")) return;
    ui_item *item = item_state(self);
    if (!item) return;
    if (!strcmp(sel, "setRightBarButtonItem:")) item->right_item = button;
    else item->left_item = button;
}

static void *navigation_button(void *self, const char *sel)
{
    if (!chm_objc_is_kind(self, "UINavigationItem")) return NULL;
    ui_item *item = item_state(self);
    if (!item) return NULL;
    return !strcmp(sel, "rightBarButtonItem") ? item->right_item : item->left_item;
}
static ui_point scroll_size(ui_view *v)
{
    if (v->table) return (ui_point){v->w, v->table->content_height};
    return v->content_size;
}

static ui_point scroll_limit(ui_view *v, ui_point offset)
{
    ui_point size = scroll_size(v);
    double min_x = -v->inset.left, min_y = -v->inset.top;
    double max_x = fmax(min_x, size.x - v->w + v->inset.right);
    double max_y = fmax(min_y, size.y - v->h + v->inset.bottom);
    return (ui_point){fmin(max_x, fmax(min_x, offset.x)), fmin(max_y, fmax(min_y, offset.y))};
}

static int scroll_to(ui_view *v, ui_point requested)
{
    if (!v || !v->scrollable || !isfinite(requested.x) || !isfinite(requested.y)) return 0;
    ui_point next = scroll_limit(v, requested);
    if (next.x == v->offset.x && next.y == v->offset.y) return 0;
    v->offset = next;
    if (!v->notifying_scroll) {
        const char *sel = "scrollViewDidScroll:";
        void (*did)(void *, const char *, void *) = chm_objc_find_imp(v->delegate, sel);
        if (did) {
            v->notifying_scroll = 1;
            did(v->delegate, sel, v->object);
            v->notifying_scroll = 0;
        }
    }
    return 1;
}

static ui_point child_offset(const ui_view *parent, const ui_view *child)
{
    if (parent && parent->scrollable && parent->background_view != child->object) return parent->offset;
    return (ui_point){0, 0};
}

static void screen_origin(const ui_view *v, double *x, double *y)
{
    *x = *y = 0;
    for (; v; v = v->parent) {
        ui_point shift = child_offset(v->parent, v);
        *x += v->x - shift.x;
        *y += v->y - shift.y;
    }
}

static ui_point scroll_get_size(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->scrollable ? scroll_size(v) : (ui_point){0, 0};
}

static void scroll_set_size(void *self, const char *sel, ui_point size)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v || !v->scrollable || !isfinite(size.x) || !isfinite(size.y)) return;
    v->content_size = (ui_point){fmax(0, size.x), fmax(0, size.y)};
    scroll_to(v, v->offset);
}

static ui_point scroll_get_offset(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->scrollable ? v->offset : (ui_point){0, 0};
}

static void scroll_set_offset(void *self, const char *sel, ui_point offset)
{
    (void)sel;
    scroll_to(as_view(self), offset);
}

static void scroll_set_animated(void *self, const char *sel, ui_point offset, signed char animated)
{
    (void)animated;
    scroll_set_offset(self, sel, offset);
}

static ui_insets scroll_get_inset(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->scrollable ? v->inset : (ui_insets){0, 0, 0, 0};
}

static void scroll_set_inset(void *self, const char *sel, ui_insets inset)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v || !v->scrollable || !isfinite(inset.top) || !isfinite(inset.left) ||
        !isfinite(inset.bottom) || !isfinite(inset.right)) return;
    v->inset = inset;
    scroll_to(v, v->offset);
}

static void scroll_set_enabled(void *self, const char *sel, signed char enabled)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v && v->scrollable) v->scroll_disabled = !enabled;
}

static int scroll_dragging(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->dragging;
}

static int scroll_enabled(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->scrollable && !v->scroll_disabled;
}

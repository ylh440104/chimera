static ui_anchor *find_anchor(void *obj)
{
    for (ui_anchor *a = anchors; obj && a; a = a->next)
        if (a->object == obj) return a;
    return NULL;
}

static ui_constraint *find_constraint(void *obj)
{
    for (ui_constraint *c = constraints; obj && c; c = c->next)
        if (c->object == obj) return c;
    return NULL;
}

static int attr_family(enum ui_attr attr)
{
    if (attr == WIDTH || attr == HEIGHT) return 2;
    return attr == TOP || attr == BOTTOM || attr == CENTER_Y;
}

static void *anchor_get(void *obj, const char *sel)
{
    ui_view *v = as_view(obj);
    if (!v) {
        for (ui_guide *g = guides; g; g = g->next)
            if (obj == g->object) { v = g->view; break; }
    }
    if (!v) return NULL;
    if (!strcmp(sel, "safeAreaLayoutGuide")) {
        for (ui_guide *g = guides; g; g = g->next) if (g->view == v) return g->object;
        ui_guide *g = calloc(1, sizeof(*g));
        if (!g) return NULL;
        g->object = chm_foundation_alloc_obj("UILayoutGuide", 0);
        if (!g->object) { free(g); return NULL; }
        g->view = v; g->next = guides; guides = g;
        return g->object;
    }
    static const char *const names[] = {"leadingAnchor", "trailingAnchor", "topAnchor", "bottomAnchor", "widthAnchor", "heightAnchor", "centerXAnchor", "centerYAnchor"};
    int i = 0;
    if (!strcmp(sel, "leftAnchor")) i = LEFT;
    else if (!strcmp(sel, "rightAnchor")) i = RIGHT;
    else for (; i < 8 && strcmp(names[i], sel); i++) {}
    if (i == 8) return NULL;
    enum ui_attr attr = (enum ui_attr)i;
    for (ui_anchor *a = anchors; a; a = a->next)
        if (a->view == v && a->attr == attr) return a->object;
    ui_anchor *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    const char *const classes[] = {"NSLayoutXAxisAnchor", "NSLayoutYAxisAnchor", "NSLayoutDimension"};
    a->object = chm_foundation_alloc_obj(classes[attr_family(attr)], 0);
    if (!a->object) { free(a); return NULL; }
    a->view = v; a->attr = attr; a->next = anchors; anchors = a;
    return a->object;
}

static void *make_equation(void *first, void *second, double k, int dimension)
{
    ui_anchor *a = find_anchor(first), *b = find_anchor(second);
    if (!a || !isfinite(k) || (dimension ? attr_family(a->attr) != 2 : !b || attr_family(a->attr) != attr_family(b->attr))) return NULL;
    ui_constraint *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->object = chm_foundation_alloc_obj("NSLayoutConstraint", 0);
    if (!c->object) { free(c); return NULL; }
    c->first = a; c->second = b; c->constant = k; c->multiplier = 1;
    c->next = constraints; constraints = c;
    return c->object;
}

static void *anchor_equal(void *self, const char *sel, void *other)
{
    (void)sel; return make_equation(self, other, 0, 0);
}
static void *anchor_equal_constant(void *self, const char *sel, void *other, double k)
{
    (void)sel; return make_equation(self, other, k, 0);
}
static void *anchor_size(void *self, const char *sel, double k)
{
    (void)sel; return make_equation(self, NULL, k, 1);
}
static int constraint_active(void *self, const char *sel)
{
    (void)sel; ui_constraint *c = find_constraint(self); return c && c->active;
}
static void constraint_set_active(void *self, const char *sel, signed char flag)
{
    (void)sel; ui_constraint *c = find_constraint(self);
    if (c) { c->active = !!flag; layout_dirty = 1; }
}
static void constraint_activate(void *cls, const char *sel, void *array)
{
    if (cls != chm_foundation_lookup_class("NSLayoutConstraint") || !chm_objc_is_kind(array, "NSArray")) return;
    size_t n = (size_t)chm_objc_send0(array, "count");
    int active = !strcmp(sel, "activateConstraints:");
    for (size_t i = 0; i < n; ++i)
        constraint_set_active(chm_objc_send1(array, "objectAtIndex:", (void *)i), NULL, active);
}

static const ui_view *origin(const ui_view *v, double *x, double *y)
{
    const ui_view *root = NULL;
    *x = *y = 0;
    for (; v; v = v->parent) { *x += v->x; *y += v->y; root = v; }
    return root;
}

static double rhs_value(const ui_constraint *c)
{
    if (!c->second) return c->constant;
    const ui_anchor *a = c->second;
    ui_view *v = a->view;
    if (a->attr == WIDTH || a->attr == HEIGHT)
        return (a->attr == WIDTH ? v->w : v->h) * c->multiplier + c->constant;
    double x, y, px, py;
    const ui_view *r = origin(v, &x, &y);
    if (origin(c->first->view->parent, &px, &py) != r) return NAN;
    double value;
    switch (a->attr) {
    case LEFT: value = x - px; break;
    case RIGHT: value = x - px + v->w; break;
    case TOP: value = y - py; break;
    case BOTTOM: value = y - py + v->h; break;
    case CENTER_X: value = x - px + 0.5 * v->w; break;
    case CENTER_Y: value = y - py + 0.5 * v->h; break;
    default: return NAN;
    }
    return value + c->constant;
}

static int resolve_axis(const ui_view *v, int vertical, double *position, double *extent)
{
    enum ui_attr lo = vertical ? TOP : LEFT, hi = vertical ? BOTTOM : RIGHT;
    enum ui_attr dim = vertical ? HEIGHT : WIDTH, mid = vertical ? CENTER_Y : CENTER_X;
    unsigned mask = v->goal_mask;
    int l = !!(mask & (1u << lo)), h = !!(mask & (1u << hi)), m = !!(mask & (1u << mid));
    double p = *position, s = *extent;
    if (l && h) s = v->goals[hi] - v->goals[lo];
    else if (mask & (1u << dim)) s = v->goals[dim];
    else if (l && m) s = 2 * (v->goals[mid] - v->goals[lo]);
    else if (h && m) s = 2 * (v->goals[hi] - v->goals[mid]);
    else if (vertical && s <= 0 && v->text && *v->text) s = ceil(v->font_size * 1.25);
    s = fmax(0, s);
    if (l) p = v->goals[lo];
    else if (h) p = v->goals[hi] - s;
    else if (m) p = v->goals[mid] - 0.5 * s;
    if (!isfinite(s) || !isfinite(p) || (fabs(s - *extent) < 0.0001 && fabs(p - *position) < 0.0001)) return 0;
    *position = p; *extent = s; return 1;
}

static void ui_layout(void)
{
    if (layout_busy || !layout_dirty) return;
    layout_busy = 1; layout_dirty = 0;
    unsigned pass = 0;
    int conflict = 0;
    for (; pass < 64; pass++) {
        int changed = 0;
        for (ui_view *v = views; v; v = v->next) v->goal_mask = 0;
        for (ui_constraint *c = constraints; c; c = c->next) {
            if (!c->active) continue;
            double value = rhs_value(c);
            if (!isfinite(value)) continue;
            ui_view *v = c->first->view;
            enum ui_attr attr = c->first->attr;
            unsigned bit = 1u << attr;
            if (v->goal_mask & bit) { if (fabs(v->goals[attr] - value) > 0.01) conflict = 1; continue; }
            v->goals[attr] = value; v->goal_mask |= bit;
        }
        for (ui_view *v = views; v; v = v->next) {
            if (!v->goal_mask) continue;
            changed |= resolve_axis(v, 0, &v->x, &v->w);
            changed |= resolve_axis(v, 1, &v->y, &v->h);
        }
        for (ui_view *v = views; v; v = v->next)
            if (v->background_view || v->table) table_update(v);
            else if (v->scrollable) v->offset = scroll_limit(v, v->offset);
        if (!changed) break;
    }
    if (conflict || pass == 64) chm_session_log(g_sess, "layout: conflicting or nonconvergent equalities");
    layout_busy = 0;
}

typedef struct { double x, y; } ui_point;
typedef struct {
    ui_view *view;
    ui_vc *controller;
    void *item;
    size_t index;
    int kind;
} ui_hit;

static ui_hit pressed;
static double press_x, press_y;
static int press_cancelled;

static int inside_view(const ui_view *v, double x, double y)
{
    return v && isfinite(x) && isfinite(y) && isfinite(v->w) && isfinite(v->h) &&
           x >= 0 && y >= 0 && x < v->w && y < v->h;
}

static ui_view *hit_view(ui_view *v, double x, double y, double alpha, unsigned depth)
{
    if (!v || depth > 512 || v->hidden || !v->interactive || !isfinite(v->alpha)) return NULL;
    alpha *= fmin(1, fmax(0, v->alpha));
    if (alpha <= 0.01 || !inside_view(v, x, y)) return NULL;
    for (int i = v->nkids; i > 0; i--) {
        ui_view *child = v->kids[i - 1];
        ui_view *hit = hit_view(child, x - child->x, y - child->y, alpha, depth + 1);
        if (hit) return hit;
    }
    return v;
}

static int view_point_inside(void *self, const char *sel, ui_point point, void *event)
{
    (void)sel; (void)event;
    return inside_view(as_view(self), point.x, point.y);
}

static void *view_hit_test(void *self, const char *sel, ui_point point, void *event)
{
    (void)sel; (void)event;
    ui_view *v = hit_view(as_view(self), point.x, point.y, 1, 0);
    return v ? v->object : NULL;
}

static void view_interaction(void *self, const char *sel, signed char enabled)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v) v->interactive = !!enabled;
}

static int view_is_interactive(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->interactive;
}

static void control_enabled(void *self, const char *sel, signed char enabled)
{
    (void)sel;
    if (is_bar_item(self)) {
        ui_item *item = item_state(self);
        if (item) item->disabled = !enabled;
    } else {
        ui_view *v = as_view(self);
        if (v) { v->disabled = !enabled; if (!enabled) v->highlighted = 0; }
    }
}

static int control_is_enabled(void *self, const char *sel)
{
    (void)sel;
    if (is_bar_item(self)) {
        ui_item *item = item_state(self);
        return item && !item->disabled;
    }
    ui_view *v = as_view(self);
    return v && !v->disabled;
}

static void control_highlighted(void *self, const char *sel, signed char highlighted)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (v) v->highlighted = !!highlighted;
}

static int control_is_highlighted(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->highlighted;
}

static int invoke_action(void *target, const char *selector, void *sender, void *event, ui_view *source)
{
    if (!selector || !*selector) return 0;
    unsigned argc = 0;
    for (const char *p = selector; *p; p++) argc += *p == ':';
    if (argc > 2) { chm_session_log(g_sess, "input: unsupported action signature %s", selector); return 0; }
    if (!target) {
        for (ui_view *v = source; v && !target; v = v->parent) {
            if (chm_objc_find_imp(v->object, selector)) target = v->object;
            for (ui_vc *vc = as_vc(v->owner); vc && !target; vc = vc->parent)
                if (chm_objc_find_imp(vc->object, selector)) target = vc->object;
        }
        if (!target && g_app) {
            void *delegate = app_delegate(g_app, NULL);
            if (chm_objc_find_imp(delegate, selector)) target = delegate;
        }
    }
    void *imp = chm_objc_find_imp(target, selector);
    if (!imp) { chm_session_log(g_sess, "input: no receiver for %s", selector); return 0; }
    chm_session_log(g_sess, "input: action %s target=%s", selector, chm_objc_class_name(*(void **)target));
    if (!argc) ((void (*)(void *, const char *))imp)(target, selector);
    else if (argc == 1) ((void (*)(void *, const char *, void *))imp)(target, selector, sender);
    else ((void (*)(void *, const char *, void *, void *))imp)(target, selector, sender, event);
    return 1;
}

static void control_add_target(void *self, const char *sel, void *target, const char *action, uint64_t events)
{
    (void)sel;
    if (!chm_objc_is_kind(self, "UIControl") || !action || !*action || !events) return;
    ui_view *v = as_view(self);
    if (!v) return;
    for (ui_action *a = v->actions; a; a = a->next) {
        if (a->target == target && !strcmp(a->selector, action)) { a->events |= events; return; }
    }
    ui_action *a = calloc(1, sizeof(*a));
    if (!a) return;
    a->selector = strdup(action);
    if (!a->selector) { free(a); return; }
    a->target = target; a->events = events;
    ui_action **tail = &v->actions;
    while (*tail) tail = &(*tail)->next;
    *tail = a;
}

static void control_remove_target(void *self, const char *sel, void *target, const char *action, uint64_t events)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v) return;
    ui_action **link = &v->actions;
    while (*link) {
        ui_action *a = *link;
        if ((!target || a->target == target) && (!action || !strcmp(a->selector, action))) a->events &= ~events;
        if (!a->events) { *link = a->next; free((void *)a->selector); free(a); }
        else link = &a->next;
    }
}

static int send_control_events(ui_view *v, uint64_t events)
{
    if (!v || v->disabled) return 0;
    size_t count = 0;
    for (ui_action *a = v->actions; a; a = a->next) if (a->events & events) count++;
    ui_action *copy = count ? calloc(count, sizeof(*copy)) : NULL;
    if (count && !copy) return 0;
    size_t n = 0;
    for (ui_action *a = v->actions; a; a = a->next) {
        if (!(a->events & events)) continue;
        copy[n].target = a->target;
        copy[n].selector = strdup(a->selector);
        if (!copy[n].selector) break;
        n++;
    }
    int called = 0;
    for (size_t i = 0; i < n; i++) {
        called |= invoke_action(copy[i].target, copy[i].selector, v->object, NULL, v);
        free((void *)copy[i].selector);
    }
    free(copy);
    return called;
}

static void control_send_events(void *self, const char *sel, uint64_t events)
{
    (void)sel;
    send_control_events(as_view(self), events);
}

static void control_send_action(void *self, const char *sel, const char *action, void *target, void *event)
{
    (void)sel;
    invoke_action(target, action, self, event, as_view(self));
}

static int application_send_action(void *self, const char *sel, const char *action, void *target, void *sender, void *event)
{
    (void)self; (void)sel;
    return invoke_action(target, action, sender, event, as_view(sender));
}

static void *button_create(void *cls, const char *sel, long type)
{
    (void)sel; (void)type;
    void *object = chm_foundation_alloc_class(cls, 0);
    ui_view *v = as_view(object);
    if (!v) return NULL;
    v->font_align = 1; v->text_color = 0xff007affu;
    return object;
}

static void button_title(void *self, const char *sel, void *text, uint64_t state)
{
    if (!state) l_set_text(self, sel, text);
}

static void *button_get_title(void *self, const char *sel, uint64_t state)
{
    (void)state;
    return l_text(self, sel);
}

static void button_color(void *self, const char *sel, void *color, uint64_t state)
{
    if (!state) l_set_color(self, sel, color);
}

static void bar_target(void *self, const char *sel, void *value)
{
    if (!chm_objc_is_kind(self, "UIBarButtonItem")) return;
    ui_item *item = item_state(self);
    if (!item) return;
    if (!strcmp(sel, "setTarget:")) item->target = value;
    else item->action = value;
}

static void *bar_get_target(void *self, const char *sel)
{
    if (!chm_objc_is_kind(self, "UIBarButtonItem")) return NULL;
    ui_item *item = item_state(self);
    return item ? (!strcmp(sel, "target") ? item->target : (void *)item->action) : NULL;
}

static void *bar_title_init(void *self, const char *sel, void *title, long style, void *target, const char *action)
{
    (void)sel; (void)style;
    if (!chm_objc_is_kind(self, "UIBarButtonItem")) return NULL;
    ui_item *item = item_state(self);
    if (!item) return NULL;
    item->title = title; item->target = target; item->action = action;
    return self;
}

static ui_hit hit_at(double x, double y)
{
    ui_hit result = {0};
    if (!g_key_window || g_key_window->hidden || !g_key_window->interactive ||
        !isfinite(g_key_window->alpha) || g_key_window->alpha <= 0.01 ||
        !inside_view(g_key_window, x - g_key_window->x, y - g_key_window->y)) return result;
    ui_view *root = g_modal ? as_view(g_modal->view) : g_key_window;
    double ox, oy;
    origin(root, &ox, &oy);
    ui_view *v = hit_view(root, x - ox, y - oy, 1, 0);
    result.view = v;
    if (!v) return result;
    result.kind = 1;
    origin(v, &ox, &oy);
    for (ui_vc *vc = controllers; vc; vc = vc->next) {
        if (vc->bar != v->object) continue;
        result.controller = vc;
        if (container_kind(vc) == 2 && vc->child_count) {
            size_t index = (size_t)((x - ox) / v->w * vc->child_count);
            if (index >= vc->child_count) return result;
            ui_vc *child = as_vc(vc->children[index]);
            ui_item *item = child && child->tab_item ? item_state(child->tab_item) : NULL;
            if (!item || !item->disabled) { result.kind = 2; result.index = index; result.item = vc->children[index]; }
        } else if (container_kind(vc) == 1 && vc->active_child) {
            ui_item *nav = vc->active_child->navigation_item ? item_state(vc->active_child->navigation_item) : NULL;
            double width = fmin(48, v->w);
            int side = x - ox < width ? 0 : x - ox >= v->w - width ? 1 : -1;
            void *object = side < 0 || !nav ? NULL : side ? nav->right_item : nav->left_item;
            ui_item *item = object ? item_state(object) : NULL;
            if (item && !item->disabled && item->action) { result.kind = 3; result.item = object; }
            else if (side == 0 && !object && vc->child_count > 1) result.kind = 4;
        }
        return result;
    }
    if (chm_objc_is_kind(v->object, "UIControl")) {
        if (!v->disabled) result.kind = 5;
        return result;
    }
    for (ui_view *parent = v; parent; parent = parent->parent) {
        if (parent->cell && parent->cell->index && parent->cell->selection_style) {
            result.view = parent;
            result.item = parent->cell->index;
            result.kind = 6;
            break;
        }
    }
    return result;
}

static int same_hit(ui_hit a, ui_hit b)
{
    return a.view && a.view == b.view && a.kind == b.kind && a.controller == b.controller &&
           a.item == b.item && a.index == b.index;
}

static void input_reset(void)
{
    if (pressed.view) pressed.view->highlighted = 0;
    memset(&pressed, 0, sizeof(pressed));
    press_cancelled = 0;
}

typedef struct { int phase; double x, y; } pointer_args;

static int pointer_body(void *context)
{
    pointer_args *p = context;
    if (p->phase == 3) {
        ui_view *v = pressed.kind == 5 ? pressed.view : NULL;
        input_reset();
        return send_control_events(v, 1u << 8);
    }
    if (!isfinite(p->x) || !isfinite(p->y)) { input_reset(); return 0; }
    container_layout(as_vc(g_key_window ? g_key_window->owner : NULL));
    if (g_modal) container_layout(g_modal);
    tables_prepare(g_key_window);
    layout_dirty = 1; ui_layout();
    ui_hit hit = hit_at(p->x, p->y);
    if (p->phase == 0) {
        input_reset(); pressed = hit; press_x = p->x; press_y = p->y;
        if (hit.kind == 5) { hit.view->highlighted = 1; send_control_events(hit.view, 1); }
        return hit.view != NULL;
    }
    if (p->phase != 1 && p->phase != 2) return 0;
    if (hypot(p->x - press_x, p->y - press_y) > 12) press_cancelled = 1;
    if (pressed.kind == 5) pressed.view->highlighted = !press_cancelled && same_hit(hit, pressed);
    if (p->phase == 1) return 0;
    ui_hit down = pressed;
    int accept = !press_cancelled && same_hit(down, hit);
    int cancelled = press_cancelled;
    input_reset();
    if (!accept) return down.kind == 5 ? send_control_events(down.view, 1u << (cancelled ? 8 : 7)) : 0;
    if (hit.kind == 2) {
        tab_select(hit.controller->object, "setSelectedIndex:", hit.index);
        chm_session_log(g_sess, "input: tab selected=%zu", hit.index);
        return 1;
    }
    if (hit.kind == 3) {
        ui_item *item = item_state(hit.item);
        return item && !item->disabled ? invoke_action(item->target, item->action, hit.item, NULL, hit.view) : 0;
    }
    if (hit.kind == 4) return nav_pop(hit.controller->object, NULL, 0) != NULL;
    if (hit.kind == 5) {
        if (chm_objc_is_kind(hit.view->object, "UISwitch")) {
            hit.view->switch_on = !hit.view->switch_on;
            send_control_events(hit.view, 1u << 12);
            chm_session_log(g_sess, "input: switch on=%d", hit.view->switch_on);
            return 1;
        }
        return send_control_events(hit.view, 1u << 6);
    }
    if (hit.kind == 6) return table_select_cell(hit.view);
    return 0;
}

typedef struct { double x, y, delta; } scroll_args;

static int scroll_body(void *context)
{
    scroll_args *p = context;
    if (!isfinite(p->x) || !isfinite(p->y) || !isfinite(p->delta)) return 0;
    input_reset();
    if (!g_key_window) return 0;
    prepare_draw(NULL);
    ui_hit hit = hit_at(p->x, p->y);
    for (ui_view *v = hit.view; v; v = v->parent) {
        ui_table *t = v->table;
        if (!t) continue;
        double previous = t->offset;
        t->offset = fmin(fmax(0, t->offset + p->delta), fmax(0, t->content_height - v->h));
        table_update(v);
        layout_dirty = 1;
        chm_session_log(g_sess, "input: table offset=%.2f", t->offset);
        return fabs(previous - t->offset) > 0.001;
    }
    return 0;
}

int chm_ui_scroll(double x, double y, double delta)
{
    if (!g_sess || g_sess->exit_code == -2) return -1;
    scroll_args args = {x, y, delta};
    int rc = chm_session_callback(g_sess, scroll_body, &args);
    if (rc == -2) { input_reset(); g_sess->exit_code = -2; }
    return rc;
}

int chm_ui_pointer(int phase, double x, double y)
{
    if (!g_sess || g_sess->exit_code == -2) return -1;
    pointer_args args = {phase, x, y};
    int rc = chm_session_callback(g_sess, pointer_body, &args);
    if (rc == -2) { input_reset(); g_sess->exit_code = -2; }
    return rc;
}
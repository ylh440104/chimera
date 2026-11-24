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
        ui_point shift = child_offset(v, child);
        ui_view *hit = hit_view(child, x + shift.x - child->x, y + shift.y - child->y, alpha, depth + 1);
        if (hit) return hit;
    }
    return v;
}

static int view_point_inside(void *self, const char *sel, ui_point point, void *event)
{
    (void)sel; (void)event;
    ui_view *v = as_view(self);
    return v && inside_view(v, point.x - v->offset.x, point.y - v->offset.y);
}

static void *view_hit_test(void *self, const char *sel, ui_point point, void *event)
{
    (void)sel; (void)event;
    ui_view *root = as_view(self);
    ui_view *v = root ? hit_view(root, point.x - root->offset.x, point.y - root->offset.y, 1, 0) : NULL;
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

static ui_view *view_at(double x, double y)
{
    if (!g_key_window || g_key_window->hidden || !g_key_window->interactive ||
        !isfinite(g_key_window->alpha) || g_key_window->alpha <= 0.01 ||
        !inside_view(g_key_window, x - g_key_window->x, y - g_key_window->y)) return NULL;
    ui_view *root = g_modal ? as_view(g_modal->view) : g_key_window;
    double ox, oy;
    screen_origin(root, &ox, &oy);
    return hit_view(root, x - ox, y - oy, 1, 0);
}

static ui_hit hit_at(double x, double y)
{
    ui_hit result = {0};
    double ox, oy;
    ui_view *v = view_at(x, y);
    result.view = v;
    if (!v) return result;
    result.kind = 1;
    screen_origin(v, &ox, &oy);
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
    for (ui_view *parent = v; parent; parent = parent->parent) {
        if (!parent->picker) continue;
        screen_origin(parent, &ox, &oy);
        long component, row;
        result.view = parent;
        if (picker_hit(parent->picker, x - ox, y - oy, &component, &row)) {
            result.kind = 8;
            result.index = (size_t)component;
            result.item = (void *)(uintptr_t)(row + 1);
        }
        return result;
    }
    if (v->segments) {
        long index = segment_hit(v->segments, x - ox);
        if (index >= 0) { result.kind = 7; result.index = (size_t)index; }
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
    dialogs_prepare();
    layout_dirty = 1; ui_layout();
    choice_prepare(g_key_window);
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
        if (hit.view->alert_action) return dialog_select(hit.view->alert_action);
        if (chm_objc_is_kind(hit.view->object, "UITextField")) return field_focus(hit.view->object, "becomeFirstResponder");
        if (chm_objc_is_kind(hit.view->object, "UISwitch")) {
            hit.view->switch_on = !hit.view->switch_on;
            send_control_events(hit.view, 1u << 12);
            chm_session_log(g_sess, "input: switch on=%d", hit.view->switch_on);
            return 1;
        }
        return send_control_events(hit.view, 1u << 6);
    }
    if (hit.kind == 6) return table_select_cell(hit.view);
    if (hit.kind == 7) return segment_activate(hit.view->segments, (long)hit.index);
    if (hit.kind == 8) return picker_choose(hit.view->picker, (long)(uintptr_t)hit.item - 1, (long)hit.index, 1);
    return 0;
}

static ui_view *pan_anchor, *pan_target;
static ui_point pan_last;
static long pan_component, pan_initial_row;
static double pan_remainder;

static void pan_reset(void)
{
    if (pan_target) pan_target->dragging = 0;
    pan_anchor = pan_target = NULL;
    pan_remainder = 0;
}

static int pan_finish(void)
{
    ui_view *v = pan_target;
    pan_reset();
    if (!v) return 0;
    if (v->picker) {
        long row = picker_selected(v->object, NULL, pan_component);
        if (row >= 0 && row != pan_initial_row) {
            const char *sel = "pickerView:didSelectRow:inComponent:";
            void (*did)(void *, const char *, void *, long, long) = chm_objc_find_imp(v->delegate, sel);
            if (did) did(v->delegate, sel, v->object, row, pan_component);
        }
        return 1;
    }
    const char *sel = "scrollViewDidEndDragging:willDecelerate:";
    void (*did)(void *, const char *, void *, signed char) = chm_objc_find_imp(v->delegate, sel);
    if (did) did(v->delegate, sel, v->object, 0);
    return 1;
}

static int pan_visible(ui_view *v)
{
    ui_view *root = g_modal ? as_view(g_modal->view) : g_key_window;
    for (; v; v = v->parent) {
        if (v->hidden || !v->interactive || v->alpha <= 0.01) return 0;
        if (v == root) return 1;
    }
    return 0;
}

static int pan_body(void *context)
{
    pointer_args *p = context;
    if (p->phase == 3 || !isfinite(p->x) || !isfinite(p->y)) return pan_finish();
    if (p->phase == 0) {
        pan_finish();
        input_reset();
        if (!g_key_window) return 0;
        prepare_draw(NULL);
        pan_anchor = view_at(p->x, p->y);
        for (ui_view *v = pan_anchor; v; v = v->parent) {
            if (!v->picker) continue;
            double ox, oy;
            screen_origin(v, &ox, &oy);
            if (picker_hit(v->picker, p->x - ox, v->h / 2, &pan_component, &pan_initial_row)) {
                pan_target = v;
                v->dragging = 1;
            }
            break;
        }
        pan_last = (ui_point){p->x, p->y};
        return 0;
    }
    if (!pan_anchor || (p->phase != 1 && p->phase != 2)) return 0;
    if (!pan_visible(pan_target ? pan_target : pan_anchor)) return pan_finish();
    double dx = pan_last.x - p->x, dy = pan_last.y - p->y;
    pan_last = (ui_point){p->x, p->y};
    if (pan_target && pan_target->picker) {
        ui_picker *state = pan_target->picker;
        if (state->dirty || pan_component < 0 || (size_t)pan_component >= state->count) return pan_finish();
        picker_component *entry = &state->components[pan_component];
        if (!entry->count) return pan_finish();
        pan_remainder = fmax(-8192, fmin(8192, pan_remainder + dy));
        long steps = (long)trunc(pan_remainder / entry->height);
        long row = entry->selected + steps;
        if (row < 0) row = 0;
        if (row >= entry->count) row = entry->count - 1;
        pan_remainder -= steps * entry->height;
        int changed = picker_choose(state, row, pan_component, 0);
        if (p->phase == 2) changed |= pan_finish();
        return changed;
    }
    if (!pan_target) {
        for (ui_view *v = pan_anchor; v; v = v->parent) {
            if (v->scrollable && !v->scroll_disabled) {
                ui_point next = scroll_limit(v, (ui_point){v->offset.x + dx, v->offset.y + dy});
                if (next.x != v->offset.x || next.y != v->offset.y) {
                    pan_target = v;
                    v->dragging = 1;
                    const char *sel = "scrollViewWillBeginDragging:";
                    void (*will)(void *, const char *, void *) = chm_objc_find_imp(v->delegate, sel);
                    if (will) will(v->delegate, sel, v->object);
                    break;
                }
            }
            if (g_modal && v->object == g_modal->view) break;
        }
    }
    ui_view *v = pan_target;
    int changed = v && !v->scroll_disabled && scroll_to(v, (ui_point){v->offset.x + dx, v->offset.y + dy});
    if (p->phase == 2) changed |= pan_finish();
    return changed;
}

int chm_ui_pan(int phase, double x, double y)
{
    if (!g_sess || g_sess->exit_code == -2) return -1;
    pointer_args args = {phase, x, y};
    int rc = chm_session_callback(g_sess, pan_body, &args);
    if (rc == -2) { pan_reset(); g_sess->exit_code = -2; }
    return rc;
}

int chm_ui_scroll(double x, double y, double delta)
{
    if (!isfinite(delta)) return 0;
    int rc = chm_ui_pan(0, x, y);
    return rc < 0 ? rc : chm_ui_pan(2, x, y - delta);
}

int chm_ui_pointer(int phase, double x, double y)
{
    if (!g_sess || g_sess->exit_code == -2) return -1;
    pointer_args args = {phase, x, y};
    int rc = chm_session_callback(g_sess, pointer_body, &args);
    if (rc == -2) { input_reset(); g_sess->exit_code = -2; }
    return rc;
}
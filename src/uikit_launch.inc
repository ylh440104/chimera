typedef struct launch_target {
    void *object;
    void *target;
    char action[96];
    struct launch_target *next;
} launch_target;

static launch_target *launch_targets;

typedef struct launch_object {
    void *object;
    void *first;
    void *second;
    void *third;
    double values[8];
    long integers[8];
    struct launch_object *next;
} launch_object;

static launch_object *launch_objects;

static launch_object *launch_record(void *object)
{
    if (!object) return NULL;
    for (launch_object *value = launch_objects; value; value = value->next)
        if (value->object == object) return value;
    launch_object *value = calloc(1, sizeof(*value));
    if (!value) return NULL;
    value->object = object;
    value->next = launch_objects;
    launch_objects = value;
    return value;
}

static launch_object *launch_find(void *object)
{
    for (launch_object *value = launch_objects; value; value = value->next)
        if (value->object == object) return value;
    return NULL;
}

static void launch_remove(void *object)
{
    launch_object **link = &launch_objects;
    while (*link && (*link)->object != object) link = &(*link)->next;
    if (!*link) return;
    launch_object *value = *link;
    *link = value->next;
    free(value);
}

static void launch_reset(void)
{
    while (launch_targets) {
        launch_target *next = launch_targets->next;
        free(launch_targets);
        launch_targets = next;
    }
    while (launch_objects) {
        launch_object *value = launch_objects;
        launch_objects = value->next;
        free(value);
    }
}

static void *view_child(void *self, const char *cls)
{
    ui_view *v = as_view(self);
    if (!v) return NULL;
    for (int i = 0; i < v->nkids; i++) {
        void *kid = v->kids[i]->object;
        if (chm_objc_is_kind(kid, cls)) return kid;
    }
    return NULL;
}

static void *effect_new(void *cls, const char *sel, long style)
{
    (void)cls;
    (void)sel;
    if (style < 0 || style > 20) return NULL;
    void *object = chm_foundation_alloc_obj("UIBlurEffect", 0);
    launch_object *value = launch_record(object);
    if (!value) return NULL;
    value->integers[0] = style;
    return object;
}

static long effect_style(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->integers[0] : 0;
}

static void visual_set_effect(void *self, const char *sel, void *effect)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->first = effect;
}

static void *visual_effect(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void *visual_content(void *self, const char *sel)
{
    (void)sel;
    void *content = view_child(self, "UIView");
    if (content) return content;
    ui_view *v = as_view(self);
    if (!v) return NULL;
    void *object = chm_foundation_alloc_obj("UIView", 0);
    if (!object) return NULL;
    ui_view *kid = as_view(object);
    if (!kid || !view_add(v, kid)) return NULL;
    kid->x = 0;
    kid->y = 0;
    kid->w = v->w;
    kid->h = v->h;
    layout_dirty = 1;
    return object;
}

static void *menu_item_new(void *cls, const char *sel, void *title, void *action)
{
    (void)cls;
    (void)sel;
    if (!title || !action) return NULL;
    void *object = chm_foundation_alloc_obj("UIMenuItem", 0);
    launch_object *value = launch_record(object);
    if (!value) return NULL;
    value->first = chm_foundation_string(chm_foundation_cstr(title));
    const char *name = chm_foundation_cstr(action);
    if (!name || !*name) { launch_remove(object); return NULL; }
    snprintf((char *)value->values, sizeof(value->values), "%s", name);
    return object;
}

static void *menu_item_title(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void menu_item_set_title(void *self, const char *sel, void *title)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value) return;
    chm_foundation_cf_release(value->first);
    value->first = title ? chm_foundation_string(chm_foundation_cstr(title)) : NULL;
}

static void *menu_item_action(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value && ((const char *)value->values)[0] ? chm_foundation_string((const char *)value->values) : NULL;
}

static void *menu_shared(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    static void *shared;
    if (!shared) shared = chm_foundation_alloc_obj("UIMenuController", 0);
    return shared;
}

static void menu_set_items(void *self, const char *sel, void *items)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return;
    value->first = items;
}

static void *menu_items(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void menu_target_rect(void *self, const char *sel, ui_rect rect, void *view)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return;
    value->values[0] = rect.x;
    value->values[1] = rect.y;
    value->values[2] = rect.w;
    value->values[3] = rect.h;
    value->second = view;
}

static void menu_set_visible(void *self, const char *sel, signed char visible, signed char animated)
{
    (void)sel;
    (void)animated;
    launch_object *value = launch_record(self);
    if (!value) return;
    value->integers[0] = !!visible;
    chm_session_log(g_sess, "uikit: menu visibility %d has no menu overlay", !!visible);
}

static signed char menu_visible(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (signed char)value->integers[0] : 0;
}

static void menu_hide(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[0] = 0;
}

static void *pinch_new(void *self, const char *sel)
{
    (void)sel;
    if (!self) return NULL;
    launch_object *value = launch_record(self);
    if (!value) return NULL;
    value->values[0] = 1;
    return self;
}

static double pinch_scale(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->values[0] : 1;
}

static void pinch_set_scale(void *self, const char *sel, double scale)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value && scale > 0) value->values[0] = scale;
}

static double pinch_velocity(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->values[1] : 0;
}

static void gesture_add_target(void *self, const char *sel, void *target, void *action)
{
    (void)sel;
    if (!self || !target || !action) return;
    const char *name = chm_foundation_cstr(action);
    if (!name || !*name) return;
    launch_target *value = calloc(1, sizeof(*value));
    if (!value) return;
    value->object = self;
    value->target = target;
    snprintf(value->action, sizeof(value->action), "%s", name);
    value->next = launch_targets;
    launch_targets = value;
}

static void gesture_set_view(void *self, const char *sel, void *view)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->first = view;
}

static void *gesture_view(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void gesture_set_state(void *self, const char *sel, long state)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[0] = state;
}

static long gesture_state(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->integers[0] : 0;
}

static void gesture_set_enabled(void *self, const char *sel, signed char enabled)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[1] = !!enabled;
}

static signed char gesture_enabled(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (signed char)(value->integers[1] == 0) : 1;
}

static void gesture_set_cancels(void *self, const char *sel, signed char cancels)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[2] = !!cancels;
}

static void edge_set_edges(void *self, const char *sel, unsigned long edges)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[3] = (long)(edges & 15);
}

static unsigned long edge_edges(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (unsigned long)value->integers[3] : 0;
}

static void *path_new(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    void *object = chm_foundation_alloc_obj("UIBezierPath", 0);
    launch_object *value = launch_record(object);
    if (!value) return NULL;
    value->values[6] = 1;
    return object;
}

static int path_reserve(launch_object *value, size_t count)
{
    if (value->integers[6] >= (long)count) return 1;
    size_t capacity = (size_t)value->integers[6];
    while (capacity < count) capacity *= 2;
    double *points = realloc(value->second, capacity * sizeof(double));
    if (!points) return 0;
    value->second = points;
    value->integers[6] = (long)capacity;
    return 1;
}

static void path_push(launch_object *value, double x, double y)
{
    if (!path_reserve(value, (size_t)value->integers[7] + 2)) return;
    double *points = value->second;
    points[value->integers[7]++] = x;
    points[value->integers[7]++] = y;
    if (value->integers[7] == 2) { value->values[0] = x; value->values[1] = y; }
    value->values[4] = x;
    value->values[5] = y;
}

static void *path_move(void *self, const char *sel, ui_point point)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value) return NULL;
    if (value->integers[7]) path_push(value, point.x, point.y);
    else { path_push(value, point.x, point.y); }
    return NULL;
}

static void *path_line(void *self, const char *sel, ui_point point)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value || value->integers[7] < 2) return NULL;
    path_push(value, point.x, point.y);
    return NULL;
}

static void *path_curve(void *self, const char *sel, ui_point point, ui_point first, ui_point second)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value || value->integers[7] < 2) return NULL;
    double x0 = value->values[4], y0 = value->values[5];
    for (int i = 1; i <= 24; i++) {
        double t = i / 24.0, u = 1 - t;
        double x = u * u * u * x0 + 3 * u * u * t * first.x + 3 * u * t * t * second.x + t * t * t * point.x;
        double y = u * u * u * y0 + 3 * u * u * t * first.y + 3 * u * t * t * second.y + t * t * t * point.y;
        path_push(value, x, y);
    }
    return NULL;
}

static void *path_quad(void *self, const char *sel, ui_point point, ui_point control)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value || value->integers[7] < 2) return NULL;
    double x0 = value->values[4], y0 = value->values[5];
    for (int i = 1; i <= 16; i++) {
        double t = i / 16.0, u = 1 - t;
        double x = u * u * x0 + 2 * u * t * control.x + t * t * point.x;
        double y = u * u * y0 + 2 * u * t * control.y + t * t * point.y;
        path_push(value, x, y);
    }
    return NULL;
}

static void *path_arc(void *self, const char *sel, ui_point center, double radius, double start, double end, signed char clockwise)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value || radius <= 0) return NULL;
    double delta = end - start;
    if (clockwise && delta > 0) delta -= 2 * M_PI;
    if (!clockwise && delta < 0) delta += 2 * M_PI;
    const int steps = 48;
    for (int i = 0; i <= steps; i++) {
        double angle = start + delta * i / steps;
        double x = center.x + cos(angle) * radius, y = center.y + sin(angle) * radius;
        if (i == 0 && value->integers[7] < 2) { path_push(value, x, y); continue; }
        path_push(value, x, y);
    }
    return NULL;
}

static void path_close(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value || value->integers[7] < 4) return;
    path_push(value, value->values[0], value->values[1]);
}

static void path_append(void *self, const char *sel, void *other)
{
    (void)sel;
    launch_object *value = launch_find(self), *source = launch_find(other);
    if (!value || !source || value == source) return;
    for (long i = 0; i + 1 < source->integers[7]; i += 2)
        path_push(value, ((double *)source->second)[i], ((double *)source->second)[i + 1]);
}

static ui_rect path_bounds(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    ui_rect empty = {0, 0, 0, 0};
    if (!value || value->integers[7] < 2) return empty;
    double *points = value->second;
    double min_x = points[0], max_x = points[0], min_y = points[1], max_y = points[1];
    for (long i = 2; i + 1 < value->integers[7]; i += 2) {
        if (points[i] < min_x) min_x = points[i];
        if (points[i] > max_x) max_x = points[i];
        if (points[i + 1] < min_y) min_y = points[i + 1];
        if (points[i + 1] > max_y) max_y = points[i + 1];
    }
    return (ui_rect){min_x, min_y, max_x - min_x, max_y - min_y};
}

static ui_point path_current(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    ui_point point = {0, 0};
    if (value && value->integers[7] >= 2) { point.x = value->values[4]; point.y = value->values[5]; }
    return point;
}

static signed char path_empty(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return !value || value->integers[7] < 2;
}

static void path_clear(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value) return;
    value->integers[7] = 0;
    value->values[0] = value->values[1] = 0;
}

static double path_line_width(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->values[6] : 1;
}

static void path_set_line_width(void *self, const char *sel, double width)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value && width >= 0) value->values[6] = width;
}

static void path_draw(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return;
    chm_session_log(g_sess, "uikit: %s keeps %ld points but no path rasterizer is available",
                    sel ? sel : "fill", value->integers[7] / 2);
}

static void *path_cgpath(void *self, const char *sel)
{
    (void)sel;
    return launch_find(self) ? self : NULL;
}

static void *shape_set_path(void *self, const char *sel, void *path)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->first = path;
    return NULL;
}

static void *shape_path(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void shape_set_color(void *self, const char *sel, void *color)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setFillColor:")) value->second = color;
    else value->third = color;
}

static void *shape_color(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return NULL;
    return !strcmp(sel, "fillColor") ? value->second : value->third;
}

static void shape_set_number(void *self, const char *sel, double number)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setLineWidth:")) value->values[0] = number;
    else if (!strcmp(sel, "setStrokeStart:")) value->values[1] = number;
    else value->values[2] = number;
}

static double shape_number(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return 0;
    if (!strcmp(sel, "lineWidth")) return value->values[0] ? value->values[0] : 1;
    if (!strcmp(sel, "strokeStart")) return value->values[1];
    return value->values[2] ? value->values[2] : 1;
}

static void text_view_flag(void *self, const char *sel, signed char flag)
{
    ui_view *v = as_view(self);
    if (!v) return;
    if (!strcmp(sel, "setEditable:")) v->secure = 0;
    if (!strcmp(sel, "setSelectable:")) v->interactive = !!flag;
}

static void text_view_inset(void *self, const char *sel, ui_insets inset)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v) return;
    v->inset = inset;
    layout_dirty = 1;
}

static signed char text_view_editable(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) ? 1 : 0;
}

static void *web_config_new(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    void *object = chm_foundation_alloc_obj("WKWebViewConfiguration", 0);
    launch_object *value = launch_record(object);
    if (!value) return NULL;
    value->integers[0] = 1;
    return object;
}

static void web_config_flag(void *self, const char *sel, signed char flag)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setAllowsInlineMediaPlayback:")) value->integers[0] = !!flag;
    else value->integers[1] = !!flag;
}

static signed char web_config_get(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return 0;
    return (signed char)(!strcmp(sel, "allowsInlineMediaPlayback") ? value->integers[0] : value->integers[1]);
}

static void web_config_value(void *self, const char *sel, void *value, void *key)
{
    (void)sel;
    (void)value;
    (void)key;
    launch_record(self);
}

static void *web_config_get_value(void *self, const char *sel, void *key)
{
    (void)sel;
    (void)key;
    return launch_find(self) ? NULL : NULL;
}

static void *web_view_new(void *self, const char *sel, ui_rect frame, void *configuration)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v) return NULL;
    v->x = frame.x;
    v->y = frame.y;
    v->w = frame.w;
    v->h = frame.h;
    launch_object *value = launch_record(self);
    if (!value) return NULL;
    value->first = configuration;
    v->text = strdup("WebKit");
    v->font_size = 13;
    v->text_color = 0xff888888u;
    layout_dirty = 1;
    return self;
}

static void *web_configuration(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void *web_scroll(void *self, const char *sel)
{
    (void)sel;
    void *scroll = view_child(self, "UIScrollView");
    if (scroll) return scroll;
    ui_view *v = as_view(self);
    if (!v) return NULL;
    void *object = chm_foundation_alloc_obj("UIScrollView", 0);
    ui_view *kid = as_view(object);
    if (!kid || !view_add(v, kid)) return NULL;
    kid->w = v->w;
    kid->h = v->h;
    layout_dirty = 1;
    return object;
}

static void web_load(void *self, const char *sel, void *request, void *base)
{
    launch_object *value = launch_record(self);
    const char *address = request ? chm_foundation_cstr(chm_objc_send0(request, "URL")) : NULL;
    if (!address && request) address = chm_foundation_cstr(chm_objc_send0(request, "absoluteString"));
    (void)base;
    chm_session_log(g_sess, "webkit: %s without renderer %s", sel, address ? address : "about:blank");
    if (value) value->integers[2] = 1;
}

static void web_stop(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (value) value->integers[2] = 0;
}

static signed char web_flag(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (value && !strcmp(sel, "isLoading")) return (signed char)value->integers[2];
    return 0;
}

static double web_progress(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->values[0] : 0;
}

static void *web_title(void *self, const char *sel)
{
    (void)sel;
    return launch_find(self) ? NULL : NULL;
}

static void *web_url(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->third : NULL;
}

static void *web_user_script(void *cls, const char *sel, void *source, long time, signed char main_frame)
{
    (void)cls;
    (void)sel;
    if (!source) return NULL;
    void *object = chm_foundation_alloc_obj("WKUserScript", 0);
    launch_object *value = launch_record(object);
    if (!value) return NULL;
    value->first = chm_foundation_string(chm_foundation_cstr(source));
    value->integers[0] = time;
    value->integers[1] = main_frame ? 1 : 0;
    return object;
}

static void *web_script_source(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static long web_script_time(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->integers[0] : 0;
}

static signed char web_script_main(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (signed char)value->integers[1] : 0;
}

static void *web_auth_new(void *cls, const char *sel, void *url, void *scheme, void *handler)
{
    (void)cls;
    (void)sel;
    if (!url) return NULL;
    void *object = chm_foundation_alloc_obj("ASWebAuthenticationSession", 0);
    launch_object *value = launch_record(object);
    if (!value) return NULL;
    value->first = url;
    value->second = scheme;
    value->third = chm_block_copy(handler);
    return object;
}

static signed char web_auth_start(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (!value) return 0;
    const char *address = chm_foundation_cstr(chm_objc_send0(value->first, "absoluteString"));
    chm_session_log(g_sess, "authentication: session start without browser %s", address ? address : "unknown");
    if (value->third) {
        void *error = chm_foundation_error("com.apple.AuthenticationServices.WebAuthenticationSession", 1, "此环境不提供网页认证界面");
        struct { void *isa; int flags, reserved; void (*invoke)(void *, void *, void *); } *block = value->third;
        if (block && block->invoke) block->invoke(block, NULL, error);
        chm_block_release(value->third);
        value->third = NULL;
    }
    value->integers[0] = 1;
    return 1;
}

static void web_auth_cancel(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (value) value->integers[0] = 0;
}

static void web_auth_flag(void *self, const char *sel, signed char flag)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setPrefersEphemeralWebBrowserSession:")) value->integers[1] = !!flag;
    else if (!strcmp(sel, "setPresentationContextProvider:")) value->second = NULL;
}

static signed char web_auth_can_start(void *self, const char *sel)
{
    (void)sel;
    return launch_find(self) ? 1 : 0;
}

static const chm_host_method launch_methods[] = {
    {"UIBlurEffect", "effectWithStyle:", "@@:q", effect_new, 1},
    {"UIBlurEffect", "style", "q@:", effect_style, 0},
    {"UIVisualEffectView", "setEffect:", "v@:@", visual_set_effect, 0},
    {"UIVisualEffectView", "effect", "@@:", visual_effect, 0},
    {"UIVisualEffectView", "contentView", "@@:", visual_content, 0},
    {"UIMenuItem", "initWithTitle:action:", "@@:@@", menu_item_new, 0},
    {"UIMenuItem", "title", "@@:", menu_item_title, 0},
    {"UIMenuItem", "setTitle:", "v@:@", menu_item_set_title, 0},
    {"UIMenuItem", "action", "@@:", menu_item_action, 0},
    {"UIMenuController", "sharedMenuController", "@@:", menu_shared, 1},
    {"UIMenuController", "setMenuItems:", "v@:@", menu_set_items, 0},
    {"UIMenuController", "menuItems", "@@:", menu_items, 0},
    {"UIMenuController", "setTargetRect:inView:", "v@:{CGRect={CGPoint=dd}{CGSize=dd}}@", menu_target_rect, 0},
    {"UIMenuController", "setMenuVisible:animated:", "v@:cc", menu_set_visible, 0},
    {"UIMenuController", "setMenuVisible:", "v@:c", menu_set_visible, 0},
    {"UIMenuController", "isMenuVisible", "c@:", menu_visible, 0},
    {"UIMenuController", "hideMenu", "v@:", menu_hide, 0},
    {"UIPinchGestureRecognizer", "init", "@@:", pinch_new, 0},
    {"UIPinchGestureRecognizer", "scale", "d@:", pinch_scale, 0},
    {"UIPinchGestureRecognizer", "setScale:", "v@:d", pinch_set_scale, 0},
    {"UIPinchGestureRecognizer", "velocity", "d@:", pinch_velocity, 0},
    {"UIGestureRecognizer", "addTarget:action:", "v@:@@", gesture_add_target, 0},
    {"UIGestureRecognizer", "setView:", "v@:@", gesture_set_view, 0},
    {"UIGestureRecognizer", "view", "@@:", gesture_view, 0},
    {"UIGestureRecognizer", "setState:", "v@:q", gesture_set_state, 0},
    {"UIGestureRecognizer", "state", "q@:", gesture_state, 0},
    {"UIGestureRecognizer", "setEnabled:", "v@:c", gesture_set_enabled, 0},
    {"UIGestureRecognizer", "isEnabled", "c@:", gesture_enabled, 0},
    {"UIGestureRecognizer", "setCancelsTouchesInView:", "v@:c", gesture_set_cancels, 0},
    {"UIScreenEdgePanGestureRecognizer", "setEdges:", "v@:Q", edge_set_edges, 0},
    {"UIScreenEdgePanGestureRecognizer", "edges", "Q@:", edge_edges, 0},
    {"UIBezierPath", "bezierPath", "@@:", path_new, 1},
    {"UIBezierPath", "bezierPathWithRect:", "@@:{CGRect={CGPoint=dd}{CGSize=dd}}", path_new, 1},
    {"UIBezierPath", "bezierPathWithRoundedRect:cornerRadius:", "@@:{CGRect={CGPoint=dd}{CGSize=dd}}d", path_new, 1},
    {"UIBezierPath", "moveToPoint:", "v@:{CGPoint=dd}", path_move, 0},
    {"UIBezierPath", "addLineToPoint:", "v@:{CGPoint=dd}", path_line, 0},
    {"UIBezierPath", "addQuadCurveToPoint:controlPoint:", "v@:{CGPoint=dd}{CGPoint=dd}", path_quad, 0},
    {"UIBezierPath", "addCurveToPoint:controlPoint1:controlPoint2:", "v@:{CGPoint=dd}{CGPoint=dd}{CGPoint=dd}", path_curve, 0},
    {"UIBezierPath", "addArcWithCenter:radius:startAngle:endAngle:clockwise:", "v@:{CGPoint=dd}dddc", path_arc, 0},
    {"UIBezierPath", "closePath", "v@:", path_close, 0},
    {"UIBezierPath", "appendPath:", "v@:@", path_append, 0},
    {"UIBezierPath", "bounds", "{CGRect={CGPoint=dd}{CGSize=dd}}@:", path_bounds, 0},
    {"UIBezierPath", "currentPoint", "{CGPoint=dd}@:", path_current, 0},
    {"UIBezierPath", "isEmpty", "c@:", path_empty, 0},
    {"UIBezierPath", "removeAllPoints", "v@:", path_clear, 0},
    {"UIBezierPath", "lineWidth", "d@:", path_line_width, 0},
    {"UIBezierPath", "setLineWidth:", "v@:d", path_set_line_width, 0},
    {"UIBezierPath", "fill", "v@:", path_draw, 0},
    {"UIBezierPath", "stroke", "v@:", path_draw, 0},
    {"UIBezierPath", "CGPath", "^v@:", path_cgpath, 0},
    {"CAShapeLayer", "setPath:", "v@:^v", shape_set_path, 0},
    {"CAShapeLayer", "path", "^v@:", shape_path, 0},
    {"CAShapeLayer", "setFillColor:", "v@:^v", shape_set_color, 0},
    {"CAShapeLayer", "fillColor", "^v@:", shape_color, 0},
    {"CAShapeLayer", "setStrokeColor:", "v@:^v", shape_set_color, 0},
    {"CAShapeLayer", "strokeColor", "^v@:", shape_color, 0},
    {"CAShapeLayer", "setLineWidth:", "v@:d", shape_set_number, 0},
    {"CAShapeLayer", "lineWidth", "d@:", shape_number, 0},
    {"CAShapeLayer", "setStrokeStart:", "v@:d", shape_set_number, 0},
    {"CAShapeLayer", "strokeStart", "d@:", shape_number, 0},
    {"CAShapeLayer", "setStrokeEnd:", "v@:d", shape_set_number, 0},
    {"CAShapeLayer", "strokeEnd", "d@:", shape_number, 0},
    {"UITextView", "setEditable:", "v@:c", text_view_flag, 0},
    {"UITextView", "setSelectable:", "v@:c", text_view_flag, 0},
    {"UITextView", "isEditable", "c@:", text_view_editable, 0},
    {"UITextView", "setTextContainerInset:", "v@:{UIEdgeInsets=dddd}", text_view_inset, 0},
    {"UITextView", "setContentInset:", "v@:{UIEdgeInsets=dddd}", text_view_inset, 0},
    {"WKWebViewConfiguration", "init", "@@:", web_config_new, 0},
    {"WKWebViewConfiguration", "setAllowsInlineMediaPlayback:", "v@:c", web_config_flag, 0},
    {"WKWebViewConfiguration", "allowsInlineMediaPlayback", "c@:", web_config_get, 0},
    {"WKWebViewConfiguration", "setValue:forKey:", "v@:@@", web_config_value, 0},
    {"WKWebViewConfiguration", "valueForKey:", "@@:@", web_config_get_value, 0},
    {"WKWebView", "initWithFrame:configuration:", "@@:{CGRect={CGPoint=dd}{CGSize=dd}}@", web_view_new, 0},
    {"WKWebView", "configuration", "@@:", web_configuration, 0},
    {"WKWebView", "scrollView", "@@:", web_scroll, 0},
    {"WKWebView", "loadRequest:", "v@:@", web_load, 0},
    {"WKWebView", "loadHTMLString:baseURL:", "v@:@@", web_load, 0},
    {"WKWebView", "stopLoading", "v@:", web_stop, 0},
    {"WKWebView", "reload", "v@:", web_load, 0},
    {"WKWebView", "isLoading", "c@:", web_flag, 0},
    {"WKWebView", "canGoBack", "c@:", web_flag, 0},
    {"WKWebView", "canGoForward", "c@:", web_flag, 0},
    {"WKWebView", "estimatedProgress", "d@:", web_progress, 0},
    {"WKWebView", "title", "@@:", web_title, 0},
    {"WKWebView", "URL", "@@:", web_url, 0},
    {"WKUserScript", "initWithSource:injectionTime:forMainFrameOnly:", "@@:@qc", web_user_script, 0},
    {"WKUserScript", "source", "@@:", web_script_source, 0},
    {"WKUserScript", "injectionTime", "q@:", web_script_time, 0},
    {"WKUserScript", "isForMainFrameOnly", "c@:", web_script_main, 0},
    {"ASWebAuthenticationSession", "initWithURL:callbackURLScheme:completionHandler:", "@@:@@@", web_auth_new, 0},
    {"ASWebAuthenticationSession", "start", "c@:", web_auth_start, 0},
    {"ASWebAuthenticationSession", "cancel", "v@:", web_auth_cancel, 0},
    {"ASWebAuthenticationSession", "canStart", "c@:", web_auth_can_start, 0},
    {"ASWebAuthenticationSession", "setPrefersEphemeralWebBrowserSession:", "v@:c", web_auth_flag, 0},
    {"ASWebAuthenticationSession", "setPresentationContextProvider:", "v@:@", web_auth_flag, 0}
};

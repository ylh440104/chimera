typedef struct ui_alert {
    void *object, *message, *preferred;
    void *actions, *fields;
    ui_view *panel, *heading, *body;
    long style;
    struct ui_alert *next;
} ui_alert;

static ui_alert *alerts;

static ui_alert *alert_state(void *object)
{
    for (ui_alert *a = alerts; a; a = a->next) if (a->object == object) return a;
    return NULL;
}

static void invoke_one(void *object, void *argument)
{
    struct { void *isa; int flags, reserved; void (*invoke)(void *, void *); } *b = object;
    if (b && b->invoke) b->invoke(b, argument);
}

static void *alert_new(void *cls, const char *sel, void *title, void *message, long style)
{
    (void)sel;
    if (cls != chm_foundation_lookup_class("UIAlertController") || (style != 0 && style != 1)) return NULL;
    ui_alert *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->object = chm_foundation_alloc_class(cls, 0);
    a->actions = chm_objc_new_object("NSMutableArray");
    a->fields = chm_objc_new_object("NSMutableArray");
    if (!a->object || !a->actions || !a->fields) { free(a); return NULL; }
    a->message = message; a->style = style;
    a->next = alerts; alerts = a;
    wc_set_title(a->object, "setTitle:", title);
    return a->object;
}

static void *alert_action_new(void *cls, const char *sel, void *title, long style, void *handler)
{
    (void)sel;
    if (cls != chm_foundation_lookup_class("UIAlertAction") || style < 0 || style > 2) return NULL;
    void *object = chm_foundation_alloc_class(cls, 0);
    ui_item *item = item_state(object);
    if (!item) return NULL;
    item->title = title; item->style = style;
    item->handler = chm_block_copy(handler);
    if (handler && !item->handler) return NULL;
    return object;
}

static void alert_add_action(void *self, const char *sel, void *object)
{
    (void)sel;
    ui_alert *a = alert_state(self);
    if (!a || !chm_objc_is_kind(object, "UIAlertAction")) return;
    ui_item *item = item_state(object);
    if (!item || item->owner) return;
    item->owner = self;
    chm_objc_send1(a->actions, "addObject:", object);
    layout_dirty = 1;
}

static void *alert_property(void *self, const char *sel)
{
    ui_alert *a = alert_state(self);
    if (!a) return NULL;
    if (!strcmp(sel, "actions")) return a->actions;
    if (!strcmp(sel, "textFields")) return a->fields;
    if (!strcmp(sel, "preferredAction")) return a->preferred;
    return a->message;
}

static long alert_style(void *self, const char *sel)
{
    (void)sel;
    ui_alert *a = alert_state(self);
    return a ? a->style : 0;
}

static void alert_set_property(void *self, const char *sel, void *value)
{
    ui_alert *a = alert_state(self);
    if (!a) return;
    if (!strcmp(sel, "setPreferredAction:")) a->preferred = value;
    else a->message = value;
    layout_dirty = 1;
}

static void field_placeholder(void *self, const char *sel, void *text)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v || !chm_objc_is_kind(self, "UITextField")) return;
    char *copy = strdup(chm_foundation_cstr(text));
    if (copy) { free(v->placeholder); v->placeholder = copy; }
}

static void *field_get_placeholder(void *self, const char *sel)
{
    (void)sel; ui_view *v = as_view(self);
    return v && v->placeholder ? chm_foundation_string(v->placeholder) : NULL;
}

static void field_option(void *self, const char *sel, long value)
{
    ui_view *v = as_view(self);
    if (!v || !chm_objc_is_kind(self, "UITextField")) return;
    if (!strcmp(sel, "setSecureTextEntry:")) v->secure = !!value;
    else if (!strcmp(sel, "setKeyboardType:")) v->keyboard_type = (int)value;
    else if (!strcmp(sel, "setAutocapitalizationType:")) v->capitalization = (int)value;
}

static long field_get_option(void *self, const char *sel)
{
    ui_view *v = as_view(self);
    if (!v) return 0;
    if (!strcmp(sel, "isSecureTextEntry")) return v->secure;
    if (!strcmp(sel, "keyboardType")) return v->keyboard_type;
    return v->capitalization;
}

static int field_focus(void *self, const char *sel)
{
    ui_view *v = as_view(self);
    if (!v || !chm_objc_is_kind(self, "UITextField")) return 0;
    if (!strcmp(sel, "isFirstResponder")) return focused_field == v;
    if (!strcmp(sel, "resignFirstResponder")) {
        if (focused_field == v) { focused_field = NULL; send_control_events(v, 1u << 18); }
        return 1;
    }
    if (v->disabled || v->hidden) return 0;
    int (*should)(void *, const char *, void *) = chm_objc_find_imp(v->delegate, "textFieldShouldBeginEditing:");
    if (should && !should(v->delegate, "textFieldShouldBeginEditing:", self)) return 0;
    focused_field = v;
    send_control_events(v, 1u << 16);
    return 1;
}

static void alert_add_field(void *self, const char *sel, void *handler)
{
    (void)sel;
    ui_alert *a = alert_state(self);
    if (!a || a->style != 1) return;
    void *object = chm_objc_new_object("UITextField");
    ui_view *v = as_view(object);
    if (!v) return;
    v->bg = 0xff111111u; v->clips = 1;
    chm_objc_send1(a->fields, "addObject:", object);
    invoke_one(handler, object);
    layout_dirty = 1;
}

static ui_view *dialog_view(const char *kind, ui_view *parent)
{
    ui_view *v = as_view(chm_objc_new_object(kind));
    if (!v || !view_add(parent, v)) return NULL;
    return v;
}

static void dialog_frame(ui_view *v, double x, double y, double w, double h)
{
    if (!v) return;
    v->x = x; v->y = y; v->w = w; v->h = h;
}

static void dialogs_prepare(void)
{
    for (ui_alert *a = alerts; a; a = a->next) {
        ui_vc *vc = as_vc(a->object);
        if (!vc || !vc->presenting) continue;
        ui_view *root = as_view(vc->view);
        if (!root) continue;
        root->bg = 0x99000000u;
        if (!a->panel) {
            a->panel = dialog_view("UIView", root);
            if (!a->panel) return;
            a->panel->bg = 0xff2c2c2eu; a->panel->clips = 1;
            a->heading = dialog_view("UILabel", a->panel);
            a->body = dialog_view("UILabel", a->panel);
            if (!a->heading || !a->body) return;
            a->heading->font_align = a->body->font_align = 1;
            a->heading->font_size = 19; a->body->font_size = 15;
            a->body->number_of_lines = 0;
        }
        size_t fields = (size_t)chm_objc_send0(a->fields, "count");
        size_t actions = (size_t)chm_objc_send0(a->actions, "count");
        if (fields > 16 || actions > 128) continue;
        double w = fmin(a->style ? 360 : 560, fmax(0, root->w - 32));
        const char *message = chm_foundation_cstr(a->message);
        double message_height = *message ? fmin(160, 22 * (1 + strlen(message) / 36)) : 0;
        double h = 64 + message_height + fields * 52 + actions * 48;
        h = fmin(h, fmax(0, root->h - 32));
        dialog_frame(a->panel, (root->w - w) / 2, a->style ? (root->h - h) / 2 : root->h - h - 16, w, h);
        l_set_text(a->heading->object, "setText:", vc->title);
        l_set_text(a->body->object, "setText:", a->message);
        dialog_frame(a->heading, 16, 8, w - 32, 40);
        dialog_frame(a->body, 16, 48, w - 32, message_height);
        double y = 56 + message_height;
        for (size_t i = 0; i < fields; i++) {
            ui_view *field = as_view(chm_objc_send1(a->fields, "objectAtIndex:", (void *)i));
            if (!field) continue;
            if (field->parent != a->panel) view_add(a->panel, field);
            dialog_frame(field, 16, y, w - 32, 40); y += 52;
        }
        for (size_t i = 0; i < actions; i++) {
            void *object = chm_objc_send1(a->actions, "objectAtIndex:", (void *)i);
            ui_item *item = item_state(object);
            if (!item) continue;
            ui_view *button = as_view(item->button);
            if (!button) {
                button = dialog_view("UIButton", a->panel);
                if (!button) continue;
                item->button = button->object; button->alert_action = object;
            }
            button->disabled = item->disabled;
            button->font_align = 1;
            button->text_color = item->disabled ? 0xff777777u : item->style == 2 ? 0xffff453au : 0xff0a84ffu;
            l_set_text(button->object, "setText:", item->title);
            dialog_frame(button, 0, y, w, 48); y += 48;
        }
    }
}

static int dialog_select(void *object)
{
    ui_item *item = item_state(object);
    if (!item || item->disabled || !item->owner) return 0;
    ui_vc *vc = as_vc(item->owner);
    if (!vc || g_modal != vc) return 0;
    void *handler = chm_block_copy(item->handler);
    focused_field = NULL;
    wc_dismiss(vc->object, "dismissViewControllerAnimated:completion:", 0, NULL);
    invoke_one(handler, object);
    chm_block_release(handler);
    return 1;
}

uint64_t chm_ui_focused_field(void) { return focused_field ? (uint64_t)(uintptr_t)focused_field->object : 0; }
const char *chm_ui_field_text(void) { return focused_field && focused_field->text ? focused_field->text : ""; }
const char *chm_ui_field_placeholder(void) { return focused_field && focused_field->placeholder ? focused_field->placeholder : ""; }
int chm_ui_field_options(void) { return focused_field ? (focused_field->secure ? 256 : 0) | (focused_field->keyboard_type & 255) : 0; }

typedef struct { uint64_t token; const char *text; int commit; } field_edit;
static int field_edit_body(void *context)
{
    field_edit *edit = context;
    ui_view *v = focused_field;
    if (!v || (uint64_t)(uintptr_t)v->object != edit->token) return 0;
    if (edit->commit) {
        l_set_text(v->object, "setText:", chm_foundation_string(edit->text));
        send_control_events(v, 1u << 17);
        void (*did)(void *, const char *, void *) = chm_objc_find_imp(v->delegate, "textFieldDidEndEditing:");
        if (did) did(v->delegate, "textFieldDidEndEditing:", v->object);
    }
    focused_field = NULL;
    send_control_events(v, 1u << 18);
    return 1;
}
int chm_ui_edit_text(uint64_t token, const char *text, int commit)
{
    if (!g_sess || g_sess->exit_code == -2 || !text) return -1;
    field_edit edit = {token, text, commit};
    int rc = chm_session_callback(g_sess, field_edit_body, &edit);
    if (rc == -2) g_sess->exit_code = -2;
    return rc;
}

static void *activity_init(void *self, const char *sel, long style)
{
    (void)style;
    return v_init_frame(self, sel, n_rect(0, 0, 28, 28));
}
static void activity_animate(void *self, const char *sel)
{
    ui_view *v = as_view(self);
    if (!v) return;
    v->animating = !strcmp(sel, "startAnimating");
    v->hidden = !v->animating;
}
static int activity_is_animating(void *self, const char *sel)
{
    (void)sel; ui_view *v = as_view(self); return v && v->animating;
}
static void *popover_get(void *self, const char *sel) { (void)sel; return alert_state(self) ? self : NULL; }
static void popover_set(void *self, const char *sel, void *value) { (void)sel; ui_item *i = item_state(self); if (i) i->right_item = value; }

static void dialogs_shutdown(void)
{
    focused_field = NULL;
    while (alerts) { ui_alert *next = alerts->next; free(alerts); alerts = next; }
    for (ui_item *i = items; i; i = i->next) { chm_block_release(i->handler); i->handler = NULL; }
}
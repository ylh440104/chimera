typedef struct attr_record {
    void *object;
    char *text;
    void *attributes;
    struct attr_record *next;
} attr_record;

typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, void *, unsigned long, unsigned long, signed char *);
} range_block;

typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, signed char);
} bool_block;

typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, void *, void *);
} result_block;

static attr_record *attr_records;

static attr_record *attr_slot(void *object)
{
    if (!object) return NULL;
    for (attr_record *record = attr_records; record; record = record->next)
        if (record->object == object) return record;
    attr_record *record = calloc(1, sizeof(*record));
    if (!record) return NULL;
    record->object = object;
    record->next = attr_records;
    attr_records = record;
    return record;
}

static void *attr_init_string(void *self, const char *sel, void *string)
{
    (void)sel;
    attr_record *record = attr_slot(self);
    const char *text = string ? chm_foundation_cstr(string) : NULL;
    if (!record) return self;
    char *copy = strdup(text ? text : "");
    free(record->text);
    record->text = copy;
    return self;
}

static void *attr_init_attributes(void *self, const char *sel, void *string, void *attributes)
{
    attr_init_string(self, sel, string);
    attr_record *record = attr_slot(self);
    if (record) record->attributes = attributes;
    return self;
}

static void *attr_string_value(void *self, const char *sel)
{
    (void)sel;
    attr_record *record = attr_slot(self);
    return chm_foundation_string(record && record->text ? record->text : "");
}

static unsigned long attr_length(void *self, const char *sel)
{
    (void)sel;
    attr_record *record = attr_slot(self);
    return record && record->text ? (unsigned long)strlen(record->text) : 0;
}

static void *attr_attributes_at(void *self, const char *sel, unsigned long index, void *range)
{
    (void)sel;
    (void)index;
    (void)range;
    attr_record *record = attr_slot(self);
    void *dictionary = chm_objc_send0(chm_foundation_lookup_class("NSMutableDictionary"), "dictionary");
    if (record && record->attributes) return record->attributes;
    return dictionary;
}

static void attr_enumerate(void *self, const char *sel, void *attribute, void *range, unsigned long options, void *block)
{
    (void)sel;
    (void)options;
    (void)range;
    attr_record *record = attr_slot(self);
    range_block *invoke = block;
    if (!record || !invoke || !invoke->invoke) return;
    void *value = record->attributes;
    signed char stop = 0;
    invoke->invoke(invoke, value, 0, record->text ? (unsigned long)strlen(record->text) : 0, &stop);
    (void)attribute;
}

static void attr_append(void *self, const char *sel, void *other)
{
    (void)sel;
    attr_record *target = attr_slot(self), *source = attr_slot(other);
    if (!target || !source || !source->text) return;
    size_t left = target->text ? strlen(target->text) : 0;
    size_t right = strlen(source->text);
    char *joined = realloc(target->text, left + right + 1);
    if (!joined) return;
    memcpy(joined + left, source->text, right);
    joined[left + right] = 0;
    target->text = joined;
}

static void *attr_copy(void *self, const char *sel)
{
    void *copy = chm_foundation_alloc_obj("NSMutableAttributedString", 0);
    if (!copy) return NULL;
    attr_record *source = attr_slot(self);
    (void)sel;
    if (source && source->text) attr_init_string(copy, NULL, chm_foundation_string(source->text));
    return copy;
}

static void label_set_attributed(void *self, const char *sel, void *text)
{
    (void)sel;
    attr_record *record = attr_slot(text);
    if (!record || !record->text) return;
    l_set_text(self, "setText:", chm_foundation_string(record->text));
}

static void button_set_image_state(void *self, const char *sel, void *image, long state)
{
    (void)sel;
    ui_view *view = as_view(self);
    if (!view) return;
    if (state == 0) {
        view->image = image;
        layout_dirty = 1;
    } else {
        launch_object *value = launch_record(self);
        if (value) value->second = image;
    }
}

static void *button_image_state(void *self, const char *sel, long state)
{
    (void)sel;
    launch_object *value = launch_find(self);
    if (state == 0) {
        ui_view *view = as_view(self);
        return view ? view->image : NULL;
    }
    return value ? value->second : NULL;
}

static void gesture_add(void *self, const char *sel, void *gesture)
{
    (void)sel;
    launch_object *value = launch_record(gesture);
    if (value) value->first = self;
}

static void gesture_remove(void *self, const char *sel, void *gesture)
{
    (void)self;
    (void)sel;
    launch_object *value = launch_find(gesture);
    if (value) value->first = NULL;
}

static ui_point gesture_location(void *self, const char *sel, void *view)
{
    (void)self;
    (void)sel;
    (void)view;
    return (ui_point){0, 0};
}

static unsigned long gesture_touches(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return 0;
}

static void *standard_color(void *cls, const char *sel)
{
    (void)cls;
    static const struct {
        const char *sel;
        double r, g, b, a;
    } table[] = {
        {"redColor", 1, 0.23, 0.19, 1},
        {"systemRedColor", 1, 0.23, 0.19, 1},
        {"blueColor", 0, 0.48, 1, 1},
        {"systemBlueColor", 0, 0.48, 1, 1},
        {"greenColor", 0.2, 0.78, 0.35, 1},
        {"systemGreenColor", 0.2, 0.78, 0.35, 1},
        {"orangeColor", 1, 0.58, 0, 1},
        {"systemOrangeColor", 1, 0.58, 0, 1},
        {"yellowColor", 1, 0.8, 0, 1},
        {"systemYellowColor", 1, 0.8, 0, 1},
        {"purpleColor", 0.69, 0.32, 0.87, 1},
        {"systemPurpleColor", 0.69, 0.32, 0.87, 1},
        {"grayColor", 0.56, 0.56, 0.58, 1},
        {"systemGrayColor", 0.56, 0.56, 0.58, 1},
        {"lightGrayColor", 0.78, 0.78, 0.8, 1},
        {"darkGrayColor", 0.33, 0.33, 0.35, 1},
        {"labelColor", 0.1, 0.1, 0.12, 1},
        {"secondaryLabelColor", 0.4, 0.4, 0.45, 1},
        {"tertiaryLabelColor", 0.6, 0.6, 0.65, 1},
        {"systemBackgroundColor", 1, 1, 1, 1},
        {"secondarySystemBackgroundColor", 0.95, 0.95, 0.97, 1},
        {"tertiarySystemBackgroundColor", 1, 1, 1, 1},
        {"groupTableViewBackgroundColor", 0.95, 0.95, 0.97, 1},
        {"separatorColor", 0.6, 0.6, 0.65, 0.3},
        {"tableSeparatorColor", 0.6, 0.6, 0.65, 0.3}
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(*table); i++)
        if (!strcmp(table[i].sel, sel)) return color_new(table[i].r, table[i].g, table[i].b, table[i].a);
    return NULL;
}

static void *menu_new(void *cls, const char *sel, void *title, void *children)
{
    (void)sel;
    (void)cls;
    void *object = chm_foundation_alloc_obj("UIMenu", 0);
    launch_object *value = launch_record(object);
    if (value) {
        value->first = title;
        value->second = children;
    }
    return object;
}

static void *menu_new_full(void *cls, const char *sel, void *title, void *image, void *identifier,
                           unsigned long options, void *children)
{
    (void)sel;
    (void)image;
    (void)identifier;
    (void)options;
    (void)cls;
    void *object = chm_foundation_alloc_obj("UIMenu", 0);
    launch_object *value = launch_record(object);
    if (value) {
        value->first = title;
        value->second = children;
    }
    return object;
}

static void *action_new(void *cls, const char *sel, void *title, void *image, void *identifier, void *handler)
{
    (void)cls;
    (void)sel;
    (void)image;
    (void)identifier;
    void *object = chm_foundation_alloc_obj("UIAction", 0);
    launch_object *value = launch_record(object);
    if (value) {
        value->first = title;
        value->third = handler;
    }
    return object;
}

static void *menu_property(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return NULL;
    if (!strcmp(sel, "title")) return value->first;
    if (!strcmp(sel, "children")) return value->second;
    return NULL;
}

static void *action_property(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return NULL;
    if (!strcmp(sel, "title")) return value->first;
    if (!strcmp(sel, "handler")) return value->third;
    return NULL;
}

static void *activity_new(void *self, const char *sel, void *items, void *activities)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) {
        value->first = items;
        value->second = activities;
    }
    return self;
}

static void *picker_new(void *self, const char *sel, void *types, long mode)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) {
        value->first = types;
        value->integers[1] = mode;
    }
    return self;
}

static void application_open_url(void *self, const char *sel, void *url, void *options, void *completion)
{
    (void)self;
    (void)sel;
    (void)options;
    const char *address = url ? chm_foundation_cstr(chm_objc_send0(url, "absoluteString")) : NULL;
    chm_session_log(g_sess, "openURL %s (no external handler)", address ? address : "?");
    bool_block *block = completion;
    if (block && block->invoke) block->invoke(block, 0);
}

static signed char application_can_open(void *self, const char *sel, void *url)
{
    (void)self;
    (void)sel;
    (void)url;
    return 0;
}

static char *html_trimmed(const char *html, size_t length)
{
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, html, length);
    copy[length] = 0;
    return copy;
}

static int html_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static void html_entity(const char **cursor, char *out, size_t *position, size_t capacity)
{
    const char *text = *cursor;
    static const struct {
        const char *name;
        char value;
    } entities[] = {
        {"&" "amp;", '&'}, {"&" "lt;", '<'}, {"&" "gt;", '>'}, {"&#39;", 39}, {"&" "nbsp;", ' '}
    };
    if (!strncmp(text, "&quo", 4) && text[4] == 't' && text[5] == ';') {
        if (*position + 1 < capacity) out[(*position)++] = 34;
        *cursor = text + 6;
        return;
    }
    for (size_t i = 0; i < sizeof(entities) / sizeof(*entities); i++) {
        size_t size = strlen(entities[i].name);
        if (!strncmp(text, entities[i].name, size)) {
            if (*position + 1 < capacity) out[(*position)++] = entities[i].value;
            *cursor = text + size;
            return;
        }
    }
    if (*position + 1 < capacity) out[(*position)++] = *text;
    *cursor = text + 1;
}

static char *html_plain_text(const char *html, size_t length)
{
    char *source = html_trimmed(html, length);
    if (!source) return NULL;
    char *out = malloc(length + 1);
    if (!out) {
        free(source);
        return NULL;
    }
    size_t position = 0, index = 0;
    int space = 0;
    while (source[index]) {
        char c = source[index];
        if (c == '<') {
            const char *end = strchr(source + index, '>');
            size_t tag_end = end ? (size_t)(end - source) : length;
            if (!strncmp(source + index, "<script", 7) || !strncmp(source + index, "<style", 6)) {
                const char *close = strstr(source + index, !strncmp(source + index, "<script", 7)
                                                           ? "</script" : "</style");
                if (!close) break;
                end = strchr(close, '>');
                index = end ? (size_t)(end - source) + 1 : length;
                continue;
            }
            int block = !strncmp(source + index, "<br", 3) || !strncmp(source + index, "</p", 3) ||
                        !strncmp(source + index, "</div", 5) || !strncmp(source + index, "</li", 4) ||
                        !strncmp(source + index, "</h", 3) || !strncmp(source + index, "</tr", 4);
            if (block && position && out[position - 1] != '\n' && position + 1 < length + 1)
                out[position++] = '\n';
            index = end ? tag_end + 1 : length;
            continue;
        }
        if (c == '&') {
            const char *cursor = source + index;
            html_entity(&cursor, out, &position, length + 1);
            index = (size_t)(cursor - source);
            continue;
        }
        if (html_space((unsigned char)c)) {
            space = 1;
            index++;
            continue;
        }
        if (space && position && out[position - 1] != '\n' && position + 1 < length + 1)
            out[position++] = ' ';
        space = 0;
        if (position < length) out[position++] = c;
        index++;
    }
    out[position] = 0;
    free(source);
    return out;
}

static void web_apply_text(void *self, const char *text)
{
    ui_view *view = as_view(self);
    if (!view || !text) return;
    char *copy = strdup(text);
    if (!copy) return;
    free(view->text);
    view->text = copy;
    view->font_size = 14;
    view->text_color = 0xff333333u;
    view->number_of_lines = 0;
    layout_dirty = 1;
}

static void web_render_bytes(void *object, const void *bytes, size_t length)
{
    char *text = html_plain_text(bytes, length);
    if (text) web_apply_text(object, text);
    free(text);
}

static void web_load_address(void *self, const char *address)
{
    if (!address) return;
    if (!strncmp(address, "file://", 7)) {
        const char *path = address + 7;
        FILE *file = fopen(path, "rb");
        if (!file) {
            chm_session_log(g_sess, "webkit: cannot open %s", path);
            return;
        }
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size <= 0 || size > 8 * 1024 * 1024) {
            fclose(file);
            return;
        }
        char *buffer = malloc((size_t)size + 1);
        if (!buffer) {
            fclose(file);
            return;
        }
        size_t got = fread(buffer, 1, (size_t)size, file);
        fclose(file);
        buffer[got] = 0;
        web_render_bytes(self, buffer, got);
        free(buffer);
        return;
    }
    if (!strncmp(address, "http://", 7) || !strncmp(address, "https://", 8)) {
        if (!chm_network_page(address, self))
            chm_session_log(g_sess, "webkit: cannot start load %s", address);
        return;
    }
    chm_session_log(g_sess, "webkit: unsupported scheme %s", address);
}

static const char *request_address(void *request)
{
    if (!request) return NULL;
    void *url = chm_objc_send0(request, "URL");
    if (url) {
        const char *text = chm_foundation_cstr(chm_objc_send0(url, "absoluteString"));
        if (text) return text;
    }
    return chm_foundation_cstr(chm_objc_send0(request, "absoluteString"));
}

static void web_load_request_new(void *self, const char *sel, void *request, void *base)
{
    (void)sel;
    (void)base;
    web_load_address(self, request_address(request));
}

static void web_load_html(void *self, const char *sel, void *html, void *base)
{
    (void)sel;
    (void)base;
    const char *text = html ? chm_foundation_cstr(html) : NULL;
    if (!text) return;
    web_render_bytes(self, text, strlen(text));
}

static void web_load_file_url(void *self, const char *sel, void *url, void *read_access)
{
    (void)sel;
    (void)read_access;
    const char *address = url ? chm_foundation_cstr(chm_objc_send0(url, "absoluteString")) : NULL;
    web_load_address(self, address);
}

static void *controller_content_controller(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return NULL;
    if (!value->third) value->third = chm_foundation_alloc_obj("WKUserContentController", 0);
    return value->third;
}

static void controller_set_content_controller(void *self, const char *sel, void *controller)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->third = controller;
}

static void web_set_navigation_delegate(void *self, const char *sel, void *delegate)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->first = delegate;
}

static void *web_navigation_delegate(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void web_set_user_content(void *self, const char *sel, void *controller)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->second = controller;
}

static void *web_user_content(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->second : NULL;
}

static void *user_content_new(void *self, const char *sel)
{
    (void)sel;
    return self;
}

static void user_content_add_script(void *self, const char *sel, void *script)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value || !script) return;
    void *list = value->first;
    if (!list) list = chm_objc_send0(chm_foundation_lookup_class("NSMutableArray"), "array");
    if (!list) return;
    chm_objc_send1(list, "addObject:", script);
    value->first = list;
}

static void *user_content_scripts(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void user_content_remove_all(void *self, const char *sel)
{
    (void)self;
    (void)sel;
}

static void web_evaluate_script(void *self, const char *sel, void *script, void *completion)
{
    (void)self;
    (void)sel;
    (void)script;
    result_block *block = completion;
    if (!block || !block->invoke) return;
    void *error = chm_foundation_error("WKErrorDomain", 1, "javascript unavailable");
    block->invoke(block, NULL, error);
}

static void *web_view_title_value(void *self, const char *sel)
{
    (void)sel;
    ui_view *view = as_view(self);
    if (!view || !view->text) return chm_foundation_string("");
    const char *line = view->text;
    size_t length = strcspn(line, "\n");
    char *copy = malloc(length + 1);
    if (!copy) return chm_foundation_string("");
    memcpy(copy, line, length);
    copy[length] = 0;
    void *title = chm_foundation_string(copy);
    free(copy);
    return title;
}

static const chm_host_method extra_methods[] = {
    {"NSAttributedString", "initWithString:", "@@:@", attr_init_string, 0},
    {"NSAttributedString", "initWithString:attributes:", "@@:@@", attr_init_attributes, 0},
    {"NSAttributedString", "string", "@@:", attr_string_value, 0},
    {"NSAttributedString", "length", "Q@:", attr_length, 0},
    {"NSAttributedString", "attributesAtIndex:effectiveRange:", "@@:Q^v", attr_attributes_at, 0},
    {"NSAttributedString", "enumerateAttribute:inRange:options:usingBlock:", "v@:@{_NSRange=QQ}Q@?",
     attr_enumerate, 0},
    {"NSMutableAttributedString", "initWithString:", "@@:@", attr_init_string, 0},
    {"NSMutableAttributedString", "initWithString:attributes:", "@@:@@", attr_init_attributes, 0},
    {"NSMutableAttributedString", "appendAttributedString:", "v@:@", attr_append, 0},
    {"NSMutableAttributedString", "string", "@@:", attr_string_value, 0},
    {"NSMutableAttributedString", "length", "Q@:", attr_length, 0},
    {"NSMutableAttributedString", "mutableCopy", "@@:", attr_copy, 0},
    {"NSAttributedString", "mutableCopy", "@@:", attr_copy, 0},
    {"UILabel", "setAttributedText:", "v@:@", label_set_attributed, 0},
    {"UIButton", "setImage:forState:", "v@:@Q", button_set_image_state, 0},
    {"UIButton", "imageForState:", "@@:Q", button_image_state, 0},
    {"UIButton", "setBackgroundImage:forState:", "v@:@Q", button_set_image_state, 0},
    {"UIButton", "backgroundImageForState:", "@@:Q", button_image_state, 0},
    {"UIView", "addGestureRecognizer:", "v@:@", gesture_add, 0},
    {"UIView", "removeGestureRecognizer:", "v@:@", gesture_remove, 0},
    {"UIGestureRecognizer", "locationInView:", "{CGPoint=dd}@:@", gesture_location, 0},
    {"UIGestureRecognizer", "numberOfTouches", "Q@:", gesture_touches, 0},
    {"UIColor", "redColor", "@@:", standard_color, 1},
    {"UIColor", "systemRedColor", "@@:", standard_color, 1},
    {"UIColor", "blueColor", "@@:", standard_color, 1},
    {"UIColor", "systemBlueColor", "@@:", standard_color, 1},
    {"UIColor", "greenColor", "@@:", standard_color, 1},
    {"UIColor", "systemGreenColor", "@@:", standard_color, 1},
    {"UIColor", "orangeColor", "@@:", standard_color, 1},
    {"UIColor", "systemOrangeColor", "@@:", standard_color, 1},
    {"UIColor", "yellowColor", "@@:", standard_color, 1},
    {"UIColor", "systemYellowColor", "@@:", standard_color, 1},
    {"UIColor", "purpleColor", "@@:", standard_color, 1},
    {"UIColor", "systemPurpleColor", "@@:", standard_color, 1},
    {"UIColor", "grayColor", "@@:", standard_color, 1},
    {"UIColor", "systemGrayColor", "@@:", standard_color, 1},
    {"UIColor", "lightGrayColor", "@@:", standard_color, 1},
    {"UIColor", "darkGrayColor", "@@:", standard_color, 1},
    {"UIColor", "labelColor", "@@:", standard_color, 1},
    {"UIColor", "tertiaryLabelColor", "@@:", standard_color, 1},
    {"UIColor", "systemBackgroundColor", "@@:", standard_color, 1},
    {"UIColor", "secondarySystemBackgroundColor", "@@:", standard_color, 1},
    {"UIColor", "tertiarySystemBackgroundColor", "@@:", standard_color, 1},
    {"UIColor", "groupTableViewBackgroundColor", "@@:", standard_color, 1},
    {"UIColor", "separatorColor", "@@:", standard_color, 1},
    {"UIColor", "tableSeparatorColor", "@@:", standard_color, 1},
    {"UIMenu", "menuWithTitle:children:", "@@:@@", menu_new, 1},
    {"UIMenu", "menuWithTitle:image:identifier:options:children:", "@@:@@@@Q@", menu_new_full, 1},
    {"UIAction", "actionWithTitle:image:identifier:handler:", "@@:@@@@@?", action_new, 1},
    {"UIMenu", "title", "@@:", menu_property, 0},
    {"UIMenu", "children", "@@:", menu_property, 0},
    {"UIAction", "title", "@@:", action_property, 0},
    {"UIAction", "handler", "@@:", action_property, 0},
    {"UIActivityViewController", "initWithActivityItems:applicationActivities:", "@@:@@", activity_new, 0},
    {"UIDocumentPickerViewController", "initWithDocumentTypes:inMode:", "@@:@q", picker_new, 0},
    {"UIApplication", "openURL:options:completionHandler:", "v@:@@@?", application_open_url, 0},
    {"UIApplication", "canOpenURL:", "c@:@", application_can_open, 0},
    {"WKWebView", "setNavigationDelegate:", "v@:@", web_set_navigation_delegate, 0},
    {"WKWebView", "navigationDelegate", "@@:", web_navigation_delegate, 0},
    {"WKWebView", "loadRequest:", "v@:@", web_load_request_new, 0},
    {"WKWebView", "loadHTMLString:baseURL:", "v@:@@", web_load_html, 0},
    {"WKWebView", "loadFileURL:allowingReadAccessToURL:", "v@:@@", web_load_file_url, 0},
    {"WKWebView", "evaluateJavaScript:completionHandler:", "v@:@?@?", web_evaluate_script, 0},
    {"WKWebView", "title", "@@:", web_view_title_value, 0},
    {"WKWebViewConfiguration", "userContentController", "@@:", web_user_content, 0},
    {"WKWebViewConfiguration", "setUserContentController:", "v@:@", web_set_user_content, 0},
    {"WKUserContentController", "init", "@@:", user_content_new, 0},
    {"WKUserContentController", "addUserScript:", "v@:@", user_content_add_script, 0},
    {"WKUserContentController", "userScripts", "@@:", user_content_scripts, 0},
    {"WKUserContentController", "removeAllUserScripts", "v@:", user_content_remove_all, 0},
    {"UIViewController", "userContentController", "@@:", controller_content_controller, 0},
    {"UIViewController", "setUserContentController:", "v@:@", controller_set_content_controller, 0}
};
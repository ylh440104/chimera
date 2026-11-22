static void *plist_read_data(void *data)
{
    if (!fo_is_data(data)) return NULL;
    fo_data *d = data;
    if (d->length <= 0 || d->length > 32 * 1024 * 1024) return NULL;
    if (d->bytes[0] != '<') return NULL;
    struct { char name[16]; void *object, *key; fo_buffer text; } stack[66] = {0};
    char parser_stack[8192]; yxml_t parser;
    yxml_init(&parser, parser_stack, sizeof(parser_stack));
    unsigned depth = 0; void *root = NULL; int failed = 0;
    for (int64_t i = 0; i < d->length && !failed; i++) {
        yxml_ret_t rc = yxml_parse(&parser, d->bytes[i]);
        if (rc < 0) { failed = 1; break; }
        if (rc == YXML_ELEMSTART) {
            if (depth == 65 || strlen(parser.elem) >= 16) { failed = 1; break; }
            unsigned n = ++depth;
            strcpy(stack[n].name, parser.elem);
            if (!strcmp(parser.elem, "dict")) stack[n].object = fo_dict_new();
            else if (!strcmp(parser.elem, "array")) stack[n].object = fo_array_new();
            else if (strcmp(parser.elem, "plist") && strcmp(parser.elem, "key") && strcmp(parser.elem, "string") &&
                     strcmp(parser.elem, "integer") && strcmp(parser.elem, "real") && strcmp(parser.elem, "true") && strcmp(parser.elem, "false")) failed = 1;
        } else if (rc == YXML_CONTENT) {
            if (depth && !buffer_add(&stack[depth].text, parser.data, strlen(parser.data))) failed = 1;
        } else if (rc == YXML_ELEMEND) {
            if (!depth) { failed = 1; break; }
            unsigned n = depth--;
            const char *text = stack[n].text.data ? stack[n].text.data : "";
            void *value = stack[n].object;
            int key = !strcmp(stack[n].name, "key");
            if (key || !strcmp(stack[n].name, "string")) value = fo_string_new(text);
            else if (!strcmp(stack[n].name, "integer")) { char *end; errno = 0; long long v = strtoll(text, &end, 0); if (errno || !*text || *end) failed = 1; else value = n_signed(NULL, NULL, v); }
            else if (!strcmp(stack[n].name, "real")) { char *end; errno = 0; double v = strtod(text, &end); if (errno || !*text || *end || !isfinite(v)) failed = 1; else value = n_double(NULL, NULL, v); }
            else if (!strcmp(stack[n].name, "true")) value = n_bool(NULL, NULL, 1);
            else if (!strcmp(stack[n].name, "false")) value = n_bool(NULL, NULL, 0);
            free(stack[n].text.data); stack[n].text = (fo_buffer){0};
            if (!strcmp(stack[n].name, "plist")) continue;
            if (!value || failed) { failed = 1; break; }
            if (key) {
                if (!depth || !fo_is_dict(stack[depth].object) || stack[depth].key) failed = 1;
                else stack[depth].key = value;
            } else if (depth && fo_is_dict(stack[depth].object)) {
                if (!stack[depth].key || !d_put(stack[depth].object, stack[depth].key, value)) failed = 1;
                stack[depth].key = NULL;
            } else if (depth && fo_is_array(stack[depth].object)) {
                if (!a_push(stack[depth].object, value)) failed = 1;
            } else if (!root) root = value;
            else failed = 1;
            if (stack[n].key) failed = 1;
            stack[n].object = stack[n].key = NULL;
        }
    }
    if (yxml_eof(&parser) < 0 || depth) failed = 1;
    for (unsigned i = 0; i < 66; i++) free(stack[i].text.data);
    return failed ? NULL : root;
}

static int plist_escape(fo_buffer *b, const char *text)
{
    for (const char *p = text; *p; p++) {
        const char *replacement = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;" : NULL;
        if (!buffer_add(b, replacement ? replacement : p, replacement ? strlen(replacement) : 1)) return 0;
    }
    return 1;
}
static int plist_encode(fo_buffer *b, void *object, unsigned depth)
{
    if (depth > 64) return 0;
    if (fo_is_string(object) || fo_is_cfstring(object)) return buffer_add(b, "<string>", 8) && plist_escape(b, fo_bytes(object)) && buffer_add(b, "</string>", 9);
    if (fo_is_number(object)) {
        fo_number *n = object;
        if (n->type == 'B') return buffer_add(b, n->value.signed_value ? "<true/>" : "<false/>", n->value.signed_value ? 7 : 8);
        int real = n->type == 'd';
        return buffer_add(b, real ? "<real>" : "<integer>", real ? 6 : 9) && json_encode(b, object, 0) && buffer_add(b, real ? "</real>" : "</integer>", real ? 7 : 10);
    }
    if (fo_is_array(object)) {
        fo_array *a = object; if (!buffer_add(b, "<array>", 7)) return 0;
        for (int64_t i = 0; i < a->count; i++) if (!plist_encode(b, a->items[i], depth + 1)) return 0;
        return buffer_add(b, "</array>", 8);
    }
    if (fo_is_dict(object)) {
        fo_dict *d = object; if (!buffer_add(b, "<dict>", 6)) return 0;
        for (int64_t i = 0; i < d->count; i++) {
            if ((!fo_is_string(d->keys[i]) && !fo_is_cfstring(d->keys[i])) || !buffer_add(b, "<key>", 5) ||
                !plist_escape(b, fo_bytes(d->keys[i])) || !buffer_add(b, "</key>", 6) || !plist_encode(b, d->values[i], depth + 1)) return 0;
        }
        return buffer_add(b, "</dict>", 7);
    }
    return 0;
}

static int collection_write_file(void *self, const char *sel, void *path, int atomic)
{
    if (fo_is_data(self)) return d_write_file(self, sel, path, atomic);
    fo_buffer b = {0};
    const char *prefix = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\">";
    int ok = buffer_add(&b, prefix, strlen(prefix)) && plist_encode(&b, self, 0) && buffer_add(&b, "</plist>", 8);
    if (ok) ok = d_write_file(fo_data_new(b.data, (int64_t)b.size), sel, path, atomic);
    free(b.data); return ok;
}
static void *collection_read_file(void *cls, const char *sel, void *path)
{
    (void)sel;
    void *object = plist_read_data(f_contents_at_path(NULL, NULL, path));
    if (!object) return NULL;
    if (cls == chm_foundation_lookup_class("NSDictionary") || cls == chm_foundation_lookup_class("NSMutableDictionary")) return fo_is_dict(object) ? object : NULL;
    return fo_is_array(object) ? object : NULL;
}
#include <time.h>
#include <ctype.h>
#include "foundation_bplist.inc"

static void *plist_base64(const char *text)
{
    fo_buffer buffer = {0}; unsigned group[4], count = 0; int ended = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') continue;
        if (ended) goto fail;
        unsigned value = *p >= 'A' && *p <= 'Z' ? *p - 'A' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 26 :
            *p >= '0' && *p <= '9' ? *p - '0' + 52 : *p == '+' ? 62 : *p == '/' ? 63 : *p == '=' ? 64 : 65;
        if (value == 65) goto fail;
        group[count++] = value;
        if (count == 4) {
            if (group[0] >= 64 || group[1] >= 64 || (group[2] == 64 && group[3] != 64)) goto fail;
            unsigned char bytes[3] = {(unsigned char)((group[0] << 2) | (group[1] >> 4)),
                (unsigned char)((group[1] << 4) | (group[2] >> 2)), (unsigned char)((group[2] << 6) | group[3])};
            size_t length = group[2] == 64 ? 1 : group[3] == 64 ? 2 : 3;
            if ((length == 1 && (group[1] & 15)) || (length == 2 && (group[2] & 3)) || !buffer_add(&buffer, bytes, length)) goto fail;
            ended = length != 3; count = 0;
        }
    }
    if (count) goto fail;
    void *data = fo_data_new(buffer.data, (int64_t)buffer.size);
    free(buffer.data); return data;
fail:
    free(buffer.data); return NULL;
}

static void *plist_date_read(const char *text)
{
    if (strlen(text) != 20) return NULL;
    struct tm value = {0};
    char *end = strptime(text, "%Y-%m-%dT%H:%M:%SZ", &value);
    if (!end || *end) return NULL;
    time_t epoch = timegm(&value);
    char check[32];
    if (!strftime(check, sizeof(check), "%Y-%m-%dT%H:%M:%SZ", &value) || strcmp(check, text)) return NULL;
    return date_unix(NULL, NULL, (double)epoch);
}

static void *plist_read_data(void *data)
{
    if (!fo_is_data(data)) return NULL;
    fo_data *d = data;
    if (d->length <= 0 || d->length > 32 * 1024 * 1024 || !d->bytes) return NULL;
    if (d->length >= 8 && !memcmp(d->bytes, "bplist00", 8)) return bplist_read(d);
    struct { char name[16]; void *object, *key; fo_buffer text; } stack[66] = {0};
    char parser_stack[8192]; yxml_t parser;
    yxml_init(&parser, parser_stack, sizeof(parser_stack));
    unsigned depth = 0, nodes = 0; void *root = NULL; int failed = 0, wrapper = 0;
    for (int64_t i = 0; i < d->length && !failed; i++) {
        yxml_ret_t rc = yxml_parse(&parser, d->bytes[i]);
        if (rc < 0) { failed = 1; break; }
        if (rc == YXML_ELEMSTART) {
            if (depth == 65 || strlen(parser.elem) >= 16 || ++nodes > 1000000) { failed = 1; break; }
            if (depth && strcmp(stack[depth].name, "dict") && strcmp(stack[depth].name, "array") && strcmp(stack[depth].name, "plist")) { failed = 1; break; }
            unsigned n = ++depth;
            strcpy(stack[n].name, parser.elem);
            if (!strcmp(parser.elem, "plist")) { if (n != 1 || wrapper++) failed = 1; }
            else if (!strcmp(parser.elem, "dict")) stack[n].object = fo_dict_new();
            else if (!strcmp(parser.elem, "array")) stack[n].object = fo_array_new();
            else if (strcmp(parser.elem, "key") && strcmp(parser.elem, "string") && strcmp(parser.elem, "integer") &&
                     strcmp(parser.elem, "real") && strcmp(parser.elem, "true") && strcmp(parser.elem, "false") &&
                     strcmp(parser.elem, "data") && strcmp(parser.elem, "date")) failed = 1;
        } else if (rc == YXML_CONTENT) {
            if (depth && !buffer_add(&stack[depth].text, parser.data, strlen(parser.data))) failed = 1;
        } else if (rc == YXML_ELEMEND) {
            if (!depth) { failed = 1; break; }
            unsigned n = depth--;
            const char *text = stack[n].text.data ? stack[n].text.data : "";
            void *value = stack[n].object;
            int key = !strcmp(stack[n].name, "key");
            if (key || !strcmp(stack[n].name, "string")) value = fo_string_new(text);
            else if (!strcmp(stack[n].name, "integer")) {
                while (isspace((unsigned char)*text)) text++;
                char *end; errno = 0;
                if (*text == '-') { long long v = strtoll(text, &end, 10); value = n_signed(NULL, NULL, v); }
                else { unsigned long long v = strtoull(text, &end, 10); value = n_unsigned(NULL, NULL, v); }
                if (errno || !*text || end == text) failed = 1;
                while (isspace((unsigned char)*end)) end++;
                if (*end) failed = 1;
            } else if (!strcmp(stack[n].name, "real")) {
                char *end; errno = 0; double v = strtod(text, &end);
                if (errno || !*text || end == text || !isfinite(v)) failed = 1;
                while (isspace((unsigned char)*end)) end++;
                if (*end) failed = 1;
                value = n_double(NULL, NULL, v);
            } else if (!strcmp(stack[n].name, "true") || !strcmp(stack[n].name, "false")) {
                if (*text) failed = 1;
                value = n_bool(NULL, NULL, !strcmp(stack[n].name, "true"));
            } else if (!strcmp(stack[n].name, "data")) value = plist_base64(text);
            else if (!strcmp(stack[n].name, "date")) value = plist_date_read(text);
            else for (const char *p = text; *p; p++) if (!isspace((unsigned char)*p)) failed = 1;
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
        if ((unsigned char)*p < 32 && *p != '\t' && *p != '\n' && *p != '\r') return 0;
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
    if (fo_is_data(object)) {
        fo_data *data = object;
        static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        if (data->length < 0 || !buffer_add(b, "<data>", 6)) return 0;
        for (int64_t i = 0; i < data->length; i += 3) {
            size_t count = (size_t)(data->length - i); if (count > 3) count = 3;
            unsigned value = (unsigned)data->bytes[i] << 16;
            if (count > 1) value |= (unsigned)data->bytes[i + 1] << 8;
            if (count > 2) value |= data->bytes[i + 2];
            char out[4] = {digits[value >> 18], digits[(value >> 12) & 63], count > 1 ? digits[(value >> 6) & 63] : '=', count > 2 ? digits[value & 63] : '='};
            if (!buffer_add(b, out, 4)) return 0;
        }
        return buffer_add(b, "</data>", 7);
    }
    if (fo_isa_is(object, "NSDate")) {
        double epoch = ((fo_date *)object)->epoch;
        if (!isfinite(epoch) || epoch < -62135596800.0 || epoch > 253402300799.0) return 0;
        time_t seconds = (time_t)floor(epoch); struct tm value; char date[48];
        if (!gmtime_r(&seconds, &value) || !strftime(date, sizeof(date), "<date>%Y-%m-%dT%H:%M:%SZ</date>", &value)) return 0;
        return buffer_add(b, date, strlen(date));
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

static void *plist_xml_data(void *object)
{
    fo_buffer b = {0};
    const char *prefix = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\">";
    int ok = buffer_add(&b, prefix, strlen(prefix)) && plist_encode(&b, object, 0) && buffer_add(&b, "</plist>", 8);
    void *data = ok ? fo_data_new(b.data, (int64_t)b.size) : NULL;
    free(b.data); return data;
}

static int collection_write_file(void *self, const char *sel, void *path, int atomic)
{
    if (fo_is_data(self)) return d_write_file(self, sel, path, atomic);
    void *data = plist_xml_data(self);
    return data && d_write_file(data, sel, path, atomic);
}
static void *collection_read_file(void *cls, const char *sel, void *path)
{
    (void)sel;
    void *object = plist_read_data(f_contents_at_path(NULL, NULL, path));
    if (!object) return NULL;
    if (cls == chm_foundation_lookup_class("NSDictionary") || cls == chm_foundation_lookup_class("NSMutableDictionary")) return fo_is_dict(object) ? object : NULL;
    return fo_is_array(object) ? object : NULL;
}

static void plist_mutability(void *object, unsigned long options, unsigned depth)
{
    if (depth > 64) return;
    if (fo_is_array(object)) {
        fo_array *array = object;
        array->isa = chm_foundation_lookup_class(options ? "NSMutableArray" : "NSArray");
        for (int64_t i = 0; i < array->count; i++) plist_mutability(array->items[i], options, depth + 1);
    } else if (fo_is_dict(object)) {
        fo_dict *dictionary = object;
        dictionary->isa = chm_foundation_lookup_class(options ? "NSMutableDictionary" : "NSDictionary");
        for (int64_t i = 0; i < dictionary->count; i++) plist_mutability(dictionary->values[i], options, depth + 1);
    } else if (options == 2 && (fo_is_string(object) || fo_is_data(object))) {
        *(void **)object = chm_foundation_lookup_class(fo_is_data(object) ? "NSMutableData" : "NSMutableString");
    }
}

static void *plist_read_method(void *cls, const char *sel, void *data, unsigned long options, unsigned long *format, void **error)
{
    (void)cls; (void)sel;
    if (error) *error = NULL;
    void *object = options <= 2 ? plist_read_data(data) : NULL;
    if (!object) {
        if (error) *error = make_error("NSCocoaErrorDomain", 3840, "Invalid property list data or options");
        return NULL;
    }
    plist_mutability(object, options, 0);
    fo_data *bytes = data;
    if (format) *format = bytes->length >= 8 && !memcmp(bytes->bytes, "bplist00", 8) ? 200 : 100;
    return object;
}
static void *plist_write_method(void *cls, const char *sel, void *object, unsigned long format, unsigned long options, void **error)
{
    (void)cls; (void)sel;
    if (error) *error = NULL;
    void *data = options ? NULL : format == 100 ? plist_xml_data(object) : format == 200 ? bplist_write(object) : NULL;
    if (!data && error) *error = make_error("NSCocoaErrorDomain", 3851, "Object or requested property list format is unsupported");
    return data;
}
static signed char plist_valid_method(void *cls, const char *sel, void *object, unsigned long format)
{
    fo_data *data = plist_write_method(cls, sel, object, format, 0, NULL);
    if (!data) return 0;
    free(data->bytes); free(data); return 1;
}
static const chm_host_method plist_methods[] = {
    {"NSPropertyListSerialization", "propertyListWithData:options:format:error:", "@@:@Q^Q^@", plist_read_method, 1},
    {"NSPropertyListSerialization", "dataWithPropertyList:format:options:error:", "@@:@QQ^@", plist_write_method, 1},
    {"NSPropertyListSerialization", "propertyList:isValidForFormat:", "B@:@Q", plist_valid_method, 1}
};
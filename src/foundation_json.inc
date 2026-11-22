typedef struct { char *data; size_t size, capacity; int failed; } fo_buffer;

static int buffer_add(fo_buffer *b, const void *data, size_t size)
{
    if (b->failed || size > 32u * 1024u * 1024u - b->size) { b->failed = 1; return 0; }
    size_t need = b->size + size + 1;
    if (need > b->capacity) {
        size_t capacity = b->capacity ? b->capacity * 2 : 256;
        if (capacity < need) capacity = need;
        char *next = realloc(b->data, capacity);
        if (!next) { b->failed = 1; return 0; }
        b->data = next; b->capacity = capacity;
    }
    memcpy(b->data + b->size, data, size); b->size += size; b->data[b->size] = 0;
    return 1;
}

static int json_quote(fo_buffer *b, const char *s)
{
    if (!buffer_add(b, "\"", 1)) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char escape[7];
        if (*p == '"' || *p == '\\') { escape[0] = '\\'; escape[1] = (char)*p; if (!buffer_add(b, escape, 2)) return 0; }
        else if (*p < 32) { snprintf(escape, sizeof(escape), "\\u%04x", *p); if (!buffer_add(b, escape, 6)) return 0; }
        else if (!buffer_add(b, p, 1)) return 0;
    }
    return buffer_add(b, "\"", 1);
}

static int json_encode(fo_buffer *b, void *object, unsigned depth)
{
    if (!object || depth > 64) return 0;
    if (fo_is_string(object) || fo_is_cfstring(object)) return json_quote(b, fo_bytes(object));
    if (fo_is_number(object)) {
        fo_number *n = object; char text[96];
        if (n->type == 'B') return buffer_add(b, n->value.signed_value ? "true" : "false", n->value.signed_value ? 4 : 5);
        if (n->type == 'd') { if (!isfinite(n->value.real_value)) return 0; snprintf(text, sizeof(text), "%.17g", n->value.real_value); }
        else if (n->type == 'Q') snprintf(text, sizeof(text), "%llu", (unsigned long long)n->value.unsigned_value);
        else snprintf(text, sizeof(text), "%lld", (long long)n->value.signed_value);
        return buffer_add(b, text, strlen(text));
    }
    if (fo_isa_is(object, "NSNull")) return buffer_add(b, "null", 4);
    if (fo_is_array(object)) {
        fo_array *a = object;
        if (!buffer_add(b, "[", 1)) return 0;
        for (int64_t i = 0; i < a->count; i++) {
            if (i && !buffer_add(b, ",", 1)) return 0;
            if (!json_encode(b, a->items[i], depth + 1)) return 0;
        }
        return buffer_add(b, "]", 1);
    }
    if (fo_is_dict(object)) {
        fo_dict *d = object;
        if (!buffer_add(b, "{", 1)) return 0;
        for (int64_t i = 0; i < d->count; i++) {
            if (!fo_is_string(d->keys[i]) && !fo_is_cfstring(d->keys[i])) return 0;
            if (i && !buffer_add(b, ",", 1)) return 0;
            if (!json_quote(b, fo_bytes(d->keys[i])) || !buffer_add(b, ":", 1) || !json_encode(b, d->values[i], depth + 1)) return 0;
        }
        return buffer_add(b, "}", 1);
    }
    return 0;
}

typedef struct { const unsigned char *p, *end; int failed; unsigned nodes; } json_reader;
static void json_space(json_reader *r) { while (r->p < r->end && (*r->p == ' ' || *r->p == '\r' || *r->p == '\n' || *r->p == '\t')) r->p++; }
static int json_hex(json_reader *r)
{
    if (r->end - r->p < 4) return -1;
    int value = 0;
    for (int i = 0; i < 4; i++) {
        int c = *r->p++, digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return -1;
        value = value * 16 + digit;
    }
    return value;
}
static int buffer_unicode(fo_buffer *b, unsigned code)
{
    unsigned char bytes[4]; size_t n;
    if (!code || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    if (code < 0x80) { bytes[0] = code; n = 1; }
    else if (code < 0x800) { bytes[0] = 0xc0 | (code >> 6); bytes[1] = 0x80 | (code & 63); n = 2; }
    else if (code < 0x10000) { bytes[0] = 0xe0 | (code >> 12); bytes[1] = 0x80 | ((code >> 6) & 63); bytes[2] = 0x80 | (code & 63); n = 3; }
    else { bytes[0] = 0xf0 | (code >> 18); bytes[1] = 0x80 | ((code >> 12) & 63); bytes[2] = 0x80 | ((code >> 6) & 63); bytes[3] = 0x80 | (code & 63); n = 4; }
    return buffer_add(b, bytes, n);
}
static void *json_string(json_reader *r)
{
    if (r->p == r->end || *r->p++ != '"') return NULL;
    fo_buffer b = {0}; int closed = 0;
    while (r->p < r->end) {
        unsigned char c = *r->p++;
        if (c == '"') { closed = 1; break; }
        if (c < 32) break;
        if (c == '\\') {
            if (r->p == r->end) break;
            c = *r->p++;
            if (c == 'u') {
                int code = json_hex(r);
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (r->end - r->p < 6 || r->p[0] != '\\' || r->p[1] != 'u') break;
                    r->p += 2; int low = json_hex(r);
                    if (low < 0xdc00 || low > 0xdfff) break;
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                }
                if (code < 0 || !buffer_unicode(&b, (unsigned)code)) break;
                continue;
            }
            if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
            else if (c != '"' && c != '\\' && c != '/') break;
        }
        if (!buffer_add(&b, &c, 1)) break;
    }
    void *out = closed && !b.failed ? fo_string_new(b.data ? b.data : "") : NULL;
    free(b.data); return out;
}
static void *json_value(json_reader *r, unsigned depth)
{
    json_space(r);
    if (depth > 64 || r->p == r->end || ++r->nodes > 1000000) return NULL;
    if (*r->p == '"') return json_string(r);
    if (*r->p == '[' || *r->p == '{') {
        int dict = *r->p++ == '{';
        void *out = dict ? fo_dict_new() : fo_array_new();
        if (!out) return NULL;
        json_space(r);
        if (r->p < r->end && *r->p == (dict ? '}' : ']')) { r->p++; return out; }
        for (;;) {
            void *key = NULL;
            if (dict) {
                json_space(r); key = json_string(r); json_space(r);
                if (!key || r->p == r->end || *r->p++ != ':') return NULL;
            }
            void *value = json_value(r, depth + 1);
            if (!value || !(dict ? d_put(out, key, value) : a_push(out, value))) return NULL;
            json_space(r);
            if (r->p == r->end) return NULL;
            unsigned char delimiter = *r->p++;
            if (delimiter == (dict ? '}' : ']')) return out;
            if (delimiter != ',') return NULL;
        }
    }
    const char *literals[] = {"true", "false", "null"};
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(literals[i]);
        if ((size_t)(r->end - r->p) >= n && !memcmp(r->p, literals[i], n)) {
            r->p += n;
            return i == 2 ? singleton("__null", "NSNull") : n_bool(NULL, NULL, i == 0);
        }
    }
    const unsigned char *start = r->p;
    if (*r->p == '-') r->p++;
    if (r->p == r->end || *r->p < '0' || *r->p > '9') return NULL;
    if (*r->p == '0') r->p++; else while (r->p < r->end && *r->p >= '0' && *r->p <= '9') r->p++;
    int real = 0;
    if (r->p < r->end && *r->p == '.') {
        real = 1; r->p++; const unsigned char *digits = r->p;
        while (r->p < r->end && *r->p >= '0' && *r->p <= '9') r->p++;
        if (digits == r->p) return NULL;
    }
    if (r->p < r->end && (*r->p == 'e' || *r->p == 'E')) {
        real = 1; r->p++;
        if (r->p < r->end && (*r->p == '+' || *r->p == '-')) r->p++;
        const unsigned char *digits = r->p;
        while (r->p < r->end && *r->p >= '0' && *r->p <= '9') r->p++;
        if (digits == r->p) return NULL;
    }
    size_t n = (size_t)(r->p - start);
    if (n >= 96) return NULL;
    char number[96]; memcpy(number, start, n); number[n] = 0; errno = 0;
    if (real) { double v = strtod(number, NULL); return !errno && isfinite(v) ? n_double(NULL, NULL, v) : NULL; }
    if (*start == '-') { long long v = strtoll(number, NULL, 10); return !errno ? n_signed(NULL, NULL, v) : NULL; }
    unsigned long long v = strtoull(number, NULL, 10);
    return !errno ? n_unsigned(NULL, NULL, v) : NULL;
}

static void *json_read(void *cls, const char *sel, void *data, unsigned long options, void **error)
{
    (void)cls; (void)sel;
    if (error) *error = NULL;
    if (!fo_is_data(data)) goto bad;
    fo_data *d = data;
    if (d->length <= 0 || !d->bytes) goto bad;
    json_reader r = {d->bytes, d->bytes + d->length, 0, 0};
    void *out = json_value(&r, 0); json_space(&r);
    if (out && r.p == r.end && ((options & 4) || fo_is_array(out) || fo_is_dict(out))) return out;
bad:
    if (error) *error = make_error("NSCocoaErrorDomain", 3840, "Invalid JSON data");
    return NULL;
}
static void *json_write(void *cls, const char *sel, void *object, unsigned long options, void **error)
{
    (void)cls; (void)sel;
    if (error) *error = NULL;
    fo_buffer b = {0}; void *out = NULL;
    if (((options & 4) || fo_is_array(object) || fo_is_dict(object)) && json_encode(&b, object, 0)) out = fo_data_new(b.data, (int64_t)b.size);
    if (!out && error) *error = make_error("NSCocoaErrorDomain", 3840, "Object is not JSON serializable");
    free(b.data); return out;
}
static int json_valid(void *cls, const char *sel, void *object)
{
    (void)cls; (void)sel;
    fo_buffer b = {0}; int ok = (fo_is_array(object) || fo_is_dict(object)) && json_encode(&b, object, 0);
    free(b.data); return ok;
}
typedef struct string_cache { void *object; char *bytes; struct string_cache *next; } string_cache;
static string_cache *constant_strings;

static const char *constant_utf8(void *object)
{
    uint64_t flags = *(const uint64_t *)((const char *)object + 8);
    const void *raw = *(const void *const *)((const char *)object + 16);
    if (!raw) return "";
    if (!(flags & 0x10)) return raw;
    for (string_cache *s = constant_strings; s; s = s->next) if (s->object == object) return s->bytes;
    uint64_t count = *(const uint64_t *)((const char *)object + 24);
    if (count > 1024 * 1024) return "";
    string_cache *s = calloc(1, sizeof(*s));
    if (!s) return "";
    s->bytes = malloc((size_t)count * 4 + 1);
    if (!s->bytes) { free(s); return ""; }
    const uint16_t *units = raw; size_t length = 0;
    for (uint64_t i = 0; i < count; i++) {
        unsigned code = units[i];
        if (code >= 0xd800 && code <= 0xdbff && i + 1 < count && units[i + 1] >= 0xdc00 && units[i + 1] <= 0xdfff)
            code = 0x10000 + ((code - 0xd800) << 10) + units[++i] - 0xdc00;
        else if (code >= 0xd800 && code <= 0xdfff) code = 0xfffd;
        if (code < 0x80) s->bytes[length++] = (char)code;
        else if (code < 0x800) { s->bytes[length++] = (char)(0xc0 | (code >> 6)); s->bytes[length++] = (char)(0x80 | (code & 63)); }
        else if (code < 0x10000) {
            s->bytes[length++] = (char)(0xe0 | (code >> 12)); s->bytes[length++] = (char)(0x80 | ((code >> 6) & 63)); s->bytes[length++] = (char)(0x80 | (code & 63));
        } else {
            s->bytes[length++] = (char)(0xf0 | (code >> 18)); s->bytes[length++] = (char)(0x80 | ((code >> 12) & 63));
            s->bytes[length++] = (char)(0x80 | ((code >> 6) & 63)); s->bytes[length++] = (char)(0x80 | (code & 63));
        }
    }
    s->bytes[length] = 0; s->object = object; s->next = constant_strings; constant_strings = s;
    return s->bytes;
}

static void constant_strings_reset(void)
{
    while (constant_strings) { string_cache *next = constant_strings->next; free(constant_strings->bytes); free(constant_strings); constant_strings = next; }
}
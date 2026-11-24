typedef struct { long location, length; } cf_string_range;

static uint16_t *cf_string_units(void *object, int32_t *length)
{
    if (!object || !length || !icu_ready()) return NULL;
    const char *bytes;
    int64_t count;
    if (fo_is_string(object)) {
        fo_string *string = object;
        count = string->length;
        bytes = string->bytes ? string->bytes : "";
    } else if (fo_is_cfstring(object)) {
        uint64_t flags = *(uint64_t *)((char *)object + 8);
        const void *raw = *(const void **)((char *)object + 16);
        count = *(int64_t *)((char *)object + 24);
        if (!raw || count < 0 || count > 1024 * 1024) return NULL;
        if (flags & 0x10) {
            uint16_t *result = calloc((size_t)count + 1, sizeof(*result));
            if (!result) return NULL;
            memcpy(result, raw, (size_t)count * sizeof(*result));
            *length = (int32_t)count;
            return result;
        }
        bytes = raw;
    } else return NULL;
    if (count < 0 || count > 4 * 1024 * 1024) return NULL;
    int32_t status = 0, needed = 0;
    icu.from_utf8(NULL, 0, &needed, bytes, (int32_t)count, &status);
    if ((status > 0 && status != 15) || needed < 0 || needed > 1024 * 1024) return NULL;
    uint16_t *result = calloc((size_t)needed + 1, sizeof(*result));
    if (!result) return NULL;
    status = 0;
    icu.from_utf8(result, needed + 1, length, bytes, (int32_t)count, &status);
    if (status > 0) { free(result); return NULL; }
    return result;
}

static char *cf_string_utf8(const uint16_t *units, int32_t length, int32_t *byte_count)
{
    int32_t status = 0, needed = 0;
    icu.to_utf8(NULL, 0, &needed, units, length, &status);
    if ((status > 0 && status != 15) || needed < 0 || needed > 4 * 1024 * 1024) return NULL;
    char *result = calloc((size_t)needed + 1, 1);
    if (!result) return NULL;
    status = 0;
    icu.to_utf8(result, needed + 1, byte_count, units, length, &status);
    if (status > 0) { free(result); return NULL; }
    return result;
}

static long cf_string_length(void *object)
{
    int32_t length = 0;
    uint16_t *units = cf_string_units(object, &length);
    free(units);
    return length;
}

static void *cf_string_copy(void *allocator, long maximum, void *source)
{
    if (allocator || maximum < 0) return NULL;
    int32_t length = 0, byte_count = 0;
    uint16_t *units = cf_string_units(source, &length);
    if (!units) return NULL;
    if (maximum && length > maximum) { free(units); return NULL; }
    char *bytes = cf_string_utf8(units, length, &byte_count);
    free(units);
    if (!bytes) return NULL;
    fo_string *string = fo_alloc("NSMutableString", sizeof(*string));
    if (!string) { free(bytes); return NULL; }
    string->bytes = bytes;
    string->length = byte_count;
    if (!cf_owned_add(string, NULL, NULL)) { free(bytes); free(string); return NULL; }
    cf_owned_find(string)->string_maximum = maximum;
    return string;
}

static void *cf_string_immutable_copy(void *allocator, void *source)
{
    fo_string *string = cf_string_copy(allocator, 0, source);
    if (string) string->isa = chm_foundation_lookup_class("NSString");
    return string;
}

static const char *cf_string_cstring_ptr(void *object, uint32_t encoding)
{
    if (encoding != 0x08000100 && encoding != 0x0600) return NULL;
    if (!fo_is_string(object) && !fo_is_cfstring(object)) return NULL;
    const char *bytes = fo_bytes(object);
    if (encoding == 0x0600) {
        int32_t length;
        uint16_t *units = cf_string_units(object, &length);
        if (!units) return NULL;
        int ascii = 1;
        for (int32_t i = 0; i < length; i++) if (units[i] > 127) { ascii = 0; break; }
        free(units);
        if (!ascii) return NULL;
    }
    return bytes;
}

static unsigned char cf_string_cstring(void *object, char *buffer, long capacity, uint32_t encoding)
{
    if (!buffer || capacity <= 0 || (encoding != 0x08000100 && encoding != 0x0600)) return 0;
    int32_t length, byte_count;
    uint16_t *units = cf_string_units(object, &length);
    if (!units) return 0;
    if (encoding == 0x0600) {
        for (int32_t i = 0; i < length; i++) {
            if (units[i] > 127) { free(units); return 0; }
        }
    }
    char *bytes = cf_string_utf8(units, length, &byte_count);
    free(units);
    if (!bytes) return 0;
    int fits = byte_count < capacity;
    if (fits) memcpy(buffer, bytes, (size_t)byte_count + 1);
    free(bytes);
    return (unsigned char)fits;
}

static unsigned char cf_string_transform(void *object, cf_string_range *range, void *identifier, unsigned char reverse)
{
    if (!fo_is_string(object) || !chm_objc_is_kind(object, "NSMutableString") ||
        (!fo_is_string(identifier) && !fo_is_cfstring(identifier)) || !icu_ready() ||
        !icu.transform_open || !icu.transform_apply || !icu.transform_close) return 0;
    int32_t source_length = 0, id_length = 0;
    uint16_t *source = cf_string_units(object, &source_length);
    if (!source) return 0;
    long start = range ? range->location : 0;
    long count = range ? range->length : source_length;
    if (start < 0 || count < 0 || start > source_length || count > source_length - start) { free(source); return 0; }
    uint16_t *name = cf_string_units(identifier, &id_length);
    if (!name) { free(source); return 0; }
    int32_t status = 0;
    void *transform = icu.transform_open(name, id_length, reverse != 0, NULL, 0, NULL, &status);
    free(name);
    if (!transform || status > 0) {
        if (transform) icu.transform_close(transform);
        free(source);
        return 0;
    }
    int32_t capacity = source_length + 32;
    if (capacity > 1024 * 1024 + 1) capacity = 1024 * 1024 + 1;
    unsigned char success = 0;
    for (;;) {
        uint16_t *text = calloc((size_t)capacity, sizeof(*text));
        if (!text) break;
        memcpy(text, source, (size_t)source_length * sizeof(*text));
        int32_t length = source_length, limit = (int32_t)(start + count);
        status = 0;
        icu.transform_apply(transform, text, &length, capacity, (int32_t)start, &limit, &status);
        if (status == 15 && capacity < 1024 * 1024 + 1) {
            free(text);
            int64_t next = (int64_t)capacity * 2;
            if (next < (int64_t)length + 1) next = (int64_t)length + 1;
            if (next > 1024 * 1024 + 1) next = 1024 * 1024 + 1;
            capacity = (int32_t)next;
            continue;
        }
        cf_owned *owned = cf_owned_find(object);
        if (status <= 0 && length >= 0 && length < capacity && limit >= start && limit <= length &&
            (!owned || !owned->string_maximum || length <= owned->string_maximum)) {
            int32_t byte_count;
            char *bytes = cf_string_utf8(text, length, &byte_count);
            if (bytes) {
                fo_string *string = object;
                free(string->bytes);
                string->bytes = bytes;
                string->length = byte_count;
                if (range) range->length = limit - start;
                success = 1;
            }
        }
        free(text);
        break;
    }
    icu.transform_close(transform);
    free(source);
    return success;
}

static void *string_copy_method(void *self, const char *selector)
{
    int mutable = !strcmp(selector, "mutableCopy") || !strcmp(selector, "mutableCopyWithZone:");
    return mutable ? cf_string_copy(NULL, 0, self) : cf_string_immutable_copy(NULL, self);
}

static signed char string_can_convert(void *self, const char *selector, unsigned long encoding)
{
    (void)selector;
    int32_t length;
    uint16_t *units = cf_string_units(self, &length);
    if (!units) return 0;
    signed char result = encoding == 1 || encoding == 4 || encoding == 10;
    if (encoding == 1) {
        for (int32_t i = 0; i < length; i++) if (units[i] > 127) { result = 0; break; }
    }
    free(units);
    return result;
}

static const chm_host_method cf_string_methods[] = {
    {"NSString", "length", "Q@:", cf_string_length, 0},
    {"NSString", "copy", "@@:", string_copy_method, 0},
    {"NSString", "mutableCopy", "@@:", string_copy_method, 0},
    {"NSString", "copyWithZone:", "@@:^v", string_copy_method, 0},
    {"NSString", "mutableCopyWithZone:", "@@:^v", string_copy_method, 0},
    {"NSString", "canBeConvertedToEncoding:", "c@:Q", string_can_convert, 0}
};

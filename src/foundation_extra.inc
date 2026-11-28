#include <pthread.h>

typedef struct enum_record {
    void *object;
    void *items;
    long index;
    struct enum_record *next;
} enum_record;

typedef struct exception_record {
    void *object;
    void *name;
    void *reason;
    struct exception_record *next;
} exception_record;

static enum_record *enum_records;
static exception_record *exception_records;
static pthread_t main_thread_id;
static int main_thread_ready;

static enum_record *enum_slot(void *object)
{
    if (!object) return NULL;
    for (enum_record *record = enum_records; record; record = record->next)
        if (record->object == object) return record;
    enum_record *record = calloc(1, sizeof(*record));
    if (!record) return NULL;
    record->object = object;
    record->next = enum_records;
    enum_records = record;
    return record;
}

static exception_record *exception_slot(void *object)
{
    if (!object) return NULL;
    for (exception_record *record = exception_records; record; record = record->next)
        if (record->object == object) return record;
    exception_record *record = calloc(1, sizeof(*record));
    if (!record) return NULL;
    record->object = object;
    record->next = exception_records;
    exception_records = record;
    return record;
}

static size_t utf16_units(const char *text, size_t bytes)
{
    size_t units = 0;
    for (size_t i = 0; i < bytes;) {
        unsigned char c = (unsigned char)text[i];
        unsigned codepoint;
        if (c < 0x80) { codepoint = c; i += 1; }
        else if ((c & 0xe0) == 0xc0 && i + 1 < bytes) { codepoint = ((c & 0x1f) << 6) | (text[i + 1] & 0x3f); i += 2; }
        else if ((c & 0xf0) == 0xe0 && i + 2 < bytes) {
            codepoint = ((c & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
            i += 3;
        } else if ((c & 0xf8) == 0xf0 && i + 3 < bytes) {
            codepoint = ((c & 0x07) << 18) | ((text[i + 1] & 0x3f) << 12) |
                        ((text[i + 2] & 0x3f) << 6) | (text[i + 3] & 0x3f);
            i += 4;
        } else { codepoint = c; i += 1; }
        units += codepoint > 0xffff ? 2 : 1;
    }
    return units;
}

static void ms_append_string(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_string(self) || !fo_is_string(other)) return;
    fo_string *target = self, *source = other;
    size_t left = (size_t)target->length, right = (size_t)source->length;
    char *joined = realloc(target->bytes, left + right + 1);
    if (!joined) return;
    memcpy(joined + left, source->bytes, right);
    joined[left + right] = 0;
    target->bytes = joined;
    target->length = (int64_t)(left + right);
}

static signed char s_contains(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_string(self) || !fo_is_string(other)) return 0;
    return strstr(fo_bytes(self), fo_bytes(other)) != NULL;
}

static unsigned short s_character_at(void *self, const char *sel, unsigned long index)
{
    (void)sel;
    if (!fo_is_string(self)) return 0;
    const char *text = fo_bytes(self);
    size_t bytes = (size_t)((fo_string *)self)->length;
    unsigned long unit = 0;
    for (size_t i = 0; i < bytes;) {
        unsigned char c = (unsigned char)text[i];
        unsigned codepoint;
        size_t step;
        if (c < 0x80) { codepoint = c; step = 1; }
        else if ((c & 0xe0) == 0xc0 && i + 1 < bytes) {
            codepoint = ((c & 0x1f) << 6) | (text[i + 1] & 0x3f);
            step = 2;
        } else if ((c & 0xf0) == 0xe0 && i + 2 < bytes) {
            codepoint = ((c & 0x0f) << 12) | ((text[i + 1] & 0x3f) << 6) | (text[i + 2] & 0x3f);
            step = 3;
        } else if ((c & 0xf8) == 0xf0 && i + 3 < bytes) {
            codepoint = ((c & 0x07) << 18) | ((text[i + 1] & 0x3f) << 12) |
                        ((text[i + 2] & 0x3f) << 6) | (text[i + 3] & 0x3f);
            step = 4;
        } else { codepoint = c; step = 1; }
        if (codepoint > 0xffff) {
            if (unit == index) return (unsigned short)(0xd800 + ((codepoint - 0x10000) >> 10));
            if (unit + 1 == index) return (unsigned short)(0xdc00 + ((codepoint - 0x10000) & 0x3ff));
            unit += 2;
        } else {
            if (unit == index) return (unsigned short)codepoint;
            unit++;
        }
        i += step;
    }
    return 0;
}

typedef struct {
    unsigned long location, length;
} ns_range;

static ns_range s_range_of(void *self, const char *sel, void *other, unsigned long options)
{
    (void)options;
    ns_range empty = {0, 0};
    if (!fo_is_string(self) || !fo_is_string(other)) return empty;
    const char *text = fo_bytes(self), *needle = fo_bytes(other);
    const char *found = strstr(text, needle);
    if (!found) return empty;
    ns_range range;
    range.location = utf16_units(text, (size_t)(found - text));
    range.length = utf16_units(needle, strlen(needle));
    (void)sel;
    return range;
}

static unsigned long array_index_of(void *self, const char *sel, void *object)
{
    (void)sel;
    if (!fo_is_array(self)) return (unsigned long)-1;
    fo_array *array = self;
    for (int64_t i = 0; i < array->count; i++)
        if (o_is_equal(array->items[i], NULL, object)) return (unsigned long)i;
    return (unsigned long)-1;
}

typedef struct {
    void *isa;
    int flags, reserved;
    signed char (*invoke)(void *, void *, unsigned long, signed char *);
} predicate_block;

static unsigned long array_index_passing(void *self, const char *sel, void *block)
{
    (void)sel;
    if (!fo_is_array(self)) return (unsigned long)-1;
    predicate_block *invoke = block;
    if (!invoke || !invoke->invoke) return (unsigned long)-1;
    fo_array *array = self;
    signed char stop = 0;
    for (int64_t i = 0; i < array->count; i++)
        if (invoke->invoke(invoke, array->items[i], (unsigned long)i, &stop)) return (unsigned long)i;
    return (unsigned long)-1;
}

static void array_make_perform(void *self, const char *sel, void *selector)
{
    (void)sel;
    if (!fo_is_array(self) || !selector) return;
    const char *name = chm_foundation_cstr(selector);
    if (!name || !*name) return;
    fo_array *array = self;
    for (int64_t i = 0; i < array->count; i++) chm_objc_send0(array->items[i], name);
}

static signed char dict_equal(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_dict(self) || !fo_is_dict(other)) return 0;
    fo_dict *left = self, *right = other;
    if (left->count != right->count) return 0;
    for (int64_t i = 0; i < left->count; i++) {
        void *value = d_value(right, NULL, left->keys[i]);
        if (!value || !o_is_equal(left->values[i], NULL, value)) return 0;
    }
    return 1;
}

static void *enum_new(void *object, void *items)
{
    void *enumerator = chm_foundation_alloc_obj("NSEnumerator", 0);
    enum_record *record = enum_slot(enumerator);
    if (!record) return NULL;
    record->object = enumerator;
    record->items = items;
    record->index = 0;
    (void)object;
    return enumerator;
}

static void *dict_key_enumerator(void *self, const char *sel)
{
    (void)sel;
    if (!fo_is_dict(self)) return NULL;
    fo_array *keys = fo_array_new();
    fo_dict *dict = self;
    if (!keys) return NULL;
    for (int64_t i = 0; i < dict->count; i++) a_push(keys, dict->keys[i]);
    return enum_new(self, keys);
}

static void *dict_object_enumerator(void *self, const char *sel)
{
    (void)sel;
    if (!fo_is_dict(self)) return NULL;
    fo_array *values = fo_array_new();
    fo_dict *dict = self;
    if (!values) return NULL;
    for (int64_t i = 0; i < dict->count; i++) a_push(values, dict->values[i]);
    return enum_new(self, values);
}

static void *array_object_enumerator(void *self, const char *sel)
{
    (void)sel;
    if (!fo_is_array(self)) return NULL;
    fo_array *items = fo_array_new(), *array = self;
    if (!items) return NULL;
    for (int64_t i = 0; i < array->count; i++) a_push(items, array->items[i]);
    return enum_new(self, items);
}

static void *enumerator_next(void *self, const char *sel)
{
    (void)sel;
    enum_record *record = enum_slot(self);
    if (!record || !record->items) return NULL;
    fo_array *items = record->items;
    return record->index < items->count ? items->items[record->index++] : NULL;
}

static void *enumerator_all(void *self, const char *sel)
{
    (void)sel;
    enum_record *record = enum_slot(self);
    return record ? record->items : NULL;
}

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void *data_base64(void *self, const char *sel, unsigned long options)
{
    (void)sel;
    (void)options;
    if (!fo_is_data(self)) return NULL;
    fo_data *data = self;
    const unsigned char *bytes = data->bytes;
    size_t length = (size_t)data->length;
    size_t out_length = ((length + 2) / 3) * 4;
    char *out = malloc(out_length + 1);
    if (!out) return NULL;
    size_t position = 0;
    for (size_t i = 0; i < length; i += 3) {
        unsigned value = (unsigned)bytes[i] << 16;
        if (i + 1 < length) value |= (unsigned)bytes[i + 1] << 8;
        if (i + 2 < length) value |= bytes[i + 2];
        out[position++] = base64_table[(value >> 18) & 63];
        out[position++] = base64_table[(value >> 12) & 63];
        out[position++] = i + 1 < length ? base64_table[(value >> 6) & 63] : '=';
        out[position++] = i + 2 < length ? base64_table[value & 63] : '=';
    }
    out[position] = 0;
    void *result = fo_string_new(out);
    free(out);
    return result;
}

static void *string_from_data(void *self, const char *sel, void *data, unsigned long encoding)
{
    (void)sel;
    (void)encoding;
    if (!fo_is_data(data)) return self;
    fo_data *source = data;
    char *buffer = malloc((size_t)source->length + 1);
    if (!buffer) return self;
    if (source->length) memcpy(buffer, source->bytes, (size_t)source->length);
    buffer[source->length] = 0;
    if (fo_is_string(self)) {
        fo_string *target = self;
        free(target->bytes);
        target->bytes = buffer;
        target->length = source->length;
        return self;
    }
    void *result = fo_string_new(buffer);
    free(buffer);
    return result;
}

static void *error_new(void *cls, const char *sel, void *domain, long code, void *user_info)
{
    (void)cls;
    (void)sel;
    (void)user_info;
    return make_error(domain && fo_is_string(domain) ? fo_bytes(domain) : "NSCocoaErrorDomain", code, NULL);
}

static void *exception_new(void *cls, const char *sel, void *name, void *reason, void *user_info)
{
    (void)sel;
    (void)user_info;
    void *object = chm_foundation_alloc_obj("NSException", 0);
    if (!object) return NULL;
    exception_record *record = exception_slot(object);
    if (record) {
        record->name = name;
        record->reason = reason;
    }
    (void)cls;
    return object;
}

static void *exception_property(void *self, const char *sel)
{
    exception_record *record = exception_slot(self);
    if (!record) return NULL;
    return !strcmp(sel, "name") ? record->name : !strcmp(sel, "reason") ? record->reason : NULL;
}

static long process_identifier(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return (long)getpid();
}

static signed char thread_is_main(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    if (!main_thread_ready) {
        main_thread_id = pthread_self();
        main_thread_ready = 1;
    }
    return pthread_equal(pthread_self(), main_thread_id) ? 1 : 0;
}

static void *thread_current(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return singleton("__ns_thread", "NSThread");
}

static void *runloop_current(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return singleton("__ns_runloop", "NSRunLoop");
}

static void runloop_add_timer(void *self, const char *sel, void *timer, void *mode)
{
    (void)self;
    (void)sel;
    (void)timer;
    (void)mode;
}

static int url_path(const char *address, char *buffer, size_t capacity)
{
    if (!address) return 0;
    const char *path = !strncmp(address, "file://", 7) ? address + 7 : address;
    if (!*path || strlen(path) + 1 > capacity) return 0;
    snprintf(buffer, capacity, "%s", path);
    return 1;
}

static signed char file_move_url(void *self, const char *sel, void *source, void *destination, void **error)
{
    (void)self;
    (void)sel;
    if (error) *error = NULL;
    char from[2048], to[2048];
    if (!url_path(chm_foundation_cstr(chm_objc_send0(source, "absoluteString")), from, sizeof(from)) ||
        !url_path(chm_foundation_cstr(chm_objc_send0(destination, "absoluteString")), to, sizeof(to)))
        return 0;
    if (rename(from, to)) {
        if (error) *error = make_error("NSCocoaErrorDomain", 4, strerror(errno));
        return 0;
    }
    return 1;
}

static const chm_host_method extra_foundation_methods[] = {
    {"NSMutableString", "appendString:", "v@:@", ms_append_string, 0},
    {"NSString", "containsString:", "c@:@", s_contains, 0},
    {"NSString", "characterAtIndex:", "S@:Q", s_character_at, 0},
    {"NSString", "rangeOfString:", "{_NSRange=QQ}@:@", s_range_of, 0},
    {"NSString", "rangeOfString:options:", "{_NSRange=QQ}@:Q", s_range_of, 0},
    {"NSArray", "indexOfObject:", "Q@:@", array_index_of, 0},
    {"NSArray", "indexOfObjectPassingTest:", "Q@:@?", array_index_passing, 0},
    {"NSArray", "makeObjectsPerformSelector:", "v@:@", array_make_perform, 0},
    {"NSArray", "objectEnumerator", "@@:", array_object_enumerator, 0},
    {"NSDictionary", "isEqualToDictionary:", "c@:@", dict_equal, 0},
    {"NSDictionary", "keyEnumerator", "@@:", dict_key_enumerator, 0},
    {"NSDictionary", "objectEnumerator", "@@:", dict_object_enumerator, 0},
    {"NSEnumerator", "nextObject", "@@:", enumerator_next, 0},
    {"NSEnumerator", "allObjects", "@@:", enumerator_all, 0},
    {"NSData", "base64EncodedStringWithOptions:", "@@:Q", data_base64, 0},
    {"NSString", "initWithData:encoding:", "@@:@Q", string_from_data, 0},
    {"NSMutableString", "initWithData:encoding:", "@@:@Q", string_from_data, 0},
    {"NSError", "errorWithDomain:code:userInfo:", "@@:@q@", error_new, 1},
    {"NSError", "initWithDomain:code:userInfo:", "@@:@q@", error_new, 0},
    {"NSException", "exceptionWithName:reason:userInfo:", "@@:@@@", exception_new, 1},
    {"NSException", "name", "@@:", exception_property, 0},
    {"NSException", "reason", "@@:", exception_property, 0},
    {"NSProcessInfo", "processIdentifier", "q@:", process_identifier, 0},
    {"NSThread", "isMainThread", "c@:", thread_is_main, 1},
    {"NSThread", "currentThread", "@@:", thread_current, 1},
    {"NSRunLoop", "currentRunLoop", "@@:", runloop_current, 1},
    {"NSRunLoop", "addTimer:forMode:", "v@:@@", runloop_add_timer, 0},
    {"NSFileManager", "moveItemAtURL:toURL:error:", "c@:@@^v", file_move_url, 0}
};
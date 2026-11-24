typedef struct {
    long version;
    const void *(*retain)(void *, const void *);
    void (*release)(void *, const void *);
    void *(*description)(const void *);
    unsigned char (*equal)(const void *, const void *);
} cf_array_callbacks;

typedef struct cf_owned {
    void *object, *allocator;
    size_t references;
    cf_array_callbacks callbacks;
    struct cf_owned *next;
} cf_owned;

static cf_owned *cf_objects;

static cf_owned *cf_owned_find(void *object)
{
    for (cf_owned *entry = cf_objects; entry; entry = entry->next) if (entry->object == object) return entry;
    return NULL;
}

static int cf_owned_add(void *object, void *allocator, const cf_array_callbacks *callbacks)
{
    cf_owned *entry = calloc(1, sizeof(*entry));
    if (!entry) return 0;
    entry->object = object;
    entry->allocator = allocator;
    entry->references = 1;
    if (callbacks) entry->callbacks = *callbacks;
    entry->next = cf_objects;
    cf_objects = entry;
    return 1;
}

static void *cf_owned_retain(void *object)
{
    cf_owned *entry = cf_owned_find(object);
    if (entry && entry->references < SIZE_MAX) entry->references++;
    return object;
}

static void cf_owned_release(void *object)
{
    cf_owned *entry = cf_owned_find(object);
    if (!entry || --entry->references) return;
    cf_owned **link = &cf_objects;
    while (*link != entry) link = &(*link)->next;
    *link = entry->next;
    if (fo_is_array(object)) {
        fo_array *array = object;
        if (entry->callbacks.release)
            for (int64_t i = 0; i < array->count; i++) entry->callbacks.release(entry->allocator, array->items[i]);
        free(array->items);
    } else if (fo_is_data(object)) free(((fo_data *)object)->bytes);
    free(object);
    free(entry);
}

void *chm_foundation_cf_retain(void *object) { return cf_owned_retain(object); }
void chm_foundation_cf_release(void *object) { cf_owned_release(object); }

static int cf_owned_reset_body(void *context)
{
    (void)context;
    while (cf_objects) { cf_objects->references = 1; cf_owned_release(cf_objects->object); }
    return 0;
}

static void cf_owned_reset(void)
{
    chm_session_callback(g_session, cf_owned_reset_body, NULL);
}

int chm_foundation_data_bytes(void *object, const void **bytes, size_t *length)
{
    if (!bytes || !length || !fo_is_data(object)) return 0;
    fo_data *data = object;
    if (data->length < 0 || (data->length && !data->bytes)) return 0;
    *bytes = data->bytes;
    *length = (size_t)data->length;
    return 1;
}

void *chm_foundation_data(const void *bytes, size_t length)
{
    if (length > 256 * 1024 * 1024 || (!bytes && length)) return NULL;
    fo_data *data = fo_data_new(bytes, (int64_t)length);
    if (data && !cf_owned_add(data, NULL, NULL)) { free(data->bytes); free(data); return NULL; }
    return data;
}

static void *cf_data_create(void *allocator, const unsigned char *bytes, long length)
{
    if (allocator || length < 0) return NULL;
    return chm_foundation_data(bytes, (size_t)length);
}

static const void *cf_data_bytes(void *object)
{
    const void *bytes = NULL; size_t length;
    chm_foundation_data_bytes(object, &bytes, &length);
    return bytes;
}

static long cf_data_length(void *object)
{
    const void *bytes; size_t length = 0;
    chm_foundation_data_bytes(object, &bytes, &length);
    return (long)length;
}

static const void *cf_type_retain(void *allocator, const void *object)
{
    (void)allocator;
    void *(*retain)(void *) = chm_frameworks_lookup("_CFRetain");
    return retain((void *)object);
}

static void cf_type_release(void *allocator, const void *object)
{
    (void)allocator;
    void (*release)(void *) = chm_frameworks_lookup("_CFRelease");
    release((void *)object);
}

static void *cf_type_description(const void *object)
{
    return chm_objc_send0((void *)object, "description");
}

static unsigned char cf_equal(const void *left, const void *right)
{
    if (left == right) return 1;
    if (!left || !right) return 0;
    if (fo_is_data((void *)left) && fo_is_data((void *)right)) {
        const fo_data *a = left, *b = right;
        return a->length == b->length && (!a->length || !memcmp(a->bytes, b->bytes, (size_t)a->length));
    }
    return o_is_equal((void *)left, NULL, (void *)right) != 0;
}

static const cf_array_callbacks cf_type_array_callbacks = {0, cf_type_retain, cf_type_release, cf_type_description, cf_equal};

static void *cf_array_create(void *allocator, const void **values, long count, const cf_array_callbacks *callbacks)
{
    if (allocator || count < 0 || count > 1024 * 1024 || (!values && count) || (callbacks && callbacks->version)) return NULL;
    fo_array *array = collection_with_capacity(chm_foundation_lookup_class("NSArray"), NULL, (unsigned long)count);
    if (!array) return NULL;
    if (!cf_owned_add(array, allocator, callbacks)) { collection_dispose(array); return NULL; }
    for (long i = 0; i < count; i++) {
        void *value = (void *)(callbacks && callbacks->retain ? callbacks->retain(allocator, values[i]) : values[i]);
        if (!value || !a_push(array, value)) {
            if (value && callbacks && callbacks->release) callbacks->release(allocator, value);
            cf_owned_release(array);
            return NULL;
        }
    }
    return array;
}

static long cf_array_count(void *object) { return fo_is_array(object) ? (long)((fo_array *)object)->count : 0; }
static const void *cf_array_value(void *object, long index) { return a_object_at(object, NULL, index); }
static long cf_dictionary_count(void *object) { return fo_is_dict(object) ? (long)((fo_dict *)object)->count : 0; }
static const void *cf_dictionary_value(void *object, const void *key) { return d_value(object, NULL, (void *)key); }
static void cf_dictionary_values(void *object, const void **keys, const void **values)
{
    if (!fo_is_dict(object)) return;
    fo_dict *dictionary = object;
    for (int64_t i = 0; i < dictionary->count; i++) {
        if (keys) keys[i] = dictionary->keys[i];
        if (values) values[i] = dictionary->values[i];
    }
}

static unsigned long cf_hash(void *object)
{
    if (fo_is_string(object) || fo_is_cfstring(object)) {
        const unsigned char *p = (const unsigned char *)fo_bytes(object);
        unsigned long hash = 5381;
        while (*p) hash = hash * 33 ^ *p++;
        return hash;
    }
    if (fo_is_data(object)) {
        fo_data *data = object;
        unsigned long hash = 5381;
        for (int64_t i = 0; i < data->length; i++) hash = hash * 33 ^ data->bytes[i];
        return hash;
    }
    if (fo_is_number(object)) {
        double value = o_double(object);
        if (!value) return 0;
        uint64_t bits; memcpy(&bits, &value, sizeof(bits));
        return (unsigned long)(bits ^ (bits >> 32));
    }
    return (unsigned long)(uintptr_t)object;
}

static unsigned long cf_type_id(void *object)
{
    if (!object) return 0;
    if (fo_is_string(object) || fo_is_cfstring(object)) return 7;
    if (fo_is_data(object)) return 19;
    if (fo_is_array(object)) return 18;
    if (fo_is_dict(object)) return 17;
    if (fo_is_number(object)) return ((fo_number *)object)->type == 'B' ? 21 : 22;
    return 1;
}

static void *cf_runloop_main(void) { return main_run_loop(); }

void *chm_foundation_cf_lookup(const char *symbol)
{
    static const struct { const char *name; void *function; } entries[] = {
        {"_CFDataCreate", cf_data_create},
        {"_CFDataGetBytePtr", cf_data_bytes},
        {"_CFDataGetLength", cf_data_length},
        {"_CFArrayCreate", cf_array_create},
        {"_CFArrayGetCount", cf_array_count},
        {"_CFArrayGetValueAtIndex", cf_array_value},
        {"_CFDictionaryGetCount", cf_dictionary_count},
        {"_CFDictionaryGetValue", cf_dictionary_value},
        {"_CFDictionaryGetKeysAndValues", cf_dictionary_values},
        {"_CFEqual", cf_equal},
        {"_CFHash", cf_hash},
        {"_CFGetTypeID", cf_type_id},
        {"_CFRunLoopGetMain", cf_runloop_main},
        {"_kCFTypeArrayCallBacks", (void *)&cf_type_array_callbacks}
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); i++)
        if (!strcmp(symbol, entries[i].name)) return entries[i].function;
    return NULL;
}
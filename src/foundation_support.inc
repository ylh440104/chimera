typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, void *, unsigned long, signed char *);
} index_block;

typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, void *, void *, signed char *);
} pair_block;

typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, void *, signed char *);
} member_block;

static void array_blocks(void *self, const char *sel, void *block)
{
    (void)sel;
    fo_array *a = self;
    index_block *b = block;
    if (!fo_is_array(a) || !b || !b->invoke) return;
    signed char stop = 0;
    for (int64_t i = 0; i < a->count && !stop; i++)
        b->invoke(b, a->items[i], (unsigned long)i, &stop);
}

static void dict_blocks(void *self, const char *sel, void *block)
{
    (void)sel;
    fo_dict *d = self;
    pair_block *b = block;
    if (!fo_is_dict(d) || !b || !b->invoke) return;
    signed char stop = 0;
    for (int64_t i = 0; i < d->count && !stop; i++)
        b->invoke(b, d->keys[i], d->values[i], &stop);
}

static int set_index(fo_array *a, void *value)
{
    for (int64_t i = 0; i < a->count; i++)
        if (o_is_equal(a->items[i], NULL, value)) return (int)i;
    return -1;
}

static void *set_empty(void)
{
    return fo_alloc("NSMutableSet", sizeof(fo_array));
}

static void set_insert(fo_array *a, void *value)
{
    if (!value || set_index(a, value) >= 0) return;
    a_push(a, value);
}

extern void chm_ns_set_entry(void);

void *chm_ns_set_list(void *cls, const char *sel, void *first, void **stack)
{
    (void)cls;
    (void)sel;
    fo_array *a = set_empty();
    if (!a) return NULL;
    if (first) set_insert(a, first);
    for (int i = 0; stack && stack[i]; i++) set_insert(a, stack[i]);
    return a;
}

static void *set_with_array(void *cls, const char *sel, void *source)
{
    (void)cls;
    (void)sel;
    fo_array *a = set_empty();
    if (!a) return NULL;
    if (fo_is_array(source)) {
        fo_array *items = source;
        for (int64_t i = 0; i < items->count; i++) set_insert(a, items->items[i]);
    }
    return a;
}

static signed char set_contains(void *self, const char *sel, void *value)
{
    (void)sel;
    if (!fo_is_array(self) || !value) return 0;
    return set_index(self, value) >= 0;
}

static void *set_member(void *self, const char *sel, void *value)
{
    (void)sel;
    if (!fo_is_array(self) || !value) return NULL;
    int index = set_index(self, value);
    return index >= 0 ? ((fo_array *)self)->items[index] : NULL;
}

static void set_add(void *self, const char *sel, void *value)
{
    (void)sel;
    if (!fo_is_array(self)) return;
    set_insert(self, value);
}

static void set_add_from_array(void *self, const char *sel, void *source)
{
    (void)sel;
    if (!fo_is_array(self) || !fo_is_array(source)) return;
    fo_array *items = source;
    for (int64_t i = 0; i < items->count; i++) set_insert(self, items->items[i]);
}

static void set_remove(void *self, const char *sel, void *value)
{
    (void)sel;
    if (!fo_is_array(self) || !value) return;
    fo_array *a = self;
    int index = set_index(a, value);
    if (index < 0) return;
    cf_array_drop(a, a->items[index]);
    for (int64_t i = index; i + 1 < a->count; i++) a->items[i] = a->items[i + 1];
    a->items[--a->count] = NULL;
    a->mutations++;
}

static void *set_all_objects(void *self, const char *sel)
{
    (void)sel;
    if (!fo_is_array(self)) return NULL;
    fo_array *items = self, *out = fo_array_new();
    if (!out) return NULL;
    for (int64_t i = 0; i < items->count; i++) a_push(out, items->items[i]);
    return out;
}

static void *set_any_object(void *self, const char *sel)
{
    (void)sel;
    if (!fo_is_array(self)) return NULL;
    fo_array *items = self;
    return items->count ? items->items[0] : NULL;
}

static signed char set_is_subset(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_array(self) || !fo_is_array(other)) return 0;
    fo_array *items = self, *target = other;
    for (int64_t i = 0; i < items->count; i++)
        if (set_index(target, items->items[i]) < 0) return 0;
    return 1;
}

static signed char set_intersects(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_array(self) || !fo_is_array(other)) return 0;
    fo_array *items = self, *target = other;
    for (int64_t i = 0; i < items->count; i++)
        if (set_index(target, items->items[i]) >= 0) return 1;
    return 0;
}

static signed char set_equal_to(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_array(self) || !fo_is_array(other)) return 0;
    fo_array *items = self, *target = other;
    if (items->count != target->count) return 0;
    for (int64_t i = 0; i < items->count; i++)
        if (set_index(target, items->items[i]) < 0) return 0;
    return 1;
}

static void *set_by_adding(void *self, const char *sel, void *value)
{
    (void)sel;
    if (!fo_is_array(self)) return NULL;
    fo_array *out = set_all_objects(self, NULL);
    if (out) set_insert(out, value);
    return out;
}

static void *set_by_adding_set(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_array(self)) return NULL;
    fo_array *out = set_all_objects(self, NULL);
    if (out && fo_is_array(other)) {
        fo_array *items = other;
        for (int64_t i = 0; i < items->count; i++) set_insert(out, items->items[i]);
    }
    return out;
}

static void set_blocks(void *self, const char *sel, void *block)
{
    (void)sel;
    fo_array *a = self;
    member_block *b = block;
    if (!fo_is_array(a) || !b || !b->invoke) return;
    signed char stop = 0;
    for (int64_t i = 0; i < a->count && !stop; i++) b->invoke(b, a->items[i], &stop);
}

static void *mutable_data_empty(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return fo_alloc("NSMutableData", sizeof(fo_data));
}

static void *mutable_data_capacity(void *self, const char *sel, unsigned long capacity)
{
    (void)sel;
    (void)capacity;
    if (!self) return NULL;
    return self;
}

static void data_append_bytes(void *self, const char *sel, const void *bytes, unsigned long length)
{
    (void)sel;
    if (!fo_is_data(self) || !bytes || !length) return;
    fo_data *d = self;
    if (length > (unsigned long)(INT64_MAX - d->length)) return;
    size_t total = (size_t)d->length + length;
    uint8_t *buffer = realloc(d->bytes, total);
    if (!buffer) return;
    memcpy(buffer + d->length, bytes, length);
    d->bytes = buffer;
    d->length = (int64_t)total;
}

static void data_append_data(void *self, const char *sel, void *other)
{
    if (!fo_is_data(other)) return;
    fo_data *source = other;
    data_append_bytes(self, sel, source->bytes, (unsigned long)source->length);
}

static void data_set_length(void *self, const char *sel, long length)
{
    (void)sel;
    if (!fo_is_data(self) || length < 0) return;
    fo_data *d = self;
    if ((int64_t)length == d->length) return;
    if (length == 0) {
        free(d->bytes);
        d->bytes = NULL;
        d->length = 0;
        return;
    }
    uint8_t *buffer = realloc(d->bytes, (size_t)length);
    if (!buffer) return;
    if ((int64_t)length > d->length) memset(buffer + d->length, 0, (size_t)length - (size_t)d->length);
    d->bytes = buffer;
    d->length = length;
}

static void *assert_handler_current(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return singleton("__assertion_handler", "NSAssertionHandler");
}

static void assert_handler_failure(void *self, const char *sel, void *method, void *object, void *file,
                                   unsigned long line, void *format, ...)
{
    (void)self;
    (void)method;
    (void)object;
    if (g_session)
        chm_session_log(g_session, "assertion %s:%lu %s %s", file && fo_is_string(file) ? fo_bytes(file) : "?",
                        line, sel, fo_is_string(format) ? fo_bytes(format) : "");
}

static void kvo_add(void *self, const char *sel, void *observer, void *key, unsigned long options, void *context)
{
    (void)self;
    (void)sel;
    (void)observer;
    (void)key;
    (void)options;
    (void)context;
}

static void kvo_remove(void *self, const char *sel, void *observer, void *key)
{
    (void)self;
    (void)sel;
    (void)observer;
    (void)key;
}

static void kvo_remove_context(void *self, const char *sel, void *observer, void *key, void *context)
{
    (void)self;
    (void)sel;
    (void)observer;
    (void)key;
    (void)context;
}

static void *progress_init_parent(void *self, const char *sel, void *parent, void *info)
{
    (void)sel;
    (void)info;
    progress_record *p = progress_record_for(self);
    progress_record *q = parent ? progress_record_for(parent) : NULL;
    if (p && q && !p->parent) {
        p->parent = q;
        p->sibling = q->children;
        q->children = p;
        progress_notify(q);
    }
    return self;
}

static void *url_with_relative(void *cls, const char *sel, void *string, void *base)
{
    (void)cls;
    (void)sel;
    const char *text = fo_bytes(string);
    if (!text || !*text) return NULL;
    if (strstr(text, "://")) return url_new(NULL, "URLWithString:", string);
    void *base_string = base ? url_string(base, "absoluteString") : NULL;
    const char *prefix = base_string ? fo_bytes(base_string) : NULL;
    if (!prefix || !*prefix) return url_new(NULL, "URLWithString:", string);
    size_t prefix_length = strlen(prefix), text_length = strlen(text);
    int slash = prefix[prefix_length - 1] != '/' && text[0] != '/';
    int skip = prefix[prefix_length - 1] == '/' && text[0] == '/';
    char *joined = malloc(prefix_length + text_length + 2);
    if (!joined) return NULL;
    memcpy(joined, prefix, prefix_length);
    size_t position = prefix_length;
    if (slash) joined[position++] = '/';
    memcpy(joined + position, text + (skip ? 1 : 0), text_length - (skip ? 1 : 0));
    position += text_length - (skip ? 1 : 0);
    joined[position] = 0;
    void *result = url_new(NULL, "URLWithString:", fo_string_new(joined));
    free(joined);
    return result;
}

static void *request_init_url(void *self, const char *sel, void *url)
{
    (void)sel;
    if (!self) return NULL;
    net_object *n = net_find(self);
    if (!n) {
        n = calloc(1, sizeof(*n));
        if (!n) return NULL;
        n->object = self;
        n->timeout = 30;
        n->next = net_objects;
        net_objects = n;
    }
    n->url = url;
    if (!n->method) n->method = strdup("GET");
    if (!n->headers) n->headers = fo_dict_new();
    return self;
}

static void *request_header_value(void *self, const char *sel, void *field)
{
    (void)sel;
    net_object *n = net_find(self);
    if (!n || !fo_is_dict(n->headers)) return NULL;
    return d_value(n->headers, NULL, field);
}

static void progress_setting_noop(void *self, const char *sel, ...)
{
    (void)self;
    (void)sel;
}

static signed char progress_flag_true(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return 1;
}

static void operation_queue_limit(void *self, const char *sel, long count)
{
    (void)self;
    (void)sel;
    (void)count;
}

static long operation_queue_get_limit(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return 1;
}

static const chm_host_method support_methods[] = {
    {"NSSet", "setWithObjects:", "@@:", (void *)chm_ns_set_entry, 1},
    {"NSSet", "set", "@@:", set_empty, 1},
    {"NSSet", "setWithArray:", "@@:@", set_with_array, 1},
    {"NSSet", "initWithObjects:", "@@:", (void *)chm_ns_set_entry, 0},
    {"NSSet", "containsObject:", "c@:@", set_contains, 0},
    {"NSSet", "member:", "@@:@", set_member, 0},
    {"NSSet", "allObjects", "@@:", set_all_objects, 0},
    {"NSSet", "anyObject", "@@:", set_any_object, 0},
    {"NSSet", "isSubsetOfSet:", "c@:@", set_is_subset, 0},
    {"NSSet", "intersectsSet:", "c@:@", set_intersects, 0},
    {"NSSet", "isEqualToSet:", "c@:@", set_equal_to, 0},
    {"NSSet", "setByAddingObject:", "@@:@", set_by_adding, 0},
    {"NSSet", "setByAddingObjectsFromSet:", "@@:@", set_by_adding_set, 0},
    {"NSSet", "enumerateObjectsUsingBlock:", "v@:@?", set_blocks, 0},
    {"NSMutableSet", "set", "@@:", set_empty, 1},
    {"NSMutableSet", "setWithObjects:", "@@:", (void *)chm_ns_set_entry, 1},
    {"NSMutableSet", "setWithArray:", "@@:@", set_with_array, 1},
    {"NSMutableSet", "initWithObjects:", "@@:", (void *)chm_ns_set_entry, 0},
    {"NSMutableSet", "addObject:", "v@:@", set_add, 0},
    {"NSMutableSet", "addObjectsFromArray:", "v@:@", set_add_from_array, 0},
    {"NSMutableSet", "removeObject:", "v@:@", set_remove, 0},
    {"NSMutableSet", "unionSet:", "v@:@", set_add_from_array, 0},
    {"NSMutableSet", "containsObject:", "c@:@", set_contains, 0},
    {"NSMutableSet", "allObjects", "@@:", set_all_objects, 0},
    {"NSMutableSet", "anyObject", "@@:", set_any_object, 0},
    {"NSMutableSet", "enumerateObjectsUsingBlock:", "v@:@?", set_blocks, 0},
    {"NSArray", "enumerateObjectsUsingBlock:", "v@:@?", array_blocks, 0},
    {"NSMutableArray", "enumerateObjectsUsingBlock:", "v@:@?", array_blocks, 0},
    {"NSDictionary", "enumerateKeysAndObjectsUsingBlock:", "v@:@?", dict_blocks, 0},
    {"NSMutableDictionary", "enumerateKeysAndObjectsUsingBlock:", "v@:@?", dict_blocks, 0},
    {"NSMutableData", "data", "@@:", mutable_data_empty, 1},
    {"NSData", "data", "@@:", mutable_data_empty, 1},
    {"NSMutableData", "dataWithCapacity:", "@@:Q", mutable_data_capacity, 1},
    {"NSMutableData", "initWithCapacity:", "@@:Q", mutable_data_capacity, 0},
    {"NSMutableData", "initWithLength:", "@@:Q", mutable_data_capacity, 0},
    {"NSMutableData", "appendData:", "v@:@", data_append_data, 0},
    {"NSMutableData", "appendBytes:length:", "v@:^vQ", data_append_bytes, 0},
    {"NSMutableData", "setLength:", "v@:Q", data_set_length, 0},
    {"NSAssertionHandler", "currentHandler", "@@:", assert_handler_current, 1},
    {"NSAssertionHandler", "handleFailureInMethod:object:file:lineNumber:description:", "v@:@@@@Q@",
     assert_handler_failure, 0},
    {"NSAssertionHandler", "handleFailureInFunction:file:lineNumber:description:", "v@:@@Q@",
     assert_handler_failure, 0},
    {"NSObject", "addObserver:forKeyPath:options:context:", "v@:@@Q^v", kvo_add, 0},
    {"NSObject", "removeObserver:forKeyPath:", "v@:@@", kvo_remove, 0},
    {"NSObject", "removeObserver:forKeyPath:context:", "v@:@@^v", kvo_remove_context, 0},
    {"NSProgress", "initWithParent:userInfo:", "@@:@@", progress_init_parent, 0},
    {"NSURL", "URLWithString:relativeToURL:", "@@:@@", url_with_relative, 1},
    {"NSMutableURLRequest", "initWithURL:", "@@:@", request_init_url, 0},
    {"NSURLRequest", "valueForHTTPHeaderField:", "@@:@", request_header_value, 0},
    {"NSMutableURLRequest", "valueForHTTPHeaderField:", "@@:@", request_header_value, 0},
    {"NSProgress", "setCancellable:", "v@:c", progress_setting_noop, 0},
    {"NSProgress", "setCancellationHandler:", "v@:@?", progress_setting_noop, 0},
    {"NSProgress", "setPausable:", "v@:c", progress_setting_noop, 0},
    {"NSProgress", "setPausingHandler:", "v@:@?", progress_setting_noop, 0},
    {"NSProgress", "setResumingHandler:", "v@:@?", progress_setting_noop, 0},
    {"NSProgress", "isCancellable", "c@:", progress_flag_true, 0},
    {"NSProgress", "isPausable", "c@:", progress_flag_true, 0},
    {"NSOperationQueue", "setMaxConcurrentOperationCount:", "v@:q", operation_queue_limit, 0},
    {"NSOperationQueue", "maxConcurrentOperationCount", "q@:", operation_queue_get_limit, 0}
};
#define METHOD_BUCKETS 257

typedef struct objc_method {
    chm_objc_class *owner;
    char *name, *types;
    void *imp;
    struct objc_method *next;
} objc_method;

typedef struct association {
    void *object, *value;
    const void *key;
    struct association *next;
} association;

typedef struct objc_method_store {
    objc_method *bucket[METHOD_BUCKETS];
    association *associations;
} objc_method_store;

static int class_receiver(void *obj);
static void *sel_register_impl(const char *name);

static size_t method_bucket(const void *owner, const char *name)
{
    uintptr_t h = (uintptr_t)owner >> 3;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) h = h * 33u + *p;
    return h % METHOD_BUCKETS;
}

static objc_method *method_local(chm_objc_class *owner, const char *sel)
{
    objc_method_store *store = g_active_session ? g_active_session->objc_methods : NULL;
    if (!store || !owner || !sel) return NULL;
    for (objc_method *m = store->bucket[method_bucket(owner, sel)]; m; m = m->next)
        if (m->owner == owner && !strcmp(m->name, sel)) return m;
    return NULL;
}

static int method_put(chm_session *s, chm_objc_class *owner, const char *sel, const char *types, void *imp)
{
    if (!s || !s->objc_methods || !owner || !sel || !*sel || !imp) return 0;
    objc_method *m = method_local(owner, sel);
    char *encoding = strdup(types ? types : "");
    if (!encoding) return 0;
    if (!m) {
        m = calloc(1, sizeof(*m));
        if (!m || !(m->name = strdup(sel))) { free(m); free(encoding); return 0; }
        size_t bucket = method_bucket(owner, sel);
        m->owner = owner;
        m->next = s->objc_methods->bucket[bucket];
        s->objc_methods->bucket[bucket] = m;
    }
    free(m->types);
    m->types = encoding;
    m->imp = imp;
    return 1;
}

void chm_objc_register_host(chm_session *s, const chm_host_method *entries, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const chm_host_method *e = &entries[i];
        void *owner = e->class_method ? chm_foundation_lookup_metaclass(e->owner) : chm_foundation_lookup_class(e->owner);
        if (!method_put(s, owner, e->sel, e->types, e->imp))
            chm_session_log(s, "objc: cannot register %s %s", e->owner, e->sel);
    }
}

static uint64_t metadata_bytes(chm_image *img, uintptr_t address, uint32_t access)
{
    for (uint32_t i = 0; address && i < img->nsegs; i++) {
        const chm_segment *seg = &img->segs[i];
        uintptr_t start = (uintptr_t)seg->mapped;
        if (!start || (seg->initprot & access) != access || address < start) continue;
        uint64_t offset = address - start;
        if (offset < seg->vmsize) return seg->vmsize - offset;
    }
    return 0;
}

static const char *metadata_text(chm_image *img, uintptr_t addr)
{
    size_t limit = metadata_bytes(img, addr, 1);
    if (limit > 8192) limit = 8192;
    return limit && memchr((const void *)addr, 0, limit) ? (const char *)addr : NULL;
}

static void index_method_list(chm_session *s, chm_image *img, chm_objc_class *owner, uintptr_t list)
{
    chm_method_list header;
    uint64_t available = metadata_bytes(img, list, 1);
    if (!owner || available < sizeof(header)) return;
    memcpy(&header, (void *)list, sizeof(header));
    int small = (header.entsize_and_flags & CHM_METHOD_LIST_SMALL) != 0;
    size_t stride = header.entsize_and_flags & 0xfffcu;
    if (stride < (small ? sizeof(chm_small_method) : sizeof(chm_classic_method)) ||
        header.count > 65536 || header.count > (available - sizeof(header)) / stride) return;
    uintptr_t at = list + sizeof(header);
    for (uint32_t i = 0; i < header.count; i++, at += stride) {
        uintptr_t name, types, imp;
        if (small) {
            chm_small_method entry;
            memcpy(&entry, (const void *)at, sizeof(entry));
            name = at + (intptr_t)entry.name_offset;
            types = at + offsetof(chm_small_method, types_offset) + (intptr_t)entry.types_offset;
            imp = at + offsetof(chm_small_method, imp_offset) + (intptr_t)entry.imp_offset;
            if (!(header.entsize_and_flags & 0x40000000u)) {
                if (metadata_bytes(img, name, 1) < sizeof(name)) continue;
                memcpy(&name, (const void *)name, sizeof(name));
            }
        } else {
            chm_classic_method entry;
            memcpy(&entry, (const void *)at, sizeof(entry));
            name = (uintptr_t)entry.name; types = (uintptr_t)entry.types; imp = (uintptr_t)entry.imp;
        }
        const char *selector = metadata_text(img, name), *encoding = metadata_text(img, types);
        if (selector && encoding && !(imp & 3) && metadata_bytes(img, imp, 4) >= 4)
            method_put(s, owner, selector, encoding, (void *)imp);
    }
}

static void index_class_methods(chm_session *s, chm_image *img, chm_objc_class *owner)
{
    if (metadata_bytes(img, (uintptr_t)owner, 1) < sizeof(*owner)) return;
    uintptr_t data = owner->bits & CHM_FAST_DATA_MASK;
    if (metadata_bytes(img, data, 1) < sizeof(chm_class_ro)) return;
    index_method_list(s, img, owner, (uintptr_t)((chm_class_ro *)data)->methods);
}

void chm_objc_index_methods(chm_session *s)
{
    for (int i = 0; i < s->class_count; i++) {
        chm_objc_cls_entry *e = &s->classes[i];
        chm_image *img = &s->images[e->image_index];
        if (!img->executable) continue;
        index_class_methods(s, img, e->cls);
        index_class_methods(s, img, e->meta);
    }
    for (int i = s->image_count - 1; i >= 0; i--) {
        chm_image *img = &s->images[i];
        if (!img->executable) continue;
        for (uint32_t j = 0; j < img->nsegs; j++) {
            chm_segment *seg = &img->segs[j];
            for (uint32_t k = 0; k < seg->nsects; k++) {
                chm_section *sec = &seg->sects[k];
                if (strcmp(sec->sect, "__objc_catlist") && strcmp(sec->sect, "__objc_catlist2")) continue;
                uintptr_t slots = sec->addr + img->slide;
                if (metadata_bytes(img, slots, 1) < sec->size) continue;
                for (size_t off = 0; off + sizeof(uintptr_t) <= sec->size; off += sizeof(uintptr_t)) {
                    uintptr_t address;
                    struct { const char *name; chm_objc_class *owner; uintptr_t instance, meta; } category;
                    memcpy(&address, (const void *)(slots + off), sizeof(address));
                    if (metadata_bytes(img, address, 1) < sizeof(category)) continue;
                    memcpy(&category, (const void *)address, sizeof(category));
                    if (!class_receiver(category.owner)) continue;
                    index_method_list(s, img, category.owner, category.instance);
                    index_method_list(s, img, category.owner->isa, category.meta);
                }
            }
        }
    }
}

static objc_method *instance_method(chm_objc_class *cls, const char *sel)
{
    if (!class_receiver(cls) || !sel) return NULL;
    for (int i = 0; cls && i < 64; i++, cls = cls->superclass) {
        objc_method *m = method_local(cls, sel);
        if (m) return m;
    }
    return NULL;
}

static objc_method *meta_method(chm_objc_class *cls, const char *sel)
{
    return class_receiver(cls) ? instance_method(cls->isa, sel) : NULL;
}

static signed char add_method(chm_objc_class *cls, const char *sel, void *imp, const char *types)
{
    if (!class_receiver(cls) || method_local(cls, sel)) return 0;
    return method_put(g_active_session, cls, sel, types, imp);
}

static void exchange_methods(objc_method *a, objc_method *b)
{
    if (!a || !b) return;
    void *tmp = a->imp;
    a->imp = b->imp;
    b->imp = tmp;
}

static void *method_implementation(objc_method *m) { return m ? m->imp : NULL; }
static const char *method_name(objc_method *m) { return m ? sel_register_impl(m->name) : NULL; }
static const char *method_types(objc_method *m) { return m ? m->types : NULL; }

static objc_method **copy_methods(chm_objc_class *cls, unsigned *count)
{
    if (count) *count = 0;
    if (!class_receiver(cls)) return NULL;
    objc_method_store *store = g_active_session->objc_methods;
    if (!store) return NULL;
    size_t n = 0;
    for (size_t i = 0; i < METHOD_BUCKETS; i++)
        for (objc_method *m = store->bucket[i]; m; m = m->next) n += m->owner == cls;
    objc_method **out = n ? calloc(n + 1, sizeof(*out)) : NULL;
    if (!out) return NULL;
    n = 0;
    for (size_t i = 0; i < METHOD_BUCKETS; i++)
        for (objc_method *m = store->bucket[i]; m; m = m->next) if (m->owner == cls) out[n++] = m;
    if (count) *count = (unsigned)n;
    return out;
}

static void *associated_get(void *object, const void *key)
{
    objc_method_store *store = g_active_session->objc_methods;
    for (association *a = store ? store->associations : NULL; a; a = a->next)
        if (a->object == object && a->key == key) return a->value;
    return NULL;
}

static void associated_set(void *object, const void *key, void *value, uintptr_t policy)
{
    objc_method_store *store = g_active_session->objc_methods;
    if (!store || !object || !key) return;
    if (value && (policy & 0xffu) == 3) value = chm_objc_send0(value, "copy");
    association **p = &store->associations;
    while (*p && ((*p)->object != object || (*p)->key != key)) p = &(*p)->next;
    if (*p && !value) { association *a = *p; *p = a->next; free(a); return; }
    if (!value) return;
    if (!*p) {
        *p = calloc(1, sizeof(**p));
        if (!*p) return;
        (*p)->object = object; (*p)->key = key;
    }
    (*p)->value = value;
}

void chm_objc_methods_reset(chm_session *s)
{
    objc_method_store *store = s->objc_methods;
    if (g_active_session == s) g_active_session = NULL;
    if (!store) return;
    for (size_t i = 0; i < METHOD_BUCKETS; i++) {
        while (store->bucket[i]) {
            objc_method *m = store->bucket[i]; store->bucket[i] = m->next;
            free(m->name); free(m->types); free(m);
        }
    }
    while (store->associations) {
        association *a = store->associations; store->associations = a->next; free(a);
    }
    free(store);
    s->objc_methods = NULL;
}
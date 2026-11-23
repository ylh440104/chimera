#include "chm_session.h"
#include "chm_scene.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHM_FAST_DATA_MASK 0x00007ffffffffff8ull
#define CHM_METHOD_LIST_SMALL 0x80000000u

typedef struct {
    uint32_t flags;
    uint32_t instance_start;
    uint32_t instance_size;
    uint32_t reserved;
    const uint8_t *ivar_layout;
    const char *name;
    const void *methods;
    const void *protocols;
    const void *ivars;
    const void *weak_ivar_layout;
    const void *properties;
} chm_class_ro;

typedef struct {
    void *isa;
    void *superclass;
    void *cache;
    void *vtable;
    uint64_t bits;
} chm_objc_class;

typedef struct {
    const char *name;
    const char *types;
    void *imp;
} chm_classic_method;

typedef struct {
    int32_t name_offset;
    int32_t types_offset;
    int32_t imp_offset;
} chm_small_method;

typedef struct {
    uint32_t entsize_and_flags;
    uint32_t count;
} chm_method_list;

extern void chm_objc_msgSend_trampoline(void);
extern void chm_objc_msgSendSuper2_trampoline(void);
static void *alloc_impl(void *cls);
static void *alloc_method(void *cls, const char *sel);
static void *new_method(void *cls, const char *sel);
static void *class_method(void *self, const char *sel);

static chm_session *g_active_session;
static uint8_t g_empty_cache[64];
static uint8_t g_block_isa[64];
static uint8_t g_cfstr_isa[64];

static chm_class_ro *ro_of(chm_objc_class *c)
{
    if (!c)
        return NULL;
    chm_class_ro *host = chm_foundation_class_ro(c);
    if (host)
        return host;
    return (chm_class_ro *)(uintptr_t)(c->bits & CHM_FAST_DATA_MASK);
}

static const char *class_name(chm_objc_class *c)
{
    chm_class_ro *ro = ro_of(c);

    return ro ? ro->name : NULL;
}

static size_t class_size(chm_objc_class *c)
{
    chm_class_ro *ro = ro_of(c);

    return ro ? (size_t)ro->instance_size : 0;
}

#include "objc_methods.inc"

#define CHM_TRACE_RING 48

typedef struct {
    char cls[64];
    char sel[64];
    void *imp;
} chm_trace_entry;

static chm_trace_entry g_trace[CHM_TRACE_RING];
static unsigned g_trace_pos;
static unsigned g_trace_total;

static void trace_msg(const char *cls, const char *sel, void *imp)
{
    chm_trace_entry *e = &g_trace[g_trace_pos % CHM_TRACE_RING];

    snprintf(e->cls, sizeof(e->cls), "%s", cls ? cls : "?");
    snprintf(e->sel, sizeof(e->sel), "%s", sel ? sel : "?");
    e->imp = imp;
    g_trace_pos++;
    g_trace_total++;
}

static const char *class_name_of(chm_objc_class *c)
{
    chm_class_ro *ro = ro_of(c);

    return ro && ro->name ? ro->name : "?";
}

void chm_objc_dump_trace(chm_session *s)
{
    unsigned start;
    unsigned i;

    if (!s)
        return;
    chm_session_log(s, "msg trace (last %u of %u):", g_trace_total < CHM_TRACE_RING ? g_trace_total : CHM_TRACE_RING, g_trace_total);
    start = g_trace_total > CHM_TRACE_RING ? g_trace_total - CHM_TRACE_RING : 0;
    for (i = start; i < g_trace_total; i++) {
        chm_trace_entry *e = &g_trace[i % CHM_TRACE_RING];

        chm_session_log(s, "  [%s %s] -> %p", e->cls, e->sel, e->imp);
    }
}

int chm_objc_session_add_method(chm_session *s, const chm_method_reg *m)
{
    if (!s || !m || !m->sel[0])
        return -1;
    if (s->method_count >= CHM_MAX_METHOD_REGS)
        return -1;
    s->methods[s->method_count++] = *m;
    return 0;
}

static void *lookup_registered(chm_session *s, const char *sel)
{
    int i;

    if (!s || !sel)
        return NULL;
    for (i = 0; i < s->method_count; i++) {
        if (strcmp(s->methods[i].sel, sel) == 0)
            return s->methods[i].imp;
    }
    return NULL;
}

static void *resolve_imp(void *self, const char *sel, int report)
{
    chm_objc_class *c;
    int depth = 0;
    void *fallback;
    const char *origin;

    if (!self || !sel)
        return NULL;
    origin = class_name_of((chm_objc_class *)(*(void **)self));
    c = (chm_objc_class *)(*(void **)self);
    if (!c) {
        trace_msg("nil-isa", sel, NULL);
        return NULL;
    }
    while (c && depth < 64) {
        objc_method *method = method_local(c, sel);
        void *imp = method ? method->imp : NULL;

        if (imp) {
            trace_msg(origin, sel, imp);
            return imp;
        }
        c = (chm_objc_class *)c->superclass;
        depth++;
    }
    if (!strcmp(sel, "alloc") || !strcmp(sel, "allocWithZone:"))
        fallback = (void *)alloc_method;
    else if (!strcmp(sel, "new"))
        fallback = (void *)new_method;
    else if (!strcmp(sel, "class"))
        fallback = (void *)class_method;
    else {
        fallback = chm_scene_method(self, sel);
        if (!fallback)
            fallback = lookup_registered(g_active_session, sel);
    }
    if (!fallback)
        fallback = chm_foundation_lookup_method(NULL, sel);
    trace_msg(origin, sel, fallback);
    if (!fallback && report && g_active_session)
        chm_session_log(g_active_session, "selector miss: %c[%s %s]", class_receiver(self) ? '+' : '-', origin, sel);
    return fallback;
}

void *chm_objc_resolve_imp(void *self, const char *sel)
{
    return resolve_imp(self, sel, 1);
}

void *chm_objc_find_imp(void *self, const char *sel)
{
    return resolve_imp(self, sel, 0);
}

int chm_objc_is_kind(void *obj, const char *name)
{
    if (!obj || !name) return 0;
    chm_objc_class *cls = *(chm_objc_class **)obj;
    for (int depth = 0; cls && depth < 64; depth++, cls = cls->superclass) {
        const char *current = class_name(cls);
        if (current && !strcmp(current, name)) return 1;
    }
    return 0;
}

void *chm_objc_resolve_super2_imp(void *super_ptr, const char *sel)
{
    void **s = (void **)super_ptr;
    chm_objc_class *c;
    int depth = 0;

    if (!s || !sel)
        return NULL;
    c = (chm_objc_class *)s[1];
    if (c)
        c = (chm_objc_class *)c->superclass;
    while (c && depth < 64) {
        objc_method *method = method_local(c, sel);
        void *imp = method ? method->imp : NULL;

        if (imp)
            return imp;
        c = (chm_objc_class *)c->superclass;
        depth++;
    }
    void *fallback = lookup_registered(g_active_session, sel);
    return fallback ? fallback : chm_foundation_lookup_method(NULL, sel);
}

void chm_objc_session_init(chm_session *s)
{
    g_active_session = s;
    s->objc_methods = calloc(1, sizeof(*s->objc_methods));
    g_trace_pos = g_trace_total = 0;
    s->class_count = 0;
    s->sel_count = 0;
}

static bool in_image(chm_image *img, uint64_t addr, uint64_t size)
{
    uint32_t i;

    if (!addr || addr + size < addr)
        return false;
    for (i = 0; i < img->nsegs; i++) {
        chm_segment *seg = &img->segs[i];

        if (!seg->vmsize || !strcmp(seg->name, "__PAGEZERO"))
            continue;
        uint64_t start = seg->vmaddr + img->slide;

        if (addr >= start && addr + size <= start + seg->vmsize)
            return true;
    }
    return false;
}

void chm_objc_session_scan(chm_session *s, int img_idx)
{
    chm_image *img = &s->images[img_idx];
    uint32_t i;
    uint32_t k;
    int added = 0;

    for (i = 0; i < img->nsegs; i++) {
        chm_segment *seg = &img->segs[i];

        for (k = 0; k < seg->nsects; k++) {
            chm_section *sec = &seg->sects[k];
            uint64_t *slots;
            uint64_t count;
            uint64_t j;

            if (strcmp(sec->sect, "__objc_classlist") != 0)
                continue;
            slots = (uint64_t *)(uintptr_t)(sec->addr + img->slide);
            count = sec->size / 8;
            for (j = 0; j < count; j++) {
                uint64_t raw = slots[j];
                chm_objc_class *c;
                const char *name;

                if (!raw || !in_image(img, raw, sizeof(chm_objc_class)))
                    continue;
                c = (chm_objc_class *)(uintptr_t)raw;
                name = class_name(c);
                if (!name || !in_image(img, (uint64_t)(uintptr_t)name, 1))
                    continue;
                if (s->class_count >= CHM_MAX_CLASSES)
                    continue;
                chm_objc_cls_entry *e = &s->classes[s->class_count++];
                snprintf(e->name, sizeof(e->name), "%s", name);
                e->cls = c;
                e->meta = c->isa;
                e->image_index = img_idx;
                added++;
            }
        }
    }
    if (added)
        chm_session_log(s, "objc[%d]: registered %d classes", img_idx, added);
}

void *chm_objc_session_get_class(chm_session *s, const char *name)
{
    int i;

    if (!s || !name)
        return NULL;
    for (i = 0; i < s->class_count; i++) {
        if (strcmp(s->classes[i].name, name) == 0)
            return s->classes[i].cls;
    }
    return NULL;
}

void *chm_objc_session_get_metaclass(chm_session *s, const char *name)
{
    int i;

    if (!s || !name)
        return NULL;
    for (i = 0; i < s->class_count; i++) {
        if (strcmp(s->classes[i].name, name) == 0)
            return s->classes[i].meta;
    }
    return NULL;
}

static void *sel_register_impl(const char *name)
{
    chm_session *s = g_active_session;
    int i;

    if (!name)
        return NULL;
    if (!s)
        return (void *)name;
    for (i = 0; i < s->sel_count; i++) {
        if (strcmp(s->sels[i], name) == 0)
            return s->sels[i];
    }
    if (s->sel_count < CHM_MAX_SELS) {
        char *copy = strdup(name);

        if (copy) {
            s->sels[s->sel_count++] = copy;
            return copy;
        }
    }
    return (void *)name;
}

const char *chm_objc_class_name(void *cls)
{
    return cls ? class_name(cls) : NULL;
}

void *chm_objc_get_class(const char *name)
{
    void *cls = chm_objc_session_get_class(g_active_session, name);
    return cls ? cls : chm_foundation_lookup_class(name);
}

static void *get_class_impl(const char *name)
{
    return chm_objc_get_class(name);
}

static void *get_metaclass_impl(const char *name)
{
    void *cls = chm_objc_session_get_metaclass(g_active_session, name);
    return cls ? cls : chm_foundation_lookup_metaclass(name);
}

static void *object_get_class_impl(void *obj)
{
    return obj ? *(void **)obj : NULL;
}

static int class_receiver(void *obj)
{
    if (!obj) return 0;
    if (chm_foundation_class_ro(obj)) return 1;
    if (g_active_session)
        for (int i = 0; i < g_active_session->class_count; ++i)
            if (g_active_session->classes[i].cls == obj || g_active_session->classes[i].meta == obj)
                return 1;
    return 0;
}

static void *class_method(void *self, const char *sel)
{
    (void)sel;
    return class_receiver(self) ? self : object_get_class_impl(self);
}

static void *alloc_impl(void *cls)
{
    if (!cls) return NULL;
    if (chm_foundation_class_ro(cls)) return chm_foundation_alloc_class(cls, 0);
    size_t sz = class_size(cls);
    if (sz < 16) sz = 16;
    void *obj = calloc(1, sz);
    if (obj) *(void **)obj = cls;
    return obj;
}

static void *alloc_method(void *cls, const char *sel)
{
    (void)sel;
    return class_receiver(cls) ? alloc_impl(cls) : NULL;
}

static void *alloc_init_impl(void *cls)
{
    void *obj = alloc_impl(cls);
    return obj ? chm_objc_send0(obj, "init") : NULL;
}

static void *new_method(void *cls, const char *sel)
{
    (void)sel;
    return class_receiver(cls) ? alloc_init_impl(cls) : NULL;
}

static void *alloc_zone_impl(void *cls, void *zone)
{
    (void)zone;
    return alloc_impl(cls);
}

static void *retain_impl(void *obj)
{
    return obj;
}

static void release_impl(void *obj)
{
    (void)obj;
}

static void store_strong_impl(void **slot, void *obj)
{
    if (slot)
        *slot = obj;
}

static void *autorelease_push_impl(void)
{
    return NULL;
}

static void autorelease_pop_impl(void *ctx)
{
    (void)ctx;
}

#include "blocks.inc"

void *chm_objc_msgSend_bridge(void)
{
    return (void *)chm_objc_msgSend_trampoline;
}

void *chm_objc_msgSendSuper2_bridge(void)
{
    return (void *)chm_objc_msgSendSuper2_trampoline;
}

void *chm_objc_cfstr_isa(void)
{
    return g_cfstr_isa;
}

void chm_objc_set_cfstr_isa(void *isa)
{
    if (isa)
        memcpy(g_cfstr_isa, isa, 64);
}

void *chm_objc_send0(void *obj, const char *sel)
{
    void *(*fn)(void *, const char *);

    if (!obj || !sel)
        return NULL;
    fn = (void *(*)(void *, const char *))chm_objc_resolve_imp(obj, sel);
    if (!fn)
        return NULL;
    return fn(obj, sel);
}

void *chm_objc_send1(void *obj, const char *sel, void *a)
{
    void *(*fn)(void *, const char *, void *);

    if (!obj || !sel)
        return NULL;
    fn = (void *(*)(void *, const char *, void *))chm_objc_resolve_imp(obj, sel);
    if (!fn)
        return NULL;
    return fn(obj, sel, a);
}

void *chm_objc_send2(void *obj, const char *sel, void *a, void *b)
{
    void *(*fn)(void *, const char *, void *, void *);

    if (!obj || !sel)
        return NULL;
    fn = (void *(*)(void *, const char *, void *, void *))chm_objc_resolve_imp(obj, sel);
    if (!fn)
        return NULL;
    return fn(obj, sel, a, b);
}

void *chm_objc_send3(void *obj, const char *sel, void *a, void *b, void *c)
{
    void *(*fn)(void *, const char *, void *, void *, void *);

    if (!obj || !sel)
        return NULL;
    fn = (void *(*)(void *, const char *, void *, void *, void *))chm_objc_resolve_imp(obj, sel);
    if (!fn)
        return NULL;
    return fn(obj, sel, a, b, c);
}

void *chm_objc_new_object(const char *cls_name)
{
    void *cls = chm_objc_session_get_class(g_active_session, cls_name);
    void *obj;

    if (!cls)
        cls = chm_foundation_lookup_class(cls_name);
    if (!cls)
        return NULL;
    obj = alloc_impl(cls);
    if (!obj)
        return NULL;
    return chm_objc_send0(obj, "init");
}

void *chm_objc_session_lookup(chm_session *s, const char *name)
{
    static const struct {
        const char *name;
        void *addr;
    } table[] = {
        { "_class_addMethod", add_method },
        { "_class_getInstanceMethod", instance_method },
        { "_class_getClassMethod", meta_method },
        { "_class_copyMethodList", copy_methods },
        { "_method_getImplementation", method_implementation },
        { "_method_getName", method_name },
        { "_method_getTypeEncoding", method_types },
        { "_method_exchangeImplementations", exchange_methods },
        { "_objc_getAssociatedObject", associated_get },
        { "_objc_setAssociatedObject", associated_set },
        { "_objc_msgSend", (void *)chm_objc_msgSend_trampoline },
        { "_objc_msgSendSuper2", (void *)chm_objc_msgSendSuper2_trampoline },
        { "_objc_getClass", (void *)get_class_impl },
        { "_objc_lookUpClass", (void *)get_class_impl },
        { "_objc_getMetaClass", (void *)get_metaclass_impl },
        { "_object_getClass", (void *)object_get_class_impl },
        { "_objc_sel_registerName", (void *)sel_register_impl },
        { "_sel_registerName", (void *)sel_register_impl },
        { "_objc_alloc", (void *)alloc_impl },
        { "_objc_alloc_init", (void *)alloc_init_impl },
        { "_objc_allocWithZone", (void *)alloc_zone_impl },
        { "_objc_opt_new", (void *)alloc_init_impl },
        { "_objc_retain", (void *)retain_impl },
        { "_objc_release", (void *)release_impl },
        { "_objc_autorelease", (void *)retain_impl },
        { "_objc_autoreleaseReturnValue", (void *)retain_impl },
        { "_objc_retainAutoreleasedReturnValue", (void *)retain_impl },
        { "_objc_retainAutorelease", (void *)retain_impl },
        { "_objc_retainAutoreleaseReturnValue", (void *)retain_impl },
        { "_objc_unsafeClaimAutoreleasedReturnValue", (void *)retain_impl },
        { "_objc_claimAutoreleasedReturnValue", (void *)retain_impl },
        { "_objc_retainBlock", (void *)chm_block_copy },
        { "_objc_copyWeak", (void *)copy_weak_impl },
        { "_objc_moveWeak", (void *)move_weak_impl },
        { "_objc_storeStrong", (void *)store_strong_impl },
        { "_objc_autoreleasePoolPush", (void *)autorelease_push_impl },
        { "_objc_autoreleasePoolPop", (void *)autorelease_pop_impl },
        { "__Block_copy", (void *)chm_block_copy },
        { "__Block_release", (void *)chm_block_release },
        { "__Block_object_assign", (void *)block_object_assign_impl },
        { "__Block_object_dispose", (void *)block_object_dispose_impl },
        { "__objc_empty_cache", (void *)g_empty_cache },
        { "__NSConcreteStackBlock", (void *)g_block_isa },
        { "__NSConcreteGlobalBlock", (void *)g_block_isa },
        { "__NSConcreteMallocBlock", (void *)g_block_isa },
        { "___CFConstantStringClassReference", (void *)g_cfstr_isa },
        { NULL, NULL }
    };
    int i;

    if (!name)
        return NULL;
    for (i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, name) == 0)
            return table[i].addr;
    }
    if (strncmp(name, "_OBJC_CLASS_$_", 14) == 0) {
        void *c = chm_objc_session_get_class(s, name + 14);

        return c ? c : chm_foundation_lookup_class(name + 14);
    }
    if (strncmp(name, "_OBJC_METACLASS_$_", 18) == 0) {
        void *m = chm_objc_session_get_metaclass(s, name + 18);

        return m ? m : chm_foundation_lookup_metaclass(name + 18);
    }
    return NULL;
}
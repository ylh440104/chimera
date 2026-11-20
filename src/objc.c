#include "chm.h"
#include "chm_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHM_FAST_DATA_MASK 0x00007ffffffffff8ull
#define CHM_MAX_CLASSES 1024
#define CHM_MAX_SELS 1024
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

typedef struct {
    char name[160];
    chm_objc_class *cls;
    chm_objc_class *meta;
} chm_class_entry;

static chm_class_entry g_classes[CHM_MAX_CLASSES];
static int g_class_count;
static char *g_sels[CHM_MAX_SELS];
static int g_sel_count;
static char g_block_isa[64];

static chm_class_ro *chm_class_ro_of(chm_objc_class *c)
{
    uint64_t bits;

    if (!c)
        return NULL;
    bits = c->bits & CHM_FAST_DATA_MASK;
    return (chm_class_ro *)(uintptr_t)bits;
}

static const char *chm_class_name(chm_objc_class *c)
{
    chm_class_ro *ro = chm_class_ro_of(c);

    return ro ? ro->name : NULL;
}

static chm_method_list *chm_class_methods(chm_objc_class *c)
{
    chm_class_ro *ro = chm_class_ro_of(c);

    return ro ? (chm_method_list *)(uintptr_t)ro->methods : NULL;
}

static size_t chm_class_size(chm_objc_class *c)
{
    chm_class_ro *ro = chm_class_ro_of(c);

    return ro ? (size_t)ro->instance_size : 0;
}

void chm_objc_scan(chm_image *img, chm_report *r)
{
    uint32_t i;
    uint32_t k;
    int found = 0;

    g_class_count = 0;
    for (i = 0; i < img->nsegs; i++) {
        chm_segment *s = &img->segs[i];

        for (k = 0; k < s->nsects; k++) {
            chm_section *sec = &s->sects[k];
            uint64_t *slots;
            uint64_t count;
            uint64_t j;

            if (strcmp(sec->sect, "__objc_classlist") != 0)
                continue;
            slots = (uint64_t *)(uintptr_t)(sec->addr + img->slide);
            count = sec->size / 8;
            for (j = 0; j < count; j++) {
                chm_objc_class *c = (chm_objc_class *)(uintptr_t)slots[j];
                const char *name;

                if (!c)
                    continue;
                name = chm_class_name(c);
                if (!name)
                    continue;
                if (g_class_count < CHM_MAX_CLASSES) {
                    chm_class_entry *e = &g_classes[g_class_count];

                    snprintf(e->name, sizeof(e->name), "%s", name);
                    e->cls = c;
                    e->meta = (chm_objc_class *)c->isa;
                    g_class_count++;
                    found++;
                }
            }
        }
    }
    chm_report_add(r, "objc: 类 %d", found);
    if (found > 0) {
        char line[2000];
        int shown = found < 6 ? found : 6;

        line[0] = 0;
        for (k = 0; k < (uint32_t)shown; k++) {
            size_t used = strlen(line);

            snprintf(line + used, sizeof(line) - used, " %.*s", (int)sizeof(g_classes[k].name), g_classes[k].name);
        }
        chm_report_add(r, "objc: 例 %s", line);
    }
}

int chm_objc_class_count(void)
{
    return g_class_count;
}

const char *chm_objc_class_at(int index)
{
    if (index < 0 || index >= g_class_count)
        return NULL;
    return g_classes[index].name;
}

void *chm_objc_getClass(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_class_count; i++) {
        if (strcmp(g_classes[i].name, name) == 0)
            return g_classes[i].cls;
    }
    return NULL;
}

void *chm_objc_getMetaClass(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_class_count; i++) {
        if (strcmp(g_classes[i].name, name) == 0)
            return g_classes[i].meta;
    }
    return NULL;
}

void *chm_objc_sel_registerName(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_sel_count; i++) {
        if (strcmp(g_sels[i], name) == 0)
            return g_sels[i];
    }
    if (g_sel_count < CHM_MAX_SELS) {
        char *copy = strdup(name);

        if (!copy)
            return (void *)name;
        g_sels[g_sel_count] = copy;
        g_sel_count++;
        return copy;
    }
    return (void *)name;
}

static void *chm_method_imp(chm_method_list *ml, const char *sel)
{
    uint32_t header;
    uint32_t entsize;
    uint32_t i;
    uint8_t *base;

    if (!ml || !sel)
        return NULL;
    header = ml->entsize_and_flags;
    base = (uint8_t *)ml + sizeof(chm_method_list);

    if (header & CHM_METHOD_LIST_SMALL) {
        chm_small_method *m = (chm_small_method *)base;

        for (i = 0; i < ml->count; i++) {
            const char *name = (const char *)&m[i] + m[i].name_offset;

            if (strcmp(name, sel) == 0)
                return (void *)((uint8_t *)&m[i] + m[i].imp_offset);
        }
        return NULL;
    }

    entsize = header & ~0x3u;
    if (entsize < sizeof(chm_classic_method))
        entsize = sizeof(chm_classic_method);
    for (i = 0; i < ml->count; i++) {
        chm_classic_method *e = (chm_classic_method *)(base + (size_t)i * entsize);

        if (e->name && strcmp(e->name, sel) == 0)
            return e->imp;
    }
    return NULL;
}

void *chm_objc_lookup_imp(void *self, const char *sel)
{
    void *isa;
    chm_objc_class *c;
    int depth = 0;

    if (!self || !sel)
        return NULL;
    isa = *(void **)self;
    c = (chm_objc_class *)isa;
    while (c && depth < 64) {
        void *imp = chm_method_imp(chm_class_methods(c), sel);

        if (imp)
            return imp;
        c = (chm_objc_class *)c->superclass;
        depth++;
    }
    return NULL;
}

static void *chm_send6(void *imp, void *self, const char *sel, uint64_t a1, uint64_t a2,
                       uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    typedef void *(*fn6)(void *, const char *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

    if (!imp)
        return NULL;
    return ((fn6)imp)(self, sel, a1, a2, a3, a4, a5, a6);
}

static void *chm_objc_msgSend(void *self, const char *sel, uint64_t a1, uint64_t a2,
                              uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    return chm_send6(chm_objc_lookup_imp(self, sel), self, sel, a1, a2, a3, a4, a5, a6);
}

static void *chm_objc_msgSendSuper2(void *super_ptr, const char *sel, uint64_t a1, uint64_t a2,
                                    uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    void **s = (void **)super_ptr;
    void *recv;
    chm_objc_class *c;
    int depth = 0;

    if (!s)
        return NULL;
    recv = s[0];
    c = (chm_objc_class *)s[1];
    if (c)
        c = (chm_objc_class *)c->superclass;
    while (c && depth < 64) {
        void *imp = chm_method_imp(chm_class_methods(c), sel);

        if (imp)
            return chm_send6(imp, recv, sel, a1, a2, a3, a4, a5, a6);
        c = (chm_objc_class *)c->superclass;
        depth++;
    }
    return NULL;
}

static void *chm_objc_alloc(void *cls)
{
    size_t size = chm_class_size((chm_objc_class *)cls);
    void *obj;

    if (size < 16)
        size = 16;
    obj = calloc(1, size);
    if (obj)
        *(void **)obj = cls;
    return obj;
}

static void *chm_objc_allocWithZone(void *cls, void *zone)
{
    (void)zone;
    return chm_objc_alloc(cls);
}

static void *chm_objc_retain(void *obj)
{
    return obj;
}

static void chm_objc_release(void *obj)
{
    (void)obj;
}

static void *chm_objc_retainBlock(void *obj)
{
    return obj;
}

static void chm_objc_storeStrong(void **slot, void *obj)
{
    if (slot)
        *slot = obj;
}

static int chm_objc_sync_enter(void *obj)
{
    (void)obj;
    return 0;
}

static int chm_objc_sync_exit(void *obj)
{
    (void)obj;
    return 0;
}

void chm_objc_call_loads(chm_report *r)
{
    int i;
    int called = 0;

    for (i = 0; i < g_class_count; i++) {
        chm_objc_class *meta = g_classes[i].meta;
        void *imp;

        if (!meta)
            continue;
        imp = chm_method_imp(chm_class_methods(meta), "load");
        if (imp) {
            typedef void (*load_fn)(void *, const char *);

            ((load_fn)imp)(g_classes[i].cls, "load");
            called++;
        }
    }
    chm_report_add(r, "objc: +load %d", called);
}

void *chm_objc_block_isa(void)
{
    return g_block_isa;
}

void *chm_objc_resolve_import(const char *name)
{
    static const struct {
        const char *name;
        void *addr;
    } table[] = {
        { "_objc_msgSend", (void *)chm_objc_msgSend },
        { "_objc_msgSendSuper2", (void *)chm_objc_msgSendSuper2 },
        { "_objc_getClass", (void *)chm_objc_getClass },
        { "_objc_lookUpClass", (void *)chm_objc_getClass },
        { "_objc_getMetaClass", (void *)chm_objc_getMetaClass },
        { "_objc_sel_registerName", (void *)chm_objc_sel_registerName },
        { "_sel_registerName", (void *)chm_objc_sel_registerName },
        { "_objc_retain", (void *)chm_objc_retain },
        { "_objc_release", (void *)chm_objc_release },
        { "_objc_autorelease", (void *)chm_objc_retain },
        { "_objc_autoreleaseReturnValue", (void *)chm_objc_retain },
        { "_objc_retainAutoreleasedReturnValue", (void *)chm_objc_retain },
        { "_objc_retainAutorelease", (void *)chm_objc_retain },
        { "_objc_retainAutoreleaseReturnValue", (void *)chm_objc_retain },
        { "_objc_unsafeClaimAutoreleasedReturnValue", (void *)chm_objc_retain },
        { "_objc_claimAutoreleasedReturnValue", (void *)chm_objc_retain },
        { "_objc_storeStrong", (void *)chm_objc_storeStrong },
        { "_objc_retainBlock", (void *)chm_objc_retainBlock },
        { "_objc_alloc", (void *)chm_objc_alloc },
        { "_objc_allocWithZone", (void *)chm_objc_allocWithZone },
        { "_objc_opt_new", (void *)chm_objc_alloc },
        { "_objc_sync_enter", (void *)chm_objc_sync_enter },
        { "_objc_sync_exit", (void *)chm_objc_sync_exit },
        { NULL, NULL }
    };
    int i;

    for (i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, name) == 0)
            return table[i].addr;
    }
    if (strcmp(name, "__NSConcreteStackBlock") == 0 || strcmp(name, "__NSConcreteGlobalBlock") == 0 ||
        strcmp(name, "__NSConcreteMallocBlock") == 0)
        return g_block_isa;
    if (strncmp(name, "_OBJC_CLASS_$_", 14) == 0) {
        void *cls = chm_objc_getClass(name + 14);

        if (cls)
            return cls;
        return NULL;
    }
    if (strncmp(name, "_OBJC_METACLASS_$_", 18) == 0) {
        void *meta = chm_objc_getMetaClass(name + 18);

        if (meta)
            return meta;
        return NULL;
    }
    return NULL;
}
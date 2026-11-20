#include "chm.h"
#include "chm_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHM_STUB_MAX 512
#define CHM_FAST_DATA_MASK 0x00007ffffffffff8ull

typedef struct {
    void *isa;
    void *superclass;
    void *cache;
    void *vtable;
    uint64_t bits;
} chm_cls;

typedef struct {
    uint32_t flags;
    uint32_t instance_start;
    uint32_t instance_size;
    uint32_t reserved;
    const void *ivar_layout;
    const char *name;
    const void *methods;
    const void *protocols;
    const void *ivars;
    const void *weak_ivar_layout;
    const void *properties;
} chm_ro;

typedef struct {
    char *name;
    void *cls;
} chm_stub_entry;

static chm_stub_entry g_stubs[CHM_STUB_MAX];
static int g_stub_count;
static char g_meta_class[64];

extern void chm_trap_unimplemented(const char *what);

static const char *chm_name_of(void *cls)
{
    chm_cls *c = (chm_cls *)cls;
    chm_ro *ro;

    if (!c)
        return "?";
    ro = (chm_ro *)(uintptr_t)(c->bits & CHM_FAST_DATA_MASK);
    if (!ro || !ro->name)
        return "?";
    return ro->name;
}

void *chm_objc_empty_class(const char *name)
{
    chm_cls *c;
    chm_ro *ro;
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_stub_count; i++) {
        if (strcmp(g_stubs[i].name, name) == 0)
            return g_stubs[i].cls;
    }
    if (g_stub_count >= CHM_STUB_MAX)
        return NULL;
    c = (chm_cls *)calloc(1, sizeof(chm_cls));
    ro = (chm_ro *)calloc(1, sizeof(chm_ro));
    if (!c || !ro)
        return NULL;
    ro->name = strdup(name);
    ro->instance_size = 16;
    c->isa = g_meta_class;
    c->bits = (uint64_t)(uintptr_t)ro;
    g_stubs[g_stub_count].name = strdup(name);
    g_stubs[g_stub_count].cls = (void *)c;
    g_stub_count++;
    return c;
}

int chm_objc_stub_count(void)
{
    return g_stub_count;
}

void *chm_objc_missing_method(void *self, const char *sel)
{
    char buf[320];
    void *isa = self ? *(void **)self : NULL;

    snprintf(buf, sizeof(buf), "方法未实现 -[%s %s]", chm_name_of(isa), sel ? sel : "?");
    chm_trap_unimplemented(buf);
    return NULL;
}
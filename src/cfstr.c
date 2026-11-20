#include "chm.h"
#include "chm_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHM_CFSTR_MAGIC 0x4346535452ull
#define CHM_CFSTR_MAX 512

typedef struct {
    uint64_t magic;
    void *isa;
    int64_t length;
    uint32_t flags;
    uint32_t reserved;
    char *bytes;
} chm_cfstr;

typedef struct {
    char *name;
    chm_cfstr *obj;
} chm_const_entry;

static chm_const_entry g_consts[CHM_CFSTR_MAX];
static int g_const_count;
static char g_cfstr_class[64];
static char g_empty_cache[64];
static char g_os_activity_current[64];
static char g_nsarray0[128];
static char g_nsdictionary0[128];
static char g_ehtype_nsexception[64];

int chm_cfstr_owns(void *p)
{
    chm_cfstr *o = (chm_cfstr *)p;

    if (!o)
        return 0;
    return o->magic == CHM_CFSTR_MAGIC;
}

static chm_cfstr *chm_cfstr_new(const char *s)
{
    chm_cfstr *o = (chm_cfstr *)calloc(1, sizeof(chm_cfstr));

    if (!o)
        return NULL;
    o->magic = CHM_CFSTR_MAGIC;
    o->isa = g_cfstr_class;
    o->length = (int64_t)strlen(s);
    o->bytes = strdup(s);
    return o;
}

void *chm_cfstr_const(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_const_count; i++) {
        if (strcmp(g_consts[i].name, name) == 0)
            return g_consts[i].obj;
    }
    if (g_const_count >= CHM_CFSTR_MAX)
        return NULL;
    g_consts[g_const_count].name = strdup(name);
    g_consts[g_const_count].obj = chm_cfstr_new(name);
    g_const_count++;
    return g_consts[g_const_count - 1].obj;
}

int chm_cfstr_is_const_name(const char *name)
{
    if (!name)
        return 0;
    if (strncmp(name, "_OBJC_", 6) == 0)
        return 0;
    if (strstr(name, "Concrete") || strstr(name, "Block"))
        return 0;
    if (strncmp(name, "_k", 2) == 0)
        return 1;
    if (strncmp(name, "_NS", 3) == 0)
        return 1;
    if (strncmp(name, "_UI", 3) == 0)
        return 1;
    if (strncmp(name, "_CF", 3) == 0)
        return 1;
    if (strncmp(name, "_GC", 3) == 0)
        return 1;
    if (strncmp(name, "_AV", 3) == 0)
        return 1;
    if (strncmp(name, "_CG", 3) == 0)
        return 1;
    return 0;
}

static int64_t cf_length(void *self)
{
    return ((chm_cfstr *)self)->length;
}

static void *cf_utf8(void *self)
{
    return ((chm_cfstr *)self)->bytes;
}

static uint64_t cf_hash(void *self)
{
    const unsigned char *s = (const unsigned char *)((chm_cfstr *)self)->bytes;
    uint64_t h = 5381;

    while (s && *s)
        h = (h * 33) ^ *s++;
    return h;
}

static int cf_equal(void *self, void *other)
{
    chm_cfstr *a = (chm_cfstr *)self;
    chm_cfstr *b = (chm_cfstr *)other;

    if (!chm_cfstr_owns(other))
        return 0;
    if (a->length != b->length)
        return 0;
    return strcmp(a->bytes, b->bytes) == 0;
}

static void *cf_retain(void *self)
{
    return self;
}

static void cf_release(void *self)
{
    (void)self;
}

static int cf_true(void *self)
{
    (void)self;
    return 1;
}

static int cf_zero(void *self)
{
    (void)self;
    return 0;
}

static void *cf_class(void *self)
{
    (void)self;
    return g_cfstr_class;
}

void *chm_cfstr_builtin(void *self, const char *sel)
{
    if (!sel || !chm_cfstr_owns(self))
        return NULL;
    if (strcmp(sel, "length") == 0)
        return (void *)cf_length;
    if (strcmp(sel, "UTF8String") == 0)
        return (void *)cf_utf8;
    if (strcmp(sel, "hash") == 0)
        return (void *)cf_hash;
    if (strcmp(sel, "isEqualToString:") == 0)
        return (void *)cf_equal;
    if (strcmp(sel, "isEqual:") == 0)
        return (void *)cf_equal;
    if (strcmp(sel, "retain") == 0 || strcmp(sel, "copy") == 0 || strcmp(sel, "autorelease") == 0)
        return (void *)cf_retain;
    if (strcmp(sel, "release") == 0)
        return (void *)cf_release;
    if (strcmp(sel, "description") == 0)
        return (void *)cf_utf8;
    if (strcmp(sel, "boolValue") == 0)
        return (void *)cf_true;
    if (strcmp(sel, "integerValue") == 0 || strcmp(sel, "intValue") == 0)
        return (void *)cf_zero;
    if (strcmp(sel, "class") == 0)
        return (void *)cf_class;
    return NULL;
}

static int64_t CFStringGetLength(void *s)
{
    return chm_cfstr_owns(s) ? ((chm_cfstr *)s)->length : 0;
}

static uint16_t CFStringGetCharacterAtIndex(void *s, int64_t idx)
{
    chm_cfstr *o = (chm_cfstr *)s;

    if (!chm_cfstr_owns(s) || idx < 0 || idx >= o->length)
        return 0;
    return (uint16_t)(unsigned char)o->bytes[idx];
}

static const char *CFStringGetCStringPtr(void *s, unsigned int encoding)
{
    (void)encoding;
    return chm_cfstr_owns(s) ? ((chm_cfstr *)s)->bytes : NULL;
}

static int CFStringGetCString(void *s, char *buf, long size, unsigned int encoding)
{
    chm_cfstr *o = (chm_cfstr *)s;
    size_t n;

    (void)encoding;
    if (!chm_cfstr_owns(s) || !buf || size <= 0)
        return 0;
    n = strlen(o->bytes);
    if ((long)n >= size)
        n = (size_t)size - 1;
    memcpy(buf, o->bytes, n);
    buf[n] = 0;
    return 1;
}

static int CFEqual(void *a, void *b)
{
    return cf_equal(a, b);
}

static unsigned long CFHash(void *s)
{
    return (unsigned long)cf_hash(s);
}

static void *CFRetain(void *s)
{
    return s;
}

static void CFRelease(void *s)
{
    (void)s;
}

static int chm_objc_personality(void)
{
    return 5;
}

static void chm_stub_binder(void)
{
}

void *chm_cfstr_resolve(const char *name)
{
    static const struct {
        const char *name;
        void *addr;
    } table[] = {
        { "_CFStringGetLength", (void *)CFStringGetLength },
        { "_CFStringGetCharacterAtIndex", (void *)CFStringGetCharacterAtIndex },
        { "_CFStringGetCStringPtr", (void *)CFStringGetCStringPtr },
        { "_CFStringGetCString", (void *)CFStringGetCString },
        { "_CFEqual", (void *)CFEqual },
        { "_CFHash", (void *)CFHash },
        { "_CFRetain", (void *)CFRetain },
        { "_CFRelease", (void *)CFRelease },
        { "___objc_personality_v0", (void *)chm_objc_personality },
        { "dyld_stub_binder", (void *)chm_stub_binder },
        { NULL, NULL }
    };
    int i;

    for (i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, name) == 0)
            return table[i].addr;
    }
    if (strcmp(name, "__objc_empty_cache") == 0)
        return g_empty_cache;
    if (strcmp(name, "__os_activity_current") == 0)
        return g_os_activity_current;
    if (strcmp(name, "___NSArray0__") == 0)
        return g_nsarray0;
    if (strcmp(name, "___NSDictionary0__") == 0)
        return g_nsdictionary0;
    if (strcmp(name, "_OBJC_EHTYPE_$_NSException") == 0)
        return g_ehtype_nsexception;
    if (strcmp(name, "___CFConstantStringClassReference") == 0)
        return g_cfstr_class;
    return NULL;
}
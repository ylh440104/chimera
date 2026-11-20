#include "chm.h"
#include "chm_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_DEMAND 8192

typedef struct {
    char name[CHM_MAX_NAME];
    uint32_t hits;
} demand_entry;

static demand_entry g_demand[MAX_DEMAND];
static int g_count;
static int g_resolved;
static int g_unresolved;

static int classify_resolve(const chm_image *img, int32_t ordinal, const char *name, bool weak, uint64_t *out, void *ctx)
{
    (void)img;
    (void)ordinal;
    (void)ctx;
    if (weak)
        return 0;
    void *fn = chm_runtime_lookup(name);
    if (fn) {
        *out = (uint64_t)(uintptr_t)fn;
        g_resolved++;
        return 1;
    }
    for (int i = 0; i < g_count; ++i) {
        if (!strcmp(g_demand[i].name, name)) {
            g_demand[i].hits++;
            g_unresolved++;
            return 0;
        }
    }
    if (g_count < MAX_DEMAND) {
        snprintf(g_demand[g_count].name, CHM_MAX_NAME, "%s", name);
        g_demand[g_count].hits = 1;
        g_count++;
    }
    g_unresolved++;
    return 0;
}

static const char *family(const char *name)
{
    if (!strncmp(name, "_OBJC_CLASS_$_", 14))
        return "objc-class";
    if (!strncmp(name, "_OBJC_METACLASS_$_", 18))
        return "objc-metaclass";
    if (!strncmp(name, "_OBJC_EHTYPE_$_", 15))
        return "objc-ehtype";
    if (!strncmp(name, "_objc_", 6))
        return "objc-runtime";
    if (!strncmp(name, "_CF", 3) || !strncmp(name, "_kCF", 4))
        return "corefoundation";
    if (!strncmp(name, "_CG", 3))
        return "coregraphics";
    if (!strncmp(name, "_NS", 3))
        return "foundation";
    if (!strncmp(name, "_UI", 3))
        return "uikit";
    if (!strncmp(name, "_CA", 3))
        return "quartzcore";
    if (!strncmp(name, "_AV", 3))
        return "avfoundation";
    if (!strncmp(name, "_GC", 3))
        return "gamecontroller";
    if (!strncmp(name, "_Sec", 4) || !strncmp(name, "_kSec", 5))
        return "security";
    if (!strncmp(name, "_CC_", 4))
        return "commoncrypto";
    if (!strncmp(name, "_dispatch", 9))
        return "libdispatch";
    if (!strncmp(name, "_IO", 3) || !strncmp(name, "_kIO", 4))
        return "iokit";
    if (!strncmp(name, "_Block_", 7))
        return "blocks";
    if (!strncmp(name, "_dyld", 5))
        return "dyld";
    if (strstr(name, "Block"))
        return "blocks";
    return "libc-or-other";
}

typedef struct {
    const char *name;
    int count;
    int hits;
} fam_stat;

static int cmp_fam(const void *a, const void *b)
{
    const fam_stat *x = a;
    const fam_stat *y = b;
    return y->count - x->count;
}

static void write_csv(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "symbol,hits,family,status\n");
    for (int i = 0; i < g_count; ++i)
        fprintf(f, "\"%s\",%u,%s,unresolved\n", g_demand[i].name, g_demand[i].hits, family(g_demand[i].name));
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    chm_runtime_init();
    chm_image img;
    if (chm_open(&img, argv[1])) {
        fprintf(stderr, "open: %s\n", img.error);
        return 1;
    }
    img.resolver = classify_resolve;
    if (chm_fixup(&img)) {
        fprintf(stderr, "fixup: %s\n", img.error);
        chm_close(&img);
        return 1;
    }

    printf("image: %s\n", argv[1]);
    printf("runtime-provided: %d\n", chm_runtime_count());
    printf("bind=%u resolved=%d unresolved=%d weak=%u rebase=%u executable=%d\n\n",
           img.nfixup_bind, g_resolved, g_unresolved, img.nfixup_weak, img.nfixup_rebase, img.executable);

    fam_stat fams[32];
    int nf = 0;
    for (int i = 0; i < g_count; ++i) {
        const char *f = family(g_demand[i].name);
        int j;
        for (j = 0; j < nf; ++j) {
            if (!strcmp(fams[j].name, f)) {
                fams[j].count++;
                fams[j].hits += (int)g_demand[i].hits;
                break;
            }
        }
        if (j == nf && nf < 32) {
            fams[nf].name = f;
            fams[nf].count = 1;
            fams[nf].hits = (int)g_demand[i].hits;
            nf++;
        }
    }
    qsort(fams, (size_t)nf, sizeof(fam_stat), cmp_fam);
    printf("%-20s %8s %8s\n", "family", "symbols", "slots");
    for (int i = 0; i < nf; ++i)
        printf("%-20s %8d %8d\n", fams[i].name, fams[i].count, fams[i].hits);

    if (argc > 2)
        write_csv(argv[2]);
    chm_close(&img);
    return 0;
}
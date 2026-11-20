#include "chm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEMAND 4096

typedef struct {
    char name[CHM_MAX_NAME];
    uint32_t hits;
} demand;

static demand g_demand[MAX_DEMAND];
static int g_count;
static int g_unresolved;

static int demand_resolve(const chm_image *img, int32_t ordinal, const char *name, bool weak, uint64_t *out, void *ctx)
{
    (void)img;
    (void)ordinal;
    (void)ctx;
    if (weak) {
        return 0;
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
    *out = 0;
    return 0;
}

static int cmp_demand(const void *a, const void *b)
{
    const demand *x = a;
    const demand *y = b;
    return strcmp(x->name, y->name);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    chm_image img;
    if (chm_open(&img, argv[1])) {
        fprintf(stderr, "open: %s\n", img.error);
        return 1;
    }
    img.resolver = demand_resolve;
    if (chm_fixup(&img)) {
        fprintf(stderr, "fixup: %s\n", img.error);
        chm_close(&img);
        return 1;
    }
    qsort(g_demand, (size_t)g_count, sizeof(demand), cmp_demand);
    printf("image %s\n", argv[1]);
    printf("bind=%u missing=%u weak=%u rebase=%u executable=%d\n",
           img.nfixup_bind, img.nfixup_missing, img.nfixup_weak, img.nfixup_rebase, img.executable);
    printf("distinct-unresolved=%d total-unresolved=%d\n\n", g_count, g_unresolved);
    for (int i = 0; i < g_count; ++i)
        printf("%6u  %s\n", g_demand[i].hits, g_demand[i].name);
    chm_close(&img);
    return 0;
}
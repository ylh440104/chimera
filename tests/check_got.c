#include "chm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint64_t value;
} got_check;

static uint64_t g_stack_guard = UINT64_C(0x1122334455667788);
static uint32_t g_task_self = 0x203u;

static int probe_resolve(const chm_image *img, int32_t ordinal, const char *name, bool weak, uint64_t *out, void *ctx)
{
    (void)img;
    (void)ordinal;
    (void)weak;
    (void)ctx;
    if (!strcmp(name, "_mach_task_self_")) {
        *out = (uint64_t)(uintptr_t)&g_task_self;
        return 1;
    }
    if (!strcmp(name, "___stack_chk_guard")) {
        *out = (uint64_t)(uintptr_t)&g_stack_guard;
        return 1;
    }
    return 0;
}

static void probe_observe(const chm_image *img, uint32_t seg, uint64_t off, int32_t ordinal,
                          const char *name, uint64_t value, bool found, bool weak, void *ctx)
{
    (void)ordinal;
    (void)weak;
    int *shown = ctx;
    if (*shown < 12) {
        uint64_t vm = img->segs[seg].vmaddr + img->slide + off;
        printf("slot 0x%llx (%s+0x%llx) <- %s = 0x%llx (%s)\n",
               (unsigned long long)vm, img->segs[seg].name, (unsigned long long)off,
               name, (unsigned long long)value, found ? "bound" : "missing");
        (*shown)++;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    chm_image img;
    if (chm_open(&img, argv[1])) {
        fprintf(stderr, "open error: %s\n", img.error);
        return 1;
    }
    int shown = 0;
    img.resolver = probe_resolve;
    img.observer = probe_observe;
    img.observer_context = &shown;
    if (chm_fixup(&img)) {
        fprintf(stderr, "fixup error: %s\n", img.error);
        chm_close(&img);
        return 1;
    }
    uint64_t got0 = 0;
    uint64_t got1 = 0;
    memcpy(&got0, (const uint8_t *)img.segs[2].mapped + 0, 8);
    memcpy(&got1, (const uint8_t *)img.segs[2].mapped + 8, 8);
    printf("readback GOT[0]=0x%llx (expect 0x%llx)\n",
           (unsigned long long)got0, (unsigned long long)(uintptr_t)&g_task_self);
    printf("readback GOT[1]=0x%llx (expect 0x%llx)\n",
           (unsigned long long)got1, (unsigned long long)(uintptr_t)&g_stack_guard);
    printf("summary bind=%u missing=%u weak-null=%u rebase=%u executable=%d\n",
           img.nfixup_bind, img.nfixup_missing, img.nfixup_weak, img.nfixup_rebase, img.executable);
    chm_close(&img);
    return (got0 == (uint64_t)(uintptr_t)&g_task_self &&
            got1 == (uint64_t)(uintptr_t)&g_stack_guard &&
            !img.executable) ? 0 : 1;
}
#include "chm.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static void on_signal(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = (ucontext_t *)ctx;
    unsigned long long pc = 0;
    unsigned long long lr = 0;
    unsigned long long sp = 0;

    if (uc) {
        pc = (unsigned long long)uc->uc_mcontext.pc;
        lr = (unsigned long long)uc->uc_mcontext.regs[30];
        sp = (unsigned long long)uc->uc_mcontext.sp;
    }
    printf("\nSIGNAL %d addr %p pc 0x%llx lr 0x%llx sp 0x%llx\n",
           sig, info ? info->si_addr : NULL, pc, lr, sp);
    fflush(stdout);
    _exit(4);
}

int main(int argc, char **argv)
{
    chm_image img;
    struct sigaction sa;
    uint32_t i;

    if (argc < 2)
        return 2;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);

    if (chm_open(&img, argv[1]) != 0)
        return 1;
    if (chm_fixup(&img) != 0) {
        printf("fixup failed\n");
        return 1;
    }
    printf("slide     0x%llx\n", (unsigned long long)img.slide);
    printf("binds     %u\n", img.nfixup_bind);
    printf("missing   %u\n", img.nfixup_missing);
    for (i = 0; i < img.nsegs; i++) {
        if (!img.segs[i].mapped)
            continue;
        printf("seg %-10s at %p size 0x%llx prot %u\n", img.segs[i].name,
               img.segs[i].mapped, (unsigned long long)img.segs[i].vmsize,
               img.segs[i].initprot);
        if (strcmp(img.segs[i].name, "__TEXT") == 0)
            __builtin___clear_cache((char *)img.segs[i].mapped,
                                    (char *)img.segs[i].mapped + img.segs[i].vmsize);
    }
    printf("jumping to %p\n", (void *)(uintptr_t)(img.vm_min + img.entryoff));
    fflush(stdout);

    chm_run(&img, argc - 1, argv + 1);
    return 0;
}
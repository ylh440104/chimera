#define _GNU_SOURCE
#include "chm_session.h"

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <dlfcn.h>

#define CHM_ALTSTACK 65536

static sigjmp_buf g_jump;
static volatile sig_atomic_t g_armed;
static char g_altstack[CHM_ALTSTACK];
static char g_fault[512];
static uint64_t g_pc;
static uint64_t g_lr;
static uint64_t g_sp;
static uint64_t g_far;
static int g_signal;
static int g_installed;

static uint64_t g_regs[4];

static uint64_t g_self_base;

static void handler(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = ctx;

    g_signal = sig;
    g_far = info ? (uint64_t)(uintptr_t)info->si_addr : 0;
    if (uc) {
        g_pc = (uint64_t)uc->uc_mcontext.pc;
        g_lr = (uint64_t)uc->uc_mcontext.regs[30];
        g_sp = (uint64_t)uc->uc_mcontext.sp;
        g_regs[0] = (uint64_t)uc->uc_mcontext.regs[0];
        g_regs[1] = (uint64_t)uc->uc_mcontext.regs[1];
        g_regs[2] = (uint64_t)uc->uc_mcontext.regs[2];
        g_regs[3] = (uint64_t)uc->uc_mcontext.regs[3];
    }
    if (!g_armed)
        _exit(9);
    g_armed = 0;
    siglongjmp(g_jump, 1);
}

void chm_trap_install(void)
{
    struct sigaction sa;
    stack_t ss;

    if (g_installed)
        return;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof(g_altstack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    g_self_base = (uint64_t)(uintptr_t)&chm_trap_install;
    g_installed = 1;
}

typedef struct {
    chm_image *img;
    int argc;
    char **argv;
    int rc;
} run_args;

static void run_body(void *p)
{
    run_args *a = p;

    a->rc = chm_run(a->img, a->argc, a->argv);
}

static void describe_pc(chm_session *s, uint64_t pc)
{
    int i;
    uint32_t k;

    if (!s)
        return;
    for (i = 0; i < s->image_count; i++) {
        chm_image *img = &s->images[i];

        if (pc < img->vm_min + img->slide || pc >= img->vm_max + img->slide)
            continue;
        chm_session_log(s, "fault inside image[%d] %s offset 0x%llx",
                        i, s->image_paths[i], (unsigned long long)(pc - img->vm_min - img->slide));
        for (k = 0; k < img->nsegs; k++) {
            chm_segment *seg = &img->segs[k];

            if (!seg->vmsize || !strcmp(seg->name, "__PAGEZERO"))
                continue;
            uint64_t start = seg->vmaddr + img->slide;

            if (pc >= start && pc < start + seg->vmsize) {
                chm_session_log(s, "fault in segment %s +0x%llx", seg->name,
                                (unsigned long long)(pc - start));
                for (uint32_t j = 0; j < seg->nsects; j++) {
                    chm_section *sec = &seg->sects[j];
                    uint64_t sstart = sec->addr + img->slide;

                    if (pc >= sstart && pc < sstart + sec->size)
                        chm_session_log(s, "fault in section %s +0x%llx", sec->sect,
                                        (unsigned long long)(pc - sstart));
                }
                return;
            }
        }
        return;
    }
    chm_session_log(s, "fault pc outside loaded images");
    for (i = 0; i < s->image_count; i++)
        chm_session_log(s, "image[%d] %s range 0x%llx-0x%llx", i, s->image_paths[i],
                        (unsigned long long)(s->images[i].vm_min + s->images[i].slide),
                        (unsigned long long)(s->images[i].vm_max + s->images[i].slide));
}

int chm_session_execute(chm_session *s, chm_image *img, int argc, char **argv)
{
    run_args a;

    chm_trap_install();
    a.img = img;
    a.argc = argc;
    a.argv = argv;
    a.rc = -1;
    g_pc = g_lr = g_sp = g_far = 0;
    g_signal = 0;
    g_armed = 1;
    if (sigsetjmp(g_jump, 1) == 0) {
        run_args *arg = &a;

        run_body(arg);
        g_armed = 0;
        return a.rc;
    }
    g_armed = 0;
    {
        void *sym = NULL;
        Dl_info di;

        if (dladdr((void *)(uintptr_t)g_pc, &di) && di.dli_sname)
            sym = (void *)di.dli_sname;
        snprintf(g_fault, sizeof(g_fault),
                 "signal %d fault 0x%llx pc 0x%llx lr 0x%llx sp 0x%llx x0 0x%llx x1 0x%llx selfoff 0x%llx lroff 0x%llx in %s",
                 g_signal, (unsigned long long)g_far, (unsigned long long)g_pc,
                 (unsigned long long)g_lr, (unsigned long long)g_sp,
                 (unsigned long long)g_regs[0], (unsigned long long)g_regs[1],
                 (unsigned long long)(g_pc - g_self_base),
                 (unsigned long long)(g_lr - g_self_base),
                 sym ? (const char *)sym : "unknown");
    }
    chm_session_log(s, "%s", g_fault);
    describe_pc(s, g_pc);
    chm_objc_dump_trace(s);
    return -2;
}

uint64_t chm_trap_pc(void)
{
    return g_pc;
}

uint64_t chm_trap_lr(void)
{
    return g_lr;
}

int chm_trap_signal(void)
{
    return g_signal;
}

uint64_t chm_trap_fault_addr(void)
{
    return g_far;
}
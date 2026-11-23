#define _GNU_SOURCE
#include "chm_session.h"

#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "guest_gate.inc"

#define CHM_ALTSTACK 65536

static _Thread_local sigjmp_buf g_jump;
static _Thread_local sigjmp_buf g_probe_jump;
static _Thread_local volatile sig_atomic_t g_probing;
static _Thread_local volatile sig_atomic_t g_armed;
static _Thread_local char g_altstack[CHM_ALTSTACK];
static _Thread_local char g_fault[512];
static _Thread_local uint64_t g_pc;
static _Thread_local uint64_t g_fp;
static _Thread_local uint64_t g_lr;
static _Thread_local uint64_t g_sp;
static _Thread_local uint64_t g_far;
static _Thread_local int g_signal;
static _Thread_local int g_installed;

static void handler(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = ctx;

    if (g_probing)
        siglongjmp(g_probe_jump, 1);
    if (!g_armed) {
        dprintf(2, "[chimera] fault outside guard sig=%d pc=0x%llx\n", sig,
                uc ? (unsigned long long)uc->uc_mcontext.pc : 0ULL);
        _exit(9);
    }
    g_signal = sig;
    g_far = info ? (uint64_t)(uintptr_t)info->si_addr : 0;
    if (uc) {
        int i;

        g_pc = (uint64_t)uc->uc_mcontext.pc;
        g_lr = (uint64_t)uc->uc_mcontext.regs[30];
        g_sp = (uint64_t)uc->uc_mcontext.sp;
        g_fp = (uint64_t)uc->uc_mcontext.regs[29];
        for (i = 0; i < 31; i++)
            (void)i;
    }
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
    sigaction(SIGTRAP, &sa, NULL);
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof(g_altstack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    g_installed = 1;
}

typedef struct {
    chm_image *img;
    int argc;
    char **argv;
} run_args;

static int run_body(void *p)
{
    run_args *a = p;
    return chm_run(a->img, a->argc, a->argv);
}

static int probe_read(uint64_t addr, uint64_t *out)
{
    volatile int ok = 0;

    if (!addr || (addr & 7))
        return 0;
    g_probing = 1;
    if (sigsetjmp(g_probe_jump, 1) == 0) {
        *out = *(volatile uint64_t *)(uintptr_t)addr;
        ok = 1;
    }
    g_probing = 0;
    return (int)ok;
}

static void describe_value(chm_session *s, int slot, uint64_t v)
{
    int k;

    for (k = 0; k < s->image_count; k++) {
        chm_image *img = &s->images[k];
        uint32_t q;

        for (q = 0; q < img->nsegs; q++) {
            chm_segment *sg = &img->segs[q];
            uint64_t start = sg->vmaddr + img->slide;
            int j;

            if (!sg->vmsize || v < start || v >= start + sg->vmsize)
                continue;
            for (j = 0; j < (int)sg->nsects; j++) {
                uint64_t ss = sg->sects[j].addr + img->slide;

                if (v >= ss && v < ss + sg->sects[j].size) {
                    chm_session_log(s, "  slot %d 0x%llx img%d %s+0x%llx", slot,
                                    (unsigned long long)v, k, sg->sects[j].sect,
                                    (unsigned long long)(v - ss));
                    return;
                }
            }
            chm_session_log(s, "  slot %d 0x%llx img%d %s+0x%llx", slot,
                            (unsigned long long)v, k, sg->name,
                            (unsigned long long)(v - start));
            return;
        }
    }
}

static void dump_stack(chm_session *s, uint64_t sp)
{
    int i;

    if (!s || !sp)
        return;
    chm_session_log(s, "stack base 0x%llx", (unsigned long long)sp);
    for (i = 0; i < 64; i++) {
        uint64_t v = 0;

        if (!probe_read(sp + (uint64_t)i * 8, &v))
            break;
        if (!v)
            continue;
        describe_value(s, i, v);
    }
    {
        uint64_t fp = g_fp;
        int n;

        for (n = 0; n < 16 && fp; n++) {
            uint64_t next = 0;
            uint64_t ret = 0;

            if (!probe_read(fp, &next) || !probe_read(fp + 8, &ret))
                break;
            chm_session_log(s, "  frame %d ret 0x%llx", n, (unsigned long long)ret);
            if (!ret || next <= fp)
                break;
            fp = next;
        }
    }
}

static void describe_pc(chm_session *s, uint64_t pc)
{
    int i;

    if (!s)
        return;
    for (i = 0; i < s->image_count; i++) {
        chm_image *img = &s->images[i];

        if (pc < img->vm_min + img->slide || pc >= img->vm_max + img->slide)
            continue;
        chm_session_log(s, "fault inside image[%d] %s offset 0x%llx", i, s->image_paths[i],
                        (unsigned long long)(pc - img->vm_min - img->slide));
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
    run_args a = {img, argc, argv};
    return chm_session_callback(s, run_body, &a);
}

int chm_session_callback(chm_session *s, int (*body)(void *), void *context)
{
    if (!s || !body) return -1;
    chm_trap_install();
    if (g_armed) return body(context);
    unsigned depth = guest_depth;
    chm_guest_lock();
    g_pc = g_lr = g_sp = g_far = g_fp = 0;
    g_signal = 0;
    if (sigsetjmp(g_jump, 1) == 0) {
        g_armed = 1;
        int rc = body(context);
        g_armed = 0;
        guest_restore_depth(depth);
        return rc;
    }
    g_armed = 0;
    {
        Dl_info dp;
        Dl_info dl;
        const char *pnm = "?";
        const char *lnm = "?";
        uint64_t poff = 0;
        uint64_t loff = 0;

        memset(&dp, 0, sizeof(dp));
        memset(&dl, 0, sizeof(dl));
        if (dladdr((void *)(uintptr_t)g_pc, &dp)) {
            if (dp.dli_sname)
                pnm = dp.dli_sname;
            poff = g_pc - (uint64_t)(uintptr_t)dp.dli_fbase;
        }
        if (dladdr((void *)(uintptr_t)g_lr, &dl)) {
            if (dl.dli_sname)
                lnm = dl.dli_sname;
            loff = g_lr - (uint64_t)(uintptr_t)dl.dli_fbase;
        }
        snprintf(g_fault, sizeof(g_fault),
                 "signal %d fault 0x%llx at %s+0x%llx(%s) from %s+0x%llx(%s)",
                 g_signal, (unsigned long long)g_far,
                 dp.dli_fname ? dp.dli_fname : "?", (unsigned long long)poff, pnm,
                 dl.dli_fname ? dl.dli_fname : "?", (unsigned long long)loff, lnm);
    }
    snprintf(s->fault, sizeof(s->fault), "%s", g_fault);
    chm_session_log(s, "%s", g_fault);
    describe_pc(s, g_pc);
    dump_stack(s, g_sp);
    chm_objc_dump_trace(s);
    guest_restore_depth(depth);
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
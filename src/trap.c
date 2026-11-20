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

static void handler(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = ctx;

    g_signal = sig;
    g_far = info ? (uint64_t)(uintptr_t)info->si_addr : 0;
    if (uc) {
        g_pc = (uint64_t)uc->uc_mcontext.pc;
        g_lr = (uint64_t)uc->uc_mcontext.regs[30];
        g_sp = (uint64_t)uc->uc_mcontext.sp;
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
                 "signal %d fault_addr 0x%llx pc 0x%llx lr 0x%llx sp 0x%llx in %s",
                 g_signal, (unsigned long long)g_far, (unsigned long long)g_pc,
                 (unsigned long long)g_lr, (unsigned long long)g_sp,
                 sym ? (const char *)sym : "unknown");
    }
    chm_session_log(s, "%s", g_fault);
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
extern void *chm_cfstr_resolve(const char *name);
extern int chm_cfstr_is_const_name(const char *name);
extern void *chm_cfstr_const(const char *name);
#include "chm.h"
#include "chm_int.h"
#include "chm_rt.h"

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHM_MISSING_MAX 64
#define CHM_MISSING_NAME 128
#define CHM_ALTSTACK_SIZE 32768
#define CHM_TLV_MAX 128
#define CHM_TLV_SIZE 64
#define CHM_DARWIN_MAP_ANON 0x1000
#define CHM_LC_ENCRYPTION_INFO 0x21u
#define CHM_LC_ENCRYPTION_INFO_64 0x2cu

extern char **environ;

static char g_missing[CHM_MISSING_MAX][CHM_MISSING_NAME];
static int g_missing_count;
static char g_last_missing[CHM_MISSING_NAME];
static char g_fault[256];
static sigjmp_buf g_jump;
static volatile sig_atomic_t g_armed;
static char g_altstack[CHM_ALTSTACK_SIZE];

static uintptr_t g_stack_chk_guard = 0x9e3779b97f4a7c15ull;
static unsigned int g_task_self = 0x203u;
static FILE *g_stdoutp;
static FILE *g_stdderp;
static char **g_environ;

typedef struct {
    void *key;
    void *mem;
} chm_tlv_entry;

static chm_tlv_entry g_tlv[CHM_TLV_MAX];
static int g_tlv_count;

static ssize_t sh_write(int fd, const void *buf, size_t n)
{
    return write(fd, buf, n);
}

static ssize_t sh_read(int fd, void *buf, size_t n)
{
    return read(fd, buf, n);
}

static int *sh_error(void)
{
    return &errno;
}

static void sh_bzero(void *p, size_t n)
{
    memset(p, 0, n);
}

static uint64_t sh_absolute_time(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void *sh_mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
    int linux_flags = flags & ~CHM_DARWIN_MAP_ANON;

    if (flags & CHM_DARWIN_MAP_ANON)
        linux_flags |= MAP_ANONYMOUS;
    return mmap(addr, len, prot, linux_flags, fd, (off_t)off);
}

static int sh_mprotect(void *addr, size_t len, int prot)
{
    return mprotect(addr, len, prot);
}

static int sh_munmap(void *addr, size_t len)
{
    return munmap(addr, len);
}

static int sh_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    long arg;

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return fcntl(fd, cmd, arg);
}

static void *sh_tlv_bootstrap(void *desc)
{
    void *key;
    int i;

    key = ((void **)desc)[1];
    for (i = 0; i < g_tlv_count; i++) {
        if (g_tlv[i].key == key)
            return g_tlv[i].mem;
    }
    if (g_tlv_count < CHM_TLV_MAX) {
        void *mem = calloc(1, CHM_TLV_SIZE);

        if (!mem)
            return NULL;
        g_tlv[g_tlv_count].key = key;
        g_tlv[g_tlv_count].mem = mem;
        g_tlv_count++;
        return mem;
    }
    return NULL;
}

static void sh_trap_missing(void);

typedef struct {
    const char *name;
    void *addr;
} chm_shim_entry;

static const chm_shim_entry chm_shims[] = {
    { "_write", (void *)sh_write },
    { "_read", (void *)sh_read },
    { "___error", (void *)sh_error },
    { "___errno", (void *)sh_error },
    { "_mach_absolute_time", (void *)sh_absolute_time },
    { "_malloc", (void *)malloc },
    { "_free", (void *)free },
    { "_calloc", (void *)calloc },
    { "_realloc", (void *)realloc },
    { "_memcpy", (void *)memcpy },
    { "_memmove", (void *)memmove },
    { "_memset", (void *)memset },
    { "_bzero", (void *)sh_bzero },
    { "_strlen", (void *)strlen },
    { "_strcmp", (void *)strcmp },
    { "_strcpy", (void *)strcpy },
    { "_abort", (void *)abort },
    { "_exit", (void *)exit },
    { "_getenv", (void *)getenv },
    { "_mmap", (void *)sh_mmap },
    { "_mprotect", (void *)sh_mprotect },
    { "_munmap", (void *)sh_munmap },
    { "_fcntl", (void *)sh_fcntl },
    { "__tlv_bootstrap", (void *)sh_tlv_bootstrap },
    { NULL, NULL }
};

static void note_missing(const char *name)
{
    int i;

    if (!name || !name[0])
        return;
    for (i = 0; i < g_missing_count; i++) {
        if (strcmp(g_missing[i], name) == 0)
            return;
    }
    if (g_missing_count < CHM_MISSING_MAX) {
        snprintf(g_missing[g_missing_count], CHM_MISSING_NAME, "%s", name);
        g_missing_count++;
    }
}

int chm_missing_count(void)
{
    return g_missing_count;
}

const char *chm_missing_at(int index)
{
    if (index < 0 || index >= g_missing_count)
        return NULL;
    return g_missing[index];
}

static void data_init(void)
{
    if (!g_stdoutp) {
        g_stdoutp = stdout;
        g_stdderp = stderr;
        g_environ = environ;
    }
}

static void *locate_import(const char *name)
{
    const chm_shim_entry *e;
    void *resolved;
    int i;
    static const struct {
        const char *name;
        void *addr;
    } data[] = {
        { "_mach_task_self_", (void *)&g_task_self },
        { "___stack_chk_guard", (void *)&g_stack_chk_guard },
        { "___stdoutp", (void *)&g_stdoutp },
        { "___stderrp", (void *)&g_stdderp },
        { "_environ", (void *)&g_environ },
        { NULL, NULL }
    };

    for (e = chm_shims; e->name; e++) {
        const char *a = e->name;
        const char *b = name;

        if (a[0] == '_' && b[0] != '_')
            a++;
        if (strcmp(a, b) == 0)
            return e->addr;
        if (strcmp(e->name, name) == 0)
            return e->addr;
    }

    resolved = chm_cfstr_resolve(name);
    if (resolved)
        return resolved;
    resolved = chm_dylib_lookup(name);
    if (resolved)
        return resolved;
    resolved = chm_objc_resolve_import(name);
    if (resolved)
        return resolved;
    resolved = chm_dispatch_resolve_import(name);
    if (resolved)
        return resolved;

    data_init();
    for (i = 0; data[i].name; i++) {
        if (strcmp(data[i].name, name) == 0)
            return data[i].addr;
    }
    if (chm_cfstr_is_const_name(name))
        return chm_cfstr_const(name);
    return NULL;
}

void *chm_resolve_import(const char *name)
{
    void *resolved = locate_import(name);

    if (resolved)
        return resolved;
    snprintf(g_last_missing, sizeof(g_last_missing), "%s", name ? name : "?");
    note_missing(g_last_missing);
    return (void *)sh_trap_missing;
}

static void sh_trap_missing(void)
{
    if (!g_armed)
        _exit(4);
    snprintf(g_fault, sizeof(g_fault), "unimplemented symbol %s", g_last_missing);
    g_armed = 0;
    siglongjmp(g_jump, 1);
}

static void chm_fault_handler(int sig, siginfo_t *info, void *ctx)
{
    (void)ctx;
    if (!g_armed)
        _exit(3);
    snprintf(g_fault, sizeof(g_fault), "signal %d at %p after %s",
             sig, info ? info->si_addr : NULL,
             g_last_missing[0] ? g_last_missing : "no import");
    g_armed = 0;
    siglongjmp(g_jump, 1);
}

void chm_install_fault_handler(void)
{
    struct sigaction sa;
    stack_t ss;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = chm_fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);

    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof(g_altstack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
}

int chm_run_guarded(chm_image *img, int argc, char **argv, char *err, size_t cap)
{
    int rc;

    g_armed = 1;
    if (sigsetjmp(g_jump, 1) == 0) {
        rc = chm_run(img, argc, argv);
        g_armed = 0;
        return rc;
    }
    if (err && cap)
        snprintf(err, cap, "%s", g_fault);
    return -2;
}

int chm_call_guarded(void (*fn)(void *), void *arg, char *err, size_t cap)
{
    g_armed = 1;
    if (sigsetjmp(g_jump, 1) == 0) {
        fn(arg);
        g_armed = 0;
        return 0;
    }
    if (err && cap)
        snprintf(err, cap, "%s", g_fault);
    return -2;
}

int chm_is_encrypted(const char *path)
{
    int fd;
    struct stat st;
    const uint8_t *base;
    const uint8_t *p;
    const uint8_t *end;
    uint32_t i;
    int cryptid = -1;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(struct chm_mach_header)) {
        close(fd);
        return -1;
    }
    base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED)
        return -1;
    if (((const struct chm_mach_header *)base)->magic != CHM_MH_MAGIC_64) {
        munmap((void *)base, (size_t)st.st_size);
        return -1;
    }

    p = base + sizeof(struct chm_mach_header);
    end = base + st.st_size;
    for (i = 0; i < ((const struct chm_mach_header *)base)->ncmds; i++) {
        const struct chm_load_command *lc;

        if ((size_t)(end - p) < sizeof(struct chm_load_command))
            break;
        lc = (const struct chm_load_command *)p;
        if (lc->cmdsize < sizeof(struct chm_load_command) || (size_t)lc->cmdsize > (size_t)(end - p))
            break;
        if (lc->cmd == CHM_LC_ENCRYPTION_INFO_64 || lc->cmd == CHM_LC_ENCRYPTION_INFO) {
            const uint32_t *v = (const uint32_t *)p;

            cryptid = (int)v[4];
            break;
        }
        p += lc->cmdsize;
    }
    munmap((void *)base, (size_t)st.st_size);
    return cryptid;
}
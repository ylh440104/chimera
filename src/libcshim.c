#include "chm_runtime.h"
#include "chm_session.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const char *name;
    void *addr;
    const char *kind;
} chm_entry;

static int g_resolved;

void chm_runtime_init(void)
{
    g_resolved = 0;
}

void chm_runtime_note_resolved(void)
{
    g_resolved++;
}

int chm_runtime_resolved_count(void)
{
    return g_resolved;
}

static void *bzero_impl(void *p, size_t n)
{
    memset(p, 0, n);
    return NULL;
}

static void *memchr_impl(const void *p, int c, size_t n)
{
    if (!p || !n)
        return NULL;
    return memchr(p, c, n);
}

static void *memcpy_chk_impl(void *d, const void *s, size_t n, size_t cap)
{
    (void)cap;
    return memcpy(d, s, n);
}

static void *memmove_chk_impl(void *d, const void *s, size_t n, size_t cap)
{
    (void)cap;
    return memmove(d, s, n);
}

static void *memset_chk_impl(void *p, int v, size_t n, size_t cap)
{
    (void)cap;
    return memset(p, v, n);
}

static char *strcpy_chk_impl(char *d, const char *s, size_t cap)
{
    (void)cap;
    return strcpy(d, s);
}

static char *strcat_chk_impl(char *d, const char *s, size_t cap)
{
    (void)cap;
    return strcat(d, s);
}

static int safe_memcmp(const void *a, const void *b, size_t n)
{
    if (!n)
        return 0;
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;
    return memcmp(a, b, n);
}

static char *strerror_impl(int e)
{
    return (char *)strerror(e);
}

static char *basename_impl(char *p)
{
    char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static char *dirname_impl(char *p)
{
    char *s = strrchr(p, '/');
    if (!s)
        return (char *)".";
    if (s == p)
        return (char *)"/";
    *s = 0;
    return p;
}

static char *realpath_impl(const char *path, char *resolved)
{
    (void)path;
    (void)resolved;
    return NULL;
}

static int open_impl(const char *path, int flags, int mode)
{
    return open(path, flags, (mode_t)mode);
}

static int fcntl_impl(int fd, int cmd, void *arg)
{
    return fcntl(fd, cmd, arg);
}

static int mkdir_impl(const char *path, int mode)
{
    return mkdir(path, (mode_t)mode);
}

static int kill_impl(int pid, int sig)
{
    return kill((pid_t)pid, sig);
}

static int wait_impl(int *status)
{
    return wait(status);
}

static int waitpid_impl(int pid, int *status, int options)
{
    return waitpid((pid_t)pid, status, options);
}

static int posix_spawnp_impl(int *pid, const char *path, const void *fa, const void *attr, char **argv, char **envp)
{
    (void)pid;
    (void)path;
    (void)fa;
    (void)attr;
    (void)argv;
    (void)envp;
    return -1;
}

static int ptrace_impl(int request, int pid, void *addr, int data)
{
    (void)request;
    (void)pid;
    (void)addr;
    (void)data;
    return -1;
}

static int sysctl_impl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
    (void)name;
    (void)namelen;
    (void)oldp;
    (void)oldlenp;
    (void)newp;
    (void)newlen;
    return -1;
}

static int sysctlbyname_impl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
    (void)name;
    (void)oldp;
    (void)oldlenp;
    (void)newp;
    (void)newlen;
    return -1;
}

static int task_info_impl(unsigned int task, int flavor, void *info, unsigned int *count)
{
    (void)task;
    (void)flavor;
    (void)info;
    (void)count;
    return -1;
}

static int memorystatus_control_impl(uint32_t cmd, uint32_t pid, uint32_t flags, void *buf, size_t len)
{
    (void)cmd;
    (void)pid;
    (void)flags;
    (void)buf;
    (void)len;
    return -1;
}

static int csops_impl(uint32_t pid, unsigned int ops, void *useraddr, size_t usersize)
{
    (void)pid;
    (void)ops;
    (void)useraddr;
    (void)usersize;
    return -1;
}

static int vm_protect_impl(void *task, uint64_t addr, uint64_t size, int setmax, int prot)
{
    (void)task;
    (void)setmax;
    return mprotect((void *)(uintptr_t)addr, (size_t)size, prot);
}

static int msync_impl(void *addr, size_t len, int flags)
{
    return msync(addr, len, flags);
}

static int printf_impl(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

static int fprintf_impl(FILE *stream, const char *fmt, ...)
{
    (void)stream;
    (void)fmt;
    return 0;
}

static int setvbuf_impl(FILE *stream, char *buf, int mode, size_t size)
{
    (void)stream;
    (void)buf;
    (void)size;
    return setvbuf(stream, buf, mode, size);
}

static int fileno_impl(FILE *stream)
{
    return stream ? fileno(stream) : -1;
}

static _Thread_local int g_errno_cell;

static int *sh_errno_impl(void)
{
    return &g_errno_cell;
}

static void *sh_mmap_impl(void *addr, size_t len, int prot, int flags, int fd, long off)
{
    return mmap(addr, len, prot, flags, fd, (off_t)off);
}

static int personality_impl(void)
{
    return 0;
}

static void stack_chk_fail_impl(void)
{
    abort();
}

static void assert_rtn_impl(const char *func, const char *file, int line, const char *msg)
{
    (void)func;
    (void)file;
    (void)line;
    (void)msg;
    abort();
}

static void dyld_stub_binder_impl(void)
{
}

static void unwind_resume_impl(void **frame)
{
    (void)frame;
    abort();
}

static void *tlv_bootstrap_impl(void *desc)
{
    (void)desc;
    return NULL;
}

static unsigned int dyld_image_count_impl(void)
{
    return 0;
}

static const void *dyld_get_image_header_impl(unsigned int index)
{
    (void)index;
    return NULL;
}

static const char *dyld_get_image_name_impl(unsigned int index)
{
    (void)index;
    return NULL;
}

static intptr_t dyld_get_image_vmaddr_slide_impl(unsigned int index)
{
    (void)index;
    return 0;
}

static unsigned int dyld_get_active_platform_impl(void)
{
    return 2;
}

static void dyld_register_func_for_add_image_impl(void (*fn)(const void *, intptr_t))
{
    (void)fn;
}

static void *dlopen_impl(const char *path, int mode)
{
    (void)path;
    (void)mode;
    return NULL;
}

static void *dlsym_impl(void *handle, const char *name)
{
    (void)handle;
    return chm_runtime_lookup(name);
}

static char *dlerror_impl(void)
{
    return NULL;
}

static int dladdr_impl(const void *addr, void *info)
{
    (void)addr;
    (void)info;
    return 0;
}

static char *strncpy_chk_impl(char *d, const char *s, size_t n, size_t cap)
{
    (void)cap;
    if (!d)
        return d;
    if (!s) {
        if (n)
            d[0] = 0;
        return d;
    }
    return strncpy(d, s, n);
}

static char *safe_strdup(const char *s)
{
    if (!s)
        return NULL;
    return strdup(s);
}

static chm_session *g_null_log_session;

void chm_libc_set_session(chm_session *s)
{
    g_null_log_session = s;
}

static int g_null_strlen_count;

static void note_null_strlen(void)
{
    if (!g_null_log_session)
        return;
    if (g_null_strlen_count < 8) {
        g_null_strlen_count++;
        chm_session_log(g_null_log_session, "strlen(NULL) intercepted #%d", g_null_strlen_count);
    }
}

static size_t safe_strlen(const char *s)
{
    if (!s) {
        note_null_strlen();
        return 0;
    }
    return strlen(s);
}

static int safe_strcmp(const char *a, const char *b)
{
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;
    return strcmp(a, b);
}

static int safe_strncmp(const char *a, const char *b, size_t n)
{
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;
    return strncmp(a, b, n);
}

static char *safe_strstr(const char *h, const char *n)
{
    if (!h || !n)
        return NULL;
    return strstr(h, n);
}

static char *safe_strcpy(char *d, const char *s)
{
    if (!d)
        return d;
    if (!s) {
        d[0] = 0;
        return d;
    }
    return strcpy(d, s);
}

static void *safe_memcpy(void *d, const void *s, size_t n)
{
    if (!d || !s || !n)
        return d;
    return memcpy(d, s, n);
}

static void *safe_memmove(void *d, const void *s, size_t n)
{
    if (!d || !s || !n)
        return d;
    return memmove(d, s, n);
}

static void *safe_memset(void *d, int c, size_t n)
{
    if (!d || !n)
        return d;
    return memset(d, c, n);
}

static char *safe_basename(char *p)
{
    return p ? basename_impl(p) : (char *)"/";
}

static char *safe_dirname(char *p)
{
    return p ? dirname_impl(p) : (char *)".";
}

#define FN(n, f, k) { n, (void *)(f), k }

static const chm_entry g_entries[] = {
    FN("_malloc", malloc, "libc.alloc"),
    FN("_calloc", calloc, "libc.alloc"),
    FN("_realloc", realloc, "libc.alloc"),
    FN("_reallocf", realloc, "libc.alloc"),
    FN("_free", free, "libc.alloc"),
    FN("_memcmp", safe_memcmp, "libc.mem"),
    FN("_memcpy", safe_memcpy, "libc.mem"),
    FN("_memmove", safe_memmove, "libc.mem"),
    FN("_memset", safe_memset, "libc.mem"),
    FN("_memchr", memchr_impl, "libc.mem"),
    FN("_memcpy_chk", memcpy_chk_impl, "libc.mem"),
    FN("_memmove_chk", memmove_chk_impl, "libc.mem"),
    FN("_memset_chk", memset_chk_impl, "libc.mem"),
    FN("_strcpy_chk", strcpy_chk_impl, "libc.str"),
    FN("_strcat_chk", strcat_chk_impl, "libc.str"),
    FN("___strcpy_chk", strcpy_chk_impl, "libc.str"),
    FN("___strcat_chk", strcat_chk_impl, "libc.str"),
    FN("___strlen_chk", safe_strlen, "libc.str"),
    FN("___strncpy_chk", strncpy_chk_impl, "libc.str"),
    FN("_bzero", bzero_impl, "libc.mem"),
    FN("_strcmp", safe_strcmp, "libc.str"),
    FN("_strncmp", safe_strncmp, "libc.str"),
    FN("_strlen", safe_strlen, "libc.str"),
    FN("_strstr", safe_strstr, "libc.str"),
    FN("_strcpy", safe_strcpy, "libc.str"),
    FN("_strdup", safe_strdup, "libc.str"),
    FN("_strerror", strerror_impl, "libc.str"),
    FN("_basename", safe_basename, "libc.str"),
    FN("_dirname", safe_dirname, "libc.str"),
    FN("_realpath$DARWIN_EXTSN", realpath_impl, "libc.str"),
    FN("_realpath", realpath_impl, "libc.str"),
    FN("_printf", printf_impl, "libc.io"),
    FN("_fprintf", fprintf_impl, "libc.io"),
    FN("_setvbuf", setvbuf_impl, "libc.io"),
    FN("_fileno", fileno_impl, "libc.io"),
    FN("_abort", abort, "libc.proc"),
    FN("_exit", exit, "libc.proc"),
    FN("___stack_chk_fail", stack_chk_fail_impl, "libc.guard"),
    FN("___memcpy_chk", memcpy_chk_impl, "libc.mem"),
    FN("___memmove_chk", memmove_chk_impl, "libc.mem"),
    FN("___memset_chk", memset_chk_impl, "libc.mem"),
    FN("___strcpy_chk", strcpy_chk_impl, "libc.str"),
    FN("___error", sh_errno_impl, "libc.errno"),
    FN("___fcntl", fcntl_impl, "libc.io"),
    FN("___mmap", sh_mmap_impl, "libc.mem"),
    FN("_objc_personality_v0", personality_impl, "unwind"),
    FN("___objc_personality_v0", personality_impl, "unwind"),
    FN("___assert_rtn", assert_rtn_impl, "libc.guard"),
    FN("_access", access, "libc.io"),
    FN("_open", open_impl, "libc.io"),
    FN("_close", close, "libc.io"),
    FN("_read", read, "libc.io"),
    FN("_write", write, "libc.io"),
    FN("_lseek", lseek, "libc.io"),
    FN("_fstat", fstat, "libc.io"),
    FN("_stat", stat, "libc.io"),
    FN("_mkdir", mkdir_impl, "libc.io"),
    FN("_unlink", unlink, "libc.io"),
    FN("_opendir", opendir, "libc.io"),
    FN("_fcntl", fcntl_impl, "libc.io"),
    FN("_dup2", dup2, "libc.proc"),
    FN("_pipe", pipe, "libc.proc"),
    FN("_getpid", getpid, "libc.proc"),
    FN("_kill", kill_impl, "libc.proc"),
    FN("_sleep", sleep, "libc.proc"),
    FN("_usleep", usleep, "libc.proc"),
    FN("_setenv", setenv, "libc.env"),
    FN("_getenv", getenv, "libc.env"),
    FN("_mmap", mmap, "libc.mem"),
    FN("_mprotect", mprotect, "libc.mem"),
    FN("_munmap", munmap, "libc.mem"),
    FN("_msync", msync_impl, "libc.mem"),
    FN("_signal", signal, "libc.proc"),
    FN("_dlopen", dlopen_impl, "libc.dl"),
    FN("_dlsym", dlsym_impl, "libc.dl"),
    FN("_dlerror", dlerror_impl, "libc.dl"),
    FN("_dladdr", dladdr_impl, "libc.dl"),
    FN("_acos", acos, "libc.math"),
    FN("_asin", asin, "libc.math"),
    FN("_atan", atan, "libc.math"),
    FN("_atan2", atan2, "libc.math"),
    FN("_atan2f", atan2f, "libc.math"),
    FN("_cbrt", cbrt, "libc.math"),
    FN("_cos", cos, "libc.math"),
    FN("_cosh", cosh, "libc.math"),
    FN("_sin", sin, "libc.math"),
    FN("_sinh", sinh, "libc.math"),
    FN("_tan", tan, "libc.math"),
    FN("_tanh", tanh, "libc.math"),
    FN("_hypot", hypot, "libc.math"),
    FN("_log10", log10, "libc.math"),
    FN("_log2", log2, "libc.math"),
    FN("_pow", pow, "libc.math"),
    FN("_posix_spawnp", posix_spawnp_impl, "libc.proc"),
    FN("_wait", wait_impl, "libc.proc"),
    FN("_waitpid", waitpid_impl, "libc.proc"),
    FN("_ptrace", ptrace_impl, "libc.proc"),
    FN("_sysctl", sysctl_impl, "libc.sys"),
    FN("_sysctlbyname", sysctlbyname_impl, "libc.sys"),
    FN("_task_info", task_info_impl, "mach.sys"),
    FN("_memorystatus_control", memorystatus_control_impl, "mach.sys"),
    FN("_csops", csops_impl, "mach.sys"),
    FN("_vm_protect", vm_protect_impl, "mach.sys"),
    FN("_dyld_image_count", dyld_image_count_impl, "dyld"),
    FN("_dyld_get_image_header", dyld_get_image_header_impl, "dyld"),
    FN("_dyld_get_image_name", dyld_get_image_name_impl, "dyld"),
    FN("_dyld_get_image_vmaddr_slide", dyld_get_image_vmaddr_slide_impl, "dyld"),
    FN("_dyld_get_active_platform", dyld_get_active_platform_impl, "dyld"),
    FN("_dyld_register_func_for_add_image", dyld_register_func_for_add_image_impl, "dyld"),
    FN("__dyld_image_count", dyld_image_count_impl, "dyld"),
    FN("__dyld_get_image_header", dyld_get_image_header_impl, "dyld"),
    FN("__dyld_get_image_name", dyld_get_image_name_impl, "dyld"),
    FN("__dyld_get_image_vmaddr_slide", dyld_get_image_vmaddr_slide_impl, "dyld"),
    FN("__dyld_register_func_for_add_image", dyld_register_func_for_add_image_impl, "dyld"),
    FN("dyld_stub_binder", dyld_stub_binder_impl, "dyld"),
    FN("__tlv_bootstrap", tlv_bootstrap_impl, "libc.tlv"),
    FN("__Unwind_Resume", unwind_resume_impl, "unwind"),
    { NULL, NULL, NULL }
};

void *chm_runtime_lookup(const char *name)
{
    if (!name)
        return NULL;
    for (const chm_entry *e = g_entries; e->name; e++) {
        if (!strcmp(e->name, name))
            return e->addr;
    }
    return NULL;
}

const char *chm_runtime_kind(const char *name)
{
    if (!name)
        return NULL;
    for (const chm_entry *e = g_entries; e->name; e++) {
        if (!strcmp(e->name, name))
            return e->kind;
    }
    return NULL;
}

int chm_runtime_count(void)
{
    int n = 0;
    while (g_entries[n].name)
        n++;
    return n;
}

const char *chm_runtime_name_at(int index)
{
    int n = 0;
    while (g_entries[n].name) {
        if (n == index)
            return g_entries[n].name;
        n++;
    }
    return NULL;
}
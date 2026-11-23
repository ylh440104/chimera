#include "chm_runtime.h"
#include "chm_session.h"
#include "chm_format.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
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

static unsigned char guest_stdout_storage, guest_stderr_storage;
static FILE *guest_stdout, *guest_stderr;
static int g_vprintf_count;
extern uint64_t g_stdout_obj, g_stderr_obj;
static int g_resolved;

static chm_session *g_null_log_session;

static int g_dyld_image_count;
static const void *g_dyld_headers[CHM_MAX_IMAGES];
static const char *g_dyld_names[CHM_MAX_IMAGES];
static uint64_t g_dyld_slides[CHM_MAX_IMAGES];

void chm_libc_publish_images(chm_session *s)
{
    int i;

    g_dyld_image_count = 0;
    if (!s)
        return;
    for (i = 0; i < s->image_count && i < CHM_MAX_IMAGES; i++) {
        g_dyld_headers[i] = (const void *)(uintptr_t)(s->images[i].vm_min + s->images[i].slide);
        g_dyld_names[i] = s->image_paths[i];
        g_dyld_slides[i] = s->images[i].slide;
        g_dyld_image_count++;
    }
}

void chm_runtime_init(void)
{
    g_resolved = 0;
    guest_stdout = (FILE *)&guest_stdout_storage;
    guest_stderr = (FILE *)&guest_stderr_storage;
    g_stdout_obj = (uint64_t)(uintptr_t)guest_stdout;
    g_stderr_obj = (uint64_t)(uintptr_t)guest_stderr;
    g_vprintf_count = 0;
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
    if (!d || !s || !n)
        return d;
    return __builtin_memcpy(d, s, n);
}

static void *memmove_chk_impl(void *d, const void *s, size_t n, size_t cap)
{
    (void)cap;
    if (!d || !s || !n)
        return d;
    return __builtin_memmove(d, s, n);
}

static void *memset_chk_impl(void *p, int v, size_t n, size_t cap)
{
    (void)cap;
    if (!p || !n)
        return p;
    return __builtin_memset(p, v, n);
}

static char *strcpy_chk_impl(char *d, const char *s, size_t cap)
{
    (void)cap;
    if (!d)
        return d;
    if (!s) {
        d[0] = 0;
        return d;
    }
    return __builtin_strcpy(d, s);
}

static char *strcat_chk_impl(char *d, const char *s, size_t cap)
{
    (void)cap;
    if (!d || !s)
        return d;
    return __builtin_strcat(d, s);
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

#include "libc_io.inc"

static int open_impl(const char *path, int flags, int mode)
{
    return open(path, flags, (mode_t)mode);
}
static int fcntl_impl(int fd, int cmd, void *arg)
{
    if (cmd == F_GETFL) return chm_io_get_status(fd);
    if (cmd == F_SETFL) return chm_io_set_status(fd, (int)(intptr_t)arg);
    if (cmd == F_GETFD) return chm_io_fd_flags(fd);
    if (cmd == F_SETFD) return chm_io_set_fd_flags(fd, (int)(intptr_t)arg);
    return io_fcntl(fd, cmd, arg);
}

static int mkdir_impl(const char *path, int mode)
{
    char mapped[1024];
    int rc = chm_foundation_map_mutable(path, mapped, sizeof(mapped)) ? mkdir(mapped, (mode_t)mode) : -1;
    if (rc < 0) g_errno_cell = io_errno(errno);
    return rc;
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

static int output_number(FILE *stream)
{
    if (stream == guest_stdout) return STDOUT_FILENO;
    if (stream == guest_stderr) return STDERR_FILENO;
    return -1;
}

static size_t write_output(int fd, const void *bytes, size_t length)
{
    const unsigned char *data = bytes;
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = io_write(fd, data + offset, length - offset);
        if (written <= 0) break;
        offset += (size_t)written;
    }
    return offset;
}

static void emit_text(const char *text)
{
    if (!g_null_log_session || !text)
        return;
    chm_session_log(g_null_log_session, "out: %s", text);
}

extern void chm_printf_entry(void);
extern void chm_fprintf_entry(void);

int chm_fprintf_list(FILE *stream, const char *fmt, const void *slots)
{
    size_t len;
    if (!stream || !fmt) return -1;
    char *text = chm_format_darwin(fmt, slots, &len);
    if (!text) return -1;
    int descriptor = output_number(stream);
    int rc = (int)len;
    if (descriptor >= 0) {
        if (write_output(descriptor, text, len) != len) rc = -1;
        if (g_vprintf_count < 400) { g_vprintf_count++; emit_text(text); }
    } else if (fwrite(text, 1, len, stream) != len) rc = -1;
    free(text);
    return rc;
}

int chm_printf_list(const char *fmt, const void *slots)
{
    return chm_fprintf_list(guest_stdout, fmt, slots);
}

static int stream_close_impl(FILE *stream)
{
    int fd = output_number(stream);
    return fd >= 0 ? chm_io_close_number(fd) : fclose(stream);
}

static size_t fwrite_impl(const void *p, size_t size, size_t count, FILE *stream)
{
    if (!p || !size || !count) return 0;
    int fd = output_number(stream);
    if (fd < 0) return fwrite(p, size, count, stream);
    if (count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    return write_output(fd, p, bytes) / size;
}

static size_t fread_impl(void *p, size_t size, size_t count, FILE *stream)
{
    int fd = output_number(stream);
    if (fd < 0) return fread(p, size, count, stream);
    if (!p || !size || !count || count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    ssize_t received = io_read(fd, p, bytes);
    return received > 0 ? (size_t)received / size : 0;
}

static int fputs_impl(const char *text, FILE *stream)
{
    if (!text || !stream) return -1;
    int fd = output_number(stream);
    size_t length = strlen(text);
    if (fd >= 0) return write_output(fd, text, length) == length ? (int)length : -1;
    return fputs(text, stream);
}

static int fputc_impl(int value, FILE *stream)
{
    int fd = output_number(stream);
    if (fd < 0) return fputc(value, stream);
    unsigned char byte = (unsigned char)value;
    return write_output(fd, &byte, 1) == 1 ? byte : -1;
}

static int fflush_impl(FILE *stream)
{
    if (output_number(stream) >= 0) return 0;
    return stream ? fflush(stream) : fflush(NULL);
}

static int setvbuf_impl(FILE *stream, char *buffer, int mode, size_t size)
{
    if (output_number(stream) >= 0) return 0;
    if (!stream) return -1;
    return setvbuf(stream, buffer, mode, size);
}

static int fileno_impl(FILE *stream)
{
    int fd = output_number(stream);
    return fd >= 0 ? fd : stream ? fileno(stream) : -1;
}

static FILE *fopen_mapped(const char *path, const char *mode)
{
    char mapped[1024];
    FILE *fp;

    if (!path || !mode)
        return NULL;
    int ok = mode[0] == 'r' && !strchr(mode, '+') ? chm_foundation_map_path(path, mapped, sizeof(mapped)) :
             chm_foundation_map_path_write(path, mapped, sizeof(mapped));
    if (!ok) { g_errno_cell = io_errno(errno); return NULL; }
    fp = fopen(mapped, mode);
    if (!fp && g_null_log_session)
        chm_session_log(g_null_log_session, "fopen failed %s mode %s", mapped, mode);
    return fp;
}

static int fclose_impl(FILE *fp) { return stream_close_impl(fp); }

static int fdopen_impl(int guest_fd, const char *mode, FILE **stream)
{
    if (!mode || !stream) { errno = EINVAL; return -1; }
    if (guest_fd == STDOUT_FILENO) { *stream = guest_stdout; return 0; }
    if (guest_fd == STDERR_FILENO) { *stream = guest_stderr; return 0; }
    int host = io_host(guest_fd);
    if (host < 0) { errno = EBADF; return -1; }
    int duplicate = fcntl(host, F_DUPFD_CLOEXEC, 3);
    if (duplicate < 0) return -1;
    FILE *opened = fdopen(duplicate, mode);
    if (!opened) { close(duplicate); return -1; }
    if (guest_fd > STDERR_FILENO && chm_io_remap(guest_fd, duplicate, 0) < 0) {
        fclose(opened);
        return -1;
    }
    *stream = opened;
    return 0;
}

static char *fgets_impl(char *s, int n, FILE *stream)
{
    int fd = output_number(stream);
    if (fd < 0) return stream && s && n > 0 ? fgets(s, n, stream) : NULL;
    if (!s || n <= 0) return NULL;
    int i = 0;
    while (i + 1 < n) {
        ssize_t received = io_read(fd, s + i, 1);
        if (received <= 0) break;
        char value = s[i++];
        if (value == '\n') break;
    }
    if (!i) return NULL;
    s[i] = 0;
    return s;
}

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
    if (g_null_log_session)
        chm_session_log(g_null_log_session, "stack_chk_fail");
}

static void assert_rtn_impl(const char *func, const char *file, int line, const char *msg)
{
    if (g_null_log_session) {
        chm_session_log(g_null_log_session, "assert %s:%d %s(): %s",
                        file ? file : "?", line, func ? func : "?", msg ? msg : "?");
    }
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
    return (unsigned int)g_dyld_image_count;
}

static const void *dyld_get_image_header_impl(unsigned int index)
{
    if (index >= (unsigned int)g_dyld_image_count)
        return NULL;
    return g_dyld_headers[index];
}

static const char *dyld_get_image_name_impl(unsigned int index)
{
    if (index >= (unsigned int)g_dyld_image_count)
        return NULL;
    return g_dyld_names[index];
}

static intptr_t dyld_get_image_vmaddr_slide_impl(unsigned int index)
{
    if (index >= (unsigned int)g_dyld_image_count)
        return 0;
    return (intptr_t)g_dyld_slides[index];
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

void chm_libc_set_session(chm_session *s)
{
    g_null_log_session = s;
}

chm_session *chm_libc_session(void)
{
    return g_null_log_session;
}

static void abort_impl(void)
{
    if (g_null_log_session)
        chm_session_log(g_null_log_session, "abort() called");
    _exit(134);
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

#if defined(__ANDROID__) && defined(CHM_OVERRIDE_CHK)
size_t __strlen_chk(const char *s, size_t n)
{
    (void)n;
    if (!s) {
        note_null_strlen();
        return 0;
    }
    return __builtin_strlen(s);
}

char *__strcpy_chk(char *d, const char *s, size_t cap)
{
    (void)cap;
    if (!d)
        return d;
    if (!s) {
        d[0] = 0;
        return d;
    }
    return __builtin_strcpy(d, s);
}

char *__strcat_chk(char *d, const char *s, size_t cap)
{
    (void)cap;
    if (!d || !s)
        return d;
    return __builtin_strcat(d, s);
}

void *__memcpy_chk(void *d, const void *s, size_t n, size_t cap)
{
    (void)cap;
    if (!d || !s || !n)
        return d;
    return __builtin_memcpy(d, s, n);
}

void *__memmove_chk(void *d, const void *s, size_t n, size_t cap)
{
    (void)cap;
    if (!d || !s || !n)
        return d;
    return __builtin_memmove(d, s, n);
}

void *__memset_chk(void *d, int c, size_t n, size_t cap)
{
    (void)cap;
    if (!d || !n)
        return d;
    return __builtin_memset(d, c, n);
}
#endif

static char *safe_getenv(const char *name)
{
    if (!name)
        return NULL;
    return getenv(name);
}

static int safe_setenv(const char *name, const char *value, int overwrite)
{
    if (!name)
        return -1;
    return setenv(name, value ? value : "", overwrite);
}

static int safe_access(const char *path, int mode)
{
    if (!path)
        return -1;
    return access(path, mode);
}

static int safe_stat(const char *path, struct stat *st)
{
    if (!path || !st)
        return -1;
    return stat(path, st);
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
    FN("_printf", chm_printf_entry, "libc.io"),
    FN("_fprintf", chm_fprintf_entry, "libc.io"),
    FN("_fopen", fopen_mapped, "libc.io"),
    FN("_fdopen", fdopen_impl, "libc.io"),
    FN("_freopen", fopen_mapped, "libc.io"),
    FN("_fclose", fclose_impl, "libc.io"),
    FN("_fwrite", fwrite_impl, "libc.io"),
    FN("_fread", fread_impl, "libc.io"),
    FN("_fputs", fputs_impl, "libc.io"),
    FN("_fputc", fputc_impl, "libc.io"),
    FN("_putchar", fputc_impl, "libc.io"),
    FN("_fflush", fflush_impl, "libc.io"),
    FN("_fgets", fgets_impl, "libc.io"),
    FN("_setvbuf", setvbuf_impl, "libc.io"),
    FN("_fileno", fileno_impl, "libc.io"),
    FN("_abort", abort_impl, "libc.proc"),
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
    FN("_access", safe_access, "libc.io"),
    FN("_open", open_impl, "libc.io"),
    FN("_close", chm_io_close_number, "libc.io"),
    FN("_read", io_read, "libc.io"),
    FN("_write", io_write, "libc.io"),
    FN("_lseek", chm_io_seek_number, "libc.io"),
    FN("_fstat", chm_io_file_status, "libc.io"),
    FN("_stat", safe_stat, "libc.io"),
    FN("_mkdir", mkdir_impl, "libc.io"),
    FN("_unlink", unlink, "libc.io"),
    FN("_opendir", opendir, "libc.io"),
    FN("_fcntl", fcntl_impl, "libc.io"),
    FN("_dup2", io_dup2, "libc.proc"),
    FN("_pipe", io_pipe, "libc.proc"),
    FN("_getpid", getpid, "libc.proc"),
    FN("_kill", kill_impl, "libc.proc"),
    FN("_sleep", io_sleep, "libc.proc"),
    FN("_usleep", usleep, "libc.proc"),
    FN("_setenv", safe_setenv, "libc.env"),
    FN("_getenv", safe_getenv, "libc.env"),
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
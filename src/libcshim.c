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
#include <stddef.h>
#include <zlib.h>

_Static_assert(sizeof(z_stream) == 112 && sizeof(uLong) == 8 && sizeof(z_crc_t) == 4 &&
               offsetof(z_stream, next_out) == 24 && offsetof(z_stream, opaque) == 80 &&
               offsetof(z_stream, adler) == 96, "Darwin arm64 z_stream ABI required");

typedef struct {
    const char *name;
    void *addr;
    const char *kind;
} chm_entry;

typedef struct {
    _Alignas(max_align_t) atomic_int state;
} guest_output_stream;
#define GUEST_OUTPUT_ERROR 1
#define GUEST_OUTPUT_EOF 2
static guest_output_stream guest_output_streams[2];
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
    guest_stdout = (FILE *)&guest_output_streams[0];
    guest_stderr = (FILE *)&guest_output_streams[1];
    atomic_store(&guest_output_streams[0].state, 0);
    atomic_store(&guest_output_streams[1].state, 0);
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

#include "libc_files.inc"

void chm_io_reset(void)
{
    file_directories_reset();
    io_descriptors_reset();
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

static int output_slot(FILE *stream)
{
    if (stream == guest_stdout) return 0;
    if (stream == guest_stderr) return 1;
    return -1;
}

static void output_note(int slot, int bit)
{
    if (slot >= 0 && slot < 2) atomic_fetch_or(&guest_output_streams[slot].state, bit);
}

static int output_number(FILE *stream)
{
    int slot = output_slot(stream);
    return slot < 0 ? -1 : STDOUT_FILENO + slot;
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
    if (offset != length) output_note(fd - STDOUT_FILENO, GUEST_OUTPUT_ERROR);
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

static int puts_impl(const char *text)
{
    if (!text) return file_failure(EINVAL);
    size_t length = strlen(text);
    if (write_output(STDOUT_FILENO, text, length) != length || write_output(STDOUT_FILENO, "\n", 1) != 1) return -1;
    emit_text(text);
    return 0;
}

static int64_t time_impl(int64_t *output)
{
    time_t value = time(NULL);
    if (value == (time_t)-1) { file_failure(errno); return -1; }
    if (output) *output = (int64_t)value;
    return (int64_t)value;
}

static int stream_close_impl(FILE *stream)
{
    if (!stream) return file_failure(EBADF);
    int guest = output_number(stream);
    pthread_mutex_lock(&io_lock);
    if (guest < 0) {
        for (io_descriptor *d = io_descriptors; d; d = d->next) {
            if (d->stream == stream) { guest = d->guest; break; }
        }
    }
    int result = guest < 0 ? fclose(stream) : io_close(guest);
    int error = errno;
    pthread_mutex_unlock(&io_lock);
    return result ? file_failure(error) : 0;
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
    size_t bytes = size * count, done = 0;
    while (done < bytes) {
        ssize_t n = io_read(fd, (unsigned char *)p + done, bytes - done);
        if (n <= 0) {
            output_note(fd - STDOUT_FILENO, n < 0 ? GUEST_OUTPUT_ERROR : GUEST_OUTPUT_EOF);
            break;
        }
        done += (size_t)n;
    }
    return done / size;
}

static int ferror_impl(FILE *stream)
{
    int slot = output_slot(stream);
    if (slot < 0) return stream ? ferror(stream) : 0;
    return (atomic_load(&guest_output_streams[slot].state) & GUEST_OUTPUT_ERROR) != 0;
}

static int feof_impl(FILE *stream)
{
    int slot = output_slot(stream);
    if (slot < 0) return stream ? feof(stream) : 0;
    return (atomic_load(&guest_output_streams[slot].state) & GUEST_OUTPUT_EOF) != 0;
}

static void clearerr_impl(FILE *stream)
{
    int slot = output_slot(stream);
    if (slot < 0) { if (stream) clearerr(stream); return; }
    atomic_store(&guest_output_streams[slot].state, 0);
}

_Static_assert(sizeof(long) == 8 && sizeof(off_t) == 8, "64-bit Darwin file offsets required");

static int fseeko_impl(FILE *stream, int64_t offset, int whence)
{
    if (!stream) return file_failure(EBADF);
    int slot = output_slot(stream);
    if (slot >= 0) {
        if (chm_io_seek_number(STDOUT_FILENO + slot, offset, whence) < 0) return -1;
        atomic_fetch_and(&guest_output_streams[slot].state, ~GUEST_OUTPUT_EOF);
        return 0;
    }
    return fseeko(stream, (off_t)offset, whence) ? file_failure(errno) : 0;
}

static int64_t ftello_impl(FILE *stream)
{
    if (!stream) return file_failure(EBADF);
    int fd = output_number(stream);
    if (fd >= 0) return chm_io_seek_number(fd, 0, SEEK_CUR);
    off_t offset = ftello(stream);
    if (offset < 0) return file_failure(errno);
    return offset;
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
    if (!stream) return file_failure(EBADF);
    int fd = output_number(stream);
    if (fd >= 0) return fd;
    pthread_mutex_lock(&io_lock);
    for (io_descriptor *d = io_descriptors; d; d = d->next) {
        if (d->stream == stream) { fd = d->guest; break; }
    }
    if (fd < 0) fd = fileno(stream);
    pthread_mutex_unlock(&io_lock);
    return fd < 0 ? file_failure(errno) : fd;
}

static FILE *fopen_mapped(const char *path, const char *mode)
{
    char mapped[2048];
    if (!mode) { file_failure(EINVAL); return NULL; }
    int ok = mode[0] == 'r' && !strchr(mode, '+') ? chm_foundation_map_path(path, mapped, sizeof(mapped)) :
             chm_foundation_map_mutable(path, mapped, sizeof(mapped));
    if (!ok) { file_failure(errno); return NULL; }
    FILE *fp = fopen(mapped, mode);
    if (!fp) file_failure(errno);
    return fp;
}

static int fclose_impl(FILE *fp) { return stream_close_impl(fp); }

static FILE *fdopen_impl(int guest_fd, const char *mode)
{
    if (!mode) { file_failure(EINVAL); return NULL; }
    pthread_mutex_lock(&io_lock);
    io_descriptor *d = io_find(guest_fd);
    int host = d ? d->host : guest_fd;
    FILE *stream = NULL;
    int error = 0;
    if (host < 0 || fcntl(host, F_GETFD) < 0) error = EBADF;
    else if (d && d->stream) error = EBUSY;
    else {
        int inherited = !d && guest_fd <= STDERR_FILENO;
        if (inherited) host = fcntl(host, F_DUPFD_CLOEXEC, 3);
        if (host < 0) error = errno;
        else if (!d && io_track(guest_fd, host, NULL, 0)) {
            error = ENOMEM;
            if (inherited) close(host);
        } else {
            d = io_find(guest_fd);
            stream = fdopen(host, mode);
            if (!stream) error = errno;
            else d->stream = stream;
        }
    }
    pthread_mutex_unlock(&io_lock);
    if (error) file_failure(error);
    return stream;
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
    abort();
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

#include "libc_compat.inc"
#include "libc_pthread.inc"

#define FN(n, f, k) { n, (void *)(f), k }

static const chm_entry g_entries[] = {
    FN("_objc_sync_enter", pt_objc_sync_enter, "objc.sync"),
    FN("_objc_sync_exit", pt_objc_sync_exit, "objc.sync"),
    FN("_pthread_create", pt_create, "libc.pthread"),
    FN("_pthread_join", pt_join, "libc.pthread"),
    FN("_pthread_detach", pt_detach, "libc.pthread"),
    FN("_pthread_exit", pt_exit, "libc.pthread"),
    FN("_pthread_self", pt_self, "libc.pthread"),
    FN("_pthread_equal", pt_equal, "libc.pthread"),
    FN("_pthread_attr_init", pt_attr_init, "libc.pthread"),
    FN("_pthread_attr_destroy", pt_attr_destroy, "libc.pthread"),
    FN("_pthread_attr_setdetachstate", pt_attr_setdetachstate, "libc.pthread"),
    FN("_pthread_attr_getdetachstate", pt_attr_getdetachstate, "libc.pthread"),
    FN("_pthread_attr_setstacksize", pt_attr_setstacksize, "libc.pthread"),
    FN("_pthread_attr_getstacksize", pt_attr_getstacksize, "libc.pthread"),
    FN("_pthread_attr_setguardsize", pt_attr_setguardsize, "libc.pthread"),
    FN("_pthread_attr_getguardsize", pt_attr_getguardsize, "libc.pthread"),
    FN("_pthread_mutexattr_init", pt_mutexattr_init, "libc.pthread"),
    FN("_pthread_mutexattr_destroy", pt_mutexattr_destroy, "libc.pthread"),
    FN("_pthread_mutexattr_settype", pt_mutexattr_settype, "libc.pthread"),
    FN("_pthread_mutexattr_gettype", pt_mutexattr_gettype, "libc.pthread"),
    FN("_pthread_mutexattr_setpshared", pt_mutexattr_setpshared, "libc.pthread"),
    FN("_pthread_mutexattr_getpshared", pt_mutexattr_getpshared, "libc.pthread"),
    FN("_pthread_mutex_init", pt_mutex_init, "libc.pthread"),
    FN("_pthread_mutex_destroy", pt_mutex_destroy, "libc.pthread"),
    FN("_pthread_mutex_lock", pt_mutex_lock, "libc.pthread"),
    FN("_pthread_mutex_trylock", pt_mutex_trylock, "libc.pthread"),
    FN("_pthread_mutex_unlock", pt_mutex_unlock, "libc.pthread"),
    FN("_crc32", crc32, "libz"),
    FN("_get_crc_table", get_crc_table, "libz"),
    FN("_deflateInit2_", deflateInit2_, "libz"),
    FN("_deflate", deflate, "libz"),
    FN("_deflateEnd", deflateEnd, "libz"),
    FN("_inflateInit2_", inflateInit2_, "libz"),
    FN("_inflate", inflate, "libz"),
    FN("_inflateEnd", inflateEnd, "libz"),
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
    FN("_time", time_impl, "libc.time"),
    FN("_puts", puts_impl, "libc.io"),
    FN("_snprintf", chm_snprintf_entry, "libc.io"),
    FN("_vsnprintf", chm_snprintf_list, "libc.io"),
    FN("_printf", chm_printf_entry, "libc.io"),
    FN("_fprintf", chm_fprintf_entry, "libc.io"),
    FN("_fopen", fopen_mapped, "libc.io"),
    FN("_fdopen", fdopen_impl, "libc.io"),
    FN("_freopen", fopen_mapped, "libc.io"),
    FN("_fclose", fclose_impl, "libc.io"),
    FN("_rewind", rewind_impl, "libc.io"),
    FN("_sscanf", chm_sscanf_entry, "libc.io"),
    FN("_vsscanf", chm_sscanf_list, "libc.io"),
    FN("___sprintf_chk", chm_sprintf_chk_entry, "libc.io"),
    FN("___vsprintf_chk", chm_sprintf_chk_list, "libc.io"),
    FN("_fwrite", fwrite_impl, "libc.io"),
    FN("_fread", fread_impl, "libc.io"),
    FN("_fputs", fputs_impl, "libc.io"),
    FN("_fputc", fputc_impl, "libc.io"),
    FN("_putchar", fputc_impl, "libc.io"),
    FN("_fflush", fflush_impl, "libc.io"),
    FN("_fgets", fgets_impl, "libc.io"),
    FN("_feof", feof_impl, "libc.io"),
    FN("_ferror", ferror_impl, "libc.io"),
    FN("_clearerr", clearerr_impl, "libc.io"),
    FN("_fseek", fseeko_impl, "libc.io"),
    FN("_fseeko", fseeko_impl, "libc.io"),
    FN("_ftell", ftello_impl, "libc.io"),
    FN("_ftello", ftello_impl, "libc.io"),
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
    FN("_access", file_access, "libc.io"),
    FN("_open", chm_open_entry, "libc.io"),
    FN("_close", chm_io_close_number, "libc.io"),
    FN("_read", io_read, "libc.io"),
    FN("_write", io_write, "libc.io"),
    FN("_lseek", chm_io_seek_number, "libc.io"),
    FN("_fstat", file_stat_fd, "libc.io"),
    FN("_stat", file_stat_path, "libc.io"),
    FN("_mkdir", mkdir_impl, "libc.io"),
    FN("_unlink", file_unlink, "libc.io"),
    FN("_remove", file_remove, "libc.io"),
    FN("_opendir", file_opendir, "libc.io"),
    FN("_readdir", file_readdir, "libc.io"),
    FN("_closedir", file_closedir, "libc.io"),
    FN("_fcntl", fcntl_impl, "libc.io"),
    FN("_dup2", io_dup2, "libc.proc"),
    FN("_pipe", io_pipe, "libc.proc"),
    FN("_getpid", getpid, "libc.proc"),
    FN("_kill", kill_impl, "libc.proc"),
    FN("_sleep", io_sleep, "libc.proc"),
    FN("_usleep", usleep, "libc.proc"),
    FN("_setenv", safe_setenv, "libc.env"),
    FN("_getenv", safe_getenv, "libc.env"),
    FN("_unsetenv", unsetenv_impl, "libc.env"),
    FN("_posix_memalign", posix_memalign_impl, "libc.alloc"),
    FN("_arc4random", arc4random_impl, "libc.random"),
    FN("_getsectiondata", getsectiondata_impl, "dyld"),
    FN("_abs", abs, "libc.math"),
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
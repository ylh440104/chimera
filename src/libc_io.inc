#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>

int chm_io_close_number(int guest);
int chm_io_file_status(int guest, struct stat *status);
static _Thread_local int g_errno_cell;
static int io_errno(int value);
int chm_io_fd_flags(int guest);
int chm_io_set_fd_flags(int guest, int flags);
int chm_io_remap(int guest, int host, int reset_host);
int chm_io_get_status(int guest);
int chm_io_set_status(int guest, int flags);
static int io_close(int guest);

typedef struct io_description {
    unsigned references;
    int flags, polled;
} io_description;
typedef struct io_descriptor {
    int guest, host;
    FILE *stream;
    io_description *description;
    struct io_descriptor *next;
} io_descriptor;
static io_descriptor *io_descriptors;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static io_descriptor *io_find(int guest);
static int io_host(int guest);
static int io_track(int guest, int host, io_description *description, int polled);
static int io_close(int guest);

int chm_io_remap(int guest, int host, int reset_host)
{
    pthread_mutex_lock(&io_lock);
    io_descriptor *current = io_find(guest);
    io_description *description = current ? current->description : NULL;
    if (description) description->references++;
    if (!description) {
        description = calloc(1, sizeof(*description));
        if (!description) { pthread_mutex_unlock(&io_lock); return -1; }
        description->flags = fcntl(host, F_GETFL);
        description->references = 1;
    }
    if (current) {
        if (current->host >= 0 && current->host != host) close(current->host);
        if (current->description && !--current->description->references) free(current->description);
        current->host = host;
        current->description = description;
    } else if (io_track(guest, host, description, 0)) {
        if (!--description->references) free(description);
        pthread_mutex_unlock(&io_lock);
        return -1;
    } else {
        description->references--;
    }
    if (reset_host) {
        if (guest == STDOUT_FILENO) {
            if (dup2(host, STDOUT_FILENO) < 0) { pthread_mutex_unlock(&io_lock); return -1; }
        } else if (guest == STDERR_FILENO) {
            if (dup2(host, STDERR_FILENO) < 0) { pthread_mutex_unlock(&io_lock); return -1; }
        }
    }
    pthread_mutex_unlock(&io_lock);
    return guest;
}

int chm_io_fd_valid(int guest)
{
    pthread_mutex_lock(&io_lock);
    int host = io_host(guest);
    int valid = host >= 0 && fcntl(host, F_GETFD) >= 0;
    pthread_mutex_unlock(&io_lock);
    return valid;
}

int chm_io_get_status(int guest)
{
    pthread_mutex_lock(&io_lock);
    io_descriptor *d = io_find(guest);
    int flags = d ? d->description->flags : fcntl(io_host(guest), F_GETFL);
    pthread_mutex_unlock(&io_lock);
    if (flags < 0) return -1;
    int result = flags & 3;
    if (flags & O_APPEND) result |= 8;
    if (flags & O_NONBLOCK) result |= 4;
    if (flags & O_SYNC) result |= 0x80;
    return result;
}

int chm_io_set_status(int guest, int flags)
{
    pthread_mutex_lock(&io_lock);
    io_descriptor *d = io_find(guest);
    int host = io_host(guest);
    int translated = flags & 3;
    if (flags & 8) translated |= O_APPEND;
    if (flags & 4) translated |= O_NONBLOCK;
    if (flags & 0x80) translated |= O_SYNC;
    int rc = fcntl(host, F_SETFL, translated | (d && d->description->polled ? O_NONBLOCK : 0));
    if (!rc && d) d->description->flags = translated;
    pthread_mutex_unlock(&io_lock);
    return rc;
}

int chm_io_close_number(int guest)
{
    pthread_mutex_lock(&io_lock);
    int result = io_close(guest);
    pthread_mutex_unlock(&io_lock);
    return result;
}

int chm_io_file_status(int guest, struct stat *status)
{
    pthread_mutex_lock(&io_lock);
    int host = io_host(guest);
    int result = fstat(host, status);
    pthread_mutex_unlock(&io_lock);
    return result;
}

int64_t chm_io_seek_number(int guest, int64_t offset, int whence)
{
    pthread_mutex_lock(&io_lock);
    off_t result = lseek(io_host(guest), (off_t)offset, whence);
    if (result < 0) g_errno_cell = io_errno(errno);
    pthread_mutex_unlock(&io_lock);
    return result;
}

static int io_errno(int value)
{
    switch (value) {
    case EAGAIN: return 35;
    case ENOTSUP: return 45;
    case ELOOP: return 62;
    case ENAMETOOLONG: return 63;
    case ENOTEMPTY: return 66;
    case EDQUOT: return 69;
    case ESTALE: return 70;
    case EOVERFLOW: return 84;
    case ECANCELED: return 89;
    default: return value;
    }
}

int chm_io_fd_flags(int guest)
{
    int host = io_host(guest);
    return host < 0 ? -1 : fcntl(host, F_GETFD);
}

int chm_io_set_fd_flags(int guest, int flags)
{
    int host = io_host(guest);
    return host < 0 ? -1 : fcntl(host, F_SETFD, flags & 1 ? FD_CLOEXEC : 0);
}
static io_descriptor *io_find(int guest)
{
    for (io_descriptor *d = io_descriptors; d; d = d->next) if (d->guest == guest) return d;
    return NULL;
}
static int io_host(int guest)
{
    io_descriptor *d = io_find(guest);
    return d ? d->host : guest;
}
static int io_track(int guest, int host, io_description *description, int polled)
{
    io_descriptor *existing = io_find(guest);
    io_descriptor *d = existing ? existing : calloc(1, sizeof(*d));
    if (!d) return -1;
    if (!description) {
        description = calloc(1, sizeof(*description));
        if (!description) { if (!existing) free(d); return -1; }
        description->flags = host >= 0 ? fcntl(host, F_GETFL) : 0;
        if (polled) description->flags &= ~O_NONBLOCK;
        description->polled = polled;
    }
    description->references++;
    if (existing) {
        if (d->host >= 0 && d->host != host) {
            if (d->stream) fclose(d->stream); else close(d->host);
            d->stream = NULL;
        }
        if (!--d->description->references) free(d->description);
    } else {
        d->next = io_descriptors; io_descriptors = d;
    }
    d->guest = guest; d->host = host; d->description = description;
    return 0;
}
static int io_close(int guest)
{
    io_descriptor **link = &io_descriptors;
    while (*link && (*link)->guest != guest) link = &(*link)->next;
    if (!*link) {
        if (guest >= 0 && guest <= 2) return io_track(guest, -1, NULL, 0);
        return close(guest);
    }
    io_descriptor *d = *link;
    int result;
    if (d->host < 0) { errno = EBADF; result = -1; }
    else result = d->stream ? fclose(d->stream) : close(d->host);
    d->stream = NULL;
    if (guest <= 2) { d->host = -1; return result; }
    *link = d->next;
    if (!--d->description->references) free(d->description);
    free(d);
    return result;
}
static int io_pipe(int *descriptors)
{
    int pair[2];
    if (!descriptors || pipe(pair)) return -1;
    if (fcntl(pair[0], F_SETFL, O_NONBLOCK) || fcntl(pair[1], F_SETFL, O_NONBLOCK)) {
        close(pair[0]); close(pair[1]); return -1;
    }
    fcntl(pair[0], F_SETFD, FD_CLOEXEC); fcntl(pair[1], F_SETFD, FD_CLOEXEC);
    if (io_track(pair[0], pair[0], NULL, 1)) { close(pair[0]); close(pair[1]); return -1; }
    if (io_track(pair[1], pair[1], NULL, 1)) { io_close(pair[0]); close(pair[1]); return -1; }
    descriptors[0] = pair[0]; descriptors[1] = pair[1];
    return 0;
}
static int io_dup2(int source, int target)
{
    int host = io_host(source);
    if (host < 0 || target < 0) { errno = EBADF; g_errno_cell = EBADF; return -1; }
    if (source == target) return fcntl(host, F_GETFD) >= 0 ? target : -1;
    io_descriptor *existing = io_find(target), *original = io_find(source);
    int destination = existing && existing->host >= 0 ? existing->host : target;
    int copy = target <= 2 && destination <= 2 ? fcntl(host, F_DUPFD_CLOEXEC, 3) : dup2(host, destination);
    if (copy < 0) { g_errno_cell = io_errno(errno); return -1; }
    if (io_track(target, copy, original ? original->description : NULL, 0)) {
        close(copy); errno = ENOMEM; g_errno_cell = ENOMEM; return -1;
    }
    return target;
}
static ssize_t io_write_no_signal(int fd, const void *bytes, size_t length)
{
    sigset_t mask, previous, pending;
    sigemptyset(&mask); sigaddset(&mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &mask, &previous);
    sigpending(&pending);
    int already_pending = sigismember(&pending, SIGPIPE);
    ssize_t result = write(fd, bytes, length);
    int error = errno;
    if (result < 0 && error == EPIPE && !already_pending) {
        struct timespec zero = {0};
        sigtimedwait(&mask, NULL, &zero);
    }
    pthread_sigmask(SIG_SETMASK, &previous, NULL);
    errno = error;
    return result;
}
static ssize_t io_transfer(int guest, void *buffer, size_t length, int writing)
{
    if (!length) return 0;
    if (!buffer) { errno = EFAULT; g_errno_cell = EFAULT; return -1; }
    int host = io_host(guest);
    io_descriptor *d = io_find(guest);
    int polled = d && d->description->polled;
    int nonblocking = d && (d->description->flags & O_NONBLOCK);
    int snapshot = host >= 0 ? fcntl(host, F_DUPFD_CLOEXEC, 3) : -1;
    if (snapshot < 0) { errno = EBADF; g_errno_cell = EBADF; return -1; }
    size_t amount = length > 65536 ? 65536 : length;
    unsigned char *bytes = malloc(amount);
    if (!bytes) { close(snapshot); errno = ENOMEM; g_errno_cell = ENOMEM; return -1; }
    if (writing) memcpy(bytes, buffer, amount);
    unsigned depth = chm_guest_suspend();
    ssize_t result = -1;
    int error = 0;
    for (;;) {
        if (chm_dispatch_worker() && chm_dispatch_stopping()) { error = ECANCELED; break; }
        if (polled && !nonblocking) {
            struct pollfd p = {snapshot, writing ? POLLOUT : POLLIN, 0};
            int ready = poll(&p, 1, 20);
            if (!ready) continue;
            if (ready < 0) { if (errno == EINTR) continue; error = errno; break; }
        }
        result = writing ? io_write_no_signal(snapshot, bytes, amount) : read(snapshot, bytes, amount);
        error = errno;
        if (polled && !nonblocking && result < 0 && (error == EAGAIN || error == EINTR)) continue;
        break;
    }
    close(snapshot);
    chm_guest_resume(depth);
    if (!writing && result > 0) memcpy(buffer, bytes, (size_t)result);
    free(bytes);
    if (result < 0) g_errno_cell = io_errno(error);
    errno = error;
    return result;
}
static ssize_t io_read(int fd, void *bytes, size_t length) { return io_transfer(fd, bytes, length, 0); }
static ssize_t io_write(int fd, const void *bytes, size_t length) { return io_transfer(fd, (void *)bytes, length, 1); }
static int io_fcntl(int fd, int command, void *argument)
{
    io_descriptor *d = io_find(fd);
    int host = io_host(fd);
    if (d && d->description->polled && command == F_GETFL) {
        int flags = d->description->flags;
        return (flags & ~O_NONBLOCK) | ((flags & O_NONBLOCK) ? 4 : 0);
    }
    if (d && d->description->polled && command == F_SETFL) {
        int flags = (int)(intptr_t)argument;
        d->description->flags = (flags & ~4) | ((flags & 4) ? O_NONBLOCK : 0);
        return fcntl(host, F_SETFL, d->description->flags | O_NONBLOCK);
    }
    return fcntl(host, command, argument);
}
static void io_descriptors_reset(void)
{
    while (io_descriptors) {
        io_descriptor *d = io_descriptors; io_descriptors = d->next;
        if (d->stream) fclose(d->stream);
        else if (d->host >= 0) close(d->host);
        if (!--d->description->references) free(d->description);
        free(d);
    }
}
static unsigned int io_sleep(unsigned int seconds)
{
    unsigned depth = chm_guest_suspend();
    struct timespec start, now, pause = {0, 20000000};
    clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t target = (uint64_t)start.tv_sec * 1000000000u + start.tv_nsec + (uint64_t)seconds * 1000000000u;
    uint64_t current;
    do {
        clock_gettime(CLOCK_MONOTONIC, &now);
        current = (uint64_t)now.tv_sec * 1000000000u + now.tv_nsec;
        if (current >= target || (chm_dispatch_worker() && chm_dispatch_stopping())) break;
        nanosleep(&pause, NULL);
    } while (1);
    chm_guest_resume(depth);
    return current < target ? (unsigned int)((target - current + 999999999u) / 1000000000u) : 0;
}

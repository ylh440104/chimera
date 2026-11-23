#include <sys/sysmacros.h>

enum {
    D_OPEN_NONBLOCK = 0x4, D_OPEN_APPEND = 0x8, D_OPEN_SYNC = 0x80,
    D_OPEN_NOFOLLOW = 0x100, D_OPEN_CREATE = 0x200, D_OPEN_TRUNC = 0x400,
    D_OPEN_EXCLUSIVE = 0x800, D_OPEN_NOCTTY = 0x20000,
    D_OPEN_DIRECTORY = 0x100000, D_OPEN_CLOEXEC = 0x1000000
};

extern void chm_open_entry(void);

typedef struct {
    uint64_t inode, seek_offset;
    uint16_t record_length, name_length;
    uint8_t type;
    char name[1024];
} darwin_file_entry;

typedef struct file_directory {
    DIR *host;
    struct file_directory *next;
    darwin_file_entry entry;
} file_directory;

static file_directory *g_directories;
static pthread_mutex_t directory_lock = PTHREAD_MUTEX_INITIALIZER;

_Static_assert(offsetof(darwin_file_entry, record_length) == 16 &&
               offsetof(darwin_file_entry, name_length) == 18 &&
               offsetof(darwin_file_entry, type) == 20 &&
               offsetof(darwin_file_entry, name) == 21, "Darwin arm64 dirent layout");

static int file_failure(int error)
{
    errno = error;
    g_errno_cell = io_errno(error);
    return -1;
}

int chm_open_list(const char *path, int flags, const int *optional_mode)
{
    static const struct { int guest, host; } bits[] = {
        {D_OPEN_NONBLOCK, O_NONBLOCK}, {D_OPEN_APPEND, O_APPEND}, {D_OPEN_SYNC, O_SYNC},
        {D_OPEN_NOFOLLOW, O_NOFOLLOW}, {D_OPEN_CREATE, O_CREAT}, {D_OPEN_TRUNC, O_TRUNC},
        {D_OPEN_EXCLUSIVE, O_EXCL}, {D_OPEN_NOCTTY, O_NOCTTY},
        {D_OPEN_DIRECTORY, O_DIRECTORY}, {D_OPEN_CLOEXEC, O_CLOEXEC}
    };
    int host_flags = flags & 3;
    if (host_flags == 3) return file_failure(EINVAL);
    int remaining = flags & ~3;
    for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); i++) {
        if (remaining & bits[i].guest) host_flags |= bits[i].host;
        remaining &= ~bits[i].guest;
    }
    if (remaining) return file_failure(ENOTSUP);
    char mapped[2048];
    int writes = (host_flags & (O_ACCMODE | O_CREAT | O_TRUNC)) != 0;
    int ok = writes ? chm_foundation_map_mutable(path, mapped, sizeof(mapped)) :
                      chm_foundation_map_path(path, mapped, sizeof(mapped));
    if (!ok) return file_failure(errno);
    int fd = (host_flags & O_CREAT) ? open(mapped, host_flags, (mode_t)(*optional_mode & 07777)) : open(mapped, host_flags);
    if (fd < 0) return file_failure(errno);
    pthread_mutex_lock(&io_lock);
    int tracked = io_track(fd, fd, NULL, 0);
    pthread_mutex_unlock(&io_lock);
    if (tracked) {
        close(fd);
        return file_failure(ENOMEM);
    }
    return fd;
}

typedef struct { int64_t seconds, nanos; } darwin_file_time;
typedef struct {
    int32_t device;
    uint16_t mode, links;
    uint64_t inode;
    uint32_t uid, gid;
    int32_t special_device;
    uint32_t padding;
    darwin_file_time access_time, modify_time, change_time, birth_time;
    int64_t size, blocks;
    int32_t block_size;
    uint32_t flags, generation;
    int32_t spare;
    int64_t reserved[2];
} darwin_file_stat;

_Static_assert(sizeof(darwin_file_stat) == 144 && offsetof(darwin_file_stat, mode) == 4 &&
               offsetof(darwin_file_stat, inode) == 8 && offsetof(darwin_file_stat, access_time) == 32 &&
               offsetof(darwin_file_stat, size) == 96, "Darwin arm64 stat layout");

static darwin_file_time file_time(struct timespec t)
{
    return (darwin_file_time){t.tv_sec, t.tv_nsec};
}

static int32_t file_device(dev_t dev)
{
    return (int32_t)(((uint32_t)major(dev) << 24) | ((uint32_t)minor(dev) & 0x00ffffffu));
}

static int export_file_stat(const struct stat *host, darwin_file_stat *out)
{
    if ((uint64_t)host->st_nlink > UINT16_MAX || (uint64_t)host->st_blksize > INT32_MAX)
        return file_failure(EOVERFLOW);
    *out = (darwin_file_stat){
        .device = file_device(host->st_dev), .mode = (uint16_t)host->st_mode,
        .links = (uint16_t)host->st_nlink, .inode = host->st_ino, .uid = host->st_uid,
        .gid = host->st_gid, .special_device = file_device(host->st_rdev),
        .access_time = file_time(host->st_atim), .modify_time = file_time(host->st_mtim),
        .change_time = file_time(host->st_ctim), .size = host->st_size,
        .blocks = host->st_blocks, .block_size = (int32_t)host->st_blksize
    };
    return 0;
}

static int file_stat_path(const char *path, darwin_file_stat *out)
{
    struct stat st;
    char mapped[2048];
    if (!out) return file_failure(EFAULT);
    if (!chm_foundation_map_path(path, mapped, sizeof(mapped)) || stat(mapped, &st) < 0)
        return file_failure(errno);
    return export_file_stat(&st, out);
}

static int file_stat_fd(int fd, darwin_file_stat *out)
{
    struct stat st;
    if (!out) return file_failure(EFAULT);
    if (chm_io_file_status(fd, &st) < 0) return file_failure(errno);
    return export_file_stat(&st, out);
}

static int file_access(const char *path, int mode)
{
    char mapped[2048];
    int ok = (mode & W_OK) ? chm_foundation_map_mutable(path, mapped, sizeof(mapped)) :
                             chm_foundation_map_path(path, mapped, sizeof(mapped));
    if (!ok || access(mapped, mode)) return file_failure(errno);
    return 0;
}

static int file_unlink(const char *path)
{
    char mapped[2048];
    if (!chm_foundation_map_mutable(path, mapped, sizeof(mapped)) || unlink(mapped)) return file_failure(errno);
    return 0;
}

static int file_remove(const char *path)
{
    char mapped[2048];
    if (!chm_foundation_map_mutable(path, mapped, sizeof(mapped)) || remove(mapped)) return file_failure(errno);
    return 0;
}

static DIR *file_opendir(const char *path)
{
    char mapped[2048];
    DIR *dir = chm_foundation_map_path(path, mapped, sizeof(mapped)) ? opendir(mapped) : NULL;
    if (!dir) { file_failure(errno); return NULL; }
    pthread_mutex_lock(&directory_lock);
    file_directory *d = calloc(1, sizeof(*d));
    if (d) { d->host = dir; d->next = g_directories; g_directories = d; }
    pthread_mutex_unlock(&directory_lock);
    if (!d) { closedir(dir); file_failure(ENOMEM); return NULL; }
    return dir;
}

static int file_closedir(DIR *dir)
{
    if (!dir) return file_failure(EBADF);
    pthread_mutex_lock(&directory_lock);
    file_directory **link = &g_directories;
    while (*link && (*link)->host != dir) link = &(*link)->next;
    file_directory *d = *link;
    if (d) *link = d->next;
    pthread_mutex_unlock(&directory_lock);
    if (!d) return file_failure(EBADF);
    free(d);
    return closedir(dir) ? file_failure(errno) : 0;
}

static darwin_file_entry *file_readdir(DIR *dir)
{
    pthread_mutex_lock(&directory_lock);
    file_directory *d = g_directories;
    while (d && d->host != dir) d = d->next;
    darwin_file_entry *entry = NULL;
    int error = EBADF;
    if (d) {
        errno = 0;
        struct dirent *host = readdir(dir);
        error = errno;
        if (host) {
            size_t n = strlen(host->d_name);
            if (n >= sizeof(d->entry.name)) {
                error = ENAMETOOLONG;
            } else {
                entry = &d->entry;
                entry->inode = host->d_ino;
                entry->seek_offset = (uint64_t)host->d_off;
                entry->name_length = (uint16_t)n;
                entry->type = host->d_type;
                entry->record_length = (uint16_t)((offsetof(darwin_file_entry, name) + n + 1 + 3) & ~(size_t)3);
                memcpy(entry->name, host->d_name, n + 1);
            }
        }
    }
    pthread_mutex_unlock(&directory_lock);
    if (!entry && error) file_failure(error);
    return entry;
}

static void file_directories_reset(void)
{
    pthread_mutex_lock(&directory_lock);
    while (g_directories) {
        file_directory *d = g_directories;
        g_directories = d->next;
        closedir(d->host);
        free(d);
    }
    pthread_mutex_unlock(&directory_lock);
}

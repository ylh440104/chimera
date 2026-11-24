#include <limits.h>

typedef struct fo_stream {
    void *object, *delegate, *error;
    int fd, status, input, memory, append, queued;
    unsigned events, scheduled;
    size_t offset;
    fo_buffer buffer;
    char path[2048];
    struct fo_stream *next;
} fo_stream;

static fo_stream *streams;
static void *main_run_loop(void);

static fo_stream *stream_find(void *object)
{
    for (fo_stream *s = streams; s; s = s->next) if (s->object == object) return s;
    return NULL;
}

static void stream_deliver(void *context)
{
    fo_stream *s = context;
    unsigned events = s->events;
    s->events = 0;
    s->queued = 0;
    for (unsigned event = 1; event <= 16; event <<= 1) {
        if (!s->scheduled || s->status == 6) break;
        if (!(events & event) || (event == 2 && s->status != 2) || (event == 4 && s->status != 2)) continue;
        void *delegate = s->delegate ? s->delegate : s->object;
        void (*callback)(void *, const char *, void *, unsigned long) = chm_objc_find_imp(delegate, "stream:handleEvent:");
        if (callback) callback(delegate, "stream:handleEvent:", s->object, event);
    }
}

static void stream_event(fo_stream *s, unsigned event)
{
    if (!s->scheduled || s->status == 6) return;
    s->events |= event;
    if (s->queued) return;
    void *(*main_queue)(void) = chm_dispatch_lookup("_dispatch_get_main_queue");
    void (*enqueue)(void *, void *, void (*)(void *)) = chm_dispatch_lookup("_dispatch_async_f");
    if (main_queue && enqueue && !chm_dispatch_stopping()) {
        s->queued = 1;
        enqueue(main_queue(), s, stream_deliver);
    }
}

static void stream_failure(fo_stream *s, int code)
{
    s->error = make_error("NSPOSIXErrorDomain", code, strerror(code));
    s->status = 7;
    stream_event(s, 8);
}

static fo_stream *stream_create(void *self, int input)
{
    if (!self || stream_find(self)) return NULL;
    fo_stream *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->object = self;
    s->fd = -1;
    s->input = input;
    s->next = streams;
    streams = s;
    return s;
}

static void *stream_init_data(void *self, const char *sel, void *data)
{
    (void)sel;
    if (!fo_is_data(data)) return NULL;
    fo_data *bytes = data;
    if (bytes->length < 0 || bytes->length > 32 * 1024 * 1024 || (bytes->length && !bytes->bytes)) return NULL;
    fo_stream *s = stream_create(self, 1);
    if (!s) return NULL;
    s->memory = 1;
    if (bytes->length && !buffer_add(&s->buffer, bytes->bytes, (size_t)bytes->length)) { stream_failure(s, ENOMEM); return NULL; }
    return self;
}

static void *stream_init_path(void *self, void *path, int input, int append)
{
    if (!path || !*fo_bytes(path)) return NULL;
    char mapped[2048];
    int ok = input ? chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)) : chm_foundation_map_path_write(fo_bytes(path), mapped, sizeof(mapped));
    if (!ok) return NULL;
    fo_stream *s = stream_create(self, input);
    if (!s) return NULL;
    strcpy(s->path, mapped);
    s->append = append;
    return self;
}

static void *stream_init(void *self, const char *sel, void *path, signed char append)
{
    (void)sel;
    return stream_init_path(self, path, 0, append);
}

static void *stream_init_file(void *self, const char *sel, void *path)
{
    (void)sel;
    return stream_init_path(self, path, 1, 0);
}

static void *stream_init_url(void *self, const char *sel, void *url)
{
    net_object *entry = net_find(url);
    if (!entry || !entry->address || strncmp(entry->address, "file://", 7)) return NULL;
    return stream_init_file(self, sel, fo_string_new(entry->address + 7));
}

static void *stream_input(void *cls, const char *sel, void *value)
{
    void *object = chm_objc_send0(cls, "alloc");
    void *result = !strcmp(sel, "inputStreamWithData:") ? stream_init_data(object, sel, value) :
        !strcmp(sel, "inputStreamWithURL:") ? stream_init_url(object, sel, value) : stream_init_file(object, sel, value);
    if (!result && !stream_find(object)) free(object);
    return result;
}

static void *stream_memory(void *self, const char *sel)
{
    (void)sel;
    fo_stream *s = stream_create(self, 0);
    if (s) s->memory = 1;
    return s ? self : NULL;
}

static void *stream_memory_new(void *cls, const char *sel)
{
    void *object = chm_objc_send0(cls, "alloc");
    if (stream_memory(object, sel)) return object;
    free(object);
    return NULL;
}

static void *stream_output(void *cls, const char *sel, void *path, signed char append)
{
    void *object = chm_objc_send0(cls, "alloc");
    void *result = stream_init(object, sel, path, append);
    if (!result && !stream_find(object)) free(object);
    return result;
}

static void stream_open(void *self, const char *sel)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    if (!s || s->status) return;
    if (!s->memory) {
        int flags = s->input ? O_RDONLY : O_WRONLY | O_CREAT | (s->append ? O_APPEND : O_TRUNC);
        s->fd = open(s->path, flags | O_CLOEXEC | O_NONBLOCK, 0600);
        if (s->fd < 0) { stream_failure(s, errno); return; }
        struct stat st;
        int code = fstat(s->fd, &st) ? errno : !S_ISREG(st.st_mode) ? EINVAL : 0;
        if (code) { close(s->fd); s->fd = -1; stream_failure(s, code); return; }
    }
    s->status = 2;
    stream_event(s, 1 | (s->input ? 2 : 4));
}

static void stream_close(void *self, const char *sel)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    if (!s) return;
    if (s->fd >= 0) close(s->fd);
    s->fd = -1;
    s->status = 6;
    s->events = 0;
}

static long stream_status(void *self, const char *sel)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    return s ? s->status : 0;
}

static void *stream_error(void *self, const char *sel)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    return s ? s->error : NULL;
}

static long stream_read(void *self, const char *sel, void *bytes, unsigned long length)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    if (!s || !s->input || (s->status != 2 && s->status != 5)) return -1;
    if (!length) return 0;
    if (!bytes || length > LONG_MAX) { stream_failure(s, EINVAL); return -1; }
    if (s->status == 5) return 0;
    ssize_t count;
    if (s->memory) {
        size_t left = s->buffer.size - s->offset;
        size_t take = length < left ? length : left;
        if (take) memcpy(bytes, s->buffer.data + s->offset, take);
        s->offset += take;
        count = (ssize_t)take;
    } else {
        do { count = read(s->fd, bytes, length); } while (count < 0 && errno == EINTR);
    }
    if (count < 0) stream_failure(s, errno);
    else if (!count || (s->memory && s->offset == s->buffer.size)) { s->status = 5; stream_event(s, 16); }
    else stream_event(s, 2);
    return count;
}

static long stream_write(void *self, const char *sel, const void *bytes, unsigned long length)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    if (!s || s->input || s->status != 2) return -1;
    if ((!bytes && length) || length > LONG_MAX) { stream_failure(s, EINVAL); return -1; }
    if (!length) return 0;
    ssize_t count;
    if (s->memory) {
        if (length > 32 * 1024 * 1024 - s->buffer.size || !buffer_add(&s->buffer, bytes, length)) { stream_failure(s, ENOMEM); return -1; }
        count = (ssize_t)length;
    } else do { count = write(s->fd, bytes, length); } while (count < 0 && errno == EINTR);
    if (count < 0) stream_failure(s, errno);
    else if (count) stream_event(s, 4);
    return count;
}

static signed char stream_available(void *self, const char *sel)
{
    fo_stream *s = stream_find(self);
    int input = !strcmp(sel, "hasBytesAvailable");
    return s && s->status == 2 && s->input == input && (!input || !s->memory || s->offset < s->buffer.size);
}

static signed char stream_buffer(void *self, const char *sel, void **bytes, unsigned long *length)
{
    (void)sel;
    if (bytes) *bytes = NULL;
    if (length) *length = 0;
    fo_stream *s = stream_find(self);
    if (!s || !s->input || !s->memory || (s->status != 2 && s->status != 5) || !bytes || !length) return 0;
    *bytes = s->buffer.data ? s->buffer.data + s->offset : NULL;
    *length = s->buffer.size - s->offset;
    return 1;
}

static void *stream_property(void *self, const char *sel, void *key)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    if (!s) return NULL;
    const char *name = fo_bytes(key);
    if (!strcmp(name, "NSStreamDataWrittenToMemoryStreamKey") && s->memory && !s->input) return fo_data_new(s->buffer.data, (int64_t)s->buffer.size);
    if (!strcmp(name, "NSStreamFileCurrentOffsetKey")) {
        int64_t offset = s->memory ? (int64_t)s->offset : s->fd >= 0 ? lseek(s->fd, 0, SEEK_CUR) : -1;
        if (offset >= 0) return n_signed(NULL, NULL, offset);
    }
    return NULL;
}

static signed char stream_set_property(void *self, const char *sel, void *value, void *key)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    if (!s || (s->status != 2 && s->status != 5) || !fo_is_number(value) || strcmp(fo_bytes(key), "NSStreamFileCurrentOffsetKey")) return 0;
    int64_t offset = o_integer(value);
    if (offset < 0) return 0;
    if (s->memory) {
        if (!s->input || (uint64_t)offset > s->buffer.size) return 0;
        s->offset = (size_t)offset;
    } else if (lseek(s->fd, offset, SEEK_SET) < 0) return 0;
    s->status = 2;
    s->events &= ~16u;
    stream_event(s, s->input ? 2 : 4);
    return 1;
}

static void stream_delegate_set(void *self, const char *sel, void *delegate)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    if (s) s->delegate = delegate;
}

static void *stream_delegate(void *self, const char *sel)
{
    (void)sel;
    fo_stream *s = stream_find(self);
    return s && s->delegate ? s->delegate : self;
}

static void stream_schedule(void *self, const char *sel, void *loop, void *mode)
{
    fo_stream *s = stream_find(self);
    if (!s || loop != main_run_loop()) return;
    const char *name = fo_bytes(mode);
    unsigned flag = !strcmp(name, "kCFRunLoopDefaultMode") || !strcmp(name, "NSDefaultRunLoopMode") ? 1 :
        !strcmp(name, "kCFRunLoopCommonModes") || !strcmp(name, "NSRunLoopCommonModes") ? 2 : 0;
    if (!strcmp(sel, "removeFromRunLoop:forMode:")) s->scheduled &= ~flag;
    else {
        s->scheduled |= flag;
        if (s->status == 2) stream_event(s, s->input ? 2 : 4);
        else if (s->status == 5) stream_event(s, 16);
        else if (s->status == 7) stream_event(s, 8);
    }
    if (!s->scheduled) s->events = 0;
}

static void streams_reset(void)
{
    while (streams) {
        fo_stream *next = streams->next;
        if (streams->fd >= 0) close(streams->fd);
        free(streams->buffer.data);
        free(streams);
        streams = next;
    }
}

static const chm_host_method stream_methods[] = {
    {"NSInputStream", "initWithData:", "@@:@", stream_init_data, 0},
    {"NSInputStream", "initWithFileAtPath:", "@@:@", stream_init_file, 0},
    {"NSInputStream", "initWithURL:", "@@:@", stream_init_url, 0},
    {"NSInputStream", "inputStreamWithData:", "@@:@", stream_input, 1},
    {"NSInputStream", "inputStreamWithFileAtPath:", "@@:@", stream_input, 1},
    {"NSInputStream", "inputStreamWithURL:", "@@:@", stream_input, 1},
    {"NSInputStream", "read:maxLength:", "q@:^vQ", stream_read, 0},
    {"NSInputStream", "getBuffer:length:", "B@:^v^Q", stream_buffer, 0},
    {"NSInputStream", "hasBytesAvailable", "B@:", stream_available, 0},
    {"NSOutputStream", "initToFileAtPath:append:", "@@:@B", stream_init, 0},
    {"NSOutputStream", "outputStreamToFileAtPath:append:", "@@:@B", stream_output, 1},
    {"NSOutputStream", "initToMemory", "@@:", stream_memory, 0},
    {"NSOutputStream", "outputStreamToMemory", "@@:", stream_memory_new, 1},
    {"NSOutputStream", "write:maxLength:", "q@:^vQ", stream_write, 0},
    {"NSOutputStream", "hasSpaceAvailable", "B@:", stream_available, 0},
    {"NSStream", "open", "v@:", stream_open, 0},
    {"NSStream", "close", "v@:", stream_close, 0},
    {"NSStream", "streamStatus", "Q@:", stream_status, 0},
    {"NSStream", "streamError", "@@:", stream_error, 0},
    {"NSStream", "delegate", "@@:", stream_delegate, 0},
    {"NSStream", "setDelegate:", "v@:@", stream_delegate_set, 0},
    {"NSStream", "propertyForKey:", "@@:@", stream_property, 0},
    {"NSStream", "setProperty:forKey:", "B@:@@", stream_set_property, 0},
    {"NSStream", "scheduleInRunLoop:forMode:", "v@:@@", stream_schedule, 0},
    {"NSStream", "removeFromRunLoop:forMode:", "v@:@@", stream_schedule, 0}
};

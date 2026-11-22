#include <time.h>

static uint64_t queue_clock(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

typedef struct {
    void *block;
    void (*function)(void *);
    void *context;
    uint64_t when;
    void *group;
} queue_job;

typedef struct queue_object {
    queue_job jobs[1024];
    size_t count;
    struct queue_object *next;
} queue_object;

typedef struct queue_group {
    long count;
    struct queue_group *next;
} queue_group;

static queue_object main_queue, global_queue, *extra_queues;
static queue_group *queue_groups;
static char concurrent_attribute;

static queue_object *queue_for(void *object)
{
    if (object == &main_queue) return &main_queue;
    if (object == &global_queue) return &global_queue;
    for (queue_object *q = extra_queues; q; q = q->next) if (q == object) return q;
    return NULL;
}

static int queue_put(void *queue, void *block, void (*function)(void *), void *context, uint64_t when, void *group)
{
    queue_object *q = queue_for(queue);
    if (!q || q->count == 1024 || (!block && !function)) return -1;
    void *copy = chm_block_copy(block);
    if (block && !copy) return -1;
    q->jobs[q->count++] = (queue_job){copy, function, context, when, group};
    return 0;
}

void chm_dispatch_init(void) {}

void chm_dispatch_reset(void)
{
    queue_object *q = &main_queue;
    while (q) {
        for (size_t i = 0; i < q->count; i++) chm_block_release(q->jobs[i].block);
        q->count = 0;
        if (q == &main_queue) q = &global_queue;
        else if (q == &global_queue) q = extra_queues;
        else { queue_object *next = q->next; free(q); q = next; }
    }
    extra_queues = NULL;
    while (queue_groups) { queue_group *next = queue_groups->next; free(queue_groups); queue_groups = next; }
}

static void queue_async(void *q, void *b) { queue_put(q, b, NULL, NULL, 0, NULL); }
static void queue_async_f(void *q, void *context, void (*function)(void *)) { queue_put(q, NULL, function, context, 0, NULL); }
static void queue_after(uint64_t when, void *q, void *b) { queue_put(q, b, NULL, NULL, when, NULL); }
static void queue_after_f(uint64_t when, void *q, void *context, void (*function)(void *)) { queue_put(q, NULL, function, context, when, NULL); }

static void invoke_queue_block(void *object)
{
    struct { void *isa; int flags, reserved; void (*invoke)(void *); } *b = object;
    if (b && b->invoke) b->invoke(b);
}

static void queue_sync(void *q, void *b) { (void)q; invoke_queue_block(b); }
static void queue_sync_f(void *q, void *context, void (*function)(void *)) { (void)q; if (function) function(context); }
static void *queue_main(void) { return &main_queue; }
static void *queue_global(long priority, unsigned long flags) { (void)priority; (void)flags; return &global_queue; }
static void *queue_create(const char *label, void *attribute)
{
    (void)label; (void)attribute;
    queue_object *q = calloc(1, sizeof(*q));
    if (q) { q->next = extra_queues; extra_queues = q; }
    return q;
}

static uint64_t queue_time(uint64_t when, int64_t delta)
{
    if (when == UINT64_MAX) return when;
    if (!when) when = queue_clock();
    if (delta >= 0) return (uint64_t)delta > UINT64_MAX - when ? UINT64_MAX : when + (uint64_t)delta;
    uint64_t magnitude = (uint64_t)(-(delta + 1)) + 1;
    return magnitude >= when ? 1 : when - magnitude;
}

static void queue_once(long *predicate, void *block)
{
    if (predicate && !*predicate) { *predicate = 1; invoke_queue_block(block); *predicate = -1; }
}
static void queue_once_f(long *predicate, void *context, void (*function)(void *))
{
    if (predicate && !*predicate) { *predicate = 1; if (function) function(context); *predicate = -1; }
}

static void *group_create(void)
{
    queue_group *g = calloc(1, sizeof(*g));
    if (g) { g->next = queue_groups; queue_groups = g; }
    return g;
}
static queue_group *group_find(void *object)
{
    for (queue_group *g = queue_groups; g; g = g->next) if (g == object) return g;
    return NULL;
}
static void group_enter(void *object) { queue_group *g = group_find(object); if (g) g->count++; }
static void group_leave(void *object) { queue_group *g = group_find(object); if (g && g->count > 0) g->count--; }
static void group_notify(void *g, void *q, void *b) { if (group_find(g)) queue_put(q, b, NULL, NULL, 0, g); }

static int queue_take(queue_object *q, queue_job *job)
{
    uint64_t now = queue_clock();
    for (size_t i = 0; i < q->count; i++) {
        queue_group *g = group_find(q->jobs[i].group);
        if (q->jobs[i].when > now || (g && g->count)) continue;
        *job = q->jobs[i];
        memmove(q->jobs + i, q->jobs + i + 1, (--q->count - i) * sizeof(*q->jobs));
        return 1;
    }
    return 0;
}

size_t chm_dispatch_pump(chm_session *s)
{
    size_t runs = 0;
    for (int progress = 1; progress && runs < 256;) {
        progress = 0;
        for (queue_object *q = &global_queue; q && runs < 256;) {
            queue_job job;
            if (queue_take(q, &job)) {
                if (job.function) job.function(job.context);
                else invoke_queue_block(job.block);
                chm_block_release(job.block);
                progress = 1; runs++;
            }
            q = q == &global_queue ? &main_queue : q == &main_queue ? extra_queues : q->next;
        }
    }
    if (s && runs) chm_session_log(s, "dispatch: ran %zu queued callbacks", runs);
    return runs;
}

static long group_wait(void *object, uint64_t when)
{
    queue_group *g = group_find(object);
    if (!g) return -1;
    while (g->count && when && queue_clock() < when) {
        if (!chm_dispatch_pump(NULL)) break;
    }
    return g->count ? 1 : 0;
}

size_t chm_dispatch_pending(void)
{
    size_t n = main_queue.count + global_queue.count;
    for (queue_object *q = extra_queues; q; q = q->next) n += q->count;
    return n;
}

void *chm_dispatch_lookup(const char *name)
{
    static const struct { const char *name; void *address; } entries[] = {
        {"_dispatch_async", queue_async}, {"_dispatch_async_f", queue_async_f},
        {"_dispatch_sync", queue_sync}, {"_dispatch_sync_f", queue_sync_f},
        {"_dispatch_after", queue_after}, {"_dispatch_after_f", queue_after_f},
        {"_dispatch_once", queue_once}, {"_dispatch_once_f", queue_once_f},
        {"_dispatch_get_main_queue", queue_main}, {"_dispatch_get_global_queue", queue_global},
        {"_dispatch_queue_create", queue_create}, {"_dispatch_time", queue_time},
        {"_dispatch_group_create", group_create}, {"_dispatch_group_enter", group_enter},
        {"_dispatch_group_leave", group_leave}, {"_dispatch_group_notify", group_notify},
        {"_dispatch_group_wait", group_wait}, {"__dispatch_main_q", &main_queue},
        {"__dispatch_queue_attr_concurrent", &concurrent_attribute}, {NULL, NULL}
    };
    for (int i = 0; name && entries[i].name; i++) if (!strcmp(name, entries[i].name)) return entries[i].address;
    return NULL;
}
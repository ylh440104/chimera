#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <limits.h>

typedef struct { int done; } queue_waiter;
typedef struct {
    void *block;
    void (*function)(void *);
    void *context;
    uint64_t when;
    void *group;
    queue_waiter *waiter;
    void *completion_group;
    uint64_t group_generation;
    int group_pending, barrier;
} queue_job;
typedef struct queue_object {
    queue_job jobs[1024];
    size_t count, active;
    int concurrent, barrier_active;
    struct queue_object *next;
} queue_object;
typedef struct queue_group { long count; uint64_t generation; struct queue_group *next; } queue_group;
typedef struct semaphore_waiter { int signaled; struct semaphore_waiter *next; } semaphore_waiter;
typedef struct queue_semaphore {
    long value;
    semaphore_waiter *first, *last;
    struct queue_semaphore *next;
} queue_semaphore;

static queue_object main_queue, global_queue = {.concurrent = 1}, *extra_queues;
static queue_group *queue_groups;
static queue_semaphore *queue_semaphores;
static char concurrent_attribute;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_changed = PTHREAD_COND_INITIALIZER;
static pthread_t workers[4];
static size_t worker_count, worker_completed;
static chm_session *queue_session;
static atomic_int queue_stopping;
static _Thread_local int worker_thread;
static _Thread_local queue_object *current_queue;

int chm_dispatch_worker(void) { return worker_thread; }
int chm_dispatch_stopping(void) { return atomic_load(&queue_stopping); }
static uint64_t queue_clock(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
static queue_object *queue_for(void *object)
{
    if (object == &main_queue) return &main_queue;
    if (object == &global_queue) return &global_queue;
    for (queue_object *q = extra_queues; q; q = q->next) if (q == object) return q;
    return NULL;
}
static queue_group *group_find(void *object)
{
    for (queue_group *g = queue_groups; g; g = g->next) if (g == object) return g;
    return NULL;
}
static void group_complete_locked(queue_group *g)
{
    if (g && g->count > 0 && --g->count == 0) g->generation++;
}

static int queue_take(queue_object *q, queue_job *job)
{
    if (q->barrier_active || (!q->concurrent && q->active)) return 0;
    uint64_t now = queue_clock();
    for (size_t i = 0; i < q->count; i++) {
        queue_job *candidate = &q->jobs[i];
        queue_group *g = group_find(candidate->group);
        if (candidate->when > now || (g && candidate->group_pending && g->generation == candidate->group_generation)) continue;
        if (candidate->barrier && q->active) return 0;
        *job = *candidate;
        q->count--;
        memmove(q->jobs + i, q->jobs + i + 1, (q->count - i) * sizeof(*q->jobs));
        q->active++;
        q->barrier_active = job->barrier;
        return 1;
    }
    return 0;
}
static void queue_wait_tick(void)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(&queue_changed, &queue_lock, &deadline);
}
static int invoke_job(void *context)
{
    queue_job *job = context;
    if (chm_dispatch_stopping() || !queue_session || queue_session->exit_code == -2) return 0;
    if (job->function) job->function(job->context);
    else {
        struct { void *isa; int flags, reserved; void (*invoke)(void *); } *b = job->block;
        if (b && b->invoke) b->invoke(b);
    }
    return 0;
}
static int release_job_block(void *block)
{
    chm_block_release(block);
    return 0;
}

static void finish_job(queue_object *q, queue_job *job, int rc, int background)
{
    if (rc < 0) queue_session->exit_code = -2;
    if (job->block && chm_session_callback(queue_session, release_job_block, job->block) < 0)
        queue_session->exit_code = -2;
    pthread_mutex_lock(&queue_lock);
    q->active--;
    if (job->barrier) q->barrier_active = 0;
    group_complete_locked(group_find(job->completion_group));
    if (job->waiter) job->waiter->done = 1;
    if (background) worker_completed++;
    pthread_cond_broadcast(&queue_changed);
    pthread_mutex_unlock(&queue_lock);
}
static void *queue_worker_main(void *unused)
{
    (void)unused;
    worker_thread = 1;
    for (;;) {
        queue_job job;
        queue_object *selected = NULL;
        pthread_mutex_lock(&queue_lock);
        while (!chm_dispatch_stopping()) {
            for (queue_object *q = &global_queue; q; q = q == &global_queue ? extra_queues : q->next) {
                if (queue_take(q, &job)) { selected = q; break; }
            }
            if (selected) break;
            queue_wait_tick();
        }
        pthread_mutex_unlock(&queue_lock);
        if (!selected) break;
        current_queue = selected;
        int rc = chm_session_callback(queue_session, invoke_job, &job);
        current_queue = NULL;
        finish_job(selected, &job, rc, 1);
    }
    worker_thread = 0;
    return NULL;
}
static void start_workers(void)
{
    pthread_mutex_lock(&queue_lock);
    if (queue_session && !chm_dispatch_stopping()) {
        while (worker_count < 4) {
            if (pthread_create(&workers[worker_count], NULL, queue_worker_main, NULL)) break;
            worker_count++;
        }
    }
    pthread_mutex_unlock(&queue_lock);
}
static int queue_put_options(void *queue, void *block, void (*function)(void *), void *context,
                             uint64_t when, void *group, queue_waiter *waiter, void *completion_group, int barrier)
{
    if ((!block && !function) || chm_dispatch_stopping()) return -1;
    chm_guest_lock();
    void *copy = chm_block_copy(block);
    if (block && !copy) { chm_guest_unlock(); return -1; }
    pthread_mutex_lock(&queue_lock);
    queue_object *q = queue_for(queue);
    queue_group *notify = group_find(group), *complete = group_find(completion_group);
    int ok = q && q->count < 1024 && !chm_dispatch_stopping() &&
             (!group || notify) && (!completion_group || (complete && complete->count < LONG_MAX));
    if (ok) {
        if (complete) complete->count++;
        q->jobs[q->count++] = (queue_job){
            .block = copy, .function = function, .context = context, .when = when,
            .group = group, .waiter = waiter, .completion_group = completion_group,
            .group_generation = notify ? notify->generation : 0,
            .group_pending = notify && notify->count > 0,
            .barrier = barrier && q != &global_queue
        };
    }
    pthread_cond_broadcast(&queue_changed);
    pthread_mutex_unlock(&queue_lock);
    if (!ok) chm_block_release(copy);
    if (ok && q != &main_queue) start_workers();
    chm_guest_unlock();
    return ok ? 0 : -1;
}

static int queue_put(void *queue, void *block, void (*function)(void *), void *context,
                     uint64_t when, void *group, queue_waiter *waiter)
{
    return queue_put_options(queue, block, function, context, when, group, waiter, NULL, 0);
}
int chm_dispatch_submit(void *queue, void *context, void (*function)(void *))
{
    return queue_put(queue, NULL, function, context, 0, NULL, NULL);
}
void chm_dispatch_init(void) {}
void chm_dispatch_set_session(chm_session *session)
{
    pthread_mutex_lock(&queue_lock);
    queue_session = session;
    worker_completed = 0;
    atomic_store(&queue_stopping, 0);
    pthread_mutex_unlock(&queue_lock);
}
void chm_dispatch_stop(void)
{
    pthread_mutex_lock(&queue_lock);
    atomic_store(&queue_stopping, 1);
    for (queue_object *q = &main_queue; q; q = q == &main_queue ? &global_queue : q == &global_queue ? extra_queues : q->next)
        for (size_t i = 0; i < q->count; i++)
            if (q->jobs[i].waiter) { q->jobs[i].waiter->done = 1; q->jobs[i].waiter = NULL; }
    pthread_cond_broadcast(&queue_changed);
    pthread_mutex_unlock(&queue_lock);
}
void chm_dispatch_reset(void)
{
    chm_dispatch_stop();
    unsigned depth = chm_guest_suspend();
    for (size_t i = 0; i < worker_count; i++) pthread_join(workers[i], NULL);
    chm_guest_resume(depth);
    chm_guest_lock();
    for (queue_object *q = &main_queue; q;) {
        for (size_t i = 0; i < q->count; i++) chm_block_release(q->jobs[i].block);
        q->count = q->active = 0;
        q->barrier_active = 0;
        if (q == &main_queue) q = &global_queue;
        else if (q == &global_queue) q = extra_queues;
        else { queue_object *next = q->next; free(q); q = next; }
    }
    extra_queues = NULL;
    while (queue_groups) { queue_group *next = queue_groups->next; free(queue_groups); queue_groups = next; }
    while (queue_semaphores) { queue_semaphore *next = queue_semaphores->next; free(queue_semaphores); queue_semaphores = next; }
    queue_session = NULL; worker_count = worker_completed = 0;
    chm_guest_unlock();
}
static void queue_async(void *q, void *b) { queue_put(q, b, NULL, NULL, 0, NULL, NULL); }
static void queue_async_f(void *q, void *context, void (*function)(void *)) { queue_put(q, NULL, function, context, 0, NULL, NULL); }
static void queue_after(uint64_t when, void *q, void *b) { queue_put(q, b, NULL, NULL, when, NULL, NULL); }
static void queue_after_f(uint64_t when, void *q, void *context, void (*function)(void *)) { queue_put(q, NULL, function, context, when, NULL, NULL); }
static void queue_sync_common(void *q, void *b, void *context, void (*function)(void *))
{
    if ((!worker_thread && !chm_pthread_worker() && q == &main_queue) || q == current_queue) {
        queue_job job = {.block = b, .function = function, .context = context};
        invoke_job(&job);
        return;
    }
    queue_waiter waiter = {0};
    if (queue_put(q, b, function, context, 0, NULL, &waiter)) return;
    start_workers();
    unsigned depth = chm_guest_suspend();
    pthread_mutex_lock(&queue_lock);
    while (!waiter.done) pthread_cond_wait(&queue_changed, &queue_lock);
    pthread_mutex_unlock(&queue_lock);
    chm_guest_resume(depth);
}
static void queue_sync(void *q, void *b) { queue_sync_common(q, b, NULL, NULL); }
static void queue_sync_f(void *q, void *context, void (*function)(void *)) { queue_sync_common(q, NULL, context, function); }
static void *queue_main(void) { return &main_queue; }
static void *queue_global(long priority, unsigned long flags) { (void)priority; (void)flags; return &global_queue; }
static void *queue_create(const char *label, void *attribute)
{
    (void)label;
    queue_object *q = calloc(1, sizeof(*q));
    if (q) {
        q->concurrent = attribute == &concurrent_attribute;
        pthread_mutex_lock(&queue_lock); q->next = extra_queues; extra_queues = q; pthread_mutex_unlock(&queue_lock);
    }
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
    if (predicate && !*predicate) {
        *predicate = 1; queue_job job = {.block = block}; invoke_job(&job); *predicate = -1;
    }
}
static void queue_once_f(long *predicate, void *context, void (*function)(void *))
{
    if (predicate && !*predicate) { *predicate = 1; if (function) function(context); *predicate = -1; }
}
static void *group_create(void)
{
    queue_group *g = calloc(1, sizeof(*g));
    if (g) { pthread_mutex_lock(&queue_lock); g->next = queue_groups; queue_groups = g; pthread_mutex_unlock(&queue_lock); }
    return g;
}
static void group_enter(void *object)
{
    pthread_mutex_lock(&queue_lock); queue_group *g = group_find(object); if (g && g->count < LONG_MAX) g->count++; pthread_mutex_unlock(&queue_lock);
}
static void group_leave(void *object)
{
    pthread_mutex_lock(&queue_lock);
    group_complete_locked(group_find(object));
    pthread_cond_broadcast(&queue_changed); pthread_mutex_unlock(&queue_lock);
}
static void group_notify(void *g, void *q, void *b) { queue_put(q, b, NULL, NULL, 0, g, NULL); }
size_t chm_dispatch_pump(chm_session *session)
{
    if (session && session != queue_session) return 0;
    start_workers();
    queue_object *previous_queue = current_queue;
    current_queue = &main_queue;
    size_t runs = chm_reachability_pump(queue_session);
    current_queue = previous_queue;
    uint64_t deadline = queue_clock() + 8000000;
    while (!chm_dispatch_stopping() && runs < 64 && queue_clock() < deadline) {
        queue_job job;
        pthread_mutex_lock(&queue_lock);
        int found = queue_take(&main_queue, &job);
        pthread_mutex_unlock(&queue_lock);
        if (!found) break;
        queue_object *previous = current_queue; current_queue = &main_queue;
        int rc = chm_session_callback(queue_session, invoke_job, &job);
        current_queue = previous;
        finish_job(&main_queue, &job, rc, 0);
        runs++;
        if (rc < 0) break;
    }
    pthread_mutex_lock(&queue_lock);
    size_t completed = worker_completed; worker_completed = 0;
    pthread_mutex_unlock(&queue_lock);
    if (session && (runs || completed)) chm_session_log(session, "dispatch: main=%zu background-completed=%zu", runs, completed);
    return runs + completed;
}
static long group_wait(void *object, uint64_t when)
{
    start_workers();
    unsigned depth = chm_guest_suspend();
    pthread_mutex_lock(&queue_lock);
    queue_group *g = group_find(object);
    uint64_t generation = g ? g->generation : 0;
    while (g && g->count && g->generation == generation && when && queue_clock() < when && !chm_dispatch_stopping()) queue_wait_tick();
    long result = !g ? -1 : g->generation != generation || !g->count ? 0 : 1;
    pthread_mutex_unlock(&queue_lock);
    chm_guest_resume(depth);
    return result;
}
#include "dispatch_sync.inc"

size_t chm_dispatch_pending(void)
{
    pthread_mutex_lock(&queue_lock);
    size_t n = main_queue.count + main_queue.active + global_queue.count + global_queue.active;
    for (queue_object *q = extra_queues; q; q = q->next) n += q->count + q->active;
    pthread_mutex_unlock(&queue_lock);
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
        {"_dispatch_barrier_async", queue_barrier_async}, {"_dispatch_barrier_async_f", queue_barrier_async_f},
        {"_dispatch_barrier_sync", queue_barrier_sync}, {"_dispatch_barrier_sync_f", queue_barrier_sync_f},
        {"_dispatch_group_async", group_async}, {"_dispatch_group_async_f", group_async_f},
        {"_dispatch_semaphore_create", semaphore_create}, {"_dispatch_semaphore_signal", semaphore_signal},
        {"_dispatch_semaphore_wait", semaphore_wait},
        {"_dispatch_group_create", group_create}, {"_dispatch_group_enter", group_enter},
        {"_dispatch_group_leave", group_leave}, {"_dispatch_group_notify", group_notify},
        {"_dispatch_group_wait", group_wait}, {"__dispatch_main_q", &main_queue},
        {"__dispatch_queue_attr_concurrent", &concurrent_attribute}, {NULL, NULL}
    };
    for (int i = 0; name && entries[i].name; i++) if (!strcmp(name, entries[i].name)) return entries[i].address;
    return NULL;
}

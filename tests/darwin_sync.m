typedef unsigned long Time;
extern char _dispatch_queue_attr_concurrent;
extern void *dispatch_get_main_queue(void);
extern void *dispatch_get_global_queue(long, unsigned long);
extern void *dispatch_queue_create(const char *, void *);
extern void dispatch_async(void *, void (^)(void));
extern void dispatch_barrier_async(void *, void (^)(void));
extern void dispatch_barrier_sync(void *, void (^)(void));
extern void dispatch_barrier_async_f(void *, void *, void (*)(void *));
extern void dispatch_barrier_sync_f(void *, void *, void (*)(void *));
extern void *dispatch_group_create(void);
extern void dispatch_group_enter(void *);
extern void dispatch_group_leave(void *);
extern void dispatch_group_async(void *, void *, void (^)(void));
extern void dispatch_group_async_f(void *, void *, void *, void (*)(void *));
extern void dispatch_group_notify(void *, void *, void (^)(void));
extern long dispatch_group_wait(void *, Time);
extern void *dispatch_semaphore_create(long);
extern long dispatch_semaphore_signal(void *);
extern long dispatch_semaphore_wait(void *, Time);
extern Time dispatch_time(Time, long long);

static int failure, completed, barrier_done, function_count, notified, stopping_started;
static void *reused_group, *empty_group, *shutdown_semaphore, *shutdown_group;
static const Time forever = ~0UL;

static Time deadline(void)
{
    return dispatch_time(0, 3000000000LL);
}

static void function_job(void *context)
{
    function_count += (int)(long)context;
}

static int tokens(void)
{
    void *s = dispatch_semaphore_create(2);
    if (!s || dispatch_semaphore_create(-1)) return 1;
    if (dispatch_semaphore_wait(s, 0) || dispatch_semaphore_wait(s, 0)) return 2;
    if (!dispatch_semaphore_wait(s, 0)) return 3;
    Time start = dispatch_time(0, 0), end = dispatch_time(start, 10000000);
    if (!dispatch_semaphore_wait(s, end) || dispatch_time(0, 0) < end) return 4;
    if (dispatch_semaphore_signal(s) || dispatch_semaphore_wait(s, 0) || !dispatch_semaphore_wait(s, 0)) return 5;
    return 0;
}

static int barriers(void)
{
    void *q = dispatch_queue_create("barrier-test", &_dispatch_queue_attr_concurrent);
    void *g = dispatch_group_create();
    void *started = dispatch_semaphore_create(0), *release = dispatch_semaphore_create(0);
    void *barrier_started = dispatch_semaphore_create(0), *barrier_release = dispatch_semaphore_create(0);
    void *later = dispatch_semaphore_create(0);
    if (!q || !g || !started || !release || !barrier_started || !barrier_release || !later) return 10;
    for (int i = 0; i < 2; i++) {
        dispatch_group_async(g, q, ^{
            dispatch_semaphore_signal(started);
            if (dispatch_semaphore_wait(release, deadline())) failure = 11;
            completed++;
        });
    }
    if (dispatch_semaphore_wait(started, deadline()) || dispatch_semaphore_wait(started, deadline())) return 12;
    if (!dispatch_group_wait(g, 0)) return 13;
    dispatch_barrier_async(q, ^{
        if (completed != 2) failure = 14;
        dispatch_semaphore_signal(barrier_started);
        if (dispatch_semaphore_wait(barrier_release, deadline())) failure = 15;
        barrier_done = 1;
    });
    dispatch_async(q, ^{
        if (!barrier_done) failure = 16;
        dispatch_semaphore_signal(later);
    });
    if (!dispatch_semaphore_wait(later, dispatch_time(0, 10000000))) return 17;
    dispatch_semaphore_signal(release);
    dispatch_semaphore_signal(release);
    if (dispatch_semaphore_wait(barrier_started, deadline()) || dispatch_group_wait(g, 0)) return 18;
    if (!dispatch_semaphore_wait(later, dispatch_time(0, 10000000))) return 19;
    dispatch_semaphore_signal(barrier_release);
    if (dispatch_semaphore_wait(later, deadline())) return 20;
    dispatch_barrier_sync(q, ^{ if (completed != 2 || !barrier_done) failure = 21; });
    dispatch_group_async_f(g, q, (void *)7, function_job);
    if (dispatch_group_wait(g, deadline()) || function_count != 7) return 22;
    dispatch_barrier_async_f(q, (void *)3, function_job);
    dispatch_barrier_sync_f(q, (void *)5, function_job);
    if (function_count != 15) return 23;
    void *serial = dispatch_queue_create("serial-barrier", (void *)0);
    dispatch_barrier_async(serial, ^{ function_count += 2; });
    dispatch_barrier_sync(serial, ^{ if (function_count != 17) failure = 24; });
    void *global = dispatch_get_global_queue(0, 0);
    dispatch_async(global, ^{
        dispatch_semaphore_signal(started);
        if (dispatch_semaphore_wait(release, deadline())) failure = 25;
        dispatch_semaphore_signal(later);
    });
    if (dispatch_semaphore_wait(started, deadline())) return 26;
    dispatch_barrier_sync_f(global, (void *)1, function_job);
    if (function_count != 18) return 27;
    dispatch_semaphore_signal(release);
    if (dispatch_semaphore_wait(later, deadline())) return 28;
    return failure;
}

static int wake_one(void)
{
    void *g = dispatch_group_create(), *q = dispatch_get_global_queue(0, 0);
    void *ready = dispatch_semaphore_create(0), *entered = dispatch_semaphore_create(0);
    void *finished = dispatch_semaphore_create(0), *pause = dispatch_semaphore_create(0);
    if (!g || !ready || !entered || !finished || !pause) return 30;
    for (int i = 0; i < 2; i++) {
        dispatch_group_async(g, q, ^{
            dispatch_semaphore_signal(entered);
            if (dispatch_semaphore_wait(ready, deadline())) failure = 31;
            dispatch_semaphore_signal(finished);
        });
    }
    if (dispatch_semaphore_wait(entered, deadline()) || dispatch_semaphore_wait(entered, deadline())) return 32;
    if (!dispatch_semaphore_wait(pause, dispatch_time(0, 10000000))) return 33;
    if (dispatch_semaphore_signal(ready) <= 0 || dispatch_semaphore_wait(finished, deadline())) return 34;
    if (!dispatch_semaphore_wait(finished, 0) || !dispatch_group_wait(g, 0)) return 35;
    if (dispatch_semaphore_signal(ready) <= 0 || dispatch_semaphore_wait(finished, deadline())) return 36;
    if (dispatch_group_wait(g, deadline()) || !dispatch_semaphore_wait(ready, 0)) return 37;
    return failure;
}

int main(void)
{
    int rc = tokens();
    if (!rc) rc = barriers();
    if (!rc) rc = wake_one();
    if (rc) return rc;
    reused_group = dispatch_group_create();
    empty_group = dispatch_group_create();
    shutdown_semaphore = dispatch_semaphore_create(0);
    shutdown_group = dispatch_group_create();
    if (!reused_group || !empty_group || !shutdown_semaphore || !shutdown_group) return 40;
    dispatch_group_enter(reused_group);
    dispatch_group_notify(reused_group, dispatch_get_main_queue(), ^{ notified++; });
    dispatch_group_leave(reused_group);
    dispatch_group_enter(reused_group);
    dispatch_group_notify(empty_group, dispatch_get_main_queue(), ^{ notified++; });
    dispatch_group_enter(empty_group);
    return 0;
}

int sync_check(int stage)
{
    if (failure) return -failure;
    if (!stage) {
        if (notified != 2 || !dispatch_group_wait(reused_group, 0) || !dispatch_group_wait(empty_group, 0)) return -41;
        dispatch_group_leave(reused_group);
        dispatch_group_leave(empty_group);
        if (dispatch_group_wait(reused_group, 0) || dispatch_group_wait(empty_group, 0)) return -42;
        return 1;
    }
    if (stage == 1) {
        dispatch_group_enter(shutdown_group);
        dispatch_async(dispatch_get_global_queue(0, 0), ^{
            stopping_started++;
            dispatch_semaphore_wait(shutdown_semaphore, forever);
        });
        dispatch_async(dispatch_get_global_queue(0, 0), ^{
            stopping_started++;
            dispatch_group_wait(shutdown_group, forever);
        });
        return 1;
    }
    return stopping_started == 2;
}

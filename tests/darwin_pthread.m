typedef unsigned long Size;
typedef struct { long signature; char opaque[56]; } Attr;
typedef struct { long signature; char opaque[56]; } Mutex;
typedef struct { long signature; char opaque[8]; } MutexAttr;
typedef void *Thread;

extern int pthread_create(Thread *, const Attr *, void *(*)(void *), void *);
extern int pthread_join(Thread, void **);
extern int pthread_detach(Thread);
extern void pthread_exit(void *) __attribute__((noreturn));
extern Thread pthread_self(void);
extern int pthread_equal(Thread, Thread);
extern int pthread_attr_init(Attr *);
extern int pthread_attr_destroy(Attr *);
extern int pthread_attr_setdetachstate(Attr *, int);
extern int pthread_attr_getdetachstate(const Attr *, int *);
extern int pthread_attr_setstacksize(Attr *, Size);
extern int pthread_attr_getstacksize(const Attr *, Size *);
extern int pthread_attr_setguardsize(Attr *, Size);
extern int pthread_attr_getguardsize(const Attr *, Size *);
extern int pthread_mutexattr_init(MutexAttr *);
extern int pthread_mutexattr_destroy(MutexAttr *);
extern int pthread_mutexattr_settype(MutexAttr *, int);
extern int pthread_mutexattr_gettype(const MutexAttr *, int *);
extern int pthread_mutexattr_setpshared(MutexAttr *, int);
extern int pthread_mutexattr_getpshared(const MutexAttr *, int *);
extern int pthread_mutex_init(Mutex *, const MutexAttr *);
extern int pthread_mutex_destroy(Mutex *);
extern int pthread_mutex_lock(Mutex *);
extern int pthread_mutex_trylock(Mutex *);
extern int pthread_mutex_unlock(Mutex *);
extern int *__error(void);
extern int pipe(int *);
extern long read(int, void *, Size);
extern long write(int, const void *, Size);
extern int close(int);
extern unsigned sleep(unsigned);
extern void *dispatch_semaphore_create(long);
extern long dispatch_semaphore_signal(void *);
extern long dispatch_semaphore_wait(void *, unsigned long);
extern unsigned long dispatch_time(unsigned long, long long);
extern void *dispatch_get_main_queue(void);
extern void *dispatch_get_global_queue(long, unsigned long);
extern void dispatch_sync_f(void *, void *, void (*)(void *));
extern void dispatch_async_f(void *, void *, void (*)(void *));
extern void *memset(void *, int, Size);
extern void abort(void);

static Mutex normal = {0x32AAABA7, {0}};
static Mutex recursive = {0x32AAABA2, {0}};
static Mutex checked = {0x32AAABA1, {0}};
static Mutex stop_lock = {0x32AAABA7, {0}};
static Thread main_thread, workers[4], main_worker, stop_thread;
static Thread seen[4];
static int *errno_cells[4];
static int total, failure, main_called, main_started, main_finished, stop_started, stop_finished;
static int stop_pipe[2];
static void *ready, *finished, *pause_sem;

static unsigned long deadline(void) { return dispatch_time(0, 3000000000LL); }

static void *counter(void *argument)
{
    int index = (int)(long)argument;
    seen[index] = pthread_self();
    errno_cells[index] = __error();
    *__error() = 100 + index;
    if (pthread_equal(main_thread, seen[index]) || !pthread_equal(seen[index], pthread_self())) failure = 20;
    if (pthread_join(pthread_self(), (void **)0) != 11) failure = 21;
    if (pthread_mutex_trylock(&normal) != 16) failure = 22;
    if (pthread_mutex_unlock(&checked) != 1) failure = 23;
    dispatch_semaphore_signal(ready);
    for (int i = 0; i < 100; i++) {
        if (pthread_mutex_lock(&normal)) return (void *)101;
        int before = total;
        if (i == 0) dispatch_semaphore_wait(pause_sem, dispatch_time(0, 1000000));
        total = before + 1;
        if (pthread_mutex_unlock(&normal)) return (void *)102;
    }
    if (*__error() != 100 + index) failure = 24;
    return (void *)(long)(index + 7);
}

static void *explicit_exit(void *argument)
{
    pthread_exit(argument);
}

static void *detached(void *argument)
{
    dispatch_semaphore_signal(finished);
    return argument;
}

static void main_job(void *context)
{
    (void)context;
    if (!pthread_equal(pthread_self(), main_thread)) failure = 30;
    main_called++;
}

static void *main_dispatch(void *argument)
{
    (void)argument;
    main_started = 1;
    dispatch_sync_f(dispatch_get_main_queue(), (void *)0, main_job);
    main_finished = 1;
    return (void *)31;
}

static void *shutdown_wait(void *argument)
{
    long kind = (long)argument;
    stop_started++;
    if (kind == 0) {
        if (pthread_mutex_lock(&stop_lock) != 89) failure = 41;
    } else if (kind == 1) {
        if (dispatch_semaphore_wait(pause_sem, ~0UL) != -1) failure = 42;
    } else if (kind == 2) {
        char byte;
        if (read(stop_pipe[0], &byte, 1) != -1 || *__error() != 89) failure = 43;
    } else if (kind == 3) {
        if (!sleep(100)) failure = 44;
    } else if (kind == 4) {
        int result = pthread_join(stop_thread, (void **)0);
        if (result != 0 && result != 89) failure = 45;
    } else {
        dispatch_sync_f(dispatch_get_main_queue(), (void *)0, main_job);
    }
    stop_finished++;
    return (void *)0;
}

static void dispatch_shutdown(void *context)
{
    shutdown_wait(context);
}

static void *fault_thread(void *context)
{
    (void)context;
    abort();
    return (void *)0;
}

int main(void)
{
    struct { unsigned long before; Attr value; unsigned long after; } a = {0x1234, {0, {0}}, 0x5678};
    struct { unsigned long before; MutexAttr value; unsigned long after; } ma = {0xABCD, {0, {0}}, 0xDCBA};
    struct { unsigned long before; Mutex value; unsigned long after; } m;
    memset(&m, 0xA5, sizeof(m));
    main_thread = pthread_self();
    if (!main_thread || !pthread_equal(main_thread, pthread_self()) || pthread_join(main_thread, (void **)0) != 11) return 1;
    if (pthread_attr_init(&a.value) || a.before != 0x1234 || a.after != 0x5678) return 2;
    int value;
    Size size;
    if (pthread_attr_getdetachstate(&a.value, &value) || value != 1 || pthread_attr_setdetachstate(&a.value, 0) != 22) return 3;
    if (pthread_attr_setstacksize(&a.value, 4096) != 22 || pthread_attr_setstacksize(&a.value, 1048576)) return 4;
    if (pthread_attr_getstacksize(&a.value, &size) || size != 1048576) return 5;
    if (pthread_attr_setguardsize(&a.value, 4096) != 22 || pthread_attr_setguardsize(&a.value, 32768) ||
        pthread_attr_getguardsize(&a.value, &size) || size != 32768) return 6;
    if (pthread_mutexattr_init(&ma.value) || ma.before != 0xABCD || ma.after != 0xDCBA) return 7;
    if (pthread_mutexattr_gettype(&ma.value, &value) || value != 0 || pthread_mutexattr_settype(&ma.value, 3) != 22) return 8;
    if (pthread_mutexattr_getpshared(&ma.value, &value) || value != 2 || pthread_mutexattr_setpshared(&ma.value, 1) != 45) return 9;
    if (pthread_mutexattr_setpshared(&ma.value, 2) || pthread_mutexattr_settype(&ma.value, 2) || pthread_mutexattr_gettype(&ma.value, &value) || value != 2) return 10;
    if (pthread_mutex_init(&m.value, &ma.value) || m.before != 0xA5A5A5A5A5A5A5A5UL || m.after != m.before) return 11;
    if (pthread_mutex_lock(&m.value) || pthread_mutex_lock(&m.value) || pthread_mutex_destroy(&m.value) != 16) return 12;
    if (pthread_mutex_unlock(&m.value) || pthread_mutex_unlock(&m.value) || pthread_mutex_destroy(&m.value)) return 13;
    if (pthread_mutex_lock(&recursive) || pthread_mutex_trylock(&recursive) || pthread_mutex_unlock(&recursive) || pthread_mutex_unlock(&recursive)) return 14;
    if (pthread_mutex_lock(&checked) || pthread_mutex_lock(&checked) != 11 || pthread_mutex_trylock(&checked) != 16) return 15;
    if (pthread_mutex_lock(&normal) || pthread_mutex_destroy(&normal) != 16) return 16;
    ready = dispatch_semaphore_create(0);
    finished = dispatch_semaphore_create(0);
    pause_sem = dispatch_semaphore_create(0);
    *__error() = 777;
    for (int i = 0; i < 4; i++) if (pthread_create(&workers[i], &a.value, counter, (void *)(long)i)) return 17;
    for (int i = 0; i < 4; i++) if (dispatch_semaphore_wait(ready, deadline())) return 18;
    if (pthread_mutex_unlock(&normal) || pthread_mutex_unlock(&checked)) return 19;
    for (int i = 0; i < 4; i++) {
        void *result;
        if (pthread_join(workers[i], &result) || result != (void *)(long)(i + 7)) return 25;
        if (!pthread_equal(workers[i], seen[i]) || errno_cells[i] == __error()) return 26;
        for (int j = 0; j < i; j++) if (errno_cells[i] == errno_cells[j]) return 27;
    }
    if (failure || total != 400 || *__error() != 777) return failure ? failure : 28;
    Thread thread;
    void *result;
    if (pthread_create(&thread, (const Attr *)0, explicit_exit, (void *)0x10203040)) return 29;
    if (pthread_join(thread, &result) || result != (void *)0x10203040 || pthread_join(thread, &result) != 3) return 32;
    if (pthread_attr_setdetachstate(&a.value, 2) || pthread_create(&thread, &a.value, detached, (void *)0)) return 33;
    if (pthread_join(thread, &result) != 22 || dispatch_semaphore_wait(finished, deadline())) return 34;
    if (pthread_create(&thread, (const Attr *)0, detached, (void *)0) || pthread_detach(thread) || pthread_detach(thread) != 22) return 35;
    if (pthread_join(thread, &result) != 22 || dispatch_semaphore_wait(finished, deadline())) return 36;
    if (pthread_mutexattr_destroy(&ma.value) || pthread_attr_destroy(&a.value)) return 37;
    thread = (void *)0x1234;
    if (pthread_create(&thread, &a.value, detached, (void *)0) != 22 || thread != (void *)0x1234) return 38;
    if (pthread_mutex_destroy(&normal) || pthread_mutex_destroy(&recursive) || pthread_mutex_destroy(&checked)) return 39;
    if (pthread_create(&main_worker, (const Attr *)0, main_dispatch, (void *)0)) return 40;
    return 0;
}

int pthread_check(int stage)
{
    if (failure) return -failure;
    if (stage == 0) return main_started;
    if (stage == 1) {
        if (!main_called || !main_finished) return 0;
        void *result;
        if (main_called != 1 || pthread_join(main_worker, &result) || result != (void *)31) return -46;
        return 1;
    }
    if (stage == 2) {
        if (pthread_mutex_lock(&stop_lock) || pipe(stop_pipe)) return -47;
        if (pthread_create(&stop_thread, (const Attr *)0, shutdown_wait, (void *)0)) return -48;
        for (long i = 1; i < 6; i++) {
            Thread thread;
            if (pthread_create(&thread, (const Attr *)0, shutdown_wait, (void *)i) || pthread_detach(thread)) return -49;
        }
        dispatch_async_f(dispatch_get_global_queue(0, 0), (void *)0, dispatch_shutdown);
        return 1;
    }
    if (stage == 3) return stop_started == 7;
    if (stage == 4) return stop_finished == 7 && !failure && main_called == 1 ? 1 : -50;
    if (stage == 5) {
        Thread thread;
        if (pthread_create(&thread, (const Attr *)0, fault_thread, (void *)0) || pthread_join(thread, (void **)0)) return -51;
        return 1;
    }
    return -52;
}

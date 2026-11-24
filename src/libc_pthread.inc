#include <setjmp.h>
#include <limits.h>

#define PT_ATTR_SIG UINT64_C(0x43484d4154545231)
#define PT_MUTEXATTR_SIG UINT64_C(0x43484d4d41545431)
#define PT_MUTEX_SIG UINT64_C(0x43484d4d55545831)

typedef struct {
    uint64_t signature;
    int detach, reserved;
    uint64_t stack_size, guard_size;
    unsigned char padding[32];
} pt_attribute;

typedef struct {
    uint64_t signature;
    int type, shared;
} pt_mutex_attribute;

typedef struct pt_thread {
    pthread_t host;
    chm_session *session;
    void *(*start)(void *);
    void *argument, *result;
    int owned, detached, done, joined, joining, reaping, reaped;
    struct pt_thread *next;
} pt_thread;

typedef struct pt_mutex {
    void *guest;
    pthread_t owner;
    unsigned count, waiters;
    int type;
    struct pt_mutex *next;
} pt_mutex;

_Static_assert(sizeof(pt_attribute) == 64 && sizeof(pt_mutex_attribute) == 16, "Darwin arm64 pthread attributes");

static pthread_mutex_t pt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pt_changed = PTHREAD_COND_INITIALIZER;
static pt_thread *pt_threads;
static pt_mutex *pt_mutexes;
typedef struct pt_sync_object {
    void *object;
    uint64_t mutex[8];
    struct pt_sync_object *next;
} pt_sync_object;
static pt_sync_object *pt_sync_objects;
static atomic_int pt_stopping;
static atomic_ulong pt_epoch;
static _Thread_local unsigned long pt_local_epoch;
static _Thread_local pt_thread *pt_current;
static _Thread_local sigjmp_buf pt_exit_jump;
static _Thread_local int pt_exit_ready;

static int pt_error(int code)
{
    if (code == EDEADLK) return 11;
    if (code == ETIMEDOUT) return 60;
    return io_errno(code);
}

static pt_thread *pt_local(void)
{
    unsigned long epoch = atomic_load(&pt_epoch);
    if (pt_local_epoch != epoch) {
        pt_local_epoch = epoch;
        pt_current = NULL;
    }
    return pt_current;
}

int chm_pthread_worker(void)
{
    pt_thread *thread = pt_local();
    return thread && thread->owned;
}

int chm_pthread_stopping(void)
{
    return atomic_load(&pt_stopping);
}

void chm_pthread_init(void)
{
    atomic_fetch_add(&pt_epoch, 1);
    atomic_store(&pt_stopping, 0);
}

static int pt_attr_read(const void *object, pt_attribute *attribute)
{
    if (!object) return EINVAL;
    memcpy(attribute, object, sizeof(*attribute));
    return attribute->signature == PT_ATTR_SIG ? 0 : EINVAL;
}

static int pt_attr_init(void *object)
{
    if (!object) return EINVAL;
    pt_attribute attribute = { .signature = PT_ATTR_SIG, .detach = 1, .stack_size = 512 * 1024, .guard_size = 16384 };
    memcpy(object, &attribute, sizeof(attribute));
    return 0;
}

static int pt_attr_destroy(void *object)
{
    pt_attribute attribute;
    if (pt_attr_read(object, &attribute)) return EINVAL;
    memset(object, 0, sizeof(attribute));
    return 0;
}

static int pt_attr_setdetachstate(void *object, int value)
{
    pt_attribute attribute;
    if (pt_attr_read(object, &attribute) || (value != 1 && value != 2)) return EINVAL;
    attribute.detach = value;
    memcpy(object, &attribute, sizeof(attribute));
    return 0;
}

static int pt_attr_getdetachstate(const void *object, int *value)
{
    pt_attribute attribute;
    if (!value || pt_attr_read(object, &attribute)) return EINVAL;
    *value = attribute.detach;
    return 0;
}

static int pt_attr_setstacksize(void *object, size_t value)
{
    pt_attribute attribute;
    if (pt_attr_read(object, &attribute) || value < 16384 || (value & 16383)) return EINVAL;
    attribute.stack_size = value;
    memcpy(object, &attribute, sizeof(attribute));
    return 0;
}

static int pt_attr_getstacksize(const void *object, size_t *value)
{
    pt_attribute attribute;
    if (!value || pt_attr_read(object, &attribute)) return EINVAL;
    *value = attribute.stack_size;
    return 0;
}

static int pt_attr_setguardsize(void *object, size_t value)
{
    pt_attribute attribute;
    if (pt_attr_read(object, &attribute) || (value & 16383)) return EINVAL;
    attribute.guard_size = value;
    memcpy(object, &attribute, sizeof(attribute));
    return 0;
}

static int pt_attr_getguardsize(const void *object, size_t *value)
{
    pt_attribute attribute;
    if (!value || pt_attr_read(object, &attribute)) return EINVAL;
    *value = attribute.guard_size;
    return 0;
}

static pt_thread *pt_find(void *handle)
{
    for (pt_thread *thread = pt_threads; thread; thread = thread->next)
        if (thread == handle) return thread;
    return NULL;
}

static void *pt_self(void)
{
    pt_thread *thread = pt_local();
    if (thread) return thread;
    pthread_mutex_lock(&pt_lock);
    thread = calloc(1, sizeof(*thread));
    if (thread) {
        thread->host = pthread_self();
        thread->session = g_null_log_session;
        thread->next = pt_threads;
        pt_threads = thread;
        pt_current = thread;
    }
    pthread_mutex_unlock(&pt_lock);
    return thread;
}

static int pt_equal(void *first, void *second)
{
    return first == second;
}

static int pt_invoke(void *context)
{
    pt_thread *thread = context;
    if (chm_pthread_stopping()) return 0;
    pt_exit_ready = 1;
    if (!sigsetjmp(pt_exit_jump, 1)) thread->result = thread->start(thread->argument);
    pt_exit_ready = 0;
    return 0;
}

static void *pt_start(void *context)
{
    pt_thread *thread = context;
    pt_local_epoch = atomic_load(&pt_epoch);
    pt_current = thread;
    int result = chm_session_callback(thread->session, pt_invoke, thread);
    pt_exit_ready = 0;
    if (result < 0) atomic_store(&thread->session->exit_code, -2);
    pthread_mutex_lock(&pt_lock);
    thread->done = 1;
    pthread_cond_broadcast(&pt_changed);
    pthread_mutex_unlock(&pt_lock);
    return NULL;
}

static void pt_reap(int drain)
{
    unsigned depth = chm_guest_suspend();
    pthread_mutex_lock(&pt_lock);
    for (pt_thread *thread = pt_threads; thread; thread = thread->next) {
        if (!thread->owned) continue;
        while (drain && thread->reaping) pthread_cond_wait(&pt_changed, &pt_lock);
        if (thread->reaped || thread->reaping || (!drain && !thread->done)) continue;
        thread->reaping = 1;
        pthread_t host = thread->host;
        pthread_mutex_unlock(&pt_lock);
        int result = pthread_join(host, NULL);
        pthread_mutex_lock(&pt_lock);
        thread->reaping = 0;
        if (!result) thread->reaped = 1;
        pthread_cond_broadcast(&pt_changed);
        if (result) abort();
    }
    pthread_mutex_unlock(&pt_lock);
    chm_guest_resume(depth);
}

static int pt_create(void **handle, const void *object, void *(*start)(void *), void *argument)
{
    if (!handle || !start) return EINVAL;
    if (chm_pthread_stopping()) return 35;
    pt_attribute attribute = { .detach = 1, .stack_size = 512 * 1024, .guard_size = 16384 };
    if (object && pt_attr_read(object, &attribute)) return EINVAL;
    pt_reap(0);
    pthread_attr_t native;
    int result = pthread_attr_init(&native);
    if (result) return pt_error(result);
    result = pthread_attr_setstacksize(&native, attribute.stack_size);
    if (!result) result = pthread_attr_setguardsize(&native, attribute.guard_size);
    pt_thread *thread = result ? NULL : calloc(1, sizeof(*thread));
    if (!thread && !result) result = EAGAIN;
    if (!result) {
        thread->session = g_null_log_session;
        thread->owned = 1;
        thread->detached = attribute.detach == 2;
        thread->start = start;
        thread->argument = argument;
        pthread_mutex_lock(&pt_lock);
        result = chm_pthread_stopping() || !thread->session ? EAGAIN : pthread_create(&thread->host, &native, pt_start, thread);
        if (!result) {
            thread->next = pt_threads;
            pt_threads = thread;
            *handle = thread;
        }
        pthread_mutex_unlock(&pt_lock);
    }
    pthread_attr_destroy(&native);
    if (result) free(thread);
    return pt_error(result);
}

static int pt_join(void *handle, void **value)
{
    pthread_mutex_lock(&pt_lock);
    pt_thread *thread = pt_find(handle);
    int result = !thread || thread->joined ? ESRCH : thread == pt_local() ? EDEADLK :
        !thread->owned || thread->detached || thread->joining ? EINVAL : 0;
    if (result) {
        pthread_mutex_unlock(&pt_lock);
        return pt_error(result);
    }
    thread->joining = 1;
    unsigned depth = chm_guest_suspend();
    while (!thread->done && !chm_pthread_stopping()) pthread_cond_wait(&pt_changed, &pt_lock);
    void *returned = thread->done ? thread->result : NULL;
    if (!thread->done) result = ECANCELED;
    else thread->joined = 1;
    thread->joining = 0;
    pthread_mutex_unlock(&pt_lock);
    chm_guest_resume(depth);
    if (!result && value) *value = returned;
    return pt_error(result);
}

static int pt_detach(void *handle)
{
    pthread_mutex_lock(&pt_lock);
    pt_thread *thread = pt_find(handle);
    int result = !thread || thread->joined ? ESRCH : !thread->owned || thread->detached || thread->joining ? EINVAL : 0;
    if (!result) thread->detached = 1;
    pthread_mutex_unlock(&pt_lock);
    return result;
}

static _Noreturn void pt_exit(void *value)
{
    pt_thread *thread = pt_local();
    if (!thread || !thread->owned || !pt_exit_ready) {
        chm_session_log(g_null_log_session, "pthread_exit outside a managed pthread");
        abort();
    }
    thread->result = value;
    siglongjmp(pt_exit_jump, 1);
}

static int pt_mutexattr_read(const void *object, pt_mutex_attribute *attribute)
{
    if (!object) return EINVAL;
    memcpy(attribute, object, sizeof(*attribute));
    return attribute->signature == PT_MUTEXATTR_SIG ? 0 : EINVAL;
}

static int pt_mutexattr_init(void *object)
{
    if (!object) return EINVAL;
    pt_mutex_attribute attribute = {PT_MUTEXATTR_SIG, 0, 2};
    memcpy(object, &attribute, sizeof(attribute));
    return 0;
}

static int pt_mutexattr_destroy(void *object)
{
    pt_mutex_attribute attribute;
    if (pt_mutexattr_read(object, &attribute)) return EINVAL;
    memset(object, 0, sizeof(attribute));
    return 0;
}

static int pt_mutexattr_settype(void *object, int type)
{
    pt_mutex_attribute attribute;
    if (pt_mutexattr_read(object, &attribute) || type < 0 || type > 2) return EINVAL;
    attribute.type = type;
    memcpy(object, &attribute, sizeof(attribute));
    return 0;
}

static int pt_mutexattr_gettype(const void *object, int *type)
{
    pt_mutex_attribute attribute;
    if (!type || pt_mutexattr_read(object, &attribute)) return EINVAL;
    *type = attribute.type;
    return 0;
}

static int pt_mutexattr_setpshared(void *object, int shared)
{
    pt_mutex_attribute attribute;
    if (pt_mutexattr_read(object, &attribute) || (shared != 1 && shared != 2)) return EINVAL;
    if (shared == 1) return 45;
    attribute.shared = shared;
    memcpy(object, &attribute, sizeof(attribute));
    return 0;
}

static int pt_mutexattr_getpshared(const void *object, int *shared)
{
    pt_mutex_attribute attribute;
    if (!shared || pt_mutexattr_read(object, &attribute)) return EINVAL;
    *shared = attribute.shared;
    return 0;
}

static pt_mutex *pt_mutex_find(void *object)
{
    for (pt_mutex *mutex = pt_mutexes; mutex; mutex = mutex->next)
        if (mutex->guest == object) return mutex;
    return NULL;
}

static int pt_mutex_new(void *object, int type, pt_mutex **out)
{
    pt_mutex *mutex = calloc(1, sizeof(*mutex));
    if (!mutex) return ENOMEM;
    mutex->guest = object;
    mutex->type = type;
    mutex->next = pt_mutexes;
    pt_mutexes = mutex;
    uint64_t signature = PT_MUTEX_SIG;
    memset(object, 0, 64);
    memcpy(object, &signature, sizeof(signature));
    *out = mutex;
    return 0;
}

static int pt_mutex_get(void *object, pt_mutex **out)
{
    if (!object) return EINVAL;
    uint64_t signature;
    memcpy(&signature, object, sizeof(signature));
    *out = pt_mutex_find(object);
    if (*out) return signature == PT_MUTEX_SIG ? 0 : EINVAL;
    int type;
    if (signature == 0x32AAABA7 || signature == 0x32AAABA3) type = 0;
    else if (signature == 0x32AAABA1) type = 1;
    else if (signature == 0x32AAABA2) type = 2;
    else return EINVAL;
    return pt_mutex_new(object, type, out);
}

static int pt_mutex_init(void *object, const void *attributes)
{
    if (!object) return EINVAL;
    pt_mutex_attribute attribute = {PT_MUTEXATTR_SIG, 0, 2};
    if (attributes && pt_mutexattr_read(attributes, &attribute)) return EINVAL;
    pthread_mutex_lock(&pt_lock);
    pt_mutex *mutex;
    int result = pt_mutex_find(object) ? EBUSY : pt_mutex_new(object, attribute.type, &mutex);
    pthread_mutex_unlock(&pt_lock);
    return result;
}

static int pt_mutex_destroy(void *object)
{
    pthread_mutex_lock(&pt_lock);
    pt_mutex *mutex;
    int result = pt_mutex_get(object, &mutex);
    if (!result && (mutex->count || mutex->waiters)) result = EBUSY;
    if (!result) {
        pt_mutex **link = &pt_mutexes;
        while (*link != mutex) link = &(*link)->next;
        *link = mutex->next;
        memset(object, 0, 64);
        free(mutex);
    }
    pthread_mutex_unlock(&pt_lock);
    return result;
}

static int pt_mutex_acquire(void *object, int try_only)
{
    pthread_mutex_lock(&pt_lock);
    pt_mutex *mutex;
    int result = pt_mutex_get(object, &mutex);
    unsigned depth = 0;
    if (!result && mutex->count && pthread_equal(mutex->owner, pthread_self())) {
        if (mutex->type == 2) {
            if (mutex->count == UINT_MAX) result = EAGAIN;
            else mutex->count++;
            pthread_mutex_unlock(&pt_lock);
            return pt_error(result);
        }
        if (!try_only && mutex->type == 1) result = EDEADLK;
    }
    if (!result && mutex->count && try_only) result = EBUSY;
    if (!result && mutex->count) {
        mutex->waiters++;
        depth = chm_guest_suspend();
        while (mutex->count && !chm_pthread_stopping()) pthread_cond_wait(&pt_changed, &pt_lock);
        mutex->waiters--;
        if (chm_pthread_stopping()) result = ECANCELED;
    }
    if (!result) {
        mutex->owner = pthread_self();
        mutex->count = 1;
    }
    pthread_mutex_unlock(&pt_lock);
    chm_guest_resume(depth);
    return pt_error(result);
}

static int pt_mutex_lock(void *object) { return pt_mutex_acquire(object, 0); }
static int pt_mutex_trylock(void *object) { return pt_mutex_acquire(object, 1); }

static int pt_mutex_unlock(void *object)
{
    pthread_mutex_lock(&pt_lock);
    pt_mutex *mutex;
    int result = pt_mutex_get(object, &mutex);
    if (!result && (!mutex->count || !pthread_equal(mutex->owner, pthread_self()))) result = EPERM;
    if (!result && !--mutex->count) pthread_cond_broadcast(&pt_changed);
    pthread_mutex_unlock(&pt_lock);
    return result;
}

static int pt_objc_sync(void *object, int enter)
{
    if (!object) return 0;
    pthread_mutex_lock(&pt_lock);
    pt_sync_object *entry = pt_sync_objects;
    while (entry && entry->object != object) entry = entry->next;
    if (!entry && enter) {
        entry = calloc(1, sizeof(*entry));
        if (entry) {
            entry->object = object;
            entry->mutex[0] = 0x32AAABA2;
            entry->next = pt_sync_objects;
            pt_sync_objects = entry;
        }
    }
    pthread_mutex_unlock(&pt_lock);
    if (!entry) return enter ? -3 : -1;
    int result = enter ? pt_mutex_lock(entry->mutex) : pt_mutex_unlock(entry->mutex);
    return result ? (enter ? -2 : -1) : 0;
}
static int pt_objc_sync_enter(void *object) { return pt_objc_sync(object, 1); }
static int pt_objc_sync_exit(void *object) { return pt_objc_sync(object, 0); }

void chm_pthread_stop(void)
{
    pthread_mutex_lock(&pt_lock);
    atomic_store(&pt_stopping, 1);
    pthread_cond_broadcast(&pt_changed);
    pthread_mutex_unlock(&pt_lock);
}

void chm_pthread_drain(void)
{
    pt_reap(1);
}

void chm_pthread_reset(void)
{
    pthread_mutex_lock(&pt_lock);
    while (pt_threads) {
        pt_thread *next = pt_threads->next;
        free(pt_threads);
        pt_threads = next;
    }
    while (pt_mutexes) {
        pt_mutex *next = pt_mutexes->next;
        free(pt_mutexes);
        pt_mutexes = next;
    }
    while (pt_sync_objects) {
        pt_sync_object *next = pt_sync_objects->next;
        free(pt_sync_objects);
        pt_sync_objects = next;
    }
    atomic_fetch_add(&pt_epoch, 1);
    pthread_mutex_unlock(&pt_lock);
}

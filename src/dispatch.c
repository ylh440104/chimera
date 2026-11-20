#include "chm_session.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHM_QUEUE_CAP 1024

typedef struct {
    void *isa;
    int32_t flags;
    int32_t reserved;
    void (*invoke)(void *);
    void *descriptor;
} chm_block;

typedef struct {
    chm_block *blocks[CHM_QUEUE_CAP];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} chm_queue;

typedef struct {
    char opaque[128];
} chm_queue_object;

static chm_queue g_main_queue;
static chm_queue g_global_queue;
static chm_queue_object g_main_q_obj;
static chm_queue_object g_global_q_obj;
static chm_queue_object g_concurrent_attr;
static int g_initialized;

static void queue_init(chm_queue *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static int queue_push(chm_queue *q, chm_block *b)
{
    int rc = -1;

    pthread_mutex_lock(&q->lock);
    if (q->count < CHM_QUEUE_CAP) {
        q->blocks[q->tail] = b;
        q->tail = (q->tail + 1) % CHM_QUEUE_CAP;
        q->count++;
        pthread_cond_signal(&q->cond);
        rc = 0;
    }
    pthread_mutex_unlock(&q->lock);
    return rc;
}

static chm_block *queue_pop(chm_queue *q)
{
    chm_block *b = NULL;

    pthread_mutex_lock(&q->lock);
    if (q->count) {
        b = q->blocks[q->head];
        q->head = (q->head + 1) % CHM_QUEUE_CAP;
        q->count--;
    }
    pthread_mutex_unlock(&q->lock);
    return b;
}

void chm_dispatch_init(void)
{
    if (g_initialized)
        return;
    queue_init(&g_main_queue);
    queue_init(&g_global_queue);
    g_initialized = 1;
}

static void dispatch_async_impl(chm_queue *q, chm_block *b)
{
    if (!b || !b->invoke)
        return;
    if (queue_push(q, b))
        return;
}

static int dispatch_async_main(void *queue, chm_block *b)
{
    dispatch_async_impl(queue == &g_global_q_obj ? &g_global_queue : &g_main_queue, b);
    return 0;
}

static int dispatch_sync_impl(void *ignored, chm_block *b)
{
    (void)ignored;
    if (!b || !b->invoke)
        return -1;
    b->invoke(b);
    return 0;
}

static void dispatch_after_impl(int64_t when, void *q, chm_block *b)
{
    (void)when;
    dispatch_async_impl(q ? &g_main_queue : &g_main_queue, b);
}

static void *main_queue_impl(void)
{
    return &g_main_q_obj;
}

static void *global_queue_impl(long priority, unsigned long flags)
{
    (void)priority;
    (void)flags;
    return &g_global_q_obj;
}

static void dispatch_once_impl(void *predicate, chm_block *b)
{
    long *flag = predicate;

    if (flag && __atomic_exchange_n(flag, 1, __ATOMIC_ACQ_REL) == 0) {
        if (b && b->invoke)
            b->invoke(b);
    }
}

static void *group_create_impl(void)
{
    return calloc(1, 64);
}

static void group_enter_impl(void *g)
{
    (void)g;
}

static void group_leave_impl(void *g)
{
    (void)g;
}

static long group_wait_impl(void *g, uint64_t timeout)
{
    (void)g;
    (void)timeout;
    return 0;
}

static uint64_t dispatch_time_impl(uint64_t when, int64_t delta)
{
    return when + (uint64_t)delta;
}

size_t chm_dispatch_pump(chm_session *s)
{
    size_t runs = 0;
    chm_block *b;

    chm_dispatch_init();
    while ((b = queue_pop(&g_main_queue)) != NULL && runs < 4096) {
        if (b->invoke)
            b->invoke(b);
        runs++;
    }
    if (s && runs)
        chm_session_log(s, "dispatch: ran %zu main-queue blocks", runs);
    return runs;
}

size_t chm_dispatch_pending(void)
{
    size_t n;

    if (!g_initialized)
        return 0;
    pthread_mutex_lock(&g_main_queue.lock);
    n = g_main_queue.count;
    pthread_mutex_unlock(&g_main_queue.lock);
    return n;
}

void *chm_dispatch_lookup(const char *name)
{
    static const struct {
        const char *name;
        void *addr;
    } table[] = {
        { "_dispatch_async", (void *)dispatch_async_main },
        { "_dispatch_async_f", (void *)dispatch_async_main },
        { "_dispatch_sync", (void *)dispatch_sync_impl },
        { "_dispatch_after", (void *)dispatch_after_impl },
        { "_dispatch_after_f", (void *)dispatch_after_impl },
        { "_dispatch_once", (void *)dispatch_once_impl },
        { "_dispatch_once_f", (void *)dispatch_once_impl },
        { "_dispatch_get_main_queue", (void *)main_queue_impl },
        { "_dispatch_get_global_queue", (void *)global_queue_impl },
        { "_dispatch_group_create", (void *)group_create_impl },
        { "_dispatch_group_enter", (void *)group_enter_impl },
        { "_dispatch_group_leave", (void *)group_leave_impl },
        { "_dispatch_group_wait", (void *)group_wait_impl },
        { "_dispatch_time", (void *)dispatch_time_impl },
        { NULL, NULL }
    };
    int i;

    chm_dispatch_init();
    if (!name)
        return NULL;
    for (i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, name) == 0)
            return table[i].addr;
    }
    if (strcmp(name, "__dispatch_main_q") == 0)
        return &g_main_q_obj;
    if (strcmp(name, "__dispatch_queue_attr_concurrent") == 0)
        return &g_concurrent_attr;
    return NULL;
}
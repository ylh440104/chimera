#include "chm.h"
#include "chm_int.h"

#include <stdio.h>
#include <string.h>

#define CHM_QUEUE_MAX 256

typedef struct {
    void *isa;
    int32_t flags;
    int32_t reserved;
    void (*invoke)(void *);
    void *descriptor;
} chm_block;

typedef struct {
    void *block;
} chm_queue_item;

static chm_queue_item g_queue[CHM_QUEUE_MAX];
static int g_queue_head;
static int g_queue_tail;
static int g_queue_count;
static char g_main_q[64];
static char g_global_q[64];
static char g_queue_attr_concurrent[64];

static int chm_dispatch_async(void *queue, chm_block *block)
{
    (void)queue;
    if (!block || !block->invoke)
        return -1;
    if (g_queue_count >= CHM_QUEUE_MAX)
        return -1;
    g_queue[g_queue_tail].block = block;
    g_queue_tail = (g_queue_tail + 1) % CHM_QUEUE_MAX;
    g_queue_count++;
    return 0;
}

static int chm_dispatch_sync(void *queue, chm_block *block)
{
    (void)queue;
    if (!block || !block->invoke)
        return -1;
    block->invoke(block);
    return 0;
}

static void chm_dispatch_after(int64_t when, void *queue, chm_block *block)
{
    (void)when;
    chm_dispatch_async(queue, block);
}

static void *chm_dispatch_get_main_queue(void)
{
    return g_main_q;
}

static void *chm_dispatch_get_global_queue(long priority, unsigned long flags)
{
    (void)priority;
    (void)flags;
    return g_global_q;
}

static void chm_dispatch_once(void *predicate, chm_block *block)
{
    long *flag = (long *)predicate;

    if (flag && *flag == 0) {
        *flag = 1;
        if (block && block->invoke)
            block->invoke(block);
    }
}

void chm_dispatch_init(void)
{
    g_queue_head = 0;
    g_queue_tail = 0;
    g_queue_count = 0;
}

int chm_dispatch_pending(void)
{
    return g_queue_count;
}

void chm_dispatch_pump(chm_report *r)
{
    int runs = 0;

    while (g_queue_count > 0 && runs < CHM_QUEUE_MAX) {
        chm_block *block = (chm_block *)g_queue[g_queue_head].block;

        g_queue_head = (g_queue_head + 1) % CHM_QUEUE_MAX;
        g_queue_count--;
        if (block && block->invoke)
            block->invoke(block);
        runs++;
    }
    if (r)
        chm_report_add(r, "dispatch: 执行 %d 个 block", runs);
}

void *chm_dispatch_resolve_import(const char *name)
{
    static const struct {
        const char *name;
        void *addr;
    } table[] = {
        { "_dispatch_async", (void *)chm_dispatch_async },
        { "_dispatch_sync", (void *)chm_dispatch_sync },
        { "_dispatch_after", (void *)chm_dispatch_after },
        { "_dispatch_after_f", (void *)chm_dispatch_after },
        { "_dispatch_once", (void *)chm_dispatch_once },
        { "_dispatch_get_main_queue", (void *)chm_dispatch_get_main_queue },
        { "_dispatch_get_global_queue", (void *)chm_dispatch_get_global_queue },
        { NULL, NULL }
    };
    int i;

    for (i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, name) == 0)
            return table[i].addr;
    }
    if (strcmp(name, "__dispatch_queue_attr_concurrent") == 0)
        return g_queue_attr_concurrent;
    if (strcmp(name, "__dispatch_main_q") == 0)
        return g_main_q;
    return NULL;
}
#include <pthread.h>

static pthread_mutex_t guest_gate = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned guest_depth;

void chm_guest_lock(void)
{
    if (!guest_depth) pthread_mutex_lock(&guest_gate);
    guest_depth++;
}

void chm_guest_unlock(void)
{
    if (!guest_depth) return;
    guest_depth--;
    if (!guest_depth) pthread_mutex_unlock(&guest_gate);
}

unsigned chm_guest_suspend(void)
{
    unsigned depth = guest_depth;
    if (depth) { guest_depth = 0; pthread_mutex_unlock(&guest_gate); }
    return depth;
}

void chm_guest_resume(unsigned depth)
{
    if (depth) { pthread_mutex_lock(&guest_gate); guest_depth = depth; }
}

static void guest_restore_depth(unsigned depth)
{
    while (guest_depth > depth) chm_guest_unlock();
}
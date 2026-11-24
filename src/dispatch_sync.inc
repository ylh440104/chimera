static void queue_barrier_async(void *q, void *block)
{
    if (queue_put_options(q, block, NULL, NULL, 0, NULL, NULL, NULL, 1) && !chm_dispatch_stopping()) abort();
}

static void queue_barrier_async_f(void *q, void *context, void (*function)(void *))
{
    if (queue_put_options(q, NULL, function, context, 0, NULL, NULL, NULL, 1) && !chm_dispatch_stopping()) abort();
}

static void queue_barrier_sync_common(void *q, void *block, void *context, void (*function)(void *))
{
    if (q == current_queue || (!worker_thread && !chm_pthread_worker() && q == &main_queue)) {
        chm_session_log(queue_session, "dispatch: synchronous barrier on current queue");
        abort();
    }
    queue_waiter waiter = {0};
    if (queue_put_options(q, block, function, context, 0, NULL, &waiter, NULL, 1)) {
        if (!chm_dispatch_stopping()) abort();
        return;
    }
    start_workers();
    unsigned depth = chm_guest_suspend();
    pthread_mutex_lock(&queue_lock);
    while (!waiter.done) pthread_cond_wait(&queue_changed, &queue_lock);
    pthread_mutex_unlock(&queue_lock);
    chm_guest_resume(depth);
}

static void queue_barrier_sync(void *q, void *block)
{
    queue_barrier_sync_common(q, block, NULL, NULL);
}

static void queue_barrier_sync_f(void *q, void *context, void (*function)(void *))
{
    queue_barrier_sync_common(q, NULL, context, function);
}

static void group_async(void *group, void *q, void *block)
{
    if ((!group || queue_put_options(q, block, NULL, NULL, 0, NULL, NULL, group, 0)) && !chm_dispatch_stopping()) abort();
}

static void group_async_f(void *group, void *q, void *context, void (*function)(void *))
{
    if ((!group || queue_put_options(q, NULL, function, context, 0, NULL, NULL, group, 0)) && !chm_dispatch_stopping()) abort();
}

static queue_semaphore *semaphore_find(void *object)
{
    for (queue_semaphore *s = queue_semaphores; s; s = s->next)
        if (s == object) return s;
    return NULL;
}

static void *semaphore_create(long value)
{
    if (value < 0 || chm_dispatch_stopping()) return NULL;
    queue_semaphore *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->value = value;
    pthread_mutex_lock(&queue_lock);
    if (chm_dispatch_stopping()) {
        pthread_mutex_unlock(&queue_lock);
        free(s);
        return NULL;
    }
    s->next = queue_semaphores;
    queue_semaphores = s;
    pthread_mutex_unlock(&queue_lock);
    return s;
}

static long semaphore_signal(void *object)
{
    pthread_mutex_lock(&queue_lock);
    queue_semaphore *s = semaphore_find(object);
    if (!s || chm_dispatch_stopping()) {
        pthread_mutex_unlock(&queue_lock);
        return -1;
    }
    long woke = s->first != NULL;
    if (s->first) {
        semaphore_waiter *waiter = s->first;
        s->first = waiter->next;
        if (!s->first) s->last = NULL;
        waiter->signaled = 1;
        pthread_cond_broadcast(&queue_changed);
    } else {
        if (s->value == LONG_MAX) {
            pthread_mutex_unlock(&queue_lock);
            abort();
        }
        s->value++;
    }
    pthread_mutex_unlock(&queue_lock);
    return woke;
}

static long semaphore_wait(void *object, uint64_t when)
{
    start_workers();
    unsigned depth = chm_guest_suspend();
    pthread_mutex_lock(&queue_lock);
    queue_semaphore *s = semaphore_find(object);
    long result = 1;
    if (!s || chm_dispatch_stopping()) result = -1;
    else if (s->value > 0) { s->value--; result = 0; }
    else if (when && queue_clock() < when) {
        semaphore_waiter waiter = {0};
        if (s->last) s->last->next = &waiter;
        else s->first = &waiter;
        s->last = &waiter;
        while (!waiter.signaled && !chm_dispatch_stopping() && queue_clock() < when) queue_wait_tick();
        if (waiter.signaled) result = 0;
        else {
            semaphore_waiter *previous = NULL;
            semaphore_waiter **at = &s->first;
            while (*at && *at != &waiter) { previous = *at; at = &(*at)->next; }
            if (*at) *at = waiter.next;
            if (s->last == &waiter) s->last = previous;
            if (chm_dispatch_stopping()) result = -1;
        }
    }
    pthread_mutex_unlock(&queue_lock);
    chm_guest_resume(depth);
    return result;
}

#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static chm_session *session;
static int (*guest_check)(int);

static uint64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void fail(const char *what, int code)
{
    fprintf(stderr, "pthread failure: %s rc=%d\n%s\n", what, code, session ? session->log_buf : "");
    exit(1);
}

static int check_body(void *stage) { return guest_check(*(int *)stage); }

static int check(int stage)
{
    int rc = chm_session_callback(session, check_body, &stage);
    if (rc < 0) fail("guest state", rc);
    return rc;
}

static void await_stage(int stage, int pump)
{
    uint64_t end = now() + 3000000000u;
    while (!check(stage)) {
        if (now() >= end) fail("stage timeout", stage);
        if (pump) chm_dispatch_pump(session);
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    for (int iteration = 0; iteration < 5; iteration++) {
        session = chm_session_create();
        if (!session) fail("create", -1);
        const char *root = getenv("CHM_SANDBOX_ROOT");
        if (root) chm_foundation_set_root(root);
        if (chm_session_load(session, argv[1])) fail("load", -1);
        int rc = chm_session_run(session, 1, argv + 1);
        if (rc) fail("entry", rc);
        guest_check = NULL;
        for (int i = 0; i < session->export_count; i++)
            if (!strcmp(session->exports[i].name, "_pthread_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
        if (!guest_check) fail("export", -1);
        await_stage(0, 0);
        if (chm_dispatch_pending() == 0) fail("pthread main queue was executed inline", -1);
        await_stage(1, 1);
        if (iteration == 4) {
            if (!check(5) || atomic_load(&session->exit_code) != -2 || !strstr(session->fault, "signal 6")) fail("child fault guard", -1);
        } else {
            if (!check(2)) fail("start shutdown waiters", -1);
            await_stage(3, 0);
            if (!(iteration & 1)) {
                uint64_t start = now();
                chm_pthread_stop();
                chm_dispatch_stop();
                chm_pthread_drain();
                chm_dispatch_reset();
                if (now() - start > 3000000000u || !check(4)) fail("shutdown wakeups", -1);
            }
        }
        uint64_t start = now();
        chm_session_destroy(session);
        session = NULL;
        if (now() - start > 3000000000u || chm_dispatch_pending()) fail("session teardown", -1);
    }
    puts("pthread: Darwin storage bounds and attributes, four-thread contention, recursive/errorcheck locks, errno isolation, join/detach/exit, main queue affinity, blocked teardown and child fault guard passed");
    return 0;
}

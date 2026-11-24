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
    fprintf(stderr, "dispatch sync failure: %s rc=%d\n%s\n", what, code, session ? session->log_buf : "");
    exit(1);
}

static int check_body(void *stage)
{
    return guest_check(*(int *)stage);
}

static int check(int stage)
{
    int rc = chm_session_callback(session, check_body, &stage);
    if (rc < 0) fail("guest state", rc);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    for (int iteration = 0; iteration < 4; iteration++) {
        session = chm_session_create();
        if (!session) fail("create", -1);
        const char *root = getenv("CHM_SANDBOX_ROOT");
        if (root) chm_foundation_set_root(root);
        if (chm_session_load(session, argv[1])) fail("load", -1);
        int rc = chm_session_run(session, 1, argv + 1);
        if (rc) fail("entry", rc);
        guest_check = NULL;
        for (int i = 0; i < session->export_count; i++)
            if (!strcmp(session->exports[i].name, "_sync_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
        if (!guest_check) fail("export", -1);
        chm_dispatch_pump(session);
        if (!check(0) || !check(1)) fail("notification generation", -1);
        uint64_t end = now() + 3000000000u;
        while (!check(2)) {
            if (now() >= end) fail("shutdown waiter start", -1);
            nanosleep(&(struct timespec){0, 1000000}, NULL);
        }
        uint64_t start = now();
        chm_session_destroy(session);
        session = NULL;
        if (now() - start > 3000000000u || chm_dispatch_pending()) fail("waiter shutdown", -1);
    }
    puts("dispatch sync: barriers, group accounting and reuse, semaphore tokens/timeouts/wake-one and four waiter teardowns passed");
    return 0;
}

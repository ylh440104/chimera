#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int (*guest_check)(int);

static int check(void *context)
{
    return guest_check(*(int *)context);
}

static double now(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + (double)value.tv_nsec / 1e9;
}

static int await(chm_session *session, int stage)
{
    double end = now() + 5;
    do {
        chm_dispatch_pump(session);
        if (session->exit_code < 0) return -100;
        int result = chm_session_callback(session, check, &stage);
        if (result) return result;
        nanosleep(&(struct timespec){0, 10000000}, NULL);
    } while (now() < end);
    fprintf(stderr, "reachability timeout stage=%d", stage);
    if (stage >= 2) system("ip -details link show lo; ip route show table local");
    if (stage >= 2) fprintf(stderr, " probe=%d", chm_session_callback(session, check, &(int){5}));
    return -101;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) return 2;
    int transitions = argc == 3 && !strcmp(argv[2], "--netns");
    for (int iteration = 0; iteration < 3; iteration++) {
        chm_session *session = chm_session_create();
        if (!session) return 3;
        const char *root = getenv("CHM_SANDBOX_ROOT");
        if (root) chm_foundation_set_root(root);
        int result = chm_session_load(session, argv[1]);
        if (!result) result = chm_session_run(session, 1, argv + 1);
        guest_check = NULL;
        for (int i = 0; i < session->export_count; i++)
            if (!strcmp(session->exports[i].name, "_reachability_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
        if (!result && !guest_check) result = 4;
        if (!result) { int state = await(session, 0); if (state != 1) result = state; }
        if (!result && transitions) {
            int stage = 1;
            if (chm_session_callback(session, check, &stage) != 1 || await(session, 2) != 1) result = 5;
            if (!result && (system("ip link set lo down") || await(session, 3) != 1)) result = 6;
            if (system("ip link set lo up")) result = 7;
            if (!result && await(session, 4) != 1) result = 8;
        }
        if (!result) {
            for (int i = 0; i < 10; i++) chm_dispatch_pump(session);
            int stage = 0;
            if (chm_session_callback(session, check, &stage) != 1) result = 9;
        }
        if (result) fprintf(stderr, "reachability iteration=%d rc=%d\n%s\n", iteration, result, session->log_buf);
        chm_session_destroy(session);
        if (result) return 1;
    }
    puts(transitions ? "reachability: three sessions with real route up/down/up passed" :
        "reachability: three sessions with IPv4/IPv6, DNS, callbacks, cancellation and teardown passed");
    return 0;
}

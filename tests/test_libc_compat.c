#include "chm_session.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static chm_session *session;
static int (*intact)(void);

static void fail(const char *what, int rc)
{
    fprintf(stderr, "libc compatibility failure: %s rc=%d\n%s\n", what, rc, session ? session->log_buf : "");
    exit(1);
}

static int check_buffer(void *unused)
{
    (void)unused;
    return intact();
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    for (int phase = 0; phase < 2; phase++) {
        session = chm_session_create();
        if (!session) fail("create", -1);
        const char *root = getenv("CHM_SANDBOX_ROOT");
        if (root) chm_foundation_set_root(root);
        if (chm_session_load(session, argv[1])) fail("load", -1);
        char *args[] = {argv[1], phase ? "overflow" : "normal", NULL};
        int rc = chm_session_run(session, 2, args);
        if (!phase && rc) fail("normal entry", rc);
        if (phase) {
            if (rc != -2 || chm_trap_signal() != SIGABRT) fail("fortify did not trap", rc);
            intact = NULL;
            for (int i = 0; i < session->export_count; i++)
                if (!strcmp(session->exports[i].name,"_libc_buffer_intact")) intact = (int (*)(void))(uintptr_t)session->exports[i].addr;
            if (!intact || chm_session_callback(session, check_buffer, NULL) != 1) fail("overflow wrote destination", -1);
        }
        chm_session_destroy(session);
        session = NULL;
    }
    puts("libc compatibility: Darwin varargs, long-double translation, positional formats, fortify trap, mapped rewind, alignment, environment, random source and section lookup passed");
    return 0;
}

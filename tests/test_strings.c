#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    for (int iteration = 0; iteration < 3; iteration++) {
        chm_session *session = chm_session_create();
        if (!session) return 3;
        const char *root = getenv("CHM_SANDBOX_ROOT");
        if (root) chm_foundation_set_root(root);
        int loaded = chm_session_load(session, argv[1]);
        int result = loaded ? -1 : chm_session_run(session, 1, argv + 1);
        if (result) fprintf(stderr, "string regression: iteration=%d rc=%d\n%s\n", iteration, result, session->log_buf);
        chm_session_destroy(session);
        if (result) return 1;
    }
    puts("strings: AFNetworking ASCII conversion, UTF-16 ranges, expansion, reverse, copies, embedded NUL, failures and ownership passed in three sessions");
    return 0;
}
#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uttype_reference.inc"

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) return 2;
    for (int iteration = 0; iteration < 3; iteration++) {
        chm_session *session = chm_session_create();
        if (!session) return 3;
        const char *root = getenv("CHM_SANDBOX_ROOT");
        if (root) chm_foundation_set_root(root);
        int loaded = chm_session_load(session, argv[1]);
        int result = loaded ? -1 : chm_session_run(session, 1, argv + 1);
        if (!result && argc == 3 && !ut_reference(argv[2])) result = -2;
        if (result) fprintf(stderr, "UTType regression: iteration=%d rc=%d\n%s\n", iteration, result, session->log_buf);
        chm_session_destroy(session);
        if (result) return 1;
    }
    puts("UTType: guest assertions passed in three independent sessions");
    return 0;
}

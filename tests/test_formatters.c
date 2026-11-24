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
        chm_foundation_set_region("en_US", "Asia/Shanghai");
        int loaded = chm_session_load(session, argv[1]);
        int result = loaded ? -1 : chm_session_run(session, 1, argv + 1);
        if (result) fprintf(stderr, "formatter regression: iteration=%d rc=%d\n%s\n", iteration, result, session->log_buf);
        chm_session_destroy(session);
        if (result) return 1;
    }
    puts("formatters: dates, locale, time zone rollover, DST, leap years, byte counts, int64 precision and bundle versions passed in three sessions");
    return 0;
}
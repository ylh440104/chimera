#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>

static chm_session *session;
static int (*guest_check)(int);

static void fail(const char *what, int code)
{
    fprintf(stderr, "stream/XML failure: %s rc=%d\n%s\n", what, code, session ? session->log_buf : "");
    exit(1);
}

static int check_body(void *stage)
{
    return guest_check(*(int *)stage);
}

static int descriptor_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) fail("descriptor enumeration", -1);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) if (entry->d_name[0] != '.') count++;
    closedir(directory);
    return count;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    int baseline = descriptor_count();
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
            if (!strcmp(session->exports[i].name, "_streams_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
        if (!guest_check) fail("export", -1);
        int stage = 0, ready = 0;
        for (int attempt = 0; attempt < 50 && !ready; attempt++) {
            chm_dispatch_pump(session);
            ready = chm_session_callback(session, check_body, &stage);
            if (ready < 0) fail("delegate callback", ready);
            if (!ready) nanosleep(&(struct timespec){0, 1000000}, NULL);
        }
        if (!ready) fail("stream event sequence", -1);
        stage = 1;
        if (chm_session_callback(session, check_body, &stage) != 1) fail("pending stream", -1);
        chm_session_destroy(session);
        session = NULL;
        if (descriptor_count() != baseline || chm_dispatch_pending()) fail("session cleanup", -1);
    }
    puts("stream/XML: bytes, offsets, append, subclassing, delegate events, namespaces, Unicode, malformed input, abort and four teardowns passed");
    return 0;
}
#include "chm_session.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static chm_session *session;
static int (*guest_state)(int);

static void fail(const char *message)
{
    char report[16384] = {0};
    if (session) chm_session_report(session, report, sizeof(report));
    fprintf(stderr, "startup failure: %s\n%s", message, report);
    exit(1);
}

static uint64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int check_state(void *arg)
{
    return guest_state(*(int *)arg);
}

static void await_state(int stage)
{
    const uint64_t deadline = milliseconds() + 5000;
    do {
        chm_dispatch_pump(session);
        if (session->exit_code < 0) fail("queue callback failed");
        int state = chm_session_callback(session, check_state, &stage);
        if (state < 0) fail("guest state");
        if (state == 1) return;
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    } while (milliseconds() < deadline);
    fail("timed out waiting for guest state");
}

typedef struct { const char *label; int found; double x, y; } frame;

static void inspect(void *context, const chm_draw_command *c)
{
    frame *f = context;
    if (c->kind == 1 && c->text && !strcmp(c->text, f->label) && c->width > 0 && c->height > 0) {
        f->found++;
        f->x = c->x + c->width / 2;
        f->y = c->y + c->height / 2;
    }
}

static int descriptors(void)
{
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) fail("fd count");
    int count = 0;
    while (readdir(dir)) count++;
    closedir(dir);
    return count;
}

static void assert_host_streams(const struct stat initial[2])
{
    for (int i = 0; i < 2; i++) {
        struct stat now;
        if (fstat(i + 1, &now) || now.st_dev != initial[i].st_dev || now.st_ino != initial[i].st_ino)
            fail("guest redirection changed a host stream");
    }
}

static void run(const char *path, int deferred, const struct stat streams[2])
{
    session = chm_session_create();
    if (!session) fail("create");
    const char *root = getenv("CHM_SANDBOX_ROOT");
    if (root) chm_foundation_set_root(root);
    char *args[] = {(char *)path, deferred ? "deferred" : "immediate", NULL};
    if (chm_session_load(session, path) || chm_session_run(session, 2, args) || !chm_ui_running()) fail("launch");
    guest_state = NULL;
    for (int i = 0; i < session->export_count; i++) {
        if (!strcmp(session->exports[i].name, "_startup_state"))
            guest_state = (int (*)(int))(uintptr_t)session->exports[i].addr;
    }
    if (!guest_state) fail("state export");
    assert_host_streams(streams);

    frame initial = {.label = "Send"};
    int first = chm_ui_paint(inspect, &initial, 720, 1280);
    if (deferred ? first != 0 : (first != 1 || !initial.found)) fail("first paint before main queue");
    await_state(0);
    frame ready = {.label = "Send"};
    if (chm_ui_paint(inspect, &ready, 720, 1280) != 1 || !ready.found) fail("paint while reader waits");
    if (chm_ui_pointer(0, ready.x, ready.y) != 1 || chm_ui_pointer(2, ready.x, ready.y) != 1) fail("button input");
    await_state(1);
    frame delivered = {.label = "Delivered"};
    if (chm_ui_paint(inspect, &delivered, 720, 1280) != 1 || !delivered.found) fail("redraw after delivery");

    const uint64_t before = milliseconds();
    chm_session_destroy(session);
    session = NULL;
    if (milliseconds() - before > 5000 || chm_dispatch_pending()) fail("reader shutdown");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    struct stat initial[2];
    if (fstat(1, &initial[0]) || fstat(2, &initial[1])) fail("host streams");
    int baseline = descriptors();
    for (int i = 0; i < 4; i++) {
        run(argv[1], i % 2, initial);
        assert_host_streams(initial);
        if (descriptors() != baseline) fail("descriptor leak after reload");
    }
    puts("startup: immediate/deferred window, live pipe reader, button delivery, redraw and four clean session teardowns passed");
    return 0;
}

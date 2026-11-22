#include "chm_session.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static chm_session *session;
static int (*guest_check)(int);

static int check_body(void *context)
{
    return guest_check(*(int *)context);
}

static void fail(const char *operation, int value)
{
    fprintf(stderr, "input failure: %s rc=%d\n%s\n", operation, value, session ? session->log_buf : "");
    exit(1);
}

static void check(int stage)
{
    int rc = chm_session_callback(session, check_body, &stage);
    if (rc) fail("guest assertion", rc);
}

static int pointer(int phase, double x, double y)
{
    int rc = chm_ui_pointer(phase, x, y);
    if (rc < 0) fail("pointer", rc);
    return rc;
}

static void tap(double x, double y, int expected)
{
    pointer(0, x, y);
    int rc = pointer(2, x, y);
    if (rc != expected) fail("tap result", rc);
}

static void pixel(uint32_t expected)
{
    uint32_t *buf = calloc(720u * 1280u, sizeof(*buf));
    if (!buf) fail("allocation", -1);
    if (chm_ui_render(buf, 720, 1280) != 1 || buf[600 * 720 + 400] != expected)
        fail("render after input", (int)buf[600 * 720 + 400]);
    free(buf);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    session = chm_session_create();
    if (!session) return 1;
    const char *root = getenv("CHM_SANDBOX_ROOT");
    if (root) chm_foundation_set_root(root);
    if (chm_session_load(session, argv[1]) || chm_session_run(session, 1, argv + 1)) fail("entry", -1);
    for (int i = 0; i < session->export_count; i++) {
        if (!strcmp(session->exports[i].name, "_input_check"))
            guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
    }
    if (!guest_check) fail("missing check export", -1);
    check(0);
    pixel(0xffff0000u);
    tap(540,1250,1); check(1); pixel(0xff0000ffu);
    tap(180,1250,1); check(2); pixel(0xffff0000u);
    tap(70,150,1); check(3);
    tap(70,150,1); check(4);
    pointer(0,70,150); pointer(1,120,150); pointer(2,70,150); check(5);
    pointer(0,70,150); pointer(3,70,150); pointer(2,70,150); check(6);
    check(7); tap(70,150,0); check(8);
    check(9); tap(70,180,0); check(10);
    tap(70,150,1); check(11);
    tap(700,22,1); check(12); pixel(0xff00ff00u);
    tap(540,1250,0); tap(70,150,0); check(13);
    tap(20,22,1); check(14); pixel(0xffff0000u);
    check(15); tap(20,22,1); check(16);
    tap(-1,150,0); tap(720,1280,0); tap(NAN,150,0); check(17);
    check(18); tap(700,22,0); check(19);
    check(20);
    pointer(0,70,150);
    int rc = chm_ui_pointer(2,70,150);
    if (rc != -2 || chm_trap_signal() != SIGTRAP) fail("callback fault guard", rc);
    if (chm_ui_pointer(0,70,150) != -1) fail("faulted session accepted input", -1);
    puts("input: post-entry clicks, target/action, modal isolation, cancel and fault guard passed");
    chm_session_destroy(session);
    session = NULL;
    return 0;
}
#include "chm_session.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static chm_session *session;
static int (*guest_check)(int);
static int primary_labels;
static double primary_left, primary_top, primary_width, primary_right;

static void fail(const char *what, int rc)
{
    fprintf(stderr, "split failure: %s rc=%d\n%s\n", what, rc, session ? session->log_buf : "");
    exit(1);
}

static int check_body(void *context)
{
    return guest_check(*(int *)context);
}

static void check(int stage)
{
    int rc = chm_session_callback(session, check_body, &stage);
    if (rc) fail("guest assertion", rc);
}

static void inspect(void *context, const chm_draw_command *command)
{
    (void)context;
    if (command->kind == 1 && !strcmp(command->text, "Primary")) {
        primary_labels++;
        primary_left = command->x;
        primary_top = command->y;
        primary_width = command->width;
        primary_right = command->clip_right;
    }
}

static void paint(int width, int height, int primary, double right, int red)
{
    primary_labels = 0;
    primary_right = 0;
    if (chm_ui_paint(inspect, NULL, width, height) != 1) fail("paint", -1);
    if (primary_labels != primary || (primary && (primary_left != 16 || primary_top != 44 ||
        primary_width != right - 32 || primary_right != right - 16))) {
        fprintf(stderr, "primary geometry: count=%d x=%.8g y=%.8g width=%.8g clip_right=%.8g column=%.8g\n",
                primary_labels, primary_left, primary_top, primary_width, primary_right, right);
        fail("primary clipping", primary_labels);
    }
    size_t count = (size_t)width * (size_t)height;
    uint32_t *pixels = calloc(count, sizeof(*pixels));
    if (!pixels || chm_ui_render(pixels, width, height) != 1) fail("render", -1);
    uint32_t sample = pixels[(size_t)(height / 2) * (size_t)width + (size_t)(width - 10)];
    free(pixels);
    if (sample != (red ? 0xffff0000u : 0)) fail("detail pixels", (int)sample);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    session = chm_session_create();
    if (!session) return 1;
    const char *root = getenv("CHM_SANDBOX_ROOT");
    if (root) chm_foundation_set_root(root);
    if (chm_session_load(session, argv[1]) || chm_session_run(session, 1, argv + 1)) fail("entry", -1);
    for (int i = 0; i < session->export_count; i++)
        if (!strcmp(session->exports[i].name, "_split_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
    if (!guest_check) fail("check export", -1);
    paint(720, 1280, 1, 180, 1); check(0);
    check(1); paint(720, 1280, 0, 0, 1);
    check(2); paint(720, 1280, 1, 180, 1);
    if (chm_ui_pointer(0, 50, 65) < 0 || chm_ui_pointer(2, 50, 65) != 1) fail("primary touch", -1);
    check(7);
    check(3); paint(720, 1280, 1, 250, 1);
    if (chm_ui_viewport(1600, 720)) fail("resize", -1);
    paint(1600, 720, 1, 250, 1); check(4);
    check(5); paint(1600, 720, 0, 0, 0);
    check(6); paint(1600, 720, 0, 0, 1);
    chm_session_destroy(session);
    puts("split: inheritance, lazy table controller, two columns, overlay, secondary-only, lifecycle, resize, touch and detach passed");
    return 0;
}

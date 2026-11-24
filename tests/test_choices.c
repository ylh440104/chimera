#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static chm_session *session;
static int (*guest_check)(int);
static int titles, picker_texts, headers, icons, geometry;
static int saw_selected;

static void fail(const char *where, int rc)
{
    fprintf(stderr, "choices failure: %s rc=%d\n%s\n", where, rc, session ? session->log_buf : "");
    exit(1);
}

static int check_body(void *context)
{
    return guest_check(*(int *)context);
}

static void check(int stage)
{
    int rc = chm_session_callback(session, check_body, &stage);
    if (rc) { fprintf(stderr, "stage=%d\n", stage); fail("guest assertion", rc); }
}

static void inspect(void *context, const chm_draw_command *command)
{
    (void)context;
    if (!isfinite(command->x) || !isfinite(command->y) || !isfinite(command->width) || !isfinite(command->height)) fail("finite geometry", -1);
    if (command->kind == 3) icons++;
    if (command->kind == 2 && command->color == 0xff0a84ffu) saw_selected++;
    if (command->kind != 1) return;
    const char *text = command->text;
    if (!strcmp(text, "One") || !strcmp(text, "Two") || !strcmp(text, "Three") || !strcmp(text, "Last") || !strcmp(text, "Restored")) titles++;
    if (!strncmp(text, "P0-", 3) || !strncmp(text, "Custom-", 7)) {
        picker_texts++;
        if (command->clip_top < 100 || command->clip_bottom > 260 || command->clip_left < 20 || command->clip_right > 320) fail("picker clipping", -1);
        if (!strcmp(text, "P0-1") && command->x == 20 && command->y == 160 && command->width == 100) geometry++;
    }
    if (!strcmp(text, "Header title")) {
        headers++;
        if (command->x != 36 || command->y != 300 || command->width != 268 || command->height != 30) fail("header geometry", -1);
    }
    if (!strcmp(text, "Header detail") || !strcmp(text, "Footer title")) headers++;
}

static void draw(void)
{
    titles = picker_texts = headers = icons = geometry = saw_selected = 0;
    int rc = chm_ui_paint(inspect, NULL, 400, 600);
    if (rc != 1) fail("paint", rc);
}

static void tap(double x, double y, int expected)
{
    if (chm_ui_pointer(0, x, y) < 0) fail("down", -1);
    int rc = chm_ui_pointer(2, x, y);
    if (rc != expected) fail("up", rc);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    for (int round = 0; round < 3; round++) {
        session = chm_session_create();
        if (!session) return 1;
        const char *root = getenv("CHM_SANDBOX_ROOT");
        if (root) chm_foundation_set_root(root);
        chm_ui_viewport(400, 600);
        int rc = chm_session_load(session, argv[1]);
        if (rc) fail("load", rc);
        rc = chm_session_run(session, 1, argv + 1);
        if (rc) fail("entry", rc);
        guest_check = NULL;
        for (int i = 0; i < session->export_count; i++)
            if (!strcmp(session->exports[i].name, "_choices_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
        if (!guest_check) fail("check export", -1);
        draw(); check(0);
        if (titles != 3 || headers != 3 || picker_texts != 7 || geometry != 1 || saw_selected != 1) fail("initial drawing", -1);
        tap(170, 40, 0); check(1);
        tap(270, 40, 1); check(2);
        tap(270, 40, 0); check(2);
        if (chm_ui_pointer(0, 70, 40) < 0 || chm_ui_pointer(3, 70, 40) != 0) fail("cancel", -1);
        check(2); check(3); tap(70, 40, 1); check(4);
        draw();
        if (titles != 3 || saw_selected != 1) fail("mutation drawing", -1);
        tap(70, 220, 1); check(5);
        if (chm_ui_pan(0, 70, 180) < 0 || chm_ui_pan(1, 70, 100) != 1) fail("picker drag", -1);
        check(6); draw();
        if (chm_ui_pan(2, 70, 100) != 1) fail("picker settle", -1);
        check(7); tap(200, 210, 1); check(8);
        check(9); draw();
        if (headers != 3 || picker_texts != 5) fail("component reload", -1);
        check(10); draw(); check(11); draw();
        check(12); draw();
        if (titles || picker_texts || headers || saw_selected) fail("empty controls", -1);
        check(13); draw();
        if (titles != 1 || !icons || headers != 3 || !picker_texts) fail("restore controls", -1);
        chm_session_destroy(session);
        session = NULL;
        if (chm_ui_view_count() || chm_ui_running()) fail("teardown", -1);
    }
    puts("choices: segments, disabled and momentary events, picker geometry, custom rows, drag, component reload, callback reentry, header reuse and three teardowns passed");
    return 0;
}
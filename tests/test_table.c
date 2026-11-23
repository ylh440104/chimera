#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static chm_session *session;
static int (*guest_check)(int);
static int text_count, switch_count, saw_first, saw_second, saw_target;
static uint32_t switch_color;
static int saw_target_icon;

static void fail(const char *where, int rc)
{
    fprintf(stderr, "table failure: %s rc=%d\n%s\n", where, rc, session ? session->log_buf : "");
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

static void inspect(void *context, const chm_draw_command *c)
{
    (void)context;
    if (c->kind == 1) {
        text_count++;
        if (!strcmp(c->text, "Row 0-0")) {
            saw_first++;
            if (c->x != 36 || c->y != 72 || c->clip_left < 20 || c->clip_right > 340) fail("cell text geometry", -1);
        }
        if (!strcmp(c->text, "Row 0-1")) saw_second++;
        if (!strcmp(c->text, "Row 1-1") && c->y == 136 && c->clip_top <= 145 && c->clip_bottom > 145) saw_target++;
        if (c->clip_top < 40 || c->clip_bottom > 200) fail("table text clipping", -1);
    }
    if (c->kind == 3 && c->x == 36 && c->y == 146 && c->width == 24 && c->height == 24 &&
        c->clip_top <= c->y && c->clip_bottom >= c->y + c->height && c->text && c->text[0] == 'M') saw_target_icon++;
    if (c->kind == 2) {
        switch_count++;
        if (c->width == 51) switch_color = c->color;
    }
}

static void draw(void)
{
    text_count = switch_count = saw_first = saw_second = saw_target = saw_target_icon = 0;
    switch_color = 0;
    int rc = chm_ui_paint(inspect, NULL, 720, 1280);
    if (rc != 1) fail("drawing", rc);
}

static void tap(double x, double y, int expected)
{
    int rc = chm_ui_pointer(0, x, y);
    if (rc < 0) fail("touch down", rc);
    rc = chm_ui_pointer(2, x, y);
    if (rc != expected) fail("touch up", rc);
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
        if (!strcmp(session->exports[i].name, "_table_check"))
            guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
    if (!guest_check) fail("check export", -1);
    draw(); check(0);
    if (saw_first != 1 || saw_second != 1 || switch_count != 2 || switch_color != 0xff30d158u) fail("initial drawing", -1);
    tap(299, 94, 1); check(1); draw();
    if (switch_color != 0xff55555au) fail("switch off drawing", -1);
    tap(60, 138, 1); check(2);
    if (chm_ui_pan(0, 80, 160) < 0 || chm_ui_pan(1, 80, 60) != 1) fail("live drag", -1);
    draw();
    if (saw_first || !saw_second || switch_count || saw_target != 1) fail("viewport during drag", -1);
    if (saw_target_icon != 1) fail("row icon during drag", saw_target_icon);
    if (chm_ui_pan(2, 80, 60) < 0) fail("end drag", -1);
    tap(60, 145, 1); check(3);
    if (chm_ui_scroll(80, 120, -1000) != 1) fail("scroll back", -1);
    check(4); draw(); check(5);
    if (saw_first != 1 || switch_count != 2 || switch_color != 0xff55555au) fail("reuse drawing", -1);
    check(6); draw(); check(7);
    if (text_count || switch_count) fail("empty table kept cells", -1);
    check(8); draw(); check(9);
    if (saw_first != 1 || switch_count != 2) fail("restored table drawing", -1);
    tap(350, 94, 0);
    if (chm_ui_scroll(350, 94, 100) || chm_ui_scroll(80, 94, NAN)) fail("scroll outside table", -1);
    puts("table: lazy load, cell content, switch action, scroll hit, reuse and empty reload passed");
    chm_session_destroy(session);
    return 0;
}

#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static chm_session *session;
static int (*guest_check)(int);
static double field_x, field_y, confirm_x, confirm_y, disabled_x, disabled_y;
static int labels;
static void fail(const char *where, int rc)
{
    fprintf(stderr, "services failure: %s rc=%d\n%s\n", where, rc, session ? session->log_buf : "");
    exit(1);
}
static int check_body(void *context) { return guest_check(*(int *)context); }
static void check(int stage)
{
    int rc = chm_session_callback(session, check_body, &stage);
    if (rc) fail("guest assertion", rc);
}
static int pump(void *context) { return (int)chm_dispatch_pump(context); }
static void inspect(void *context, const chm_draw_command *c)
{
    (void)context;
    if (c->kind != 1) return;
    labels++;
    if (!strcmp(c->text, "initial")) { field_x = c->x + c->width / 2; field_y = c->y + c->height / 2; }
    if (!strcmp(c->text, "Confirm")) { confirm_x = c->x + c->width / 2; confirm_y = c->y + c->height / 2; }
    if (!strcmp(c->text, "Disabled")) { disabled_x = c->x + c->width / 2; disabled_y = c->y + c->height / 2; }
}
static void tap(double x, double y, int expected)
{
    if (chm_ui_pointer(0, x, y) < 0) fail("pointer down", -1);
    int rc = chm_ui_pointer(2, x, y);
    if (rc != expected) fail("pointer up", rc);
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    session = chm_session_create();
    if (!session) return 1;
    const char *root = getenv("CHM_SANDBOX_ROOT");
    if (root) chm_foundation_set_root(root);
    if (chm_session_load(session, argv[1])) fail("load", -1);
    int rc = chm_session_run(session, 1, argv + 1);
    if (rc) fail("entry", rc);
    for (int i = 0; i < session->export_count; i++)
        if (!strcmp(session->exports[i].name, "_services_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
    if (!guest_check) fail("check export", -1);
    if (chm_session_callback(session, pump, session) != 3) fail("queue callbacks", -1);
    check(0);
    if (chm_ui_paint(inspect, NULL, 720, 1280) != 1 || labels != 5 || !field_y || !confirm_y || !disabled_y) fail("dialog rendering", labels);
    tap(disabled_x, disabled_y, 0);
    tap(field_x, field_y, 1);
    uint64_t token = chm_ui_focused_field();
    if (!token || strcmp(chm_ui_field_placeholder(), "Name") || strcmp(chm_ui_field_text(), "initial")) fail("field focus", -1);
    if (chm_ui_edit_text(token, "discarded", 0) != 1) fail("cancel edit", -1);
    tap(field_x, field_y, 1);
    token = chm_ui_focused_field();
    if (strcmp(chm_ui_field_text(), "initial")) fail("cancel changed text", -1);
    if (chm_ui_edit_text(token + 1, "wrong", 1) != 0) fail("stale field token", -1);
    if (chm_ui_edit_text(token, "edited 中文", 1) != 1) fail("commit edit", -1);
    tap(confirm_x, confirm_y, 1); check(1);
    chm_network_request request;
    int count = 0;
    while (chm_network_next(&request)) {
        count++;
        if (strcmp(request.method, "GET") || request.body_size || request.timeout_ms != 30000) fail("request metadata", -1);
        if (!strcmp(request.url, "https://example.com/cancel")) {
            if (!request.cancelled || chm_network_complete(request.token, 0, "", 0, "{}", "cancelled") != 1) fail("cancel delivery", -1);
        } else {
            if (strcmp(request.url, "https://example.com/data") || request.cancelled) fail("request URL", -1);
            const char *body = "{\"value\":7}";
            if (chm_network_complete(request.token, 200, body, strlen(body), "{\"Content-Type\":\"application/json\"}", "") != 1) fail("network callback", -1);
            if (chm_network_complete(request.token, 200, body, strlen(body), "{}", "") != 0) fail("duplicate callback", -1);
        }
    }
    if (count != 2) fail("request count", count);
    check(2);
    char mapped[2048], again[2048];
    if (!chm_foundation_map_path("/var/mobile/Documents/services/account.json", mapped, sizeof(mapped)) ||
        !chm_foundation_map_path(mapped, again, sizeof(again)) || strcmp(mapped, again)) fail("double path mapping", -1);
    if (chm_foundation_map_path("/../../escape", mapped, sizeof(mapped))) fail("path traversal", -1);
    chm_session_destroy(session);
    if (chm_dispatch_pending()) fail("queue reset", -1);
    puts("services: escaped blocks, global/main queues, JSON/plist persistence, directory sorting, notifications, dialog editing and URL task completion passed");
    return 0;
}
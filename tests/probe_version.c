#include "chm_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *catalog_url = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
static chm_session *session;
static FILE *log_file;
static const char *wanted;
static double button_x, button_y, row_x, row_y;
static int account_message;

static void inspect(void *context, const chm_draw_command *c)
{
    (void)context;
    if (c->kind != 1 || !c->text) return;
    double x = (c->clip_left + c->clip_right) * 0.5, y = (c->clip_top + c->clip_bottom) * 0.5;
    if (!strcmp(c->text, "+") && y < 60 && x > 560) { button_x = x; button_y = y; }
    if (!strcmp(c->text, wanted)) { row_x = x; row_y = y; }
    if (!account_message && (strstr(c->text, "online account") || strstr(c->text, "local account"))) {
        account_message = 1;
        fprintf(log_file, "guest dialog: %s\n", c->text);
    }
}
static int paint(void)
{
    row_y = -1;
    return chm_ui_paint(inspect, NULL, 720, 1280);
}
static int tap(double x, double y)
{
    return chm_ui_pointer(0, x, y) > 0 ? chm_ui_pointer(2, x, y) : -1;
}
static int deliver(chm_network_request *r, const char *path)
{
    FILE *source = fopen(path, "rb");
    if (!source) return -1;
    int ok = fseek(source, 0, SEEK_END) == 0;
    long size = ok ? ftell(source) : -1;
    ok = size > 0 && size <= 16L * 1024 * 1024 && fseek(source, 0, SEEK_SET) == 0;
    unsigned char *data = ok ? malloc((size_t)size) : NULL;
    ok = data && fread(data, 1, (size_t)size, source) == (size_t)size;
    fclose(source);
    if (r->download) {
        FILE *file = r->download_fd < 0 ? NULL : fdopen(r->download_fd, "wb");
        if (!file && r->download_fd >= 0) close(r->download_fd);
        ok = file && ok && fwrite(data, 1, (size_t)size, file) == (size_t)size;
        if (file && fclose(file)) ok = 0;
    }
    int rc = ok ? chm_network_complete(r->token, 200, r->download ? NULL : data, r->download ? 0 : (size_t)size,
                                       "{\"Content-Type\":\"application/json\"}", "") : -1;
    free(data);
    fprintf(log_file, "delivered %s %ld bytes rc=%d\n", r->download ? "file" : "data", size, rc);
    return rc;
}
int main(int argc, char **argv)
{
    if (argc != 7 || !getenv("CHM_SANDBOX_ROOT")) return 2;
    wanted = argv[3];
    log_file = fopen(argv[6], "w");
    if (!log_file) return 2;
    setvbuf(log_file, NULL, _IOLBF, 0);
    session = chm_session_create();
    if (!session) return 2;
    chm_foundation_set_root(getenv("CHM_SANDBOX_ROOT"));
    int picked = 0, manifest = 0, metadata = 0, outcome = 1;
    if (chm_session_load(session, argv[1]) || chm_session_run(session, 1, argv + 1)) goto done;
    if (paint() != 1) goto done;
    if (!button_y) { button_x = 701; button_y = 22; }
    fprintf(log_file, "open installer at %.1f,%.1f\n", button_x, button_y);
    if (tap(button_x, button_y) != 1) goto done;
    for (unsigned i = 0; i < 1000 && session->exit_code >= 0; i++) {
        size_t callbacks = chm_dispatch_pump(session);
        chm_network_request req;
        chm_guest_lock();
        int found = chm_network_next(&req);
        chm_guest_unlock();
        if (found) {
            if (!strcmp(req.method, "GET") && !strcmp(req.url, catalog_url) && !manifest) {
                fprintf(log_file, "catalog request\n");
                if (deliver(&req, argv[2]) != 1) break;
                manifest = 1;
            } else if (picked && !strcmp(req.method, "GET") && !strcmp(req.url, argv[4]) && !metadata) {
                fprintf(log_file, "selected version %s requested its manifest URL\n", wanted);
                if (!req.download || deliver(&req, argv[5]) != 1) break;
                metadata = 1;
            } else {
                if (req.download_fd >= 0) close(req.download_fd);
                fprintf(log_file, "next request observed after metadata=%d\n", metadata);
                if (metadata) outcome = 0;
                break;
            }
        }
        if (found || callbacks || i % 20 == 0) {
            if (paint() != 1) break;
            if (!picked && manifest && row_y >= 0) {
                int result = tap(row_x, row_y);
                fprintf(log_file, "tap %s at %.1f,%.1f rc=%d\n", wanted, row_x, row_y, result);
                if (result != 1) break;
                picked = 1;
            }
            if (picked && account_message) { outcome = 0; break; }
            if (metadata && !chm_dispatch_pending()) { outcome = 0; break; }
        }
        struct timespec interval = {0, 10000000};
        nanosleep(&interval, NULL);
    }
done:
    if (session->exit_code < 0) outcome = 1;
    fprintf(log_file, "summary catalog=%d picked=%d metadata=%d account_dialog=%d guest_exit=%d\n", manifest, picked, metadata, account_message, session->exit_code);
    char report[32768];
    chm_session_report(session, report, sizeof(report));
    fputs(report, log_file);
    chm_session_destroy(session);
    fclose(log_file);
    return outcome;
}
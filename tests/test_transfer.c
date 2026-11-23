#include "chm_session.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static chm_session *session;
static int (*guest_check)(int);
static int seen;

static void require(int yes, const char *where)
{
    if (!yes) {
        fprintf(stderr, "transfer regression failed at %s\n%s\n", where, session ? session->log_buf : "");
        exit(1);
    }
}
static int check_body(void *stage) { return guest_check(*(int *)stage); }
static void check(int stage)
{
    int rc = chm_session_callback(session, check_body, &stage);
    if (rc) fprintf(stderr, "guest stage=%d result=%d\n", stage, rc);
    require(!rc, "guest check");
}
static void inspect(void *expected, const chm_draw_command *cmd)
{
    if (cmd->kind == 1 && cmd->text && !strcmp(expected, cmd->text)) seen++;
}
static void frame(const char *label)
{
    seen = 0;
    require(chm_ui_paint(inspect, (void *)label, 720, 1280) == 1 && seen == 1, label);
}
static chm_network_request pick(void)
{
    require(chm_ui_pointer(0, 40, 66) == 1 && chm_ui_pointer(2, 40, 66) == 1, "row tap");
    chm_network_request r, other;
    require(chm_network_next(&r) && r.download && !r.cancelled && r.download_fd >= 0, "download descriptor");
    require(r.timeout_ms == 7500 && !strcmp(r.method, "GET") && !strcmp(r.url, "https://example.invalid/sample"), "selected version request");
    require(!chm_network_next(&other), "duplicate submission");
    return r;
}
static void idle(const char *path)
{
    DIR *dir = opendir(path);
    require(dir != NULL, "open temporary directory");
    struct dirent *e;
    while ((e = readdir(dir))) require(strncmp(e->d_name, "chm-transfer-", 13) != 0, "partial file retained");
    closedir(dir);
}
int main(int argc, char **argv)
{
    require(argc == 2 && getenv("CHM_SANDBOX_ROOT"), "arguments");
    session = chm_session_create();
    require(session != NULL, "session");
    chm_foundation_set_root(getenv("CHM_SANDBOX_ROOT"));
    require(!chm_session_load(session, argv[1]) && !chm_session_run(session, 1, argv + 1), "guest entry");
    for (int i = 0; i < session->export_count; i++)
        if (!strcmp(session->exports[i].name, "_transfer_check")) guest_check = (int (*)(int))(uintptr_t)session->exports[i].addr;
    require(guest_check != NULL, "guest assertions");
    char final[2048], temp[2048];
    require(chm_foundation_map_path("/var/mobile/Documents/sample-sample.bin", final, sizeof(final)) &&
            chm_foundation_map_path("/var/mobile/tmp", temp, sizeof(temp)), "sandbox");
    frame("waiting");
    const double period = 1.0 / 60;
    require(chm_ui_vsync(10, period) == 1, "first vsync"); frame("frame 1");
    require(chm_ui_vsync(10 + period, period) == 0 && chm_ui_vsync(10 + period * 2, period) == 1, "preferred rate");
    frame("frame 2"); check(0);

    chm_network_request r = pick();
    require(write(r.download_fd, "unavailable", 11) == 11 && !close(r.download_fd), "HTTP error body");
    require(chm_network_complete(r.token, 503, NULL, 0, "{}", "") == 1, "HTTP error completion");
    require(access(final, F_OK) != 0, "failed response published"); idle(temp); check(1);

    r = pick();
    enum { total = 33 * 1024 * 1024 + 1 };
    unsigned char block[65536]; memset(block, 'Z', sizeof(block));
    for (size_t i = 0; i < (total - 1) / sizeof(block); i++) {
        require(write(r.download_fd, block, sizeof(block)) == (ssize_t)sizeof(block), "stream to file");
        if (i + 1 == (total - 1) / sizeof(block) / 2) {
            require(chm_network_progress(r.token, total / 2, total) == 1, "progress");
            check(2);
        }
    }
    require(write(r.download_fd, "!", 1) == 1 && !close(r.download_fd), "finish body");
    require(chm_network_complete(r.token, 200, NULL, 0, "{}", "") == 1, "completed download");
    require(chm_network_complete(r.token, 200, NULL, 0, "{}", "") == 0, "duplicate completion"); check(3); idle(temp);
    FILE *file = fopen(final, "rb"); require(file != NULL, "open published file");
    for (size_t i = 0; i < (total - 1) / sizeof(block); i++) {
        require(fread(block, 1, sizeof(block), file) == sizeof(block), "read published file");
        for (size_t j = 0; j < sizeof(block); j++) require(block[j] == 'Z', "published bytes");
    }
    require(fgetc(file) == '!' && fgetc(file) == EOF && !fclose(file), "published tail");

    r = pick(); check(4);
    require(chm_ui_vsync(11, period) == 0 && chm_network_progress(r.token, 0, total) == 0, "pause and cancel");
    require(!close(r.download_fd) && chm_network_complete(r.token, 0, NULL, 0, "{}", "cancelled") == 1, "cancel completion");
    require(!chm_network_next(&r), "cancel resubmitted request"); check(5); idle(temp);
    require(chm_ui_vsync(12, period) == 1, "resume display"); frame("frame 3"); check(6);
    require(chm_ui_vsync(13, period) == 0, "invalidated display link");
    chm_session_destroy(session);
    puts("version row: HTTP 503 then successful retry, 34603009 verified bytes, SHA-1 c73d1086a940fe6a14f909211904932ff9c74e6e; cancellation removed partial file");
    puts("display link: three distinct guest frames, preferred rate, pause, resume and invalidate passed");
    return 0;
}
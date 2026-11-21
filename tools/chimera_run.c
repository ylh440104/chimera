#include "chm_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: chimera_run <app-exe> [args...]\n");
        return 2;
    }
    chm_session *s = chm_session_create();
    if (!s) {
        fprintf(stderr, "session alloc failed\n");
        return 1;
    }
    const char *root = getenv("CHM_SANDBOX_ROOT");
    if (root && *root)
        chm_foundation_set_root(root);
    int rc = chm_session_load(s, argv[1]);
    if (rc) {
        char out[16384];
        chm_session_report(s, out, sizeof(out));
        fprintf(stderr, "%s", out);
        chm_session_destroy(s);
        return 1;
    }
    {
        char out[16384];
        chm_session_report(s, out, sizeof(out));
        printf("%s", out);
    }
    if (!s->images[0].executable) {
        printf("main image not executable: %d unresolved references\n",
               s->images[0].nfixup_missing);
        chm_session_destroy(s);
        return 3;
    }
    printf("executing main entry\n");
    fflush(stdout);
    rc = chm_session_run(s, argc - 1, argv + 1);
    {
        char out[65536];

        chm_session_report(s, out, sizeof(out));
        printf("%s", out);
    }
    if (rc == -2) {
        printf("entry trapped: signal=%d fault=0x%llx pc=0x%llx lr=0x%llx image=0x%llx\n",
               chm_trap_signal(), (unsigned long long)chm_trap_fault_addr(),
               (unsigned long long)chm_trap_pc(), (unsigned long long)chm_trap_lr(),
               (unsigned long long)s->images[0].vm_min);
    } else {
        printf("entry returned %d\n", rc);
    }
    const char *frame_path = getenv("CHM_FRAME_OUT");
    if (rc == 0 && frame_path && *frame_path) {
        size_t count = 720u * 1280u;
        uint32_t *pixels = calloc(count, sizeof(*pixels));
        if (!pixels || chm_ui_render(pixels, 720, 1280) != 1) {
            fprintf(stderr, "frame unavailable\n");
            rc = 4;
        } else {
            FILE *file = fopen(frame_path, "wb");
            if (!file) rc = 5;
            else {
                if (fwrite(pixels, sizeof(*pixels), count, file) != count) rc = 5;
                if (fclose(file)) rc = 5;
            }
        }
        free(pixels);
    }
    chm_session_destroy(s);
    return rc;
}
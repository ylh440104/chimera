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
    if (rc == -2) {
        char out[16384];

        chm_session_report(s, out, sizeof(out));
        printf("%s", out);
        printf("entry trapped: signal=%d fault=0x%llx pc=0x%llx lr=0x%llx image=0x%llx\n",
               chm_trap_signal(), (unsigned long long)chm_trap_fault_addr(),
               (unsigned long long)chm_trap_pc(), (unsigned long long)chm_trap_lr(),
               (unsigned long long)s->images[0].vm_min);
    } else {
        printf("entry returned %d\n", rc);
    }
    chm_session_destroy(s);
    return rc;
}
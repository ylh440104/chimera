#include "chm_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    chm_image img;

    if (argc < 2) {
        fprintf(stderr, "usage: chmdump <macho> [bundle-dir]\n");
        return 2;
    }
    if (chm_open(&img, argv[1]) != 0) {
        fprintf(stderr, "chmdump: %s\n", img.error[0] ? img.error : "open failed");
        return 1;
    }
    chm_dump(&img, stdout);
    chm_close(&img);

    if (argc > 2) {
        chm_session *s = chm_session_create();
        char out[16384];

        if (!s)
            return 1;
        if (chm_session_load(s, argv[1]) == 0) {
            chm_session_report(s, out, sizeof(out));
            printf("\n%s", out);
            printf("executable=%d missing=%u\n",
                   s->images[0].executable, s->images[0].nfixup_missing);
        } else {
            chm_session_report(s, out, sizeof(out));
            fprintf(stderr, "%s", out);
        }
        chm_session_destroy(s);
    }
    return 0;
}
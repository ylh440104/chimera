#include "chm.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    chm_image img;
    int rc;

    if (argc < 2) {
        fprintf(stderr, "usage: chmrun <mach-o> [args...]\n");
        return 2;
    }
    if (chm_open(&img, argv[1]) != 0) {
        fprintf(stderr, "chimera: cannot open %s\n", argv[1]);
        return 1;
    }
    if (chm_fixup(&img) != 0) {
        fprintf(stderr, "chimera: load or fixup failed\n");
        chm_close(&img);
        return 1;
    }
    if (img.nfixup_missing)
        fprintf(stderr, "chimera: %u imports missing\n", img.nfixup_missing);
    rc = chm_run(&img, argc - 1, argv + 1);
    fprintf(stderr, "chimera: main returned %d\n", rc);
    chm_close(&img);
    return rc;
}
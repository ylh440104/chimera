#include "chm.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    chm_image img;

    if (argc < 2) {
        fprintf(stderr, "usage: chmdump <mach-o>\n");
        return 2;
    }
    if (chm_open(&img, argv[1]) != 0) {
        fprintf(stderr, "chimera: cannot open %s\n", argv[1]);
        return 1;
    }
    chm_dump(&img, stdout);
    chm_close(&img);
    return 0;
}
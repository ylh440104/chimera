#include "chm.h"

#include <stdlib.h>
#include <string.h>

extern char **environ;

static void *chm_entry_address(chm_image *img)
{
    uint32_t i;

    if (img->has_entry) {
        uint64_t base = img->vm_min;

        for (i = 0; i < img->nsegs; i++) {
            if (strcmp(img->segs[i].name, "__TEXT") == 0) {
                base = img->segs[i].vmaddr;
                break;
            }
        }
        return (void *)(uintptr_t)(base + img->slide + img->entryoff);
    }
    return chm_sym(img, "_main");
}

int chm_run(chm_image *img, int argc, char **argv)
{
    void *entry = chm_entry_address(img);
    int (*fn)(int, char **, char **, char **);

    if (!entry) {
        fprintf(stderr, "chimera: no entry point\n");
        return -1;
    }
    fn = (int (*)(int, char **, char **, char **))entry;
    return fn(argc, argv, environ, environ);
}
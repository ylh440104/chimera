#include "chm.h"
#include "chm_int.h"

#include <string.h>

int chm_ui_main(chm_image *img, int argc, char **argv, chm_report *r)
{
    void *fn = chm_sym(img, "_UIApplicationMain");

    if (!fn) {
        chm_report_line(r, "ui: 镜像没有 UIApplicationMain 符号");
        return -1;
    }
    chm_report_line(r, "ui: 调用 UIApplicationMain");
    {
        typedef int (*ui_main_fn)(int, char **, void *, void *);

        return ((ui_main_fn)fn)(argc, argv, NULL, NULL);
    }
}
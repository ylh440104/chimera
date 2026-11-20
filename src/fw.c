#include "chm.h"
#include "chm_int.h"

#include <string.h>

#define CHM_FW_REPLACED 0
#define CHM_FW_PENDING 1

typedef struct {
    const char *name;
    int kind;
} chm_fw_entry;

static const chm_fw_entry g_known[] = {
    { "/usr/lib/libSystem.B.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libobjc.A.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libc++.1.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libc++abi.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libdispatch.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libsystem_c.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libsystem_kernel.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libsystem_blocks.dylib", CHM_FW_REPLACED },
    { "/usr/lib/libswiftCore.dylib", CHM_FW_PENDING },
    { "/usr/lib/libswift_Concurrency.dylib", CHM_FW_PENDING },
    { "/System/Library/Frameworks/Foundation.framework/Foundation", CHM_FW_PENDING },
    { "/System/Library/Frameworks/UIKit.framework/UIKit", CHM_FW_PENDING },
    { "/System/Library/Frameworks/QuartzCore.framework/QuartzCore", CHM_FW_PENDING },
    { "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", CHM_FW_PENDING },
    { "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", CHM_FW_PENDING },
    { "/System/Library/Frameworks/Metal.framework/Metal", CHM_FW_PENDING },
    { "/System/Library/Frameworks/AVFoundation.framework/AVFoundation", CHM_FW_PENDING },
    { "/System/Library/Frameworks/Security.framework/Security", CHM_FW_PENDING },
    { NULL, 0 }
};

int chm_fw_load(chm_image *img, chm_report *r)
{
    uint32_t i;
    int replaced = 0;
    int pending = 0;
    int unknown = 0;
    int shown = 0;

    for (i = 0; i < img->ndylibs; i++) {
        const char *name = img->dylibs[i].name;
        int kind = -1;
        int k;

        for (k = 0; g_known[k].name; k++) {
            if (strcmp(g_known[k].name, name) == 0) {
                kind = g_known[k].kind;
                break;
            }
        }
        if (kind == CHM_FW_REPLACED) {
            replaced++;
        } else if (kind == CHM_FW_PENDING) {
            pending++;
            if (shown < 4) {
                chm_report_add(r, "fw:待搬运 %s", name);
                shown++;
            }
        } else {
            unknown++;
            if (unknown <= 4)
                chm_report_add(r, "fw:未知 %s", name);
        }
    }
    chm_report_add(r, "fw:依赖 %u 个，替身 %d，待搬运 %d，未知 %d",
                   img->ndylibs, replaced, pending, unknown);
    return pending + unknown;
}
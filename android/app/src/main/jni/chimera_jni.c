#include <jni.h>
#include <android/log.h>
#include <stdio.h>
#include <string.h>

#include "chm.h"
#include "chm_int.h"
#include "chm_rt.h"

#define TAG "chimera"

extern int chm_fixup_at(chm_image *img, uint64_t slide);
extern int chm_call_guarded(void (*fn)(void *), void *arg, char *err, size_t cap);

static chm_report g_report;

static void log_text(const char *text)
{
    __android_log_write(ANDROID_LOG_INFO, TAG, text);
}

static void pump_blocks(void *arg)
{
    chm_dispatch_pump((chm_report *)arg);
}

JNIEXPORT jstring JNICALL Java_dev_chimera_app_Native_run(JNIEnv *env, jclass cls, jstring path)
{
    const char *p;
    chm_image img;
    char err[256];
    int rc;
    int cryptid;
    int deps;

    (void)cls;
    memset(&g_report, 0, sizeof(g_report));
    err[0] = 0;

    p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p)
        return (*env)->NewStringUTF(env, "no path");

    chm_install_fault_handler();

    cryptid = chm_is_encrypted(p);
    if (cryptid < 0) {
        chm_report_line(&g_report, "不是可用的 Mach-O");
        goto done;
    }
    if (cryptid == 1) {
        chm_report_line(&g_report, "FairPlay 加密，需要先解密");
        goto done;
    }

    memset(&img, 0, sizeof(img));
    if (chm_open(&img, p) != 0) {
        chm_report_line(&g_report, "Mach-O 解析失败");
        goto done;
    }
    chm_report_add(&g_report, "镜像: 段 %u, dylib %u, 符号 %u, 入口 0x%llx",
                   img.nsegs, img.ndylibs, img.nsyms,
                   (unsigned long long)img.entryoff);

    deps = chm_fw_load(&img, &g_report);
    chm_dylib_load_all(&img, p, &g_report);
    chm_report_add(&g_report, "依赖未满足 %d", deps);

    if (chm_fixup(&img) != 0) {
        chm_report_line(&g_report, "映射或绑定失败");
        chm_close(&img);
        goto done;
    }
    chm_report_line(&g_report, "已映射并绑定");

    chm_objc_scan(&img, &g_report);
    chm_dispatch_init();

    rc = chm_run_guarded(&img, 0, NULL, err, sizeof(err));
    if (rc == -2)
        chm_report_add(&g_report, "执行中断: %s", err);
    else
        chm_report_add(&g_report, "入口返回 %d", rc);

    if (chm_dispatch_pending() > 0) {
        int prc = chm_call_guarded(pump_blocks, &g_report, err, sizeof(err));

        if (prc == -2)
            chm_report_add(&g_report, "dispatch 中断: %s", err);
    }

    if (chm_missing_count() > 0) {
        char line[300];
        int i;
        int shown = chm_missing_count() < 10 ? chm_missing_count() : 10;

        snprintf(line, sizeof(line), "缺 %d 个符号:", chm_missing_count());
        for (i = 0; i < shown; i++) {
            size_t used = strlen(line);

            snprintf(line + used, sizeof(line) - used, " %s", chm_missing_at(i));
        }
        chm_report_line(&g_report, line);
    } else {
        chm_report_line(&g_report, "没有缺失符号");
    }

    chm_close(&img);

done:
    (*env)->ReleaseStringUTFChars(env, path, p);
    log_text(g_report.text);
    return (*env)->NewStringUTF(env, g_report.text);
}
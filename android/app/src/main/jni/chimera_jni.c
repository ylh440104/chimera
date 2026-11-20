#include <jni.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chm.h"
#include "chm_rt.h"

#define TAG "chimera"
#define REPORT_MAX 2048

static void log_line(const char *text)
{
    __android_log_write(ANDROID_LOG_INFO, TAG, text);
}

static void append(char *buf, size_t cap, const char *text)
{
    size_t used = strlen(buf);
    size_t room = cap > used ? cap - used : 0;

    if (room > 1)
        snprintf(buf + used, room, "%s", text);
}

JNIEXPORT jstring JNICALL Java_dev_chimera_app_Native_run(JNIEnv *env, jclass cls, jstring path)
{
    const char *p;
    chm_image img;
    char report[REPORT_MAX];
    char err[256];
    int rc;
    int cryptid;
    int i;

    (void)cls;
    report[0] = 0;
    err[0] = 0;

    p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) {
        log_line("no path");
        return (*env)->NewStringUTF(env, "no path");
    }
    log_line(p);

    chm_install_fault_handler();

    cryptid = chm_is_encrypted(p);
    if (cryptid < 0) {
        append(report, sizeof(report), "不是可用的 Mach-O");
        goto done;
    }
    if (cryptid == 1) {
        append(report, sizeof(report), "二进制是 FairPlay 加密的，需要先解密");
        goto done;
    }

    memset(&img, 0, sizeof(img));
    if (chm_open(&img, p) != 0) {
        append(report, sizeof(report), "Mach-O 解析失败");
        goto done;
    }

    {
        char line[256];

        snprintf(line, sizeof(line), "段 %u，dylib %u，符号 %u",
                 img.nsegs, img.ndylibs, img.nsyms);
        append(report, sizeof(report), line);
        append(report, sizeof(report), "\n");
    }

    if (chm_fixup(&img) != 0) {
        append(report, sizeof(report), "映射或绑定失败");
        chm_close(&img);
        goto done;
    }
    append(report, sizeof(report), "镜像已映射并完成绑定\n");

    rc = chm_run_guarded(&img, 0, NULL, err, sizeof(err));
    {
        char line[512];

        if (rc == -2)
            snprintf(line, sizeof(line), "执行中断：%s\n", err);
        else
            snprintf(line, sizeof(line), "入口返回 %d\n", rc);
        append(report, sizeof(report), line);
    }

    if (chm_missing_count() > 0) {
        char line[256];

        snprintf(line, sizeof(line), "缺 %d 个符号：", chm_missing_count());
        append(report, sizeof(report), line);
        for (i = 0; i < chm_missing_count() && i < 8; i++) {
            append(report, sizeof(report), " ");
            append(report, sizeof(report), chm_missing_at(i));
        }
        if (chm_missing_count() > 8)
            append(report, sizeof(report), " ...");
        append(report, sizeof(report), "\n");
    } else {
        append(report, sizeof(report), "没有缺失符号\n");
    }

    chm_close(&img);

done:
    (*env)->ReleaseStringUTFChars(env, path, p);
    log_line(report);
    return (*env)->NewStringUTF(env, report);
}
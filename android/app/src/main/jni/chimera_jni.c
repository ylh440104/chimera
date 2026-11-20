#include <jni.h>
#include <android/log.h>
#include <stdlib.h>

#include "chm.h"

#define TAG "chimera"

static void log_line(const char *text)
{
    __android_log_write(ANDROID_LOG_INFO, TAG, text);
}

JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_run(JNIEnv *env, jclass cls, jstring path)
{
    const char *p;
    chm_image img;
    int rc = -1;

    (void)cls;
    p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) {
        log_line("no path");
        return -1;
    }
    log_line(p);

    memset(&img, 0, sizeof(img));
    if (chm_open(&img, p) != 0) {
        log_line("chm_open failed");
        goto done;
    }
    log_line("image opened");
    if (chm_fixup(&img) != 0) {
        log_line("chm_fixup failed");
        goto closed;
    }
    log_line("image mapped and bound");
    rc = chm_run(&img, 0, NULL);
    log_line("main returned");

closed:
    chm_close(&img);
done:
    (*env)->ReleaseStringUTFChars(env, path, p);
    return rc;
}
#include <jni.h>
#include <android/log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chm_session.h"

#define TAG "chimera"

extern char **environ;

static chm_session *g_session;

static void log_text(const char *text)
{
    __android_log_write(ANDROID_LOG_INFO, TAG, text);
}

JNIEXPORT jstring JNICALL Java_dev_chimera_app_Native_load(JNIEnv *env, jclass cls, jstring path)
{
    const char *p;
    char out[16384];

    (void)cls;
    if (g_session) {
        chm_session_destroy(g_session);
        g_session = NULL;
    }
    g_session = chm_session_create();
    if (!g_session)
        return (*env)->NewStringUTF(env, "session allocation failed");
    p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) {
        chm_session_destroy(g_session);
        g_session = NULL;
        return (*env)->NewStringUTF(env, "no path");
    }
    int rc = chm_session_load(g_session, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    chm_session_report(g_session, out, sizeof(out));
    log_text(out);
    if (rc) {
        chm_session_destroy(g_session);
        g_session = NULL;
        return (*env)->NewStringUTF(env, out);
    }
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL Java_dev_chimera_app_Native_run(JNIEnv *env, jclass cls)
{
    char out[16384];

    (void)cls;
    if (!g_session)
        return (*env)->NewStringUTF(env, "no session");
    if (!g_session->images[0].executable) {
        snprintf(out, sizeof(out), "not executable: %u unresolved references",
                 g_session->images[0].nfixup_missing);
        return (*env)->NewStringUTF(env, out);
    }
    int rc = chm_session_run(g_session, 1, environ);
    if (rc == -2)
        snprintf(out, sizeof(out), "trapped: signal %d fault 0x%llx pc 0x%llx lr 0x%llx",
                 chm_trap_signal(), (unsigned long long)chm_trap_fault_addr(),
                 (unsigned long long)chm_trap_pc(), (unsigned long long)chm_trap_lr());
    else
        snprintf(out, sizeof(out), "entry returned %d", rc);
    log_text(out);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL Java_dev_chimera_app_Native_report(JNIEnv *env, jclass cls)
{
    char out[16384];

    (void)cls;
    if (!g_session)
        return (*env)->NewStringUTF(env, "no session");
    chm_session_report(g_session, out, sizeof(out));
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_pump(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)chm_dispatch_pump(g_session);
}

JNIEXPORT void JNICALL Java_dev_chimera_app_Native_release(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    if (g_session) {
        chm_session_destroy(g_session);
        g_session = NULL;
    }
}
#include <jni.h>
#include <android/log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chm_session.h"

#define TAG "chimera"

static chm_session *g_session;

#include "region.inc"

static void log_text(const char *text)
{
    __android_log_write(ANDROID_LOG_INFO, TAG, text);
}

JNIEXPORT jstring JNICALL Java_dev_chimera_app_Native_load(JNIEnv *env, jclass cls, jstring path, jstring sandbox,
                                                        jint width, jint height)
{
    const char *p;
    const char *root;
    char out[16384];

    (void)cls;
    if (!path || !sandbox)
        return (*env)->NewStringUTF(env, "path and sandbox are required");
    if (g_session) {
        chm_session_destroy(g_session);
        g_session = NULL;
    }
    if (width <= 0 || width > 2048 || height <= 0 || height > 2048)
        return (*env)->NewStringUTF(env, "invalid viewport");
    chm_ui_viewport(width, height);
    g_session = chm_session_create();
    if (!g_session)
        return (*env)->NewStringUTF(env, "session allocation failed");
    if (!capture_region(env)) {
        chm_session_destroy(g_session);
        g_session = NULL;
        if ((*env)->ExceptionCheck(env)) return NULL;
        return (*env)->NewStringUTF(env, "device locale and time zone unavailable");
    }
    p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) {
        chm_session_destroy(g_session);
        g_session = NULL;
        return (*env)->NewStringUTF(env, "no path");
    }
    root = (*env)->GetStringUTFChars(env, sandbox, NULL);
    if (!root) {
        (*env)->ReleaseStringUTFChars(env, path, p);
        chm_session_destroy(g_session);
        g_session = NULL;
        return NULL;
    }
    if (root[0] != '/' || strlen(root) >= 512) {
        (*env)->ReleaseStringUTFChars(env, sandbox, root);
        (*env)->ReleaseStringUTFChars(env, path, p);
        chm_session_destroy(g_session);
        g_session = NULL;
        return (*env)->NewStringUTF(env, "invalid sandbox directory");
    }
    chm_foundation_set_root(root);
    (*env)->ReleaseStringUTFChars(env, sandbox, root);
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
    char *argv[] = { g_session->exe_path, NULL };
    int rc = chm_session_run(g_session, 1, argv);
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
    if (!g_session || g_session->exit_code < 0) return -1;
    size_t completed = chm_dispatch_pump(g_session);
    return g_session->exit_code < 0 ? -1 : (jint)completed;
}

JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_tap(JNIEnv *env, jclass cls,
    jdouble down_x, jdouble down_y, jdouble up_x, jdouble up_y)
{
    (void)env;
    (void)cls;
    if (!g_session || !chm_ui_running() || g_session->exit_code != 0) return -1;
    int down = chm_ui_pointer(0, down_x, down_y);
    if (down < 0) return down;
    int rc = chm_ui_pointer(2, up_x, up_y);
    chm_session_log(g_session, "input: tap (%.2f,%.2f) result=%d", up_x, up_y, rc);
    return rc;
}

JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_pan(JNIEnv *env, jclass cls,
    jint phase, jdouble x, jdouble y)
{
    (void)env; (void)cls;
    if (!g_session || !chm_ui_running() || g_session->exit_code != 0) return -1;
    return chm_ui_pan(phase, x, y);
}

JNIEXPORT jboolean JNICALL Java_dev_chimera_app_Native_ready(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return g_session && g_session->prepared && g_session->images[0].executable;
}

JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_tick(JNIEnv *env, jclass cls, jlong nanos, jdouble period)
{
    (void)env; (void)cls;
    if (!g_session || g_session->exit_code < 0) return -1;
    size_t jobs = chm_dispatch_pump(g_session);
    int links = chm_ui_vsync((double)nanos / 1e9, period);
    return links < 0 ? links : (jint)(jobs + (size_t)links);
}

typedef struct {
    JNIEnv *env;
    jobject target;
    jmethodID draw;
    int failed;
    size_t rectangles;
    size_t texts;
} paint_context;

static void draw_to_android(void *context, const chm_draw_command *command)
{
    paint_context *p = context;
    JNIEnv *env = p->env;
    if (p->failed || (*env)->ExceptionCheck(env)) return;
    if ((*env)->PushLocalFrame(env, 4) < 0) { p->failed = 1; return; }
    jdouble geometry[] = {
        command->x, command->y, command->width, command->height,
        command->clip_left, command->clip_top, command->clip_right, command->clip_bottom,
        command->font_size
    };
    jdoubleArray coords = (*env)->NewDoubleArray(env, 9);
    jbyteArray text = NULL;
    if (!coords) p->failed = 1;
    if (!p->failed) (*env)->SetDoubleArrayRegion(env, coords, 0, 9, geometry);
    if (!p->failed && command->text) {
        size_t length = strlen(command->text);
        if (length > 1024u * 1024u) p->failed = 1;
        else {
            text = (*env)->NewByteArray(env, (jsize)length);
            if (!text) p->failed = 1;
            else (*env)->SetByteArrayRegion(env, text, 0, (jsize)length, (const jbyte *)command->text);
        }
    }
    if (!p->failed && !(*env)->ExceptionCheck(env)) {
        (*env)->CallVoidMethod(env, p->target, p->draw, (jint)command->kind, text, coords,
                              (jint)command->color, (jint)command->alignment, (jint)command->lines);
        if (command->kind == 1) p->texts++;
        else p->rectangles++;
    }
    if ((*env)->ExceptionCheck(env)) p->failed = 1;
    (*env)->PopLocalFrame(env, NULL);
}

JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_paint(JNIEnv *env, jclass cls,
                                                        jobject target, jint width, jint height)
{
    (void)cls;
    if (!target || !g_session || g_session->exit_code != 0 ||
        width <= 0 || height <= 0 || width > 2048 || height > 2048) return -1;
    jclass type = (*env)->GetObjectClass(env, target);
    if (!type) return -1;
    jmethodID draw = (*env)->GetMethodID(env, type, "draw", "(I[B[DIII)V");
    (*env)->DeleteLocalRef(env, type);
    if (!draw) return -1;
    paint_context context = {env, target, draw, 0, 0, 0};
    chm_ui_viewport(width, height);
    int rc = chm_ui_paint(draw_to_android, &context, width, height);
    if (context.failed) rc = -1;
    if (rc < 0) chm_session_log(g_session, "paint: rectangles=%zu text_commands=%zu result=%d",
                                 context.rectangles, context.texts, rc);
    return rc;
}

#include "chimera_services.inc"

JNIEXPORT void JNICALL Java_dev_chimera_app_Native_release(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    if (g_session) {
        chm_session_destroy(g_session);
        g_session = NULL;
    }
}
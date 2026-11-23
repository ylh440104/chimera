static jbyteArray bytes_to_java(JNIEnv *env, const void *bytes, size_t length)
{
    if (length > 32u * 1024u * 1024u) return NULL;
    jbyteArray out = (*env)->NewByteArray(env, (jsize)length);
    if (out && length) (*env)->SetByteArrayRegion(env, out, 0, (jsize)length, bytes);
    return out;
}

JNIEXPORT jlong JNICALL Java_dev_chimera_app_Native_focusedField(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    chm_guest_lock();
    jlong token = g_session ? (jlong)chm_ui_focused_field() : 0;
    chm_guest_unlock();
    return token;
}
JNIEXPORT jbyteArray JNICALL Java_dev_chimera_app_Native_fieldText(JNIEnv *env, jclass cls, jboolean placeholder)
{
    (void)cls;
    chm_guest_lock();
    const char *text = placeholder ? chm_ui_field_placeholder() : chm_ui_field_text();
    jbyteArray result = bytes_to_java(env, text, strlen(text));
    chm_guest_unlock();
    return result;
}
JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_fieldOptions(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    chm_guest_lock();
    jint options = chm_ui_field_options();
    chm_guest_unlock();
    return options;
}
JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_editText(JNIEnv *env, jclass cls, jlong token, jbyteArray text, jboolean commit)
{
    (void)cls;
    if (!g_session || !text) return -1;
    jsize length = (*env)->GetArrayLength(env, text);
    if (length > 1024 * 1024) return -1;
    char *copy = calloc((size_t)length + 1, 1);
    if (!copy) return -1;
    (*env)->GetByteArrayRegion(env, text, 0, length, (jbyte *)copy);
    int rc = (*env)->ExceptionCheck(env) ? -1 : chm_ui_edit_text((uint64_t)token, copy, commit);
    free(copy); return rc;
}
static jobject copy_next_request(JNIEnv *env)
{
    if (!g_session || g_session->exit_code == -2) return NULL;
    chm_network_request request;
    if (!chm_network_next(&request)) return NULL;
    jobject result = NULL;
    jclass type = (*env)->FindClass(env, "dev/chimera/app/NetworkRequest");
    if (!type) goto finished;
    jmethodID init = (*env)->GetMethodID(env, type, "<init>", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;[BIZZI)V");
    if (!init) goto finished;
    jstring url = (*env)->NewStringUTF(env, request.url ? request.url : "");
    jstring method = (*env)->NewStringUTF(env, request.method ? request.method : "GET");
    jstring headers = (*env)->NewStringUTF(env, request.headers ? request.headers : "{}");
    jbyteArray body = bytes_to_java(env, request.body, request.body_size);
    if (url && method && headers && body && !(*env)->ExceptionCheck(env)) {
        result = (*env)->NewObject(env, type, init, (jlong)request.token, url, method, headers, body,
            (jint)request.timeout_ms, (jboolean)request.cancelled, (jboolean)request.download, (jint)request.download_fd);
    }
finished:
    if (!result && request.download_fd >= 0) close(request.download_fd);
    return result;
}
JNIEXPORT jobject JNICALL Java_dev_chimera_app_Native_nextRequest(JNIEnv *env, jclass cls)
{
    (void)cls;
    chm_guest_lock();
    jobject result = copy_next_request(env);
    chm_guest_unlock();
    return result;
}
JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_transferProgress(JNIEnv *env, jclass cls, jlong token, jlong done, jlong total)
{
    (void)env; (void)cls;
    return chm_network_progress((uint64_t)token, done, total);
}
JNIEXPORT jint JNICALL Java_dev_chimera_app_Native_completeRequest(JNIEnv *env, jclass cls, jlong token, jint status, jbyteArray body, jstring headers, jstring error)
{
    (void)cls;
    if (!g_session || !body || !headers || !error) return -1;
    jsize length = (*env)->GetArrayLength(env, body);
    if (length > 32 * 1024 * 1024) return -1;
    jbyte *bytes = length ? (*env)->GetByteArrayElements(env, body, NULL) : NULL;
    const char *fields = (*env)->GetStringUTFChars(env, headers, NULL);
    const char *failure = (*env)->GetStringUTFChars(env, error, NULL);
    int rc = -1;
    if ((!length || bytes) && fields && failure) rc = chm_network_complete((uint64_t)token, status, bytes, (size_t)length, fields, failure);
    if (failure) (*env)->ReleaseStringUTFChars(env, error, failure);
    if (fields) (*env)->ReleaseStringUTFChars(env, headers, fields);
    if (bytes) (*env)->ReleaseByteArrayElements(env, body, bytes, JNI_ABORT);
    return rc;
}
static int capture_region(JNIEnv *env)
{
    if ((*env)->PushLocalFrame(env, 8) < 0) return 0;
    jclass locale_class = (*env)->FindClass(env, "java/util/Locale");
    if (!locale_class) goto failed;
    jclass zone_class = (*env)->FindClass(env, "java/util/TimeZone");
    if (!zone_class) goto failed;
    jmethodID locale_default = (*env)->GetStaticMethodID(env, locale_class, "getDefault", "()Ljava/util/Locale;");
    if (!locale_default) goto failed;
    jmethodID zone_default = (*env)->GetStaticMethodID(env, zone_class, "getDefault", "()Ljava/util/TimeZone;");
    if (!zone_default) goto failed;
    jmethodID locale_name = (*env)->GetMethodID(env, locale_class, "toString", "()Ljava/lang/String;");
    if (!locale_name) goto failed;
    jmethodID zone_name = (*env)->GetMethodID(env, zone_class, "getID", "()Ljava/lang/String;");
    if (!zone_name) goto failed;
    jobject locale = (*env)->CallStaticObjectMethod(env, locale_class, locale_default);
    if ((*env)->ExceptionCheck(env) || !locale) goto failed;
    jobject zone = (*env)->CallStaticObjectMethod(env, zone_class, zone_default);
    if ((*env)->ExceptionCheck(env) || !zone) goto failed;
    jstring locale_id = (*env)->CallObjectMethod(env, locale, locale_name);
    if ((*env)->ExceptionCheck(env) || !locale_id) goto failed;
    jstring zone_id = (*env)->CallObjectMethod(env, zone, zone_name);
    if ((*env)->ExceptionCheck(env) || !zone_id) goto failed;
    const char *locale_text = (*env)->GetStringUTFChars(env, locale_id, NULL);
    if (!locale_text) goto failed;
    const char *zone_text = (*env)->GetStringUTFChars(env, zone_id, NULL);
    if (!zone_text) { (*env)->ReleaseStringUTFChars(env, locale_id, locale_text); goto failed; }
    chm_foundation_set_region(locale_text, zone_text);
    (*env)->ReleaseStringUTFChars(env, zone_id, zone_text);
    (*env)->ReleaseStringUTFChars(env, locale_id, locale_text);
    (*env)->PopLocalFrame(env, NULL);
    return 1;
failed:
    (*env)->PopLocalFrame(env, NULL);
    return 0;
}
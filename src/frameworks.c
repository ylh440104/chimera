#include "chm_session.h"
#include "chm_scene.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_task_self;
static uint64_t g_stack_guard;
static uint64_t g_stdout_obj;
static uint64_t g_stderr_obj;
static uint64_t g_environ_ptr;
static uint64_t g_empty_array;
static uint64_t g_empty_dictionary;
static uint64_t g_ca_filter_nearest;
static uint64_t g_cf_allocator;
static uint64_t g_cf_boolean_true;
static uint64_t g_iomaster_port;
static uint64_t g_dispatch_main_q_alias;
static uint64_t g_ehtype_nsexception;
static uint64_t g_ksec[10];
static uint64_t g_nskey[11];
static uint64_t g_gcnote[4];
static uint64_t g_avcat[2];
static uint64_t g_edge_zero[4];
static uint64_t g_af_key;

static const double g_rect_zero[4];

static int64_t cf_zero_i(void)
{
    return 0;
}

static void *cf_null_p(void)
{
    return NULL;
}

static long cf_zero_l(void)
{
    return 0;
}

static int cf_fail_int(void)
{
    return -1;
}

static int64_t CGColorGetAlpha_impl(void *color)
{
    (void)color;
    return 0;
}

static void *CGColorGetComponents_impl(void *color)
{
    (void)color;
    return NULL;
}

static void *CGColorSpaceCreateDeviceRGB_impl(void)
{
    return NULL;
}

static void CGColorSpaceRelease_impl(void *space)
{
    (void)space;
}

static void *CGDataProviderCreateWithData_impl(void)
{
    return NULL;
}

static void CGDataProviderRelease_impl(void *p)
{
    (void)p;
}

static void *CGImageCreate_impl(void)
{
    return NULL;
}

static void CGImageRelease_impl(void *img)
{
    (void)img;
}

typedef struct { double x, y, w, h; } cg_rect;
typedef struct { double x, y; } cg_point;

static int CGRectContainsPoint_impl(cg_rect r, cg_point p)
{
    double left = r.w < 0 ? r.x + r.w : r.x;
    double top = r.h < 0 ? r.y + r.h : r.y;
    double right = r.w < 0 ? r.x : r.x + r.w;
    double bottom = r.h < 0 ? r.y : r.y + r.h;
    return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
}

static double CGRectGetMaxX_impl(cg_rect r)
{
    return r.w < 0 ? r.x : r.x + r.w;
}

static double CGRectGetMaxY_impl(cg_rect r)
{
    return r.h < 0 ? r.y : r.y + r.h;
}

static void *CFCopyHomeDirectoryURLForUser_impl(void)
{
    return NULL;
}

static void *CFCopySystemVersionDictionary_impl(void)
{
    return NULL;
}

static void *CFDataGetBytePtr_impl(void *data)
{
    (void)data;
    return NULL;
}

static int64_t CFDataGetLength_impl(void *data)
{
    (void)data;
    return 0;
}

static long CFDictionaryGetCount_impl(void *dict)
{
    (void)dict;
    return 0;
}

static void CFDictionaryGetKeysAndValues_impl(void *dict, void *keys, void *values)
{
    (void)dict;
    (void)keys;
    (void)values;
}

static void *CFDictionaryGetValue_impl(void *dict, void *key)
{
    (void)dict;
    (void)key;
    return NULL;
}

static void *CFPreferencesCopyAppValue_impl(void)
{
    return NULL;
}

static void *CFStringCreateMutableCopy_impl(void)
{
    return NULL;
}

static void *CFStringCreateWithBytes_impl(void)
{
    return NULL;
}

static void *CFStringCapitalize_impl(void *s)
{
    return s;
}

static int64_t CFStringGetMaximumSizeForEncoding_impl(void)
{
    return 0;
}

static void *CFURLCopyPath_impl(void *url)
{
    (void)url;
    return NULL;
}

static void *CFURLCreateCopyAppendingPathComponent_impl(void)
{
    return NULL;
}

static int SecItemAdd_impl(void)
{
    return -25300;
}

static int SecItemCopyMatching_impl(void)
{
    return -25300;
}

static int SecItemDelete_impl(void)
{
    return -25300;
}

static void *SecTaskCreateFromSelf_impl(void)
{
    return NULL;
}

static void *SecTaskCopyValueForEntitlement_impl(void)
{
    return NULL;
}

static int IOObjectRelease_impl(void *obj)
{
    (void)obj;
    return -1;
}

static void *IORegistryEntryCreateCFProperty_impl(void)
{
    return NULL;
}

static void *IORegistryEntryFromPath_impl(void)
{
    return NULL;
}

static void objc_note(const char *what)
{
    (void)what;
}

static void *class_addMethod_impl(void *cls, const char *sel, void *imp, const char *types)
{
    objc_note("class_addMethod");
    (void)cls;
    (void)sel;
    (void)imp;
    (void)types;
    return NULL;
}

static void *class_getInstanceMethod_impl(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return NULL;
}

static void *class_getClassMethod_impl(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return NULL;
}

static void *class_copyMethodList_impl(void *cls, unsigned int *count)
{
    (void)cls;
    if (count)
        *count = 0;
    return NULL;
}

static void *method_getImplementation_impl(void *m)
{
    return m;
}

static void *method_getName_impl(void *m)
{
    return m;
}

static const char *method_getTypeEncoding_impl(void *m)
{
    (void)m;
    return NULL;
}

static void method_exchangeImplementations_impl(void *a, void *b)
{
    (void)a;
    (void)b;
    objc_note("method_exchangeImplementations");
}

static void class_setSuperclass_impl(void *cls, void *super)
{
    (void)cls;
    (void)super;
    objc_note("class_setSuperclass");
}

static void *objc_getAssociatedObject_impl(void *obj, const void *key)
{
    (void)obj;
    (void)key;
    return NULL;
}

static void objc_setAssociatedObject_impl(void *obj, const void *key, void *value, int policy)
{
    (void)obj;
    (void)key;
    (void)value;
    (void)policy;
}

static void objc_setProperty_impl(void *self, const char *sel, void **slot, void *value)
{
    (void)self;
    (void)sel;
    if (slot)
        *slot = value;
}

static void objc_copyStruct_impl(void *dst, const void *src, long size, int atomic, int hasStrong)
{
    (void)atomic;
    (void)hasStrong;
    if (dst && src && size > 0)
        memcpy(dst, src, (size_t)size);
}

static void *objc_initWeak_impl(void *slot, void *obj)
{
    if (slot)
        *(void **)slot = obj;
    return obj;
}

static void *objc_loadWeakRetained_impl(void *slot)
{
    return slot ? *(void **)slot : NULL;
}

static void objc_destroyWeak_impl(void *slot)
{
    if (slot)
        *(void **)slot = NULL;
}

static void *objc_storeWeak_impl(void *slot, void *obj)
{
    if (slot)
        *(void **)slot = obj;
    return obj;
}

static void objc_enumerationMutation_impl(void *obj)
{
    (void)obj;
}

static void *objc_getProperty_impl(void *self, const char *sel, long offset, int atomic)
{
    (void)sel;
    (void)atomic;
    if (!self)
        return NULL;
    return *(void **)((uint8_t *)self + offset);
}

static void *objc_begin_catch_impl(void *exc)
{
    return exc;
}

static void objc_end_catch_impl(void)
{
}

static void *objc_exception_throw_impl(void *exc)
{
    return exc;
}

static unsigned long CC_SHA1_impl(const void *data, unsigned int len, unsigned char *md)
{
    (void)data;
    (void)len;
    if (md)
        memset(md, 0, 20);
    return md ? 20 : 0;
}

static void *CACurrentMediaTime_impl(void)
{
    return NULL;
}

typedef struct {
    const char *name;
    void *addr;
} fw_entry;

static void *string_const(const char *text)
{
    return chm_foundation_string(text);
}

static void *NSLog_impl(const char *fmt, ...)
{
    (void)fmt;
    return NULL;
}

static void NSSetUncaughtExceptionHandler_impl(void *handler)
{
    (void)handler;
}

static void *NSSearchPathForDirectoriesInDomains_impl(unsigned long dir, unsigned long mask, int expand)
{
    (void)dir;
    (void)mask;
    (void)expand;
    return NULL;
}

static void *NSUserName_impl(void)
{
    return string_const("mobile");
}

static void *NSHomeDirectory_impl(void)
{
    return string_const("/var/mobile");
}

static void *NSTemporaryDirectory_impl(void)
{
    return string_const("/tmp");
}

static void *NSStringFromClass_impl(void *cls)
{
    const char *name = chm_objc_class_name(cls);
    return name ? string_const(name) : NULL;
}

static void *NSStringFromSelector_impl(const char *sel)
{
    return string_const(sel ? sel : "");
}

static void *NSClassFromString_impl(void *name)
{
    return name ? chm_objc_get_class(chm_foundation_cstr(name)) : NULL;
}

static void *NSSelectorFromString_impl(void *name)
{
    if (!name) return NULL;
    void *(*register_name)(const char *) = chm_objc_session_lookup(NULL, "_sel_registerName");
    return register_name ? register_name(chm_foundation_cstr(name)) : NULL;
}

static void *NSStringFromUIEdgeInsets_impl(void)
{
    return string_const("{0, 0, 0, 0}");
}

static void *UIEdgeInsetsFromString_impl(const char *s)
{
    (void)s;
    return NULL;
}

static void UIGraphicsBeginImageContextWithOptions_impl(void)
{
}

static void UIGraphicsEndImageContext_impl(void)
{
}

static void *UIGraphicsGetImageFromCurrentImageContext_impl(void)
{
    return NULL;
}


static int UIApplicationMain_impl(int argc, char **argv, void *principal, void *delegate)
{
    return chm_ui_main(argc, argv, principal, delegate);
}

static void *CAFrameRateRangeMake_impl(void)
{
    return NULL;
}

static void *objc_copyWeak_impl(void *dst, void *src)
{
    if (dst)
        *(void **)dst = src ? *(void **)src : NULL;
    return dst;
}

static const fw_entry g_fw[] = {
    { "_CFCopyHomeDirectoryURLForUser", (void *)CFCopyHomeDirectoryURLForUser_impl },
    { "_CFCopySystemVersionDictionary", (void *)CFCopySystemVersionDictionary_impl },
    { "__CFCopySystemVersionDictionary", (void *)CFCopySystemVersionDictionary_impl },
    { "_CFDataGetBytePtr", (void *)CFDataGetBytePtr_impl },
    { "_CFDataGetLength", (void *)CFDataGetLength_impl },
    { "_CFDictionaryGetCount", (void *)CFDictionaryGetCount_impl },
    { "_CFDictionaryGetKeysAndValues", (void *)CFDictionaryGetKeysAndValues_impl },
    { "_CFDictionaryGetValue", (void *)CFDictionaryGetValue_impl },
    { "_CFPreferencesCopyAppValue", (void *)CFPreferencesCopyAppValue_impl },
    { "_CFStringCreateMutableCopy", (void *)CFStringCreateMutableCopy_impl },
    { "_CFStringCreateWithBytes", (void *)CFStringCreateWithBytes_impl },
    { "_CFStringCapitalize", (void *)CFStringCapitalize_impl },
    { "_CFStringGetMaximumSizeForEncoding", (void *)CFStringGetMaximumSizeForEncoding_impl },
    { "_CFURLCopyPath", (void *)CFURLCopyPath_impl },
    { "_CFURLCreateCopyAppendingPathComponent", (void *)CFURLCreateCopyAppendingPathComponent_impl },
    { "_CFRelease", (void *)cf_null_p },
    { "_CFRetain", (void *)cf_null_p },
    { "_CFEqual", (void *)cf_fail_int },
    { "_CFHash", (void *)cf_zero_l },
    { "_CFStringGetLength", (void *)cf_zero_i },
    { "_CFStringGetCStringPtr", (void *)cf_null_p },
    { "_CFStringGetCString", (void *)cf_fail_int },
    { "_CGColorGetAlpha", (void *)CGColorGetAlpha_impl },
    { "_CGColorGetComponents", (void *)CGColorGetComponents_impl },
    { "_CGColorSpaceCreateDeviceRGB", (void *)CGColorSpaceCreateDeviceRGB_impl },
    { "_CGColorSpaceRelease", (void *)CGColorSpaceRelease_impl },
    { "_CGDataProviderCreateWithData", (void *)CGDataProviderCreateWithData_impl },
    { "_CGDataProviderRelease", (void *)CGDataProviderRelease_impl },
    { "_CGImageCreate", (void *)CGImageCreate_impl },
    { "_CGImageRelease", (void *)CGImageRelease_impl },
    { "_CGRectContainsPoint", (void *)CGRectContainsPoint_impl },
    { "_CGRectGetMaxX", (void *)CGRectGetMaxX_impl },
    { "_CGRectGetMaxY", (void *)CGRectGetMaxY_impl },
    { "_CGRectZero", (void *)g_rect_zero },
    { "_SecItemAdd", (void *)SecItemAdd_impl },
    { "_SecItemCopyMatching", (void *)SecItemCopyMatching_impl },
    { "_SecItemDelete", (void *)SecItemDelete_impl },
    { "_SecTaskCreateFromSelf", (void *)SecTaskCreateFromSelf_impl },
    { "_SecTaskCopyValueForEntitlement", (void *)SecTaskCopyValueForEntitlement_impl },
    { "_IOObjectRelease", (void *)IOObjectRelease_impl },
    { "_IORegistryEntryCreateCFProperty", (void *)IORegistryEntryCreateCFProperty_impl },
    { "_IORegistryEntryFromPath", (void *)IORegistryEntryFromPath_impl },
    { "_CC_SHA1", (void *)CC_SHA1_impl },
    { "_CACurrentMediaTime", (void *)CACurrentMediaTime_impl },
    { "_class_addMethod", (void *)class_addMethod_impl },
    { "_class_getInstanceMethod", (void *)class_getInstanceMethod_impl },
    { "_class_getClassMethod", (void *)class_getClassMethod_impl },
    { "_class_copyMethodList", (void *)class_copyMethodList_impl },
    { "_class_setSuperclass", (void *)class_setSuperclass_impl },
    { "_method_getImplementation", (void *)method_getImplementation_impl },
    { "_method_getName", (void *)method_getName_impl },
    { "_method_getTypeEncoding", (void *)method_getTypeEncoding_impl },
    { "_method_exchangeImplementations", (void *)method_exchangeImplementations_impl },
    { "_objc_getAssociatedObject", (void *)objc_getAssociatedObject_impl },
    { "_objc_setAssociatedObject", (void *)objc_setAssociatedObject_impl },
    { "_objc_setProperty_atomic", (void *)objc_setProperty_impl },
    { "_objc_setProperty_nonatomic_copy", (void *)objc_setProperty_impl },
    { "_objc_copyStruct", (void *)objc_copyStruct_impl },
    { "_objc_initWeak", (void *)objc_initWeak_impl },
    { "_objc_loadWeakRetained", (void *)objc_loadWeakRetained_impl },
    { "_objc_destroyWeak", (void *)objc_destroyWeak_impl },
    { "_objc_storeWeak", (void *)objc_storeWeak_impl },
    { "_objc_enumerationMutation", (void *)objc_enumerationMutation_impl },
    { "_objc_getProperty", (void *)objc_getProperty_impl },
    { "_objc_begin_catch", (void *)objc_begin_catch_impl },
    { "_objc_end_catch", (void *)objc_end_catch_impl },
    { "_objc_exception_throw", (void *)objc_exception_throw_impl },
    { "_objc_copyWeak", (void *)objc_copyWeak_impl },
    { "_NSLog", (void *)NSLog_impl },
    { "_NSSetUncaughtExceptionHandler", (void *)NSSetUncaughtExceptionHandler_impl },
    { "_NSSearchPathForDirectoriesInDomains", (void *)NSSearchPathForDirectoriesInDomains_impl },
    { "_NSUserName", (void *)NSUserName_impl },
    { "_NSHomeDirectory", (void *)NSHomeDirectory_impl },
    { "_NSTemporaryDirectory", (void *)NSTemporaryDirectory_impl },
    { "_NSStringFromClass", (void *)NSStringFromClass_impl },
    { "_NSStringFromSelector", (void *)NSStringFromSelector_impl },
    { "_NSClassFromString", (void *)NSClassFromString_impl },
    { "_NSSelectorFromString", (void *)NSSelectorFromString_impl },
    { "_NSStringFromUIEdgeInsets", (void *)NSStringFromUIEdgeInsets_impl },
    { "_UIEdgeInsetsFromString", (void *)UIEdgeInsetsFromString_impl },
    { "_UIGraphicsBeginImageContextWithOptions", (void *)UIGraphicsBeginImageContextWithOptions_impl },
    { "_UIGraphicsEndImageContext", (void *)UIGraphicsEndImageContext_impl },
    { "_UIGraphicsGetImageFromCurrentImageContext", (void *)UIGraphicsGetImageFromCurrentImageContext_impl },
    { "_UIApplicationMain", (void *)UIApplicationMain_impl },
    { "_CAFrameRateRangeMake", (void *)CAFrameRateRangeMake_impl },
    { "_AFNetworkingOperationFailingURLResponseDataErrorKey", &g_af_key },
    { NULL, NULL }
};

static const fw_entry g_data[] = {
    { "_mach_task_self_", &g_task_self },
    { "___stack_chk_guard", &g_stack_guard },
    { "___stdoutp", &g_stdout_obj },
    { "___stderrp", &g_stderr_obj },
    { "_environ", &g_environ_ptr },
    { "___NSArray0__", &g_empty_array },
    { "___NSDictionary0__", &g_empty_dictionary },
    { "_kCAFilterNearest", &g_ca_filter_nearest },
    { "_kCFAllocatorDefault", &g_cf_allocator },
    { "_kCFBooleanTrue", &g_cf_boolean_true },
    { "_kIOMasterPortDefault", &g_iomaster_port },
    { "_dispatch_main_q", &g_dispatch_main_q_alias },
    { "_OBJC_EHTYPE_$_NSException", &g_ehtype_nsexception },
    { "_kSecAttrAccount", &g_ksec[0] },
    { "_kSecAttrService", &g_ksec[1] },
    { "_kSecClassGenericPassword", &g_ksec[2] },
    { "_kSecClass", &g_ksec[3] },
    { "_kSecReturnData", &g_ksec[4] },
    { "_kSecMatchLimitOne", &g_ksec[5] },
    { "_kSecMatchLimit", &g_ksec[6] },
    { "_kSecValueData", &g_ksec[7] },
    { "_kSecAttrAccessibleWhenUnlockedThisDeviceOnly", &g_ksec[8] },
    { "_kSecAttrAccessible", &g_ksec[9] },
    { "_UIEdgeInsetsZero", &g_edge_zero[0] },
    { "_NSURLFileAllocatedSizeKey", &g_nskey[0] },
    { "_NSURLTotalFileAllocatedSizeKey", &g_nskey[1] },
    { "_NSURLIsRegularFileKey", &g_nskey[2] },
    { "_NSFontAttributeName", &g_nskey[3] },
    { "_NSParagraphStyleAttributeName", &g_nskey[4] },
    { "_NSRunLoopCommonModes", &g_nskey[5] },
    { "_NSProgressKindFile", &g_nskey[6] },
    { "_NSProgressFileOperationKindDownloading", &g_nskey[7] },
    { "_UIFontTextStyleBody", &g_nskey[8] },
    { "_UIFontTextStyleFootnote", &g_nskey[9] },
    { "_UIWindowSceneSessionRoleApplication", &chm_scene_application_role },
    { "_GCControllerDidConnectNotification", &g_gcnote[0] },
    { "_GCControllerDidDisconnectNotification", &g_gcnote[1] },
    { "_GCMouseDidConnectNotification", &g_gcnote[2] },
    { "_GCMouseDidDisconnectNotification", &g_gcnote[3] },
    { "_AVAudioSessionCategoryPlayAndRecord", &g_avcat[0] },
    { "_AVAudioSessionCategorySoloAmbient", &g_avcat[1] },
    { NULL, NULL }
};

void *chm_frameworks_lookup(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; g_fw[i].name; i++) {
        if (strcmp(g_fw[i].name, name) == 0)
            return g_fw[i].addr;
    }
    for (i = 0; g_data[i].name; i++) {
        if (strcmp(g_data[i].name, name) == 0)
            {
                if (!strcmp(name, "_UIFontTextStyleBody"))
                    g_nskey[8] = (uint64_t)(uintptr_t)chm_foundation_string("UICTFontTextStyleBody");
                if (!strcmp(name, "_UIFontTextStyleFootnote"))
                    g_nskey[9] = (uint64_t)(uintptr_t)chm_foundation_string("UICTFontTextStyleFootnote");
                return g_data[i].addr;
            }
    }
    return NULL;
}

int chm_frameworks_count(void)
{
    int n = 0;

    while (g_fw[n].name)
        n++;
    return n;
}
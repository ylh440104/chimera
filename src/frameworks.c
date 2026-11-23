#include "chm_session.h"
#include "chm_scene.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t g_task_self;
static uint64_t g_stack_guard;
uint64_t g_stdout_obj;
uint64_t g_stderr_obj;
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

static void class_setSuperclass_impl(void *cls, void *super)
{
    (void)cls;
    (void)super;
    objc_note("class_setSuperclass");
}

static void objc_setProperty_impl(void *self, const char *sel, void *value, long offset)
{
    (void)sel;
    if (self && offset >= 0) *(void **)((char *)self + offset) = value;
}

static void objc_setProperty_copy_impl(void *self, const char *sel, void *value, long offset)
{
    if (!self || offset < 0) return;
    void *old = *(void **)((char *)self + offset);
    void *copy = chm_is_block(value) ? chm_block_copy(value) : value ? chm_objc_send0(value, "copy") : NULL;
    objc_setProperty_impl(self, sel, copy, offset);
    if (chm_is_block(old)) chm_block_release(old);
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

#include "sha1.inc"

static double CACurrentMediaTime_impl(void)
{
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) ? 0.0 : (double)now.tv_sec + (double)now.tv_nsec / 1e9;
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

static void *NSSearchPathForDirectoriesInDomains_impl(unsigned long dir, unsigned long mask, signed char expand)
{
    return chm_foundation_search_paths(dir, mask, expand);
}

static void *NSUserName_impl(void)
{
    return string_const("mobile");
}

static void *NSHomeDirectory_impl(void)
{
    return chm_foundation_home_directory();
}

static void *NSTemporaryDirectory_impl(void)
{
    return string_const("/var/mobile/tmp/");
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
    { "_class_setSuperclass", (void *)class_setSuperclass_impl },
    { "_objc_setProperty_atomic", (void *)objc_setProperty_impl },
    { "_objc_setProperty_nonatomic", (void *)objc_setProperty_impl },
    { "_objc_setProperty_nonatomic_copy", (void *)objc_setProperty_copy_impl },
    { "_objc_setProperty_atomic_copy", (void *)objc_setProperty_copy_impl },
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
    for (i = 0; g_data[i].name; i++)
        if (!strcmp(g_data[i].name, name)) return g_data[i].addr;
    return chm_foundation_constant(name);
}

int chm_frameworks_count(void)
{
    int n = 0;

    while (g_fw[n].name)
        n++;
    return n;
}
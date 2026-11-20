#include "chm_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHM_MAX_BUILTINS 256

typedef struct {
    uint32_t flags;
    uint32_t instance_start;
    uint32_t instance_size;
    uint32_t reserved;
    const void *ivar_layout;
    const char *name;
    const void *methods;
    const void *protocols;
    const void *ivars;
    const void *weak_ivar_layout;
    const void *properties;
} chm_ro;

typedef struct {
    void *isa;
    void *superclass;
    void *cache;
    void *vtable;
    uint64_t bits;
} chm_cls;

typedef struct chm_builtin {
    chm_cls cls;
    chm_cls meta;
    chm_ro ro;
    char *name;
    struct chm_builtin *parent;
} chm_builtin;

typedef struct {
    const char *name;
    const char *parent;
    uint32_t instance_size;
    const char *kind;
} chm_builtin_desc;

#define CHM_MAX_BUILTINS 256

static chm_builtin *g_builtins[CHM_MAX_BUILTINS];
static int g_builtin_count;
static chm_session *g_session;

static char g_log[4096];
static size_t g_log_len;

static const chm_builtin_desc g_desc[] = {
    { "NSObject", NULL, 16, "foundation" },
    { "NSString", "NSObject", 32, "foundation" },
    { "NSMutableString", "NSString", 32, "foundation" },
    { "NSConstantString", "NSString", 32, "foundation" },
    { "NSArray", "NSObject", 32, "foundation" },
    { "NSMutableArray", "NSArray", 32, "foundation" },
    { "NSDictionary", "NSObject", 32, "foundation" },
    { "NSMutableDictionary", "NSDictionary", 32, "foundation" },
    { "NSSet", "NSObject", 32, "foundation" },
    { "NSMutableSet", "NSSet", 32, "foundation" },
    { "NSNumber", "NSObject", 32, "foundation" },
    { "NSData", "NSObject", 32, "foundation" },
    { "NSMutableData", "NSData", 32, "foundation" },
    { "NSDate", "NSObject", 32, "foundation" },
    { "NSURL", "NSObject", 32, "foundation" },
    { "NSURLRequest", "NSObject", 32, "foundation" },
    { "NSURLSession", "NSObject", 32, "foundation" },
    { "NSURLSessionConfiguration", "NSObject", 32, "foundation" },
    { "NSError", "NSObject", 32, "foundation" },
    { "NSException", "NSObject", 32, "foundation" },
    { "NSBundle", "NSObject", 32, "foundation" },
    { "NSFileManager", "NSObject", 32, "foundation" },
    { "NSFileHandle", "NSObject", 32, "foundation" },
    { "NSOutputStream", "NSObject", 32, "foundation" },
    { "NSUserDefaults", "NSObject", 32, "foundation" },
    { "NSNotificationCenter", "NSObject", 32, "foundation" },
    { "NSProcessInfo", "NSObject", 32, "foundation" },
    { "NSThread", "NSObject", 32, "foundation" },
    { "NSRunLoop", "NSObject", 32, "foundation" },
    { "NSJSONSerialization", "NSObject", 16, "foundation" },
    { "NSNull", "NSObject", 16, "foundation" },
    { "NSIndexPath", "NSObject", 32, "foundation" },
    { "NSPredicate", "NSObject", 32, "foundation" },
    { "NSProgress", "NSObject", 32, "foundation" },
    { "NSLocale", "NSObject", 32, "foundation" },
    { "NSTimeZone", "NSObject", 32, "foundation" },
    { "NSCharacterSet", "NSObject", 32, "foundation" },
    { "NSKeyedArchiver", "NSObject", 16, "foundation" },
    { "NSKeyedUnarchiver", "NSObject", 16, "foundation" },
    { "NSAttributedString", "NSObject", 32, "foundation" },
    { "NSMutableAttributedString", "NSAttributedString", 32, "foundation" },
    { "NSParagraphStyle", "NSObject", 32, "foundation" },
    { "NSMutableParagraphStyle", "NSParagraphStyle", 32, "foundation" },
    { "NSLayoutConstraint", "NSObject", 32, "foundation" },
    { "NSExpression", "NSObject", 32, "foundation" },
    { "NSAssertionHandler", "NSObject", 16, "foundation" },
    { "NSOperationQueue", "NSObject", 32, "foundation" },
    { "NSValue", "NSObject", 32, "foundation" },
    { "NSOrderedSet", "NSObject", 32, "foundation" },
    { "NSEnumerator", "NSObject", 16, "foundation" },
    { "NSNotification", "NSObject", 32, "foundation" },
    { "UITraitCollection", "NSObject", 32, "uikit" },
    { "UIResponder", "NSObject", 32, "uikit" },
    { "UIView", "UIResponder", 96, "uikit" },
    { "UIWindow", "UIView", 96, "uikit" },
    { "UIViewController", "UIResponder", 64, "uikit" },
    { "UIApplication", "UIResponder", 64, "uikit" },
    { "UIWindowScene", "NSObject", 64, "uikit" },
    { "UISceneConfiguration", "NSObject", 64, "uikit" },
    { "UIControl", "UIView", 96, "uikit" },
    { "UIButton", "UIControl", 96, "uikit" },
    { "UITextField", "UIControl", 96, "uikit" },
    { "UISwitch", "UIControl", 96, "uikit" },
    { "UISlider", "UIControl", 96, "uikit" },
    { "UILabel", "UIView", 96, "uikit" },
    { "UIImageView", "UIView", 96, "uikit" },
    { "UIScrollView", "UIView", 96, "uikit" },
    { "UITableView", "UIScrollView", 128, "uikit" },
    { "UITableViewCell", "UIView", 128, "uikit" },
    { "UINavigationBar", "UIView", 96, "uikit" },
    { "UIToolbar", "UIView", 96, "uikit" },
    { "UIProgressView", "UIView", 96, "uikit" },
    { "UIActivityIndicatorView", "UIView", 96, "uikit" },
    { "UITabBar", "UIView", 96, "uikit" },
    { "UITabBarItem", "NSObject", 64, "uikit" },
    { "UITabBarAppearance", "NSObject", 64, "uikit" },
    { "UITabBarController", "UIViewController", 96, "uikit" },
    { "UINavigationController", "UIViewController", 96, "uikit" },
    { "UINavigationItem", "NSObject", 64, "uikit" },
    { "UIBarButtonItem", "NSObject", 64, "uikit" },
    { "UIBarItem", "NSObject", 64, "uikit" },
    { "UIAlertController", "UIViewController", 96, "uikit" },
    { "UIAlertAction", "NSObject", 64, "uikit" },
    { "UIActivityViewController", "UIViewController", 96, "uikit" },
    { "UIDocumentPickerViewController", "UIViewController", 96, "uikit" },
    { "UIImage", "NSObject", 64, "uikit" },
    { "UIColor", "NSObject", 64, "uikit" },
    { "UIFont", "NSObject", 64, "uikit" },
    { "UIScreen", "NSObject", 64, "uikit" },
    { "UIDevice", "NSObject", 64, "uikit" },
    { "UIPasteboard", "NSObject", 64, "uikit" },
    { "UITargetedPreview", "NSObject", 64, "uikit" },
    { "UIGraphicsImageRenderer", "NSObject", 64, "uikit" },
    { "UIGraphicsImageRendererFormat", "NSObject", 64, "uikit" },
    { "UIGraphicsImageRendererContext", "NSObject", 64, "uikit" },
    { "UIImpactFeedbackGenerator", "NSObject", 64, "uikit" },
    { "UIGestureRecognizer", "NSObject", 64, "uikit" },
    { "UITapGestureRecognizer", "UIGestureRecognizer", 64, "uikit" },
    { "UIPanGestureRecognizer", "UIGestureRecognizer", 64, "uikit" },
    { "UILongPressGestureRecognizer", "UIGestureRecognizer", 64, "uikit" },
    { "CADisplayLink", "NSObject", 64, "quartzcore" },
    { "CALayer", "NSObject", 96, "quartzcore" },
    { "CAMetalLayer", "CALayer", 96, "quartzcore" },
    { "CIFilter", "NSObject", 64, "coreimage" },
    { "CIImage", "NSObject", 64, "coreimage" },
    { "CIContext", "NSObject", 64, "coreimage" },
    { "CMMotionManager", "NSObject", 64, "coremotion" },
    { "GCController", "NSObject", 64, "gamecontroller" },
    { "GCMouse", "NSObject", 64, "gamecontroller" },
    { "AVAudioSession", "NSObject", 64, "avfoundation" },
    { "AVAudioPlayer", "NSObject", 64, "avfoundation" },
    { "AVPlayer", "NSObject", 64, "avfoundation" },
    { "UZKArchive", "NSObject", 64, "vendor" },
    { "AFURLSessionManager", "NSObject", 64, "vendor" },
    { "AFHTTPSessionManager", "AFURLSessionManager", 64, "vendor" },
    { "AFJSONRequestSerializer", "NSObject", 64, "vendor" },
    { NULL, NULL, 0, NULL }
};

static chm_builtin *find_builtin(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_builtin_count; i++) {
        if (strcmp(g_builtins[i]->name, name) == 0)
            return g_builtins[i];
    }
    return NULL;
}

static chm_builtin *make_builtin(chm_session *s, const chm_builtin_desc *d)
{
    chm_builtin *b = calloc(1, sizeof(*b));

    (void)s;
    if (!b)
        return NULL;
    b->name = strdup(d->name);
    b->ro.name = b->name;
    b->ro.instance_size = d->instance_size ? d->instance_size : 16;
    b->ro.flags = 0;
    b->cls.bits = (uint64_t)(uintptr_t)&b->ro;
    b->meta.bits = (uint64_t)(uintptr_t)&b->ro;
    b->cls.isa = &b->meta;
    b->meta.isa = &b->meta;
    if (d->parent) {
        b->parent = find_builtin(d->parent);
        if (b->parent) {
            b->cls.superclass = &b->parent->cls;
            b->meta.superclass = &b->parent->meta;
        }
    }
    return b;
}

static void *ns_self(void *self)
{
    return self;
}

static int ns_bool_true(void *self)
{
    (void)self;
    return 1;
}

static int ns_zero(void *self)
{
    (void)self;
    return 0;
}

static long ns_long_zero(void *self)
{
    (void)self;
    return 0;
}

static void *ns_class_impl(void *self)
{
    return self ? *(void **)self : NULL;
}

static void *make_object(void)
{
    chm_builtin *base = find_builtin("NSObject");
    void *obj = calloc(1, 128);

    if (!obj)
        return NULL;
    if (base)
        *(void **)obj = &base->cls;
    return obj;
}

static void *ns_nil_impl(void)
{
    return make_object();
}

static int ns_false_impl(void *self)
{
    (void)self;
    return 0;
}

static const char *ns_description_impl(void *self)
{
    chm_builtin *b;
    void *isa = self ? *(void **)self : NULL;

    if (!isa)
        return "nil";
    b = (chm_builtin *)0;
    for (int i = 0; i < g_builtin_count; i++) {
        if (&g_builtins[i]->cls == isa) {
            b = g_builtins[i];
            break;
        }
    }
    return b ? b->name : "?";
}

typedef struct {
    const char *sel;
    void *imp;
} chm_method_seed;

static const chm_method_seed g_methods[] = {
    { "self", (void *)ns_self },
    { "retain", (void *)ns_self },
    { "copy", (void *)ns_self },
    { "autorelease", (void *)ns_self },
    { "description", (void *)ns_description_impl },
    { "class", (void *)ns_class_impl },
    { "isKindOfClass:", (void *)ns_bool_true },
    { "isMemberOfClass:", (void *)ns_bool_true },
    { "respondsToSelector:", (void *)ns_bool_true },
    { "isEqual:", (void *)ns_bool_true },
    { "hash", (void *)ns_long_zero },
    { "release", (void *)ns_zero },
    { "count", (void *)ns_long_zero },
    { "length", (void *)ns_long_zero },
    { "integerValue", (void *)ns_long_zero },
    { "intValue", (void *)ns_long_zero },
    { "floatValue", (void *)ns_zero },
    { "doubleValue", (void *)ns_zero },
    { "boolValue", (void *)ns_zero },
    { "UTF8String", (void *)ns_description_impl },
    { "absoluteString", (void *)ns_description_impl },
    { "processInfo", (void *)ns_nil_impl },
    { "defaultManager", (void *)ns_nil_impl },
    { "mainBundle", (void *)ns_nil_impl },
    { "bundleWithIdentifier:", (void *)ns_nil_impl },
    { "standardUserDefaults", (void *)ns_nil_impl },
    { "sharedApplication", (void *)ns_nil_impl },
    { "currentDevice", (void *)ns_nil_impl },
    { "mainScreen", (void *)ns_nil_impl },
    { "sharedSession", (void *)ns_nil_impl },
    { "defaultSessionConfiguration", (void *)ns_nil_impl },
    { "ephemeralSessionConfiguration", (void *)ns_nil_impl },
    { "generalPasteboard", (void *)ns_nil_impl },
    { "defaultCenter", (void *)ns_nil_impl },
    { "currentThread", (void *)ns_nil_impl },
    { "mainRunLoop", (void *)ns_nil_impl },
    { "currentRunLoop", (void *)ns_nil_impl },
    { "mainQueue", (void *)ns_nil_impl },
    { "standardCharacterSet", (void *)ns_nil_impl },
    { "URLWithString:", (void *)ns_nil_impl },
    { "fileURLWithPath:", (void *)ns_nil_impl },
    { "stringWithUTF8String:", (void *)ns_nil_impl },
    { "stringWithFormat:", (void *)ns_nil_impl },
    { "array", (void *)ns_nil_impl },
    { "dictionary", (void *)ns_nil_impl },
    { "set", (void *)ns_nil_impl },
    { "data", (void *)ns_nil_impl },
    { "date", (void *)ns_nil_impl },
    { "null", (void *)ns_nil_impl },
    { "valueWithBool:", (void *)ns_nil_impl },
    { "numberWithInteger:", (void *)ns_nil_impl },
    { "numberWithInt:", (void *)ns_nil_impl },
    { "numberWithFloat:", (void *)ns_nil_impl },
    { "numberWithDouble:", (void *)ns_nil_impl },
    { "assetWithURL:", (void *)ns_nil_impl },
    { "requestWithURL:", (void *)ns_nil_impl },
    { "layer", (void *)ns_nil_impl },
    { "displayLinkWithTarget:selector:", (void *)ns_nil_impl },
    { "filterWithName:", (void *)ns_nil_impl },
    { "controllerPausedHandler", (void *)ns_nil_impl },
    { "isMacCatalystApp", (void *)ns_false_impl },
    { "isiOSAppOnMac", (void *)ns_false_impl },
    { "isiOSAppOnVision", (void *)ns_false_impl },
    { "isMacCatalystApp:", (void *)ns_false_impl },
    { "URLsForDirectory:inDomains:", (void *)ns_nil_impl },
    { "URLForDirectory:inDomain:appropriateForURL:create:error:", (void *)ns_nil_impl },
    { "URLForDirectory:inDomain:appropriateForURL:create:", (void *)ns_nil_impl },
    { "contentsOfDirectoryAtPath:error:", (void *)ns_nil_impl },
    { "contentsAtPath:", (void *)ns_nil_impl },
    { "createDirectoryAtPath:withIntermediateDirectories:attributes:error:", (void *)ns_false_impl },
    { "createFileAtPath:contents:attributes:", (void *)ns_false_impl },
    { "removeItemAtPath:error:", (void *)ns_false_impl },
    { "fileExistsAtPath:", (void *)ns_false_impl },
    { "objectForKey:", (void *)ns_nil_impl },
    { "objectAtIndex:", (void *)ns_nil_impl },
    { "firstObject", (void *)ns_nil_impl },
    { "lastObject", (void *)ns_nil_impl },
    { "allKeys", (void *)ns_nil_impl },
    { "allValues", (void *)ns_nil_impl },
    { "componentsSeparatedByString:", (void *)ns_nil_impl },
    { "stringByAppendingPathComponent:", (void *)ns_nil_impl },
    { "stringByAppendingString:", (void *)ns_nil_impl },
    { "pathExtension", (void *)ns_nil_impl },
    { "lastPathComponent", (void *)ns_nil_impl },
    { "deletingPathExtension", (void *)ns_nil_impl },
    { "path", (void *)ns_nil_impl },
    { "init", (void *)ns_self },
    { NULL, NULL }
};

void chm_foundation_register_selectors(void);

void chm_foundation_init(chm_session *s)
{
    int i;
    int k;

    g_session = s;
    g_log_len = 0;
    g_builtin_count = 0;
    for (i = 0; g_desc[i].name; i++) {
        chm_builtin *b;

        if (g_builtin_count >= CHM_MAX_BUILTINS)
            break;
        b = make_builtin(s, &g_desc[i]);
        if (b)
            g_builtins[g_builtin_count++] = b;
    }
    for (k = 0; g_methods[k].sel; k++) {
        chm_method_reg m;

        m.cls = NULL;
        snprintf(m.sel, sizeof(m.sel), "%s", g_methods[k].sel);
        m.imp = g_methods[k].imp;
        if (chm_objc_session_add_method(s, &m) != 0)
            break;
    }
    if (s)
        chm_session_log(s, "foundation: %d classes, %d selectors registered",
                        g_builtin_count, k);
}

void chm_foundation_shutdown(void)
{
    int i;

    for (i = 0; i < g_builtin_count; i++) {
        free(g_builtins[i]->name);
        free(g_builtins[i]);
    }
    g_builtin_count = 0;
    g_log_len = 0;
    g_log[0] = 0;
}

int chm_foundation_class_count(void)
{
    return g_builtin_count;
}

void *chm_foundation_lookup_class(const char *name)
{
    chm_builtin *b = find_builtin(name);

    return b ? &b->cls : NULL;
}

void *chm_foundation_lookup_metaclass(const char *name)
{
    chm_builtin *b = find_builtin(name);

    return b ? &b->meta : NULL;
}

void *chm_foundation_lookup_method(const char *cls_name, const char *sel)
{
    int i;

    (void)cls_name;
    if (!sel)
        return NULL;
    for (i = 0; g_methods[i].sel; i++) {
        if (strcmp(g_methods[i].sel, sel) == 0)
            return g_methods[i].imp;
    }
    return NULL;
}

void chm_foundation_note_missing_selector(const char *cls_name, const char *sel)
{
    if (g_session && cls_name && sel) {
        char buf[192];

        snprintf(buf, sizeof(buf), "-[%s %s]", cls_name, sel);
        chm_session_log(g_session, "unimplemented selector %s", buf);
    }
}

const char *chm_foundation_log(void)
{
    return g_log;
}
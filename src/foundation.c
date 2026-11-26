#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "chm_session.h"
#include "chm_format.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>
#include <math.h>
#include <yxml.h>
#include "foundation_paths.inc"

#define FO_MAX_CLASSES 320
#define FO_MAX_ITEMS 256

#define FO_STRING_MAGIC 0x464f535452ull
#define FO_DATA_MAGIC 0x464f444154ull
#define FO_ARRAY_MAGIC 0x464f415252ull
#define FO_DICT_MAGIC 0x464f444943ull
#define FO_NUMBER_MAGIC 0x464f4e554dull

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
} fo_ro;

typedef struct {
    void *isa;
    void *superclass;
    void *cache;
    void *vtable;
    uint64_t bits;
} fo_cls;

typedef struct fo_class {
    fo_cls cls;
    fo_cls meta;
    fo_ro ro;
    char *name;
    struct fo_class *parent;
} fo_class;

typedef struct {
    void *isa;
    uint64_t magic;
    char *bytes;
    int64_t length;
} fo_string;

typedef struct {
    void *isa;
    uint64_t magic;
    uint8_t *bytes;
    int64_t length;
} fo_data;

typedef struct {
    void *isa;
    uint64_t magic;
    int64_t count;
    size_t capacity;
    unsigned long mutations;
    void **items;
} fo_array;

typedef struct {
    void *isa;
    uint64_t magic;
    int64_t count;
    size_t capacity;
    unsigned long mutations;
    void **keys;
    void **values;
} fo_dict;

typedef struct {
    void *isa;
    uint64_t magic;
    char type;
    union { int64_t signed_value; uint64_t unsigned_value; double real_value; } value;
    char text[96];
} fo_number;

typedef struct {
    unsigned long state;
    void **items;
    unsigned long *mutations;
    unsigned long extra[5];
} fo_enum_state;

typedef struct {
    char *key;
    void *value;
} fo_pair;

typedef struct {
    void *isa;
    uint64_t magic;
    int fd;
    int closed;
    char path[1024];
} fo_handle;

#define FO_HANDLE_MAGIC 0x464f484e444cull

static int cf_array_hold(fo_array *array, void *object);
static void cf_array_drop(fo_array *array, void *object);
static int cf_collection_dispose(void *object);
static int sec_equal(void *left, void *right);
static unsigned long sec_hash(void *object);
static fo_class *g_classes[FO_MAX_CLASSES];
static unsigned long sec_type_id(void *object);
static int g_class_count;
static chm_session *g_session;
static fo_pair g_pairs[FO_MAX_ITEMS];
static int g_pair_count;

static const struct {
    const char *name;
    const char *parent;
    uint32_t size;
} g_seed[] = {
    { "NSObject", NULL, 16 },
    { "NSString", "NSObject", 32 },
    { "NSMutableString", "NSString", 32 },
    { "NSConstantString", "NSString", 32 },
    { "NSArray", "NSObject", 32 },
    { "NSMutableArray", "NSArray", 32 },
    { "NSDictionary", "NSObject", 32 },
    { "NSMutableDictionary", "NSDictionary", 32 },
    { "NSSet", "NSObject", 32 },
    { "NSMutableSet", "NSSet", 32 },
    { "NSNumber", "NSObject", 32 },
    { "NSData", "NSObject", 32 },
    { "NSMutableData", "NSData", 32 },
    { "NSDate", "NSObject", 32 },
    { "NSFormatter", "NSObject", 32 },
    { "NSDateFormatter", "NSFormatter", 32 },
    { "NSByteCountFormatter", "NSFormatter", 32 },
    { "NSURL", "NSObject", 32 },
    { "NSURLRequest", "NSObject", 32 },
    { "NSMutableURLRequest", "NSURLRequest", 32 },
    { "NSURLCredential", "NSObject", 32 },
    { "NSURLCache", "NSObject", 32 },
    { "NSCachedURLResponse", "NSObject", 32 },
    { "NSURLResponse", "NSObject", 32 },
    { "NSHTTPURLResponse", "NSURLResponse", 32 },
    { "NSURLSession", "NSObject", 32 },
    { "NSURLSessionConfiguration", "NSObject", 32 },
    { "NSURLSessionTask", "NSObject", 32 },
    { "NSURLSessionDownloadTask", "NSURLSessionTask", 32 },
    { "NSError", "NSObject", 32 },
    { "NSException", "NSObject", 32 },
    { "NSBundle", "NSObject", 32 },
    { "NSFileManager", "NSObject", 32 },
    { "NSFileHandle", "NSObject", 64 },
    { "NSPipe", "NSObject", 64 },
    { "NSStream", "NSObject", 32 },
    { "NSInputStream", "NSStream", 32 },
    { "NSOutputStream", "NSStream", 32 },
    { "NSXMLParser", "NSObject", 32 },
    { "NSUserDefaults", "NSObject", 32 },
    { "NSNotificationCenter", "NSObject", 32 },
    { "NSNotification", "NSObject", 32 },
    { "NSProcessInfo", "NSObject", 32 },
    { "NSThread", "NSObject", 32 },
    { "NSRunLoop", "NSObject", 32 },
    { "NSJSONSerialization", "NSObject", 16 },
    { "NSPropertyListSerialization", "NSObject", 16 },
    { "NSNull", "NSObject", 16 },
    { "NSIndexPath", "NSObject", 32 },
    { "NSIndexSet", "NSObject", 32 },
    { "NSMutableIndexSet", "NSIndexSet", 32 },
    { "NSUUID", "NSObject", 32 },
    { "NSLock", "NSObject", 80 },
    { "NSRecursiveLock", "NSLock", 80 },
    { "NSPredicate", "NSObject", 32 },
    { "NSProgress", "NSObject", 32 },
    { "NSLocale", "NSObject", 32 },
    { "NSTimeZone", "NSObject", 32 },
    { "NSCharacterSet", "NSObject", 32 },
    { "NSKeyedArchiver", "NSObject", 16 },
    { "NSKeyedUnarchiver", "NSObject", 16 },
    { "NSAttributedString", "NSObject", 32 },
    { "NSMutableAttributedString", "NSAttributedString", 32 },
    { "NSParagraphStyle", "NSObject", 32 },
    { "NSMutableParagraphStyle", "NSParagraphStyle", 32 },
    { "NSLayoutConstraint", "NSObject", 32 },
    { "NSLayoutAnchor", "NSObject", 32 },
    { "NSLayoutXAxisAnchor", "NSLayoutAnchor", 32 },
    { "NSLayoutYAxisAnchor", "NSLayoutAnchor", 32 },
    { "NSLayoutDimension", "NSLayoutAnchor", 32 },
    { "UILayoutGuide", "NSObject", 32 },
    { "NSExpression", "NSObject", 32 },
    { "NSAssertionHandler", "NSObject", 16 },
    { "NSOperationQueue", "NSObject", 32 },
    { "NSOperation", "NSObject", 32 },
    { "NSValue", "NSObject", 32 },
    { "NSOrderedSet", "NSObject", 32 },
    { "NSEnumerator", "NSObject", 16 },
    { "NSScanner", "NSObject", 32 },
    { "NSTimer", "NSObject", 32 },
    { "NSSortDescriptor", "NSObject", 32 },
    { "UITraitCollection", "NSObject", 32 },
    { "UISplitViewController", "UIViewController", 512 },
    { "UITableViewController", "UIViewController", 512 },
    { "UIResponder", "NSObject", 32 },
    { "UIView", "UIResponder", 1024 },
    { "UIWindow", "UIView", 1024 },
    { "UIViewController", "UIResponder", 512 },
    { "UIApplication", "UIResponder", 512 },
    { "UIScene", "UIResponder", 256 },
    { "UIWindowScene", "UIScene", 256 },
    { "UISceneSession", "NSObject", 256 },
    { "UISceneConnectionOptions", "NSObject", 256 },
    { "UISceneConfiguration", "NSObject", 256 },
    { "UIControl", "UIView", 1024 },
    { "UIButton", "UIControl", 1024 },
    { "UITextField", "UIControl", 1024 },
    { "UISwitch", "UIControl", 1024 },
    { "UISlider", "UIControl", 1024 },
    { "UISegmentedControl", "UIControl", 1024 },
    { "UIPickerView", "UIView", 1024 },
    { "UITableViewHeaderFooterView", "UIView", 1024 },
    { "UILabel", "UIView", 1024 },
    { "UIImageView", "UIView", 1024 },
    { "UIScrollView", "UIView", 1024 },
    { "UITableView", "UIScrollView", 1024 },
    { "UITableViewCell", "UIView", 1024 },
    { "UINavigationBar", "UIView", 1024 },
    { "UIToolbar", "UIView", 1024 },
    { "UIProgressView", "UIView", 1024 },
    { "UIActivityIndicatorView", "UIView", 1024 },
    { "UITabBar", "UIView", 1024 },
    { "UITabBarItem", "NSObject", 64 },
    { "UITabBarAppearance", "NSObject", 64 },
    { "UITabBarController", "UIViewController", 96 },
    { "UINavigationController", "UIViewController", 96 },
    { "UINavigationItem", "NSObject", 64 },
    { "UIBarButtonItem", "NSObject", 64 },
    { "UIBarItem", "NSObject", 64 },
    { "UIAlertController", "UIViewController", 96 },
    { "UIAlertAction", "NSObject", 64 },
    { "UIActivityViewController", "UIViewController", 96 },
    { "UIDocumentPickerViewController", "UIViewController", 96 },
    { "UIImage", "NSObject", 64 },
    { "UIColor", "NSObject", 64 },
    { "UIFont", "NSObject", 64 },
    { "UIScreen", "NSObject", 64 },
    { "UIDevice", "NSObject", 64 },
    { "UIPasteboard", "NSObject", 64 },
    { "UITargetedPreview", "NSObject", 64 },
    { "UIGraphicsImageRenderer", "NSObject", 64 },
    { "UIGraphicsImageRendererFormat", "NSObject", 64 },
    { "UIGraphicsImageRendererContext", "NSObject", 64 },
    { "UIImpactFeedbackGenerator", "NSObject", 64 },
    { "UIGestureRecognizer", "NSObject", 64 },
    { "UITapGestureRecognizer", "UIGestureRecognizer", 64 },
    { "UIPanGestureRecognizer", "UIGestureRecognizer", 64 },
    { "UILongPressGestureRecognizer", "UIGestureRecognizer", 64 },
    { "CADisplayLink", "NSObject", 64 },
    { "CALayer", "NSObject", 96 },
    { "CAMetalLayer", "CALayer", 96 },
    { "CIFilter", "NSObject", 64 },
    { "CIImage", "NSObject", 64 },
    { "CIContext", "NSObject", 64 },
    { "CMMotionManager", "NSObject", 64 },
    { "GCController", "NSObject", 64 },
    { "GCMouse", "NSObject", 64 },
    { "AVAudioSession", "NSObject", 64 },
    { "AVAudioPlayer", "NSObject", 64 },
    { "AVPlayer", "NSObject", 64 },
    { "UZKArchive", "NSObject", 64 },
    { "AFURLSessionManager", "NSObject", 64 },
    { "AFHTTPSessionManager", "AFURLSessionManager", 64 },
    { "AFJSONRequestSerializer", "NSObject", 64 },
    { NULL, NULL, 0 }
};

static fo_class *fo_find(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_class_count; i++) {
        if (strcmp(g_classes[i]->name, name) == 0)
            return g_classes[i];
    }
    return NULL;
}

static fo_class *fo_owning(void *cls)
{
    int i;

    if (!cls)
        return NULL;
    for (i = 0; i < g_class_count; i++) {
        if ((void *)&g_classes[i]->cls == cls || (void *)&g_classes[i]->meta == cls)
            return g_classes[i];
    }
    return NULL;
}

void *chm_foundation_class_ro(void *cls)
{
    fo_class *c = cls == chm_objc_cfstr_isa() ? fo_find("NSConstantString") : fo_owning(cls);
    return c ? &c->ro : NULL;
}

static fo_class *fo_make(const char *name, uint32_t size)
{
    fo_class *c = calloc(1, sizeof(*c));

    if (!c)
        return NULL;
    c->name = strdup(name);
    if (!c->name) {
        free(c);
        return NULL;
    }
    c->ro.name = c->name;
    c->ro.instance_size = size ? size : 16;
    c->cls.isa = &c->meta;
    c->meta.isa = &c->meta;
    c->cls.bits = (uint64_t)(uintptr_t)&c->ro;
    c->meta.bits = (uint64_t)(uintptr_t)&c->ro;
    return c;
}

static const char *fo_class_name(void *cls)
{
    fo_class *c = fo_owning(cls);

    if (c && c->name)
        return c->name;
    return cls ? "foreign-class" : "nil-class";
}

static int fo_descends(fo_class *c, const char *name)
{
    for (; c; c = c->parent)
        if (!strcmp(c->name, name))
            return 1;
    return 0;
}

void *chm_foundation_alloc_class(void *cls, size_t min)
{
    fo_class *c = fo_owning(cls);
    if (!c || cls != &c->cls)
        return NULL;
    size_t size = c->ro.instance_size;
    uint64_t magic = 0;
    if (fo_descends(c, "NSArray")) {
        if (size < sizeof(fo_array)) size = sizeof(fo_array);
        magic = FO_ARRAY_MAGIC;
    } else if (fo_descends(c, "NSDictionary")) {
        if (size < sizeof(fo_dict)) size = sizeof(fo_dict);
        magic = FO_DICT_MAGIC;
    } else if (fo_descends(c, "NSNumber")) {
        if (size < sizeof(fo_number)) size = sizeof(fo_number);
        magic = FO_NUMBER_MAGIC;
    } else if (fo_descends(c, "NSString")) {
        if (size < sizeof(fo_string)) size = sizeof(fo_string);
        magic = FO_STRING_MAGIC;
    } else if (fo_descends(c, "NSData")) {
        if (size < sizeof(fo_data)) size = sizeof(fo_data);
        magic = FO_DATA_MAGIC;
    }
    if (size < min) size = min;
    void *obj = calloc(1, size);
    if (!obj) return NULL;
    *(void **)obj = cls;
    if (magic) memcpy((char *)obj + sizeof(void *), &magic, sizeof(magic));
    if (magic == FO_ARRAY_MAGIC) {
        fo_array *a = obj;
        a->capacity = FO_MAX_ITEMS;
        a->items = calloc(a->capacity, sizeof(void *));
        if (!a->items) { free(a); return NULL; }
    } else if (magic == FO_DICT_MAGIC) {
        fo_dict *d = obj;
        d->capacity = FO_MAX_ITEMS;
        d->keys = calloc(d->capacity, sizeof(void *));
        d->values = calloc(d->capacity, sizeof(void *));
        if (!d->keys || !d->values) { free(d->keys); free(d->values); free(d); return NULL; }
    } else if (magic == FO_NUMBER_MAGIC) {
        ((fo_number *)obj)->type = 'q';
    }
    return obj;
}

static void *fo_alloc(const char *cls_name, size_t min)
{
    fo_class *c = fo_find(cls_name);
    return c ? chm_foundation_alloc_class(&c->cls, min) : NULL;
}

static int fo_isa_is(void *obj, const char *cls_name)
{
    void *cls;
    fo_class *c;

    if (!obj)
        return 0;
    cls = *(void **)obj;
    c = fo_owning(cls);
    while (c) {
        if (strcmp(c->name, cls_name) == 0)
            return 1;
        c = c->parent;
    }
    return 0;
}

#define FO_MIN_PTR 0x10000u

static int fo_is_string(void *p)
{
    if (!p || (uintptr_t)p < FO_MIN_PTR)
        return 0;
    return ((fo_string *)p)->magic == FO_STRING_MAGIC;
}

static int fo_is_data(void *p)
{
    if (!p || (uintptr_t)p < FO_MIN_PTR)
        return 0;
    return ((fo_data *)p)->magic == FO_DATA_MAGIC;
}

static int fo_is_array(void *p)
{
    if (!p || (uintptr_t)p < FO_MIN_PTR)
        return 0;
    return ((fo_array *)p)->magic == FO_ARRAY_MAGIC;
}

static int fo_is_dict(void *p)
{
    if (!p || (uintptr_t)p < FO_MIN_PTR)
        return 0;
    return ((fo_dict *)p)->magic == FO_DICT_MAGIC;
}

static int fo_is_number(void *p)
{
    return p && (uintptr_t)p >= FO_MIN_PTR && ((fo_number *)p)->magic == FO_NUMBER_MAGIC;
}

static void *n_signed(void *cls, const char *sel, int64_t value)
{
    (void)cls; (void)sel;
    fo_number *n = fo_alloc("NSNumber", sizeof(fo_number));
    if (n) { n->type = 'q'; n->value.signed_value = value; }
    return n;
}

static void *n_unsigned(void *cls, const char *sel, uint64_t value)
{
    (void)cls; (void)sel;
    fo_number *n = fo_alloc("NSNumber", sizeof(fo_number));
    if (n) { n->type = 'Q'; n->value.unsigned_value = value; }
    return n;
}

static void *n_double(void *cls, const char *sel, double value)
{
    (void)cls; (void)sel;
    fo_number *n = fo_alloc("NSNumber", sizeof(fo_number));
    if (n) { n->type = 'd'; n->value.real_value = value; }
    return n;
}

static void *n_int(void *c, const char *s, int v) { return n_signed(c, s, v); }
static void *n_uint(void *c, const char *s, unsigned int v) { return n_unsigned(c, s, v); }
static void *n_bool(void *c, const char *s, signed char v)
{
    fo_number *n = n_signed(c, s, v != 0);
    if (n) n->type = 'B';
    return n;
}
static void *n_float(void *c, const char *s, float v) { return n_double(c, s, v); }

static int fo_is_handle(void *p)
{
    if (!p || (uintptr_t)p < FO_MIN_PTR)
        return 0;
    return ((fo_handle *)p)->magic == FO_HANDLE_MAGIC;
}

static void *fo_handle_new(int fd, const char *path)
{
    fo_handle *h = (fo_handle *)fo_alloc("NSFileHandle", sizeof(fo_handle));

    if (!h)
        return NULL;
    h->magic = FO_HANDLE_MAGIC;
    h->fd = fd;
    if (chm_io_remap(fd, fd, 0) < 0) { free(h); close(fd); return NULL; }
    h->closed = 0;
    if (path)
        snprintf(h->path, sizeof(h->path), "%s", path);
    return h;
}

static int fo_is_cfstring(void *p)
{
    void *cls;

    if (!p || (uintptr_t)p < FO_MIN_PTR || fo_is_string(p))
        return 0;
    cls = *(void **)p;
    if (!cls)
        return 0;
    if (cls == chm_objc_cfstr_isa())
        return 1;
    return chm_foundation_isa_is(p, "NSConstantString") ||
           chm_foundation_isa_is(p, "__NSCFConstantString") ||
           chm_foundation_isa_is(p, "NSMutableString");
}

#include "foundation_strings.inc"

static const char *fo_bytes(void *p)
{
    if (fo_is_string(p)) {
        const char *bytes = ((fo_string *)p)->bytes;
        return bytes ? bytes : "";
    }
    if (fo_is_cfstring(p)) return constant_utf8(p);
    return "";
}

const char *chm_foundation_cstr(void *obj)
{
    return fo_bytes(obj);
}

static void *fo_string_new(const char *bytes)
{
    fo_string *s = fo_alloc("NSString", sizeof(fo_string));
    fo_string *out = (fo_string *)s;

    if (!out)
        return NULL;
    out->magic = FO_STRING_MAGIC;
    out->bytes = strdup(bytes ? bytes : "");
    if (!out->bytes) {
        free(out);
        return NULL;
    }
    out->length = (int64_t)strlen(out->bytes);
    return out;
}

static void *fo_data_new(const void *bytes, int64_t len)
{
    fo_data *d = fo_alloc("NSData", sizeof(fo_data));

    if (!d)
        return NULL;
    if (len < 0)
        len = 0;
    if (bytes && len) {
        d->bytes = malloc((size_t)len);
        if (!d->bytes) {
            free(d);
            return NULL;
        }
        memcpy(d->bytes, bytes, (size_t)len);
    }
    d->length = len;
    d->magic = FO_DATA_MAGIC;
    return d;
}

static void *fo_array_new(void)
{
    fo_array *a = fo_alloc("NSMutableArray", sizeof(fo_array));

    if (!a)
        return NULL;
    a->magic = FO_ARRAY_MAGIC;
    return a;
}

static void *fo_dict_new(void)
{
    fo_dict *d = fo_alloc("NSMutableDictionary", sizeof(fo_dict));

    if (!d)
        return NULL;
    d->magic = FO_DICT_MAGIC;
    return d;
}

static int a_reserve(fo_array *a, size_t need)
{
    if (need <= a->capacity) return 1;
    if (need > SIZE_MAX / sizeof(void *) || need > INT64_MAX) return 0;
    size_t cap = a->capacity <= SIZE_MAX / 2 ? a->capacity * 2 : need;
    if (cap < need) cap = need;
    if (cap > SIZE_MAX / sizeof(void *)) cap = need;
    void **items = realloc(a->items, cap * sizeof(void *));
    if (!items) return 0;
    a->items = items;
    a->capacity = cap;
    return 1;
}

static int d_reserve(fo_dict *d, size_t need)
{
    if (need <= d->capacity) return 1;
    if (need > SIZE_MAX / sizeof(void *) || need > INT64_MAX) return 0;
    size_t cap = d->capacity <= SIZE_MAX / 2 ? d->capacity * 2 : need;
    if (cap < need) cap = need;
    if (cap > SIZE_MAX / sizeof(void *)) cap = need;
    void **keys = malloc(cap * sizeof(void *));
    void **values = malloc(cap * sizeof(void *));
    if (!keys || !values) { free(keys); free(values); return 0; }
    if (d->count) {
        memcpy(keys, d->keys, (size_t)d->count * sizeof(void *));
        memcpy(values, d->values, (size_t)d->count * sizeof(void *));
    }
    free(d->keys); free(d->values);
    d->keys = keys; d->values = values; d->capacity = cap;
    return 1;
}

static int a_push(fo_array *a, void *obj)
{
    if (!obj || !a_reserve(a, (size_t)a->count + 1) || !cf_array_hold(a, obj)) return 0;
    a->items[a->count++] = obj;
    a->mutations++;
    return 1;
}

static void collection_error(const char *operation)
{
    chm_session_log(g_session, "foundation collection operation failed: %s", operation);
}

static void *fo_pair_get(const char *key)
{
    int i;

    if (!key)
        return NULL;
    for (i = 0; i < g_pair_count; i++) {
        if (strcmp(g_pairs[i].key, key) == 0)
            return g_pairs[i].value;
    }
    return NULL;
}

static void fo_pair_set(const char *key, void *value)
{
    int i;

    if (!key)
        return;
    for (i = 0; i < g_pair_count; i++) {
        if (strcmp(g_pairs[i].key, key) == 0) {
            g_pairs[i].value = value;
            return;
        }
    }
    if (g_pair_count < FO_MAX_ITEMS) {
        g_pairs[g_pair_count].key = strdup(key);
        g_pairs[g_pair_count].value = value;
        g_pair_count++;
    }
}

static void *singleton(const char *key, const char *cls_name)
{
    void *cached = fo_pair_get(key);

    if (!cached) {
        cached = fo_alloc(cls_name, 64);
        if (cached)
            fo_pair_set(key, cached);
    }
    return cached;
}

static void *cf_owned_retain(void *object);
static void cf_owned_release(void *object);

static void *o_retain(void *self)
{
    return cf_owned_retain(self);
}

static void o_release(void *self)
{
    cf_owned_release(self);
}

static void *o_self(void *self)
{
    return self;
}

static void *o_class(void *self)
{
    return self ? *(void **)self : NULL;
}

static long o_hash(void *self)
{
    if (sec_type_id(self)) return (long)sec_hash(self);
    const unsigned char *p;

    if (!fo_is_string(self))
        return (long)(uintptr_t)self;
    p = (const unsigned char *)((fo_string *)self)->bytes;
    unsigned long h = 5381;
    while (p && *p)
        h = (h * 33) ^ *p++;
    return (long)h;
}

static int o_yes(void *self)
{
    (void)self;
    return 1;
}

static int o_no(void *self)
{
    (void)self;
    return 0;
}

static int o_is_kind(void *self, const char *sel, void *cls)
{
    if (!self || !cls)
        return 0;
    if (!strcmp(sel, "isMemberOfClass:"))
        return *(void **)self == cls;
    return chm_objc_is_kind(self, chm_objc_class_name(cls));
}

static int o_is_string_class(void *self, const char *sel)
{
    (void)sel;
    return fo_isa_is(self, "NSString");
}

static int o_is_data_class(void *self, const char *sel)
{
    (void)sel;
    return fo_isa_is(self, "NSData");
}

static void *s_data_using_encoding(void *self, const char *sel, uint64_t encoding)
{
    const char *bytes = fo_bytes(self);

    (void)sel;
    (void)encoding;
    return fo_data_new(bytes, (int64_t)strlen(bytes));
}

static void *s_data_with_bytes(void *cls, const char *sel, const void *bytes, int64_t len)
{
    (void)cls;
    (void)sel;
    return fo_data_new(bytes, len);
}

static int o_is_equal(void *self, const char *sel, void *other)
{
    (void)sel;
    if (self == other) return 1;
    if (!self || !other) return 0;
    int security = sec_equal(self, other);
    if (security >= 0) return security;
    if (fo_is_data(self) && fo_is_data(other)) {
        fo_data *a = self, *b = other;
        return a->length == b->length && (!a->length || !memcmp(a->bytes, b->bytes, (size_t)a->length));
    }
    if ((fo_is_string(self) || fo_is_cfstring(self)) &&
        (fo_is_string(other) || fo_is_cfstring(other)))
        return strcmp(fo_bytes(self), fo_bytes(other)) == 0;
    if (fo_is_number(self) && fo_is_number(other)) {
        fo_number *a = self, *b = other;
        if (a->type == 'd' || b->type == 'd') {
            if (a->type != 'd') { fo_number *t = a; a = b; b = t; }
            double x = a->value.real_value;
            if (b->type == 'd') return x == b->value.real_value;
            if (b->type == 'Q')
                return x >= 0 && x < 0x1p64 && (uint64_t)x == b->value.unsigned_value &&
                       x == (double)b->value.unsigned_value;
            return x >= -0x1p63 && x < 0x1p63 && (int64_t)x == b->value.signed_value &&
                   x == (double)b->value.signed_value;
        }
        if (a->type == 'Q' && b->type == 'Q') return a->value.unsigned_value == b->value.unsigned_value;
        if (a->type == 'q' && b->type == 'q') return a->value.signed_value == b->value.signed_value;
        if (a->type == 'Q') { fo_number *t = a; a = b; b = t; }
        return a->value.signed_value >= 0 && (uint64_t)a->value.signed_value == b->value.unsigned_value;
    }
    return 0;
}

static long o_compare(void *self, const char *sel, void *other)
{
    (void)sel;
    int value = strcmp(fo_bytes(self), fo_bytes(other));
    return (value > 0) - (value < 0);
}

static int o_responds(void *self, const char *cmd, const char *sel)
{
    (void)cmd;
    return chm_objc_find_imp(self, sel) != NULL;
}

static int64_t o_length(void *self)
{
    if (fo_is_string(self))
        return ((fo_string *)self)->length;
    if (fo_is_data(self))
        return ((fo_data *)self)->length;
    return 0;
}

static int64_t o_count(void *self)
{
    if (fo_is_array(self))
        return ((fo_array *)self)->count;
    if (fo_is_dict(self))
        return ((fo_dict *)self)->count;
    return 0;
}

static const char *o_utf8(void *self)
{
    if (fo_is_data(self))
        return (const char *)((fo_data *)self)->bytes;
    return fo_bytes(self);
}

static const char *describe_utf8(void *self)
{
    if (!self || (uintptr_t)self < FO_MIN_PTR)
        return "(null)";
    if (fo_is_string(self) || fo_is_cfstring(self))
        return fo_bytes(self);
    return fo_class_name(*(void **)self);
}

static void *o_description(void *self, const char *sel)
{
    (void)sel;
    if (fo_is_string(self) || fo_is_cfstring(self))
        return self;
    return fo_string_new(describe_utf8(self));
}

static int64_t o_integer(void *self)
{
    if (fo_is_number(self)) {
        fo_number *n = self;
        if (n->type == 'Q') return (int64_t)n->value.unsigned_value;
        if (n->type == 'd') {
            double v = n->value.real_value;
            if (v != v) return 0;
            if (v >= 0x1p63) return INT64_MAX;
            if (v <= -0x1p63) return INT64_MIN;
            return (int64_t)v;
        }
        return n->value.signed_value;
    }
    return strtoll(fo_bytes(self), NULL, 10);
}

static uint64_t o_unsigned(void *self)
{
    if (fo_is_number(self)) {
        fo_number *n = self;
        if (n->type == 'Q') return n->value.unsigned_value;
        if (n->type == 'd') {
            double v = n->value.real_value;
            if (v != v || v <= 0) return 0;
            if (v >= 0x1p64) return UINT64_MAX;
            return (uint64_t)v;
        }
        return (uint64_t)n->value.signed_value;
    }
    return strtoull(fo_bytes(self), NULL, 10);
}

static double o_double(void *self)
{
    if (fo_is_number(self)) {
        fo_number *n = self;
        if (n->type == 'Q') return (double)n->value.unsigned_value;
        if (n->type == 'd') return n->value.real_value;
        return (double)n->value.signed_value;
    }
    return strtod(fo_bytes(self), NULL);
}

static float o_float(void *self)
{
    return (float)o_double(self);
}

static int o_bool(void *self)
{
    if (fo_is_number(self)) {
        fo_number *n = self;
        if (n->type == 'd') return n->value.real_value != 0.0;
        if (n->type == 'Q') return n->value.unsigned_value != 0;
        return n->value.signed_value != 0;
    }
    const char *b = fo_bytes(self);
    while (*b == ' ' || *b == '\t' || *b == '\n') b++;
    return *b == 't' || *b == 'T' || *b == 'y' || *b == 'Y' || strtoll(b, NULL, 10) != 0;
}

static void *s_identity(void *self)
{
    return fo_string_new(fo_bytes(self));
}

static void *s_with_utf8(void *cls, const char *sel, const char *bytes)
{
    (void)cls;
    (void)sel;
    return fo_string_new(bytes);
}

extern void chm_ns_format_entry(void);
extern void chm_ns_append_format_entry(void);

void *chm_ns_format_list(void *self, const char *sel, void *fmt, const void *slots)
{
    (void)self;
    (void)sel;
    char *text = chm_format_darwin(fo_bytes(fmt), slots, NULL);
    if (!text)
        return NULL;
    void *str = fo_string_new(text);
    free(text);
    return str;
}

void *chm_ns_append_format_list(void *self, const char *sel, void *fmt, const void *slots)
{
    (void)sel;
    size_t len;
    char *text = chm_format_darwin(fo_bytes(fmt), slots, &len);
    if (!text)
        return NULL;
    const char *prefix = fo_bytes(self);
    size_t n = strlen(prefix);
    char *joined = n <= SIZE_MAX - len - 1 ? malloc(n + len + 1) : NULL;
    void *str = NULL;
    if (joined) {
        memcpy(joined, prefix, n);
        memcpy(joined + n, text, len + 1);
        if (!strcmp(sel, "appendFormat:") && fo_is_string(self) && chm_objc_is_kind(self, "NSMutableString")) {
            fo_string *mutable = self;
            free(mutable->bytes);
            mutable->bytes = joined; mutable->length = (int64_t)(n + len);
            str = self;
        } else {
            str = fo_string_new(joined);
            free(joined);
        }
    }
    free(text);
    return str;
}

static void *s_append_component(void *self, const char *sel, void *comp)
{
    const char *base = fo_bytes(self);
    const char *suffix = fo_bytes(comp);

    (void)sel;
    size_t blen = strlen(base);
    size_t slen = strlen(suffix);
    char *buf;
    void *out;

    while (blen > 0 && base[blen - 1] == '/')
        blen--;
    buf = malloc(blen + slen + 2);
    if (!buf)
        return NULL;
    memcpy(buf, base, blen);
    buf[blen] = '/';
    memcpy(buf + blen + 1, suffix, slen + 1);
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *s_append_string(void *self, const char *sel, void *tail)
{
    const char *a = fo_bytes(self);
    const char *b = fo_bytes(tail);

    (void)sel;
    size_t total = strlen(a) + strlen(b) + 1;
    char *buf = malloc(total);
    void *out;

    if (!buf)
        return NULL;
    snprintf(buf, total, "%s%s", a, b);
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *s_delete_last(void *self)
{
    const char *base = fo_bytes(self);
    size_t len = strlen(base);
    char *buf;
    void *out;

    while (len > 1 && base[len - 1] == '/')
        len--;
    while (len > 0 && base[len - 1] != '/')
        len--;
    if (len == 0) {
        if (base[0] == '/')
            return fo_string_new("/");
        return fo_string_new(".");
    }
    while (len > 1 && base[len - 1] == '/')
        len--;
    buf = malloc(len + 1);
    if (!buf)
        return NULL;
    memcpy(buf, base, len);
    buf[len] = 0;
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *s_delete_extension(void *self)
{
    const char *base = fo_bytes(self);
    const char *slash = strrchr(base, '/');
    const char *dot = strrchr(base, '.');
    size_t len;
    char *buf;
    void *out;

    if (!dot || (slash && dot < slash))
        return fo_string_new(base);
    len = (size_t)(dot - base);
    buf = malloc(len + 1);
    if (!buf)
        return NULL;
    memcpy(buf, base, len);
    buf[len] = 0;
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *s_last_component(void *self)
{
    const char *base = fo_bytes(self);
    const char *slash = strrchr(base, '/');

    return fo_string_new(slash ? slash + 1 : base);
}

static void *s_extension(void *self)
{
    const char *base = fo_bytes(self);
    const char *slash = strrchr(base, '/');
    const char *dot = strrchr(base, '.');

    if (!dot || (slash && dot < slash))
        return fo_string_new("");
    return fo_string_new(dot + 1);
}

static int s_is_absolute(void *self)
{
    const char *base = fo_bytes(self);

    return base && base[0] == '/';
}

static void *s_split(void *self, const char *sel, void *sep)
{
    (void)sel;
    const char *s = fo_bytes(sep);
    fo_array *arr = fo_array_new();
    if (!arr) return NULL;
    char *copy = strdup(fo_bytes(self));
    if (!copy) { free(arr->items); free(arr); return NULL; }
    size_t seplen = strlen(s);
    char *cursor = copy;
    for (;;) {
        char *hit = seplen ? strstr(cursor, s) : NULL;
        if (hit) *hit = 0;
        if (!a_push(arr, fo_string_new(cursor))) {
            free(copy); free(arr->items); free(arr);
            return NULL;
        }
        if (!hit) break;
        cursor = hit + seplen;
    }
    free(copy);
    return arr;
}

static void *s_substring_to(void *self, const char *sel, int64_t idx)
{
    const char *base = fo_bytes(self);
    int64_t len = (int64_t)strlen(base);
    char *buf;
    void *out;

    (void)sel;

    if (idx < 0)
        idx = 0;
    if (idx > len)
        idx = len;
    buf = malloc((size_t)idx + 1);
    if (!buf)
        return NULL;
    memcpy(buf, base, (size_t)idx);
    buf[idx] = 0;
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *s_substring_from(void *self, const char *sel, int64_t idx)
{
    const char *base = fo_bytes(self);
    int64_t len = (int64_t)strlen(base);

    (void)sel;

    if (idx < 0 || idx > len)
        return fo_string_new("");
    return fo_string_new(base + idx);
}

static void *s_lower(void *self)
{
    char *buf = strdup(fo_bytes(self));
    void *out;

    if (!buf)
        return NULL;
    for (char *p = buf; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *s_upper(void *self)
{
    char *buf = strdup(fo_bytes(self));
    void *out;

    if (!buf)
        return NULL;
    for (char *p = buf; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static int s_has_prefix(void *self, const char *sel, void *prefix)
{
    (void)sel;
    return strncmp(fo_bytes(self), fo_bytes(prefix), strlen(fo_bytes(prefix))) == 0;
}

static int s_has_suffix(void *self, const char *sel, void *suffix)
{
    const char *a = fo_bytes(self);
    const char *b = fo_bytes(suffix);
    size_t la = strlen(a);
    size_t lb = strlen(b);

    (void)sel;
    return lb <= la && strcmp(a + la - lb, b) == 0;
}

static void *a_object_at(void *self, const char *sel, int64_t idx)
{
    (void)sel;
    fo_array *a = self;
    if (!fo_is_array(a) || idx < 0 || idx >= a->count) {
        collection_error("objectAtIndex: out of range");
        return NULL;
    }
    return a->items[idx];
}

static void *a_first(void *self, const char *sel)
{
    (void)sel;
    fo_array *a = self;
    return fo_is_array(a) && a->count ? a->items[0] : NULL;
}

static void *a_last(void *self, const char *sel)
{
    (void)sel;
    fo_array *a = self;
    return fo_is_array(a) && a->count ? a->items[a->count - 1] : NULL;
}

static void a_add(void *self, const char *sel, void *obj)
{
    (void)sel;
    if (!fo_is_array(self) || !a_push(self, obj))
        collection_error("addObject:");
}

static void a_remove_all(void *self, const char *sel)
{
    (void)sel;
    if (!fo_is_array(self)) return;
    fo_array *a = self;
    for (int64_t i = 0; i < a->count; i++) cf_array_drop(a, a->items[i]);
    if (a->count) memset(a->items, 0, (size_t)a->count * sizeof(*a->items));
    a->count = 0;
    a->mutations++;
}

static void *a_joined(void *self, const char *sel, void *sep)
{
    fo_array *a = self;
    const char *s = fo_bytes(sep);
    size_t total = 1;
    char *buf;
    void *out;

    (void)sel;

    if (!fo_is_array(a))
        return NULL;
    for (int64_t i = 0; i < a->count; i++)
        total += strlen(fo_bytes(a->items[i])) + strlen(s);
    buf = calloc(1, total);
    if (!buf)
        return NULL;
    for (int64_t i = 0; i < a->count; i++) {
        if (i)
            strcat(buf, s);
        strcat(buf, fo_bytes(a->items[i]));
    }
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static int64_t d_index(fo_dict *d, void *key)
{
    if (!fo_is_dict(d) || !key) return -1;
    for (int64_t i = 0; i < d->count; i++)
        if (o_is_equal(d->keys[i], NULL, key)) return i;
    return -1;
}

static void *d_value(void *self, const char *sel, void *key)
{
    (void)sel;
    fo_dict *d = self;
    int64_t i = d_index(d, key);
    return i >= 0 ? d->values[i] : NULL;
}

static int d_put(fo_dict *d, void *key, void *obj)
{
    if (!fo_is_dict(d) || !key || !obj) return 0;
    int64_t i = d_index(d, key);
    if (i >= 0) {
        d->values[i] = obj;
        d->mutations++;
        return 1;
    }
    if (!d_reserve(d, (size_t)d->count + 1)) return 0;
    void *stored_key = key;
    if (fo_is_string(key) || fo_is_cfstring(key)) stored_key = fo_string_new(fo_bytes(key));
    if (!stored_key) return 0;
    d->keys[d->count] = stored_key;
    d->values[d->count++] = obj;
    d->mutations++;
    return 1;
}

static void d_remove(void *self, const char *sel, void *key)
{
    (void)sel;
    fo_dict *d = self;
    int64_t i = d_index(d, key);
    if (i < 0) return;
    size_t n = (size_t)(d->count - i - 1);
    memmove(d->keys + i, d->keys + i + 1, n * sizeof(void *));
    memmove(d->values + i, d->values + i + 1, n * sizeof(void *));
    d->count--;
    d->keys[d->count] = d->values[d->count] = NULL;
    d->mutations++;
}

static void d_set(void *self, const char *sel, void *obj, void *key)
{
    (void)sel;
    if (!d_put(self, key, obj)) collection_error("setObject:forKey:");
}

static void d_set_subscript(void *self, const char *sel, void *obj, void *key)
{
    if (obj) d_set(self, sel, obj, key);
    else d_remove(self, sel, key);
}

static void *d_key_path_parent(void *self, const char *path, char **leaf)
{
    char *copy = strdup(path);
    if (!copy) return NULL;
    void *current = self;
    char *part = copy;
    char *dot;
    while ((dot = strchr(part, '.')) != NULL) {
        *dot = 0;
        if (!*part || !fo_is_dict(current)) { free(copy); return NULL; }
        void *key = fo_string_new(part);
        current = d_value(current, NULL, key);
        part = dot + 1;
    }
    if (!*part || !fo_is_dict(current)) { free(copy); return NULL; }
    *leaf = strdup(part);
    free(copy);
    return *leaf ? current : NULL;
}

static void *d_value_path(void *self, const char *sel, void *path)
{
    (void)sel;
    char *leaf = NULL;
    void *parent = d_key_path_parent(self, fo_bytes(path), &leaf);
    if (!parent) return NULL;
    void *key = fo_string_new(leaf);
    free(leaf);
    return d_value(parent, NULL, key);
}

static void d_set_value_path(void *self, const char *sel, void *value, void *path)
{
    (void)sel;
    char *leaf = NULL;
    void *parent = d_key_path_parent(self, fo_bytes(path), &leaf);
    if (!parent) return;
    void *key = fo_string_new(leaf);
    free(leaf);
    d_set_subscript(parent, NULL, value, key);
}

static void *d_keys(void *self)
{
    fo_dict *d = self;
    if (!fo_is_dict(d)) return NULL;
    fo_array *a = fo_array_new();
    if (!a) return NULL;
    if (!a_reserve(a, (size_t)d->count)) { free(a->items); free(a); return NULL; }
    for (int64_t i = 0; i < d->count; i++)
        if (!a_push(a, d->keys[i])) return NULL;
    return a;
}

static void *d_values(void *self)
{
    fo_dict *d = self;
    if (!fo_is_dict(d)) return NULL;
    fo_array *a = fo_array_new();
    if (!a) return NULL;
    if (!a_reserve(a, (size_t)d->count)) { free(a->items); free(a); return NULL; }
    for (int64_t i = 0; i < d->count; i++)
        if (!a_push(a, d->values[i])) return NULL;
    return a;
}

static void collection_dispose(void *obj)
{
    if (cf_collection_dispose(obj)) return;
    if (fo_is_array(obj)) free(((fo_array *)obj)->items);
    else if (fo_is_dict(obj)) { free(((fo_dict *)obj)->keys); free(((fo_dict *)obj)->values); }
    free(obj);
}

static void *collection_capacity(void *self, const char *sel, size_t capacity)
{
    (void)sel;
    if (fo_is_array(self)) return a_reserve(self, capacity) ? self : NULL;
    if (fo_is_dict(self)) return d_reserve(self, capacity) ? self : NULL;
    return NULL;
}

static void *collection_empty(void *cls, const char *sel)
{
    (void)sel;
    void *obj = chm_foundation_alloc_class(cls, 0);
    if (fo_is_array(obj) || fo_is_dict(obj)) return obj;
    free(obj);
    return NULL;
}

static void *collection_with_capacity(void *cls, const char *sel, size_t capacity)
{
    void *obj = collection_empty(cls, sel);
    if (obj && !collection_capacity(obj, sel, capacity)) { collection_dispose(obj); return NULL; }
    return obj;
}

static void *a_init_objects(void *self, const char *sel, void *const *objects, size_t count)
{
    (void)sel;
    fo_array *a = self;
    if (!fo_is_array(a) || (count && !objects)) return NULL;
    for (size_t i = 0; i < count; i++) if (!objects[i]) return NULL;
    if (!a_reserve(a, count)) return NULL;
    void **copy = count ? malloc(count * sizeof(*copy)) : NULL;
    if (count && !copy) return NULL;
    if (count) memcpy(copy, objects, count * sizeof(*copy));
    for (size_t i = 0; i < count; i++) {
        if (!cf_array_hold(a, copy[i])) {
            for (size_t j = 0; j < i; j++) cf_array_drop(a, copy[j]);
            free(copy); return NULL;
        }
    }
    a_remove_all(a, NULL);
    if (count) memcpy(a->items, copy, count * sizeof(*copy));
    free(copy); a->count = (int64_t)count;
    a->mutations++;
    return a;
}

static void *a_with_objects(void *cls, const char *sel, void *const *objects, size_t count)
{
    void *a = collection_empty(cls, sel);
    if (a && !a_init_objects(a, sel, objects, count)) { collection_dispose(a); return NULL; }
    return a;
}

static void *a_with_object(void *cls, const char *sel, void *obj)
{
    return a_with_objects(cls, sel, &obj, 1);
}

static void *a_with_array(void *cls, const char *sel, void *other)
{
    if (!fo_is_array(other)) return NULL;
    fo_array *a = other;
    return a_with_objects(cls, sel, a->items, (size_t)a->count);
}

static void *d_init_objects(void *self, const char *sel, void *const *objects, void *const *keys, size_t count)
{
    (void)sel;
    fo_dict *d = self;
    if (!fo_is_dict(d) || (count && (!objects || !keys))) return NULL;
    for (size_t i = 0; i < count; i++) if (!objects[i] || !keys[i]) return NULL;
    if (!d_reserve(d, count)) return NULL;
    d->count = 0;
    d->mutations++;
    for (size_t i = 0; i < count; i++) if (!d_put(d, keys[i], objects[i])) return NULL;
    return d;
}

static void *d_with_objects(void *cls, const char *sel, void *const *objects, void *const *keys, size_t count)
{
    void *d = collection_empty(cls, sel);
    if (d && !d_init_objects(d, sel, objects, keys, count)) { collection_dispose(d); return NULL; }
    return d;
}

static void *d_with_object(void *cls, const char *sel, void *obj, void *key)
{
    return d_with_objects(cls, sel, &obj, &key, 1);
}

static void *d_with_dictionary(void *cls, const char *sel, void *other)
{
    if (!fo_is_dict(other)) return NULL;
    fo_dict *d = other;
    return d_with_objects(cls, sel, d->values, d->keys, (size_t)d->count);
}

static void d_add_entries(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_dict(self) || !fo_is_dict(other) || self == other) return;
    fo_dict *d = other;
    for (int64_t i = 0; i < d->count; i++)
        if (!d_put(self, d->keys[i], d->values[i])) { collection_error("addEntriesFromDictionary:"); return; }
}

static void *o_copy(void *self, const char *sel)
{
    int mutable = !strcmp(sel, "mutableCopy") || !strcmp(sel, "mutableCopyWithZone:");
    if (fo_is_array(self))
        return a_with_array(chm_foundation_lookup_class(mutable ? "NSMutableArray" : "NSArray"), sel, self);
    if (fo_is_dict(self))
        return d_with_dictionary(chm_foundation_lookup_class(mutable ? "NSMutableDictionary" : "NSDictionary"), sel, self);
    if (fo_is_string(self) || fo_is_cfstring(self)) {
        fo_string *s = fo_string_new(fo_bytes(self));
        if (s && mutable) s->isa = chm_foundation_lookup_class("NSMutableString");
        return s;
    }
    return self;
}

static unsigned long collection_enumerate(void *self, const char *sel, fo_enum_state *state, void **buffer, unsigned long count)
{
    (void)sel;
    if (!state || !buffer || !count) return 0;
    void **items;
    unsigned long total;
    if (fo_is_array(self)) {
        fo_array *a = self;
        items = a->items; total = (unsigned long)a->count; state->mutations = &a->mutations;
    } else if (fo_is_dict(self)) {
        fo_dict *d = self;
        items = d->keys; total = (unsigned long)d->count; state->mutations = &d->mutations;
    } else return 0;
    if (state->state >= total) return 0;
    unsigned long n = total - state->state;
    if (n > count) n = count;
    memcpy(buffer, items + state->state, n * sizeof(void *));
    state->items = buffer;
    state->state += n;
    return n;
}

static int f_exists(void *self, const char *sel, void *path)
{
    char mapped[1024];
    struct stat st;

    (void)self;
    (void)sel;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)))
        return 0;
    return stat(mapped, &st) == 0 ? 1 : 0;
}

static void *make_error(const char *domain, long code, const char *desc)
{
    fo_dict *d = (fo_dict *)fo_dict_new();

    (void)domain;
    (void)code;
    if (!d)
        return NULL;
    d->keys[d->count] = fo_string_new("NSLocalizedDescription");
    d->values[d->count] = fo_string_new(desc ? desc : "error");
    d->count++;
    d_put(d, fo_string_new("domain"), fo_string_new(domain ? domain : "NSCocoaErrorDomain"));
    d_put(d, fo_string_new("code"), n_signed(NULL, NULL, code));
    return d;
}

static int ensure_parent(const char *path)
{
    char tmp[1024];
    char *slash;

    snprintf(tmp, sizeof(tmp), "%s", path);
    slash = strrchr(tmp, '/');
    if (!slash || slash == tmp)
        return 0;
    *slash = 0;
    return mkdir_p(tmp);
}

static int f_create_dir(void *self, const char *sel, void *path, int intermediate, void *attrs,
                        void **error)
{
    char mapped[1024];
    int rc;

    (void)self;
    (void)sel;
    (void)attrs;
    if (error) *error = NULL;
    if (chm_foundation_map_mutable(fo_bytes(path), mapped, sizeof(mapped))) {
        rc = intermediate ? mkdir_p(mapped) : directory_one(mapped);
        if (!rc) return 1;
    }
    int reason = errno;
    chm_session_log(g_session, "mkdir [%s] failed: errno=%d (%s)", fo_bytes(path), reason, strerror(reason));
    long code = reason == ENOENT ? 4 : reason == ENOTDIR || reason == EEXIST ? 516 : reason == EACCES ? 513 : 512;
    if (error) *error = make_error("NSCocoaErrorDomain", code, strerror(reason));
    return 0;
}

static int f_remove(void *self, const char *sel, void *path, void **error)
{
    char mapped[1024];
    struct stat st;

    (void)self;
    (void)sel;
    if (error)
        *error = NULL;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)))
        return 0;
    if (stat(mapped, &st) != 0) {
        if (error)
            *error = make_error("NSCocoaErrorDomain", 4, "no such file");
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        if (rmdir(mapped) == 0)
            return 1;
    } else if (unlink(mapped) == 0) {
        return 1;
    }
    if (error)
        *error = make_error("NSCocoaErrorDomain", 4, "remove failed");
    return 0;
}

static int f_create_file(void *self, const char *sel, void *path, void *contents, void *attrs)
{
    char mapped[1024];
    int fd;
    int ok = 1;

    (void)self;
    (void)sel;
    (void)attrs;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)))
        return 0;
    ensure_parent(mapped);
    fd = open(mapped, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return 0;
    if (fo_is_data(contents)) {
        const uint8_t *p = ((fo_data *)contents)->bytes;
        size_t left = (size_t)((fo_data *)contents)->length;

        while (left) {
            ssize_t n = write(fd, p, left);

            if (n <= 0) {
                ok = 0;
                break;
            }
            p += n;
            left -= (size_t)n;
        }
    }
    close(fd);
    return ok;
}

static int f_move(void *self, const char *sel, void *from, void *to, void **error)
{
    char a[1024];
    char b[1024];

    (void)self;
    (void)sel;
    if (error)
        *error = NULL;
    if (!chm_foundation_map_path(fo_bytes(from), a, sizeof(a)))
        return 0;
    if (!chm_foundation_map_path(fo_bytes(to), b, sizeof(b)))
        return 0;
    ensure_parent(b);
    if (rename(a, b) == 0)
        return 1;
    if (error)
        *error = make_error("NSCocoaErrorDomain", 4, "move failed");
    return 0;
}

static int copy_file_bytes(const char *src, const char *dst)
{
    int in;
    int out;
    char buf[8192];
    ssize_t n;

    in = open(src, O_RDONLY);
    if (in < 0)
        return 0;
    ensure_parent(dst);
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0) {
        close(in);
        return 0;
    }
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;

        while (off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));

            if (w <= 0) {
                close(in);
                close(out);
                return 0;
            }
            off += w;
        }
    }
    close(in);
    close(out);
    return 1;
}

static int f_copy(void *self, const char *sel, void *from, void *to, void **error)
{
    char a[1024];
    char b[1024];

    (void)self;
    (void)sel;
    if (error)
        *error = NULL;
    if (!chm_foundation_map_path(fo_bytes(from), a, sizeof(a)))
        return 0;
    if (!chm_foundation_map_path(fo_bytes(to), b, sizeof(b)))
        return 0;
    if (copy_file_bytes(a, b))
        return 1;
    if (error)
        *error = make_error("NSCocoaErrorDomain", 4, "copy failed");
    return 0;
}

static void *read_whole_file(const char *mapped)
{
    int fd;
    uint8_t *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    void *out;

    fd = open(mapped, O_RDONLY);
    if (fd < 0)
        return NULL;
    for (;;) {
        ssize_t n;

        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 4096;
            uint8_t *nb = realloc(buf, ncap);

            if (!nb)
                break;
            buf = nb;
            cap = ncap;
        }
        n = read(fd, buf + len, cap - len);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    close(fd);
    out = fo_data_new(buf, (int64_t)len);
    free(buf);
    return out;
}

static void *f_contents_at_path(void *self, const char *sel, void *path)
{
    char mapped[1024];

    (void)self;
    (void)sel;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)))
        return NULL;
    return read_whole_file(mapped);
}

typedef struct { void *isa; double epoch; } fo_date;

static void *date_unix(void *cls, const char *sel, double seconds)
{
    (void)cls; (void)sel;
    fo_date *date = fo_alloc("NSDate", sizeof(*date));
    if (date) date->epoch = seconds;
    return date;
}

static double date_interval(void *self, const char *sel)
{
    double epoch = ((fo_date *)self)->epoch;
    return !strcmp(sel, "timeIntervalSince1970") ? epoch : epoch - 978307200.0;
}

static void *file_date(const struct stat *st)
{
    return date_unix(NULL, NULL, (double)st->st_mtim.tv_sec + (double)st->st_mtim.tv_nsec / 1e9);
}

static void *f_attrs_at_path(void *self, const char *sel, void *path, void **error)
{
    char mapped[1024];
    struct stat st;
    fo_dict *d;

    (void)self;
    (void)sel;
    if (error)
        *error = NULL;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped))) {
        if (error)
            *error = make_error("NSCocoaErrorDomain", 4, "bad path");
        return NULL;
    }
    if (stat(mapped, &st) != 0) {
        if (error)
            *error = make_error("NSCocoaErrorDomain", 4, "no such file");
        return NULL;
    }
    d = (fo_dict *)fo_dict_new();
    if (!d)
        return NULL;
    const char *type = S_ISDIR(st.st_mode) ? "NSFileTypeDirectory" :
                       S_ISREG(st.st_mode) ? "NSFileTypeRegular" : "NSFileTypeUnknown";
    if (!d_put(d, fo_string_new("NSFileType"), fo_string_new(type)) ||
        !d_put(d, fo_string_new("NSFileSize"), n_unsigned(NULL, NULL, st.st_size)) ||
        !d_put(d, fo_string_new("NSFilePosixPermissions"), n_unsigned(NULL, NULL, st.st_mode & 07777)) ||
        !d_put(d, fo_string_new("NSFileModificationDate"), file_date(&st))) return NULL;
    return d;
}

static const char *directory_suffix(unsigned long dir, unsigned long domains)
{
    if (!(domains & 1)) return NULL;
    switch (dir) {
    case 5: return "Library";
    case 9: return "Documents";
    case 13: return "Library/Caches";
    case 14: return "Library/Application Support";
    case 15: return "Downloads";
    default: return NULL;
    }
}

static void *home_location(const char *suffix, int expand)
{
    const char *base = "~", *prefix = "";
    if (expand) {
        base = getenv("HOME");
        if (!base || *base != '/') base = "/var/mobile";
        if (path_below(base, g_sandbox_root)) {
            base += strlen(g_sandbox_root);
            prefix = "/var";
        }
    }
    char path[2048];
    int len = snprintf(path, sizeof(path), "%s%s", prefix, base);
    if (len <= 0 || (size_t)len >= sizeof(path)) return NULL;
    while (len > 1 && path[len - 1] == '/') path[--len] = 0;
    if (suffix) {
        size_t available = sizeof(path) - (size_t)len;
        int n = snprintf(path + len, available, "%s%s", path[len - 1] == '/' ? "" : "/", suffix);
        if (n < 0 || (size_t)n >= available) return NULL;
    }
    return fo_string_new(path);
}

void *chm_foundation_home_directory(void)
{
    return home_location(NULL, 1);
}

static void *directory_path(unsigned long dir, unsigned long domains, int expand)
{
    const char *suffix = directory_suffix(dir, domains);
    return suffix ? home_location(suffix, expand) : NULL;
}

void *chm_foundation_search_paths(unsigned long dir, unsigned long domains, signed char expand)
{
    fo_array *paths = fo_array_new();
    void *path = directory_path(dir, domains, expand);
    if (path && (!paths || !a_push(paths, path))) return NULL;
    return paths;
}

static void *url_new(void *cls, const char *sel, void *string);

static void *f_directory_url(void *self, const char *sel, unsigned long dir, unsigned long domains,
                             void *appropriate, signed char create, void **error)
{
    (void)sel; (void)appropriate;
    if (error) *error = NULL;
    void *path = directory_path(dir, domains, 1);
    if (!path) {
        if (error) *error = make_error("NSCocoaErrorDomain", 4, "Unsupported directory or domain");
        return NULL;
    }
    if (create && !f_create_dir(self, NULL, path, 1, NULL, error)) return NULL;
    return url_new(NULL, "fileURLWithPath:", path);
}

static void *f_urls(void *self, const char *sel, unsigned long dir, unsigned long domains)
{
    fo_array *urls = fo_array_new();
    void *url = f_directory_url(self, sel, dir, domains, NULL, 0, NULL);
    if (url && (!urls || !a_push(urls, url))) return NULL;
    return urls;
}

static void *directory_contents(void *self, const char *sel, void *path, void **error);
typedef struct { int fd; void *bytes; size_t length; int writing; } handle_io;
static int handle_io_body(void *context);

static void *h_write_data(void *self, const char *sel, void *data)
{
    fo_handle *h = self;
    const uint8_t *p;
    size_t left;

    (void)sel;

    if (!fo_is_handle(h) || h->closed || !fo_is_data(data))
        return NULL;
    p = ((fo_data *)data)->bytes;
    left = (size_t)((fo_data *)data)->length;
    while (left) {
        handle_io io = {h->fd, (void *)p, left, 1};
        unsigned depth = chm_guest_suspend();
        int n = chm_session_callback(g_session, handle_io_body, &io);
        chm_guest_resume(depth);

        if (n <= 0)
            break;
        p += n;
        left -= (size_t)n;
    }
    return NULL;
}

static int handle_io_body(void *context)
{
    handle_io *io = context;
    if (io->writing) {
        const uint8_t *bytes = io->bytes;
        size_t done = 0;
        while (done < io->length) {
            ssize_t count = write(io->fd, bytes + done, io->length - done);
            if (count <= 0) break;
            done += (size_t)count;
        }
        return done == io->length ? (int)done : -1;
    }
    uint8_t *bytes = io->bytes;
    size_t done = 0;
    while (done < io->length) {
        ssize_t count = read(io->fd, bytes + done, io->length - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    return (int)done;
}

static void h_close(void *self)
{
    fo_handle *h = self;
    if (!fo_is_handle(h) || h->closed) return;
    chm_io_close_number(h->fd);
    h->closed = 1;
}

static void *h_read_all(void *self)
{
    fo_handle *h = self;
    uint8_t *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    void *out;

    if (!fo_is_handle(h) || h->closed)
        return fo_data_new(NULL, 0);
    for (;;) {
        ssize_t n;

        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 4096;
            uint8_t *nb = realloc(buf, ncap);

            if (!nb)
                break;
            buf = nb;
            cap = ncap;
        }
        handle_io io = {h->fd, buf + len, cap - len, 0};
        unsigned depth = chm_guest_suspend();
        n = chm_session_callback(g_session, handle_io_body, &io);
        chm_guest_resume(depth);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    out = fo_data_new(buf, (int64_t)len);
    free(buf);
    return out;
}

static void *h_read_len(void *self, const char *sel, int64_t want)
{
    fo_handle *h = self;
    uint8_t *buf;
    size_t got = 0;

    (void)sel;

    if (!fo_is_handle(h) || h->closed || want <= 0)
        return fo_data_new(NULL, 0);
    buf = malloc((size_t)want);
    if (!buf)
        return NULL;
    while (got < (size_t)want) {
        handle_io io = {h->fd, buf + got, (size_t)want - got, 0};
        unsigned depth = chm_guest_suspend();
        int n = chm_session_callback(g_session, handle_io_body, &io);
        chm_guest_resume(depth);

        if (n <= 0)
            break;
        got += (size_t)n;
    }
    {
        void *out = fo_data_new(buf, (int64_t)got);

        free(buf);
        return out;
    }
}

static int64_t h_offset(void *self)
{
    fo_handle *h = self;

    if (!fo_is_handle(h) || h->closed)
        return -1;
    return (int64_t)lseek(h->fd, 0, SEEK_CUR);
}

static int h_seek(void *self, const char *sel, int64_t off)
{
    fo_handle *h = self;

    (void)sel;

    if (!fo_is_handle(h) || h->closed)
        return -1;
    return lseek(h->fd, (off_t)off, SEEK_SET) < 0 ? -1 : 0;
}

static int h_seek_end(void *self)
{
    fo_handle *h = self;

    if (!fo_is_handle(h) || h->closed)
        return -1;
    return (int64_t)lseek(h->fd, 0, SEEK_END);
}

static void h_sync(void *self)
{
    fo_handle *h = self;

    if (fo_is_handle(h) && !h->closed)
        fsync(h->fd);
}

static int h_fd(void *self)
{
    fo_handle *h = self;

    return fo_is_handle(h) ? h->fd : -1;
}

static void *open_handle(void *path, int flags)
{
    char mapped[1024];
    int fd;

    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)))
        return NULL;
    if ((flags & O_CREAT) != 0)
        ensure_parent(mapped);
    fd = open(mapped, flags, 0600);
    if (fd < 0)
        return NULL;
    return fo_handle_new(fd, mapped);
}

static void *h_for_reading(void *self, const char *sel, void *path)
{
    (void)self;
    (void)sel;
    return open_handle(path, O_RDONLY);
}

static void *h_for_writing(void *self, const char *sel, void *path)
{
    (void)self;
    (void)sel;
    return open_handle(path, O_WRONLY | O_CREAT | O_TRUNC);
}

static void *h_for_updating(void *self, const char *sel, void *path)
{
    (void)self;
    (void)sel;
    return open_handle(path, O_RDWR | O_CREAT);
}

static void *d_data_with_file(void *cls, const char *sel, void *path)
{
    (void)cls;
    (void)sel;
    return f_contents_at_path(NULL, NULL, path);
}

static int d_write_file(void *self, const char *sel, void *path, int atomically)
{
    (void)sel;
    char mapped[2048], temporary[2080];
    if (!fo_is_data(self) || !chm_foundation_map_path_write(fo_bytes(path), mapped, sizeof(mapped))) return 0;
    int fd;
    if (atomically) {
        int n = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", mapped);
        if (n < 0 || (size_t)n >= sizeof(temporary)) return 0;
        fd = mkstemp(temporary);
    } else fd = open(mapped, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 0;
    const uint8_t *bytes = ((fo_data *)self)->bytes;
    size_t left = (size_t)((fo_data *)self)->length;
    int ok = 1;
    while (left) {
        ssize_t n = write(fd, bytes, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = 0; break; }
        bytes += n; left -= (size_t)n;
    }
    if (atomically && fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (atomically) {
        if (ok && rename(temporary, mapped)) ok = 0;
        if (!ok) unlink(temporary);
    }
    return ok;
}

static void *str_with_contents_of_file(void *cls, const char *sel, void *path, uint64_t enc,
                                      void **error)
{
    void *data = f_contents_at_path(NULL, NULL, path);

    (void)cls;
    (void)sel;
    (void)enc;
    if (error)
        *error = NULL;
    if (!fo_is_data(data))
        return NULL;
    {
        fo_data *d = data;
        char *buf = malloc((size_t)d->length + 1);
        void *out;

        if (!buf)
            return NULL;
        if (d->length)
            memcpy(buf, d->bytes, (size_t)d->length);
        buf[d->length] = 0;
        out = fo_string_new(buf);
        free(buf);
        return out;
    }
}

static int str_write_to_file(void *self, const char *sel, void *path, int atomically, uint64_t enc,
                             void **error)
{
    const char *bytes = fo_bytes(self);
    void *data;
    int ok;

    (void)sel;
    (void)atomically;
    (void)enc;
    if (error)
        *error = NULL;
    data = fo_data_new(bytes, (int64_t)strlen(bytes));
    if (!data)
        return 0;
    ok = d_write_file(data, NULL, path, 1);
    if (!ok && error)
        *error = make_error("NSCocoaErrorDomain", 4, "write failed");
    return ok;
}

static void *proc_arguments(void)
{
    fo_array *a = (fo_array *)fo_array_new();

    if (!a)
        return NULL;
    a->items[a->count++] = fo_string_new("/var/containers/Bundle/Application/App.app/App");
    return a;
}

static void *proc_environment(void)
{
    return fo_dict_new();
}

static void *os_version(void)
{
    return fo_string_new("16.0");
}

static void *bundle_metadata(void *object);
static void *bundle_id(void *self, const char *sel)
{
    (void)sel;
    return d_value(bundle_metadata(self), NULL, fo_string_new("CFBundleIdentifier"));
}

#include "foundation_constants.inc"
#include "foundation_services.inc"
#include "foundation_file_values.inc"
#include "foundation_filter.inc"
#include "foundation_progress.inc"
#include "foundation_download.inc"
#include "foundation_icu.inc"
#include "corefoundation.inc"
#include "foundation_values.inc"
#include "foundation_formatters.inc"
#include "foundation_byte_count.inc"
#include "foundation_url_values.inc"
#include "foundation_credentials.inc"
#include "foundation_url_cache.inc"
#include "foundation_url_configuration.inc"

static const struct {
    const char *sel;
    void *imp;
} g_methods[] = {
#include "foundation_services_methods.inc"
    { "self", (void *)o_self },
    { "init", (void *)o_self },
    { "retain", (void *)o_retain },
    { "copy", (void *)o_copy },
    { "mutableCopy", (void *)o_copy },
    { "autorelease", (void *)o_retain },
    { "release", (void *)o_release },
    { "dealloc", (void *)o_release },
    { "retainCount", (void *)o_hash },
    { "class", (void *)o_class },
    { "superclass", (void *)o_class },
    { "description", (void *)o_description },
    { "debugDescription", (void *)o_description },
    { "hash", (void *)o_hash },
    { "isEqual:", (void *)o_is_equal },
    { "isEqualToString:", (void *)o_is_equal },
    { "compare:", (void *)string_compare },
    { "isKindOfClass:", (void *)o_is_kind },
    { "isMemberOfClass:", (void *)o_is_kind },
    { "respondsToSelector:", (void *)o_responds },
    { "conformsToProtocol:", (void *)o_yes },
    { "isProxy", (void *)o_no },
    { "length", (void *)o_length },
    { "lengthOfBytesUsingEncoding:", (void *)o_length },
    { "count", (void *)o_count },
    { "UTF8String", (void *)o_utf8 },
    { "fileSystemRepresentation", (void *)o_utf8 },
    { "bytes", (void *)o_utf8 },
    { "absoluteString", (void *)url_string },
    { "path", (void *)url_string },
    { "lastPathComponent", (void *)s_last_component },
    { "pathExtension", (void *)s_extension },
    { "stringByDeletingLastPathComponent", (void *)s_delete_last },
    { "stringByDeletingPathExtension", (void *)s_delete_extension },
    { "stringByAppendingPathComponent:", (void *)s_append_component },
    { "stringByAppendingString:", (void *)s_append_string },
    { "componentsSeparatedByString:", (void *)s_split },
    { "substringToIndex:", (void *)s_substring_to },
    { "substringFromIndex:", (void *)s_substring_from },
    { "lowercaseString", (void *)s_lower },
    { "uppercaseString", (void *)s_upper },
    { "hasPrefix:", (void *)s_has_prefix },
    { "hasSuffix:", (void *)s_has_suffix },
    { "isAbsolutePath", (void *)s_is_absolute },
    { "stringWithUTF8String:", (void *)s_with_utf8 },
    { "stringWithCString:encoding:", (void *)s_with_utf8 },
    { "stringWithFormat:", (void *)chm_ns_format_entry },
    { "localizedStringWithFormat:", (void *)chm_ns_format_entry },
    { "initWithFormat:", (void *)chm_ns_format_entry },
    { "stringByAppendingFormat:", (void *)chm_ns_append_format_entry },
    { "numberWithBool:", (void *)n_bool },
    { "numberWithInt:", (void *)n_int },
    { "numberWithUnsignedInt:", (void *)n_uint },
    { "numberWithInteger:", (void *)n_signed },
    { "numberWithLong:", (void *)n_signed },
    { "numberWithLongLong:", (void *)n_signed },
    { "numberWithUnsignedInteger:", (void *)n_unsigned },
    { "numberWithUnsignedLong:", (void *)n_unsigned },
    { "numberWithUnsignedLongLong:", (void *)n_unsigned },
    { "numberWithFloat:", (void *)n_float },
    { "numberWithDouble:", (void *)n_double },
    { "integerValue", (void *)o_integer },
    { "intValue", (void *)o_integer },
    { "longValue", (void *)o_integer },
    { "longLongValue", (void *)o_integer },
    { "unsignedIntValue", (void *)o_unsigned },
    { "unsignedLongValue", (void *)o_unsigned },
    { "unsignedLongLongValue", (void *)o_unsigned },
    { "unsignedIntegerValue", (void *)o_unsigned },
    { "floatValue", (void *)o_float },
    { "doubleValue", (void *)o_double },
    { "boolValue", (void *)o_bool },
    { "array", (void *)collection_empty },
    { "dictionary", (void *)collection_empty },
    { "arrayWithCapacity:", (void *)collection_with_capacity },
    { "dictionaryWithCapacity:", (void *)collection_with_capacity },
    { "initWithCapacity:", (void *)collection_capacity },
    { "arrayWithObjects:count:", (void *)a_with_objects },
    { "initWithObjects:count:", (void *)a_init_objects },
    { "arrayWithObject:", (void *)a_with_object },
    { "arrayWithArray:", (void *)a_with_array },
    { "dictionaryWithObjects:forKeys:count:", (void *)d_with_objects },
    { "initWithObjects:forKeys:count:", (void *)d_init_objects },
    { "dictionaryWithObject:forKey:", (void *)d_with_object },
    { "dictionaryWithDictionary:", (void *)d_with_dictionary },
    { "addEntriesFromDictionary:", (void *)d_add_entries },
    { "setObject:forKeyedSubscript:", (void *)d_set_subscript },
    { "removeObjectForKey:", (void *)defaults_remove },
    { "countByEnumeratingWithState:objects:count:", (void *)collection_enumerate },
    { "objectAtIndex:", (void *)a_object_at },
    { "objectAtIndexedSubscript:", (void *)a_object_at },
    { "firstObject", (void *)a_first },
    { "lastObject", (void *)a_last },
    { "addObject:", (void *)a_add },
    { "removeAllObjects", (void *)a_remove_all },
    { "componentsJoinedByString:", (void *)a_joined },
    { "valueForKey:", (void *)d_value },
    { "valueForKeyPath:", (void *)d_value_path },
    { "setValue:forKey:", (void *)d_set_subscript },
    { "setValue:forKeyPath:", (void *)d_set_value_path },
    { "objectForKey:", (void *)defaults_get },
    { "objectForKeyedSubscript:", (void *)d_value },
    { "setObject:forKey:", (void *)defaults_set },
    { "allKeys", (void *)d_keys },
    { "allValues", (void *)d_values },
    { "fileExistsAtPath:", (void *)f_exists },
    { "fileExistsAtPath:isDirectory:", (void *)file_exists_directory },
    { "createDirectoryAtPath:withIntermediateDirectories:attributes:error:", (void *)f_create_dir },
    { "removeItemAtPath:error:", (void *)f_remove },
    { "createFileAtPath:contents:attributes:", (void *)f_create_file },
    { "moveItemAtPath:toPath:error:", (void *)f_move },
    { "copyItemAtPath:toPath:error:", (void *)f_copy },
    { "contentsAtPath:", (void *)f_contents_at_path },
    { "attributesOfItemAtPath:error:", (void *)f_attrs_at_path },
    { "fileHandleForReadingAtPath:", (void *)h_for_reading },
    { "fileHandleForWritingAtPath:", (void *)h_for_writing },
    { "fileHandleForUpdatingAtPath:", (void *)h_for_updating },
    { "writeData:", (void *)h_write_data },
    { "readDataToEndOfFile", (void *)h_read_all },
    { "readDataToEndOfFileAndReturnError:", (void *)h_read_all },
    { "readDataOfLength:", (void *)h_read_len },
    { "closeFile", (void *)h_close },
    { "closeAndReturnError:", (void *)h_close },
    { "offsetInFile", (void *)h_offset },
    { "seekToFileOffset:", (void *)h_seek },
    { "seekToEndOfFile", (void *)h_seek_end },
    { "synchronizeFile", (void *)h_sync },
    { "fileDescriptor", (void *)h_fd },
    { "dataWithContentsOfFile:", (void *)d_data_with_file },
    { "writeToFile:atomically:", (void *)collection_write_file },
    { "stringWithContentsOfFile:encoding:error:", (void *)str_with_contents_of_file },
    { "writeToFile:atomically:encoding:error:", (void *)str_write_to_file },
    { "initWithFormat:arguments:", (void *)chm_ns_format_list },
    { "URLsForDirectory:inDomains:", (void *)f_urls },
    { "URLForDirectory:inDomain:appropriateForURL:create:error:", (void *)f_directory_url },
    { "contentsOfDirectoryAtPath:error:", (void *)directory_contents },
    { "processInfo", NULL },
    { "defaultManager", NULL },
    { "mainBundle", NULL },
    { "arguments", (void *)proc_arguments },
    { "environment", (void *)proc_environment },
    { "operatingSystemVersionString", (void *)os_version },
    { "bundleIdentifier", (void *)bundle_id },
    { "isMacCatalystApp", (void *)o_no },
    { "isiOSAppOnMac", (void *)o_no },
    { "isiOSAppOnVision", (void *)o_no },
    { "isNSString__", (void *)o_is_string_class },
    { "isNSData__", (void *)o_is_data_class },
    { "dataUsingEncoding:", (void *)s_data_using_encoding },
    { "dataUsingEncoding:allowLossyConversion:", (void *)s_data_using_encoding },
    { "dataWithBytes:length:", (void *)s_data_with_bytes },
    { NULL, NULL }
};

static void *process_info(void)
{
    return singleton("__process_info", "NSProcessInfo");
}

static void *file_manager(void)
{
    return singleton("__file_manager", "NSFileManager");
}

static void *main_bundle(void)
{
    return singleton("__main_bundle", "NSBundle");
}

void chm_foundation_init(chm_session *s)
{
    int i;
    int k;

    g_session = s;
    g_class_count = 0;
    g_pair_count = 0;
    chm_format_set_describer(describe_utf8);
    for (i = 0; g_seed[i].name; i++) {
        fo_class *c;

        if (g_class_count >= FO_MAX_CLASSES)
            break;
        c = fo_make(g_seed[i].name, g_seed[i].size);
        if (c)
            g_classes[g_class_count++] = c;
    }
    for (i = 0; g_seed[i].name; i++) {
        fo_class *c = fo_find(g_seed[i].name);
        if (!c || !g_seed[i].parent) continue;
        c->parent = fo_find(g_seed[i].parent);
        if (!c->parent) {
            chm_session_log(s, "foundation: missing parent %s for %s", g_seed[i].parent, c->name);
            continue;
        }
        c->cls.superclass = &c->parent->cls;
        c->meta.superclass = &c->parent->meta;
    }
    chm_objc_set_cfstr_isa(chm_foundation_lookup_class("NSConstantString"));
    chm_objc_register_host(s, cf_string_methods, sizeof(cf_string_methods) / sizeof(*cf_string_methods));
    chm_objc_register_host(s, date_formatter_methods, sizeof(date_formatter_methods) / sizeof(*date_formatter_methods));
    chm_objc_register_host(s, byte_methods, sizeof(byte_methods) / sizeof(*byte_methods));
    chm_objc_register_host(s, stream_methods, sizeof(stream_methods) / sizeof(*stream_methods));
    chm_objc_register_host(s, xml_methods, sizeof(xml_methods) / sizeof(*xml_methods));
    chm_objc_register_host(s, plist_methods, sizeof(plist_methods) / sizeof(*plist_methods));
    chm_objc_register_host(s, value_methods, sizeof(value_methods) / sizeof(*value_methods));
    chm_objc_register_host(s, file_value_methods, sizeof(file_value_methods) / sizeof(*file_value_methods));
    chm_objc_register_host(s, filter_methods, sizeof(filter_methods) / sizeof(*filter_methods));
    chm_objc_register_host(s, progress_methods, sizeof(progress_methods) / sizeof(*progress_methods));
    chm_objc_register_host(s, download_methods, sizeof(download_methods) / sizeof(*download_methods));
    chm_objc_register_host(s, credential_methods, sizeof(credential_methods) / sizeof(*credential_methods));
    chm_objc_register_host(s, url_value_methods, sizeof(url_value_methods) / sizeof(*url_value_methods));
    chm_objc_register_host(s, cache_methods, sizeof(cache_methods) / sizeof(*cache_methods));
    chm_objc_register_host(s, url_configuration_methods, sizeof(url_configuration_methods) / sizeof(*url_configuration_methods));
    for (k = 0; g_methods[k].sel; k++) {
        chm_method_reg m;

        m.cls = NULL;
        snprintf(m.sel, sizeof(m.sel), "%s", g_methods[k].sel);
        if (!g_methods[k].imp)
            continue;
        m.imp = g_methods[k].imp;
        if (chm_objc_session_add_method(s, &m) != 0)
            break;
    }
    if (s)
        chm_session_log(s, "foundation: %d classes ready", g_class_count);
}

void chm_foundation_shutdown(void)
{
    int i;
    cf_owned_reset();
    chm_graphics_reset(g_session);
    values_reset();
    byte_reset();
    formatters_reset();
    services_shutdown();
    progress_reset();
    constants_reset();
    chm_objc_set_cfstr_isa(NULL);
    for (i = 0; i < g_class_count; i++) {
        free(g_classes[i]->name);
        free(g_classes[i]);
    }
    g_class_count = 0;
    for (i = 0; i < g_pair_count; i++)
        free(g_pairs[i].key);
    g_pair_count = 0;
}

int chm_foundation_class_count(void)
{
    return g_class_count;
}

int chm_foundation_isa_is(void *obj, const char *cls_name)
{
    return fo_isa_is(obj, cls_name);
}

void *chm_foundation_alloc_obj(const char *cls_name, size_t min)
{
    return fo_alloc(cls_name, min);
}

void *chm_foundation_string(const char *bytes)
{
    return fo_string_new(bytes);
}

void *chm_foundation_lookup_class(const char *name)
{
    fo_class *c = fo_find(name);

    return c ? &c->cls : NULL;
}

void *chm_foundation_lookup_metaclass(const char *name)
{
    fo_class *c = fo_find(name);

    return c ? &c->meta : NULL;
}

void *chm_foundation_lookup_method(const char *cls_name, const char *sel)
{
    int i;

    (void)cls_name;
    if (!sel)
        return NULL;
    if (strcmp(sel, "processInfo") == 0)
        return (void *)process_info;
    if (strcmp(sel, "defaultManager") == 0)
        return (void *)file_manager;
    if (strcmp(sel, "mainBundle") == 0)
        return (void *)main_bundle;
    for (i = 0; g_methods[i].sel; i++) {
        if (g_methods[i].imp && strcmp(g_methods[i].sel, sel) == 0)
            return g_methods[i].imp;
    }
    return NULL;
}

void chm_foundation_note_missing_selector(const char *cls_name, const char *sel)
{
    if (g_session && cls_name && sel)
        chm_session_log(g_session, "unimplemented -[%s %s]", cls_name, sel);
}
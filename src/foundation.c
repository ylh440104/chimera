#include "chm_session.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FO_MAX_CLASSES 320
#define FO_MAX_ITEMS 256

#define FO_STRING_MAGIC 0x464f535452ull
#define FO_DATA_MAGIC 0x464f444154ull
#define FO_ARRAY_MAGIC 0x464f415252ull
#define FO_DICT_MAGIC 0x464f444943ull

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
    void *items[FO_MAX_ITEMS];
} fo_array;

typedef struct {
    void *isa;
    uint64_t magic;
    int64_t count;
    void *keys[FO_MAX_ITEMS];
    void *values[FO_MAX_ITEMS];
} fo_dict;

typedef struct {
    char *key;
    void *value;
} fo_pair;

static fo_class *g_classes[FO_MAX_CLASSES];
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
    { "NSURL", "NSObject", 32 },
    { "NSURLRequest", "NSObject", 32 },
    { "NSMutableURLRequest", "NSURLRequest", 32 },
    { "NSURLSession", "NSObject", 32 },
    { "NSURLSessionConfiguration", "NSObject", 32 },
    { "NSURLSessionTask", "NSObject", 32 },
    { "NSError", "NSObject", 32 },
    { "NSException", "NSObject", 32 },
    { "NSBundle", "NSObject", 32 },
    { "NSFileManager", "NSObject", 32 },
    { "NSFileHandle", "NSObject", 32 },
    { "NSOutputStream", "NSObject", 32 },
    { "NSUserDefaults", "NSObject", 32 },
    { "NSNotificationCenter", "NSObject", 32 },
    { "NSNotification", "NSObject", 32 },
    { "NSProcessInfo", "NSObject", 32 },
    { "NSThread", "NSObject", 32 },
    { "NSRunLoop", "NSObject", 32 },
    { "NSJSONSerialization", "NSObject", 16 },
    { "NSNull", "NSObject", 16 },
    { "NSIndexPath", "NSObject", 32 },
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
    { "UIResponder", "NSObject", 32 },
    { "UIView", "UIResponder", 96 },
    { "UIWindow", "UIView", 96 },
    { "UIViewController", "UIResponder", 64 },
    { "UIApplication", "UIResponder", 64 },
    { "UIWindowScene", "NSObject", 64 },
    { "UISceneConfiguration", "NSObject", 64 },
    { "UIControl", "UIView", 96 },
    { "UIButton", "UIControl", 96 },
    { "UITextField", "UIControl", 96 },
    { "UISwitch", "UIControl", 96 },
    { "UISlider", "UIControl", 96 },
    { "UILabel", "UIView", 96 },
    { "UIImageView", "UIView", 96 },
    { "UIScrollView", "UIView", 96 },
    { "UITableView", "UIScrollView", 128 },
    { "UITableViewCell", "UIView", 128 },
    { "UINavigationBar", "UIView", 96 },
    { "UIToolbar", "UIView", 96 },
    { "UIProgressView", "UIView", 96 },
    { "UIActivityIndicatorView", "UIView", 96 },
    { "UITabBar", "UIView", 96 },
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

static fo_class *fo_make(const char *name, const char *parent, uint32_t size)
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
    if (parent) {
        c->parent = fo_find(parent);
        if (c->parent) {
            c->cls.superclass = &c->parent->cls;
            c->meta.superclass = &c->parent->meta;
        }
    }
    return c;
}

static const char *fo_class_name(void *cls)
{
    fo_class *c = fo_owning(cls);

    if (c && c->name)
        return c->name;
    return cls ? "foreign-class" : "nil-class";
}

static void *fo_alloc(const char *cls_name, size_t min)
{
    fo_class *c = fo_find(cls_name);
    size_t size = c ? c->ro.instance_size : 0;
    void *obj;

    if (size < min)
        size = min;
    obj = calloc(1, size);
    if (!obj)
        return NULL;
    if (c)
        *(void **)obj = &c->cls;
    return obj;
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

static int fo_is_string(void *p)
{
    return p && ((fo_string *)p)->magic == FO_STRING_MAGIC;
}

static int fo_is_data(void *p)
{
    return p && ((fo_data *)p)->magic == FO_DATA_MAGIC;
}

static int fo_is_array(void *p)
{
    return p && ((fo_array *)p)->magic == FO_ARRAY_MAGIC;
}

static int fo_is_dict(void *p)
{
    return p && ((fo_dict *)p)->magic == FO_DICT_MAGIC;
}

static const char *fo_bytes(void *p)
{
    return fo_is_string(p) ? ((fo_string *)p)->bytes : "";
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

static void *o_retain(void *self)
{
    return self;
}

static void o_release(void *self)
{
    (void)self;
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

static int o_is_kind(void *self, void *cls)
{
    fo_class *c;

    if (!self || !cls)
        return 0;
    c = fo_owning(*(void **)self);
    while (c) {
        if ((void *)&c->cls == cls)
            return 1;
        c = c->parent;
    }
    return 0;
}

static int o_is_string_class(void *self)
{
    return fo_isa_is(self, "NSString");
}

static int o_is_data_class(void *self)
{
    return fo_isa_is(self, "NSData");
}

static void *s_data_utf8(void *cls, const char *bytes)
{
    (void)cls;
    return bytes ? fo_data_new(bytes, (int64_t)strlen(bytes)) : fo_data_new(NULL, 0);
}

static int o_is_equal(void *self, void *other)
{
    if (self == other)
        return 1;
    if (fo_is_string(self) && fo_is_string(other))
        return strcmp(fo_bytes(self), fo_bytes(other)) == 0;
    return 0;
}

static int o_compare(void *self, void *other)
{
    if (!fo_is_string(self) || !fo_is_string(other))
        return 0;
    return strcmp(fo_bytes(self), fo_bytes(other));
}

static int o_responds(void *self, const char *sel)
{
    (void)self;
    return chm_foundation_lookup_method(NULL, sel) != NULL;
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
    return fo_is_string(self) ? ((fo_string *)self)->bytes : "";
}

static const char *o_description(void *self)
{
    if (fo_is_string(self))
        return ((fo_string *)self)->bytes;
    if (!self)
        return "(null)";
    return fo_class_name(*(void **)self);
}

static int64_t o_integer(void *self)
{
    return fo_is_string(self) ? strtoll(((fo_string *)self)->bytes, NULL, 10) : 0;
}

static double o_double(void *self)
{
    return fo_is_string(self) ? strtod(((fo_string *)self)->bytes, NULL) : 0.0;
}

static int o_bool(void *self)
{
    const char *b;

    if (!fo_is_string(self))
        return 0;
    b = ((fo_string *)self)->bytes;
    return b && (b[0] == '1' || b[0] == 't' || b[0] == 'T' || b[0] == 'y' || b[0] == 'Y');
}

static void *s_identity(void *self)
{
    return fo_string_new(fo_bytes(self));
}

static void *s_with_utf8(void *cls, const char *bytes)
{
    (void)cls;
    return fo_string_new(bytes);
}

static void *s_with_format(void *cls, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    (void)cls;
    if (!fmt)
        return fo_string_new("");
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return fo_string_new(buf);
}

static void *s_append_component(void *self, void *comp)
{
    const char *base = fo_bytes(self);
    const char *suffix = fo_bytes(comp);
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

static void *s_append_string(void *self, void *tail)
{
    const char *a = fo_bytes(self);
    const char *b = fo_bytes(tail);
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

static void *s_split(void *self, void *sep)
{
    const char *base = fo_bytes(self);
    const char *s = fo_bytes(sep);
    fo_array *arr;
    char *copy;
    char *cursor;
    size_t seplen;

    if (!s[0])
        return NULL;
    arr = (fo_array *)fo_array_new();
    if (!arr)
        return NULL;
    seplen = strlen(s);
    copy = strdup(base);
    if (!copy) {
        free(arr);
        return NULL;
    }
    cursor = copy;
    for (;;) {
        char *hit = strstr(cursor, s);

        if (!hit) {
            if (arr->count < FO_MAX_ITEMS)
                arr->items[arr->count++] = fo_string_new(cursor);
            break;
        }
        *hit = 0;
        if (arr->count < FO_MAX_ITEMS)
            arr->items[arr->count++] = fo_string_new(cursor);
        cursor = hit + seplen;
    }
    free(copy);
    return arr;
}

static void *s_substring_to(void *self, int64_t idx)
{
    const char *base = fo_bytes(self);
    int64_t len = (int64_t)strlen(base);
    char *buf;
    void *out;

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

static void *s_substring_from(void *self, int64_t idx)
{
    const char *base = fo_bytes(self);
    int64_t len = (int64_t)strlen(base);

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

static int s_has_prefix(void *self, void *prefix)
{
    return strncmp(fo_bytes(self), fo_bytes(prefix), strlen(fo_bytes(prefix))) == 0;
}

static int s_has_suffix(void *self, void *suffix)
{
    const char *a = fo_bytes(self);
    const char *b = fo_bytes(suffix);
    size_t la = strlen(a);
    size_t lb = strlen(b);

    return lb <= la && strcmp(a + la - lb, b) == 0;
}

static void *a_object_at(void *self, int64_t idx)
{
    fo_array *a = self;

    if (!fo_is_array(a) || idx < 0 || idx >= a->count)
        return fo_string_new("");
    return a->items[idx];
}

static void *a_first(void *self)
{
    return a_object_at(self, 0);
}

static void *a_last(void *self)
{
    fo_array *a = self;

    if (!fo_is_array(a) || a->count == 0)
        return fo_string_new("");
    return a->items[a->count - 1];
}

static void a_add(void *self, void *obj)
{
    fo_array *a = self;

    if (!fo_is_array(a))
        return;
    if (a->count < FO_MAX_ITEMS)
        a->items[a->count++] = obj;
}

static void *a_joined(void *self, void *sep)
{
    fo_array *a = self;
    const char *s = fo_bytes(sep);
    size_t total = 1;
    char *buf;
    void *out;

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

static void *d_value(void *self, void *key)
{
    fo_dict *d = self;
    const char *k = fo_bytes(key);

    if (!fo_is_dict(d))
        return fo_string_new("");
    for (int64_t i = 0; i < d->count; i++) {
        if (strcmp(fo_bytes(d->keys[i]), k) == 0)
            return d->values[i];
    }
    return fo_string_new("");
}

static void d_set(void *self, void *obj, void *key)
{
    fo_dict *d = self;

    if (!fo_is_dict(d))
        return;
    if (d->count < FO_MAX_ITEMS) {
        d->keys[d->count] = key;
        d->values[d->count] = obj;
        d->count++;
    }
}

static void *d_keys(void *self)
{
    fo_dict *d = self;
    fo_array *a;

    if (!fo_is_dict(d))
        return NULL;
    a = (fo_array *)fo_array_new();
    if (!a)
        return NULL;
    for (int64_t i = 0; i < d->count && a->count < FO_MAX_ITEMS; i++)
        a->items[a->count++] = d->keys[i];
    return a;
}

static void *d_values(void *self)
{
    fo_dict *d = self;
    fo_array *a;

    if (!fo_is_dict(d))
        return NULL;
    a = (fo_array *)fo_array_new();
    if (!a)
        return NULL;
    for (int64_t i = 0; i < d->count && a->count < FO_MAX_ITEMS; i++)
        a->items[a->count++] = d->values[i];
    return a;
}

static int f_exists(void *self, void *path)
{
    (void)self;
    (void)path;
    return 0;
}

static int f_create_dir(void *self, void *path, int intermediate, void *attrs, void **error)
{
    (void)self;
    (void)path;
    (void)intermediate;
    (void)attrs;
    if (error)
        *error = NULL;
    return 0;
}

static int f_remove(void *self, void *path, void **error)
{
    (void)self;
    (void)path;
    if (error)
        *error = NULL;
    return 0;
}

static void *f_urls(void)
{
    fo_array *a = (fo_array *)fo_array_new();

    if (!a)
        return NULL;
    a->items[a->count++] = fo_string_new("/var/mobile/Containers/Data/Application/App/Documents");
    return a;
}

static void *f_contents(void)
{
    return fo_array_new();
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

static void *bundle_id(void)
{
    return fo_string_new("com.app.chimera");
}

static const struct {
    const char *sel;
    void *imp;
} g_methods[] = {
    { "self", (void *)o_self },
    { "init", (void *)o_self },
    { "retain", (void *)o_retain },
    { "copy", (void *)o_retain },
    { "mutableCopy", (void *)o_retain },
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
    { "compare:", (void *)o_compare },
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
    { "absoluteString", (void *)s_identity },
    { "path", (void *)s_identity },
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
    { "stringWithFormat:", (void *)s_with_format },
    { "localizedStringWithFormat:", (void *)s_with_format },
    { "integerValue", (void *)o_integer },
    { "intValue", (void *)o_integer },
    { "longLongValue", (void *)o_integer },
    { "unsignedIntegerValue", (void *)o_integer },
    { "floatValue", (void *)o_double },
    { "doubleValue", (void *)o_double },
    { "boolValue", (void *)o_bool },
    { "objectAtIndex:", (void *)a_object_at },
    { "objectAtIndexedSubscript:", (void *)a_object_at },
    { "firstObject", (void *)a_first },
    { "lastObject", (void *)a_last },
    { "addObject:", (void *)a_add },
    { "componentsJoinedByString:", (void *)a_joined },
    { "objectForKey:", (void *)d_value },
    { "objectForKeyedSubscript:", (void *)d_value },
    { "setObject:forKey:", (void *)d_set },
    { "allKeys", (void *)d_keys },
    { "allValues", (void *)d_values },
    { "fileExistsAtPath:", (void *)f_exists },
    { "fileExistsAtPath:isDirectory:", (void *)f_exists },
    { "createDirectoryAtPath:withIntermediateDirectories:attributes:error:", (void *)f_create_dir },
    { "removeItemAtPath:error:", (void *)f_remove },
    { "URLsForDirectory:inDomains:", (void *)f_urls },
    { "URLForDirectory:inDomain:appropriateForURL:create:error:", (void *)f_urls },
    { "contentsOfDirectoryAtPath:error:", (void *)f_contents },
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
    { "dataUsingEncoding:", (void *)s_data_utf8 },
    { "dataWithBytes:length:", (void *)s_data_utf8 },
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
    for (i = 0; g_seed[i].name; i++) {
        fo_class *c;

        if (g_class_count >= FO_MAX_CLASSES)
            break;
        c = fo_make(g_seed[i].name, g_seed[i].parent, g_seed[i].size);
        if (c)
            g_classes[g_class_count++] = c;
    }
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
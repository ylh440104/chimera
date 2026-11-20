#include "chm_session.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FO_MAX_CLASSES 320
#define FO_STRING_MAGIC 0x464f535452ull
#define FO_ARRAY_MAGIC 0x464f415252ull
#define FO_DICT_MAGIC 0x464f444943ull
#define FO_MAX_ITEMS 256

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

static const char *fo_class_name(void *cls)
{
    fo_class *c = fo_owning(cls);

    if (c && c->name)
        return c->name;
    return cls ? "foreign-class" : "nil-class";
}

static int fo_is_string(void *p)
{
    return p && ((fo_string *)p)->magic == FO_STRING_MAGIC;
}

static fo_class *fo_class_of(void *obj)
{
    void *cls;

    if (!obj)
        return NULL;
    cls = *(void **)obj;
    return fo_owning(cls);
}

static fo_string *fo_string_new(const char *bytes)
{
    fo_string *s = calloc(1, sizeof(*s));
    fo_class *cls = fo_find("NSString");

    if (!s)
        return NULL;
    s->magic = FO_STRING_MAGIC;
    s->isa = cls ? &cls->cls : NULL;
    s->bytes = strdup(bytes ? bytes : "");
    s->length = s->bytes ? (int64_t)strlen(s->bytes) : 0;
    if (!s->bytes) {
        free(s);
        return NULL;
    }
    return s;
}

static const char *fo_string_bytes(void *p)
{
    return fo_is_string(p) ? ((fo_string *)p)->bytes : "";
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

static void *obj_retain(void *self)
{
    return self;
}

static void obj_release(void *self)
{
    (void)self;
}

static void *obj_self_impl(void *self)
{
    return self;
}

static void *obj_class_impl(void *self)
{
    return self ? *(void **)self : NULL;
}

static long obj_hash_impl(void *self)
{
    if (fo_is_string(self)) {
        const unsigned char *p = (const unsigned char *)((fo_string *)self)->bytes;
        unsigned long h = 5381;

        while (p && *p)
            h = (h * 33) ^ *p++;
        return (long)h;
    }
    return (long)(uintptr_t)self;
}

static int obj_bool_yes(void *self)
{
    (void)self;
    return 1;
}

static int obj_bool_no(void *self)
{
    (void)self;
    return 0;
}

static int obj_is_kind_of(void *self, void *cls)
{
    fo_class *c = fo_class_of(self);

    while (c) {
        if ((void *)&c->cls == cls)
            return 1;
        c = c->parent;
    }
    return 0;
}

static int obj_is_member_of(void *self, void *cls)
{
    return (void *)fo_class_of(self) == cls;
}

static int obj_responds(void *self, const char *sel)
{
    (void)self;

    return chm_foundation_lookup_method(NULL, sel) != NULL;
}

static int obj_is_equal(void *self, void *other)
{
    if (self == other)
        return 1;
    if (fo_is_string(self) && fo_is_string(other))
        return strcmp(fo_string_bytes(self), fo_string_bytes(other)) == 0;
    return 0;
}

static int obj_compare(void *self, void *other)
{
    if (!fo_is_string(self) || !fo_is_string(other))
        return 0;
    return strcmp(fo_string_bytes(self), fo_string_bytes(other));
}

static void *str_with_utf8(void *cls, const char *bytes)
{
    (void)cls;
    return fo_string_new(bytes ? bytes : "");
}

static void *str_with_format(void *cls, const char *fmt, ...)
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

static int file_exists(void *self, void *path)
{
    (void)self;
    (void)path;
    return 0;
}

static int create_directory(void *self, void *path, int intermediate, void *attrs, void **error)
{
    (void)self;
    (void)path;
    (void)intermediate;
    (void)attrs;
    if (error)
        *error = NULL;
    return 0;
}

static int remove_item(void *self, void *path, void **error)
{
    (void)self;
    (void)path;
    if (error)
        *error = NULL;
    return 0;
}

static void *str_identity(void *self)
{
    return fo_string_new(fo_string_bytes(self));
}

static int64_t obj_length_impl(void *self)
{
    return fo_is_string(self) ? ((fo_string *)self)->length : 0;
}

static int64_t obj_count_impl(void *self)
{
    int64_t magic;

    if (!self)
        return 0;
    magic = (int64_t)((fo_string *)self)->magic;
    if (magic == (int64_t)FO_ARRAY_MAGIC || magic == (int64_t)FO_DICT_MAGIC)
        return *(int64_t *)((uint8_t *)self + offsetof(fo_array, count));
    return 0;
}

static const char *obj_utf8(void *self)
{
    return fo_is_string(self) ? ((fo_string *)self)->bytes : "";
}

static const char *obj_description(void *self)
{
    if (fo_is_string(self))
        return ((fo_string *)self)->bytes;
    if (!self)
        return "(null)";
    return fo_class_name((void *)fo_class_of(self));
}

static int64_t obj_integer_value(void *self)
{
    return fo_is_string(self) ? strtoll(((fo_string *)self)->bytes, NULL, 10) : 0;
}

static double obj_double_value(void *self)
{
    return fo_is_string(self) ? strtod(((fo_string *)self)->bytes, NULL) : 0.0;
}

static int obj_bool_value(void *self)
{
    if (!fo_is_string(self))
        return 0;
    const char *b = ((fo_string *)self)->bytes;

    return b && (b[0] == '1' || b[0] == 't' || b[0] == 'T' || b[0] == 'y' || b[0] == 'Y');
}

static void *str_append_component(void *self, void *comp)
{
    const char *base = fo_string_bytes(self);
    const char *suffix = fo_string_bytes(comp);
    size_t blen = strlen(base);
    char *buf;
    void *out;

    while (blen > 0 && base[blen - 1] == '/')
        blen--;
    buf = malloc(blen + strlen(suffix) + 2);
    if (!buf)
        return NULL;
    memcpy(buf, base, blen);
    buf[blen] = '/';
    strcpy(buf + blen + 1, suffix);
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *str_append_string(void *self, void *tail)
{
    const char *a = fo_string_bytes(self);
    const char *b = fo_string_bytes(tail);
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

static void *str_delete_last_component(void *self)
{
    const char *base = fo_string_bytes(self);
    size_t len = strlen(base);
    void *out;
    char *buf;

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

static void *str_delete_path_extension(void *self)
{
    const char *base = fo_string_bytes(self);
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

static void *str_last_path_component(void *self)
{
    const char *base = fo_string_bytes(self);
    const char *slash = strrchr(base, '/');

    return fo_string_new(slash ? slash + 1 : base);
}

static void *str_path_extension(void *self)
{
    const char *base = fo_string_bytes(self);
    const char *slash = strrchr(base, '/');
    const char *dot = strrchr(base, '.');

    if (!dot || (slash && dot < slash))
        return fo_string_new("");
    return fo_string_new(dot + 1);
}

static int64_t str_path_components(void)
{
    return 0;
}

static int str_is_absolute(void *self)
{
    const char *base = fo_string_bytes(self);

    return base && base[0] == '/';
}

static void *arr_nil_safe_last(void *self)
{
    fo_array *a = self;

    if (!a || a->magic != FO_ARRAY_MAGIC || a->count == 0)
        return fo_string_new("");
    return a->items[a->count - 1];
}

static void *str_components_separated(void *self, void *sep)
{
    const char *base = fo_string_bytes(self);
    const char *s = fo_string_bytes(sep);
    fo_array *arr;
    fo_class *cls = fo_find("NSMutableArray");
    char *copy;
    char *cursor;
    size_t seplen;

    if (!s[0])
        return NULL;
    arr = calloc(1, sizeof(*arr));
    if (!arr)
        return NULL;
    arr->magic = FO_ARRAY_MAGIC;
    arr->isa = cls ? &cls->cls : NULL;
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

static void *str_substring_to(void *self, int64_t idx)
{
    const char *base = fo_string_bytes(self);
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

static void *str_substring_from(void *self, int64_t idx)
{
    const char *base = fo_string_bytes(self);
    int64_t len = (int64_t)strlen(base);

    if (idx < 0 || idx > len)
        return fo_string_new("");
    return fo_string_new(base + idx);
}

static void *str_lowercase(void *self)
{
    const char *base = fo_string_bytes(self);
    char *buf = strdup(base);
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

static void *str_uppercase(void *self)
{
    const char *base = fo_string_bytes(self);
    char *buf = strdup(base);
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

static int str_has_prefix(void *self, void *prefix)
{
    const char *a = fo_string_bytes(self);
    const char *b = fo_string_bytes(prefix);
    size_t n = strlen(b);

    return strncmp(a, b, n) == 0;
}

static int str_has_suffix(void *self, void *suffix)
{
    const char *a = fo_string_bytes(self);
    const char *b = fo_string_bytes(suffix);
    size_t la = strlen(a);
    size_t lb = strlen(b);

    return lb <= la && strcmp(a + la - lb, b) == 0;
}

static void *arr_object_at(void *self, int64_t idx)
{
    fo_array *a = self;

    if (!a || a->magic != FO_ARRAY_MAGIC || idx < 0 || idx >= a->count)
        return fo_string_new("");
    return a->items[idx];
}

static void *arr_first(void *self)
{
    return arr_object_at(self, 0);
}

static void arr_add(void *self, void *obj)
{
    fo_array *a = self;

    if (!a || a->magic != FO_ARRAY_MAGIC)
        return;
    if (a->count < FO_MAX_ITEMS)
        a->items[a->count++] = obj;
}

static void *arr_joined(void *self, void *sep)
{
    fo_array *a = self;
    const char *s = fo_string_bytes(sep);
    size_t total = 1;
    char *buf;
    void *out;

    if (!a || a->magic != FO_ARRAY_MAGIC)
        return NULL;
    for (int64_t i = 0; i < a->count; i++)
        total += strlen(fo_string_bytes(a->items[i])) + strlen(s);
    buf = calloc(1, total);
    if (!buf)
        return NULL;
    for (int64_t i = 0; i < a->count; i++) {
        if (i)
            strcat(buf, s);
        strcat(buf, fo_string_bytes(a->items[i]));
    }
    out = fo_string_new(buf);
    free(buf);
    return out;
}

static void *dict_object_for_key(void *self, void *key)
{
    fo_dict *d = self;
    const char *k = fo_string_bytes(key);

    if (!d || d->magic != FO_DICT_MAGIC)
        return fo_string_new("");
    for (int64_t i = 0; i < d->count; i++) {
        if (strcmp(fo_string_bytes(d->keys[i]), k) == 0)
            return d->values[i];
    }
    return fo_string_new("");
}

static void dict_set_object(void *self, void *obj, void *key)
{
    fo_dict *d = self;

    if (!d || d->magic != FO_DICT_MAGIC)
        return;
    if (d->count < FO_MAX_ITEMS) {
        d->keys[d->count] = key;
        d->values[d->count] = obj;
        d->count++;
    }
}

static void *dict_all_keys(void *self)
{
    fo_dict *d = self;
    fo_array *a;
    fo_class *cls = fo_find("NSMutableArray");

    if (!d || d->magic != FO_DICT_MAGIC)
        return NULL;
    a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->magic = FO_ARRAY_MAGIC;
    a->isa = cls ? &cls->cls : NULL;
    for (int64_t i = 0; i < d->count && a->count < FO_MAX_ITEMS; i++)
        a->items[a->count++] = d->keys[i];
    return a;
}

static void *dict_all_values(void *self)
{
    fo_dict *d = self;
    fo_array *a;
    fo_class *cls = fo_find("NSMutableArray");

    if (!d || d->magic != FO_DICT_MAGIC)
        return NULL;
    a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->magic = FO_ARRAY_MAGIC;
    a->isa = cls ? &cls->cls : NULL;
    for (int64_t i = 0; i < d->count && a->count < FO_MAX_ITEMS; i++)
        a->items[a->count++] = d->values[i];
    return a;
}

static void *make_instance(const char *cls_name)
{
    fo_class *c = fo_find(cls_name);
    size_t size = c ? c->ro.instance_size : 64;
    void *obj;

    if (size < 64)
        size = 64;
    obj = calloc(1, size);
    if (!obj)
        return NULL;
    if (c)
        *(void **)obj = &c->cls;
    return obj;
}

static void *singleton(const char *key, const char *cls_name)
{
    void *cached = fo_pair_get(key);

    if (!cached) {
        cached = make_instance(cls_name);
        if (cached)
            fo_pair_set(key, cached);
    }
    return cached;
}

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

static void *bundle_path(void)
{
    return singleton("__bundle_path", "NSString");
}

static void *process_arguments(void)
{
    fo_array *a;
    fo_class *cls = fo_find("NSArray");

    a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->isa = cls ? &cls->cls : NULL;
    a->magic = FO_ARRAY_MAGIC;
    a->items[a->count++] = fo_string_new("/var/containers/Bundle/Application/App.app/App");
    return a;
}

static void *process_environment(void)
{
    fo_dict *d;
    fo_class *cls = fo_find("NSDictionary");

    d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->isa = cls ? &cls->cls : NULL;
    d->magic = FO_DICT_MAGIC;
    return d;
}

static void *operating_system_version(void)
{
    return NULL;
}

static void *operating_system_name(void)
{
    return fo_string_new("NSMACHOperatingSystem");
}

static void *urls_for_directory(void)
{
    fo_array *a;
    fo_class *cls = fo_find("NSMutableArray");

    a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->magic = FO_ARRAY_MAGIC;
    a->isa = cls ? &cls->cls : NULL;
    a->items[a->count++] = fo_string_new("file:///var/mobile/Documents");
    return a;
}

static void *contents_of_directory(void)
{
    fo_array *a;
    fo_class *cls = fo_find("NSMutableArray");

    a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->magic = FO_ARRAY_MAGIC;
    a->isa = cls ? &cls->cls : NULL;
    return a;
}

static int bool_no(void)
{
    return 0;
}

static void *nil_object(void)
{
    return NULL;
}

typedef struct {
    const char *sel;
    void *imp;
} fo_seed_method;

static const fo_seed_method g_methods[] = {
    { "stringWithUTF8String:", (void *)str_with_utf8 },
    { "stringWithCString:encoding:", (void *)str_with_utf8 },
    { "stringWithFormat:", (void *)str_with_format },
    { "localizedStringWithFormat:", (void *)str_with_format },
    { "fileExistsAtPath:", (void *)file_exists },
    { "fileExistsAtPath:isDirectory:", (void *)file_exists },
    { "createDirectoryAtPath:withIntermediateDirectories:attributes:error:", (void *)create_directory },
    { "removeItemAtPath:error:", (void *)remove_item },
    { "self", (void *)obj_self_impl },
    { "init", (void *)obj_self_impl },
    { "retain", (void *)obj_retain },
    { "copy", (void *)obj_retain },
    { "mutableCopy", (void *)obj_retain },
    { "autorelease", (void *)obj_retain },
    { "release", (void *)obj_release },
    { "dealloc", (void *)obj_release },
    { "retainCount", (void *)obj_hash_impl },
    { "class", (void *)obj_class_impl },
    { "superclass", (void *)obj_class_impl },
    { "description", (void *)obj_description },
    { "debugDescription", (void *)obj_description },
    { "hash", (void *)obj_hash_impl },
    { "isEqual:", (void *)obj_is_equal },
    { "isEqualToString:", (void *)obj_is_equal },
    { "compare:", (void *)obj_compare },
    { "isKindOfClass:", (void *)obj_is_kind_of },
    { "isMemberOfClass:", (void *)obj_is_member_of },
    { "respondsToSelector:", (void *)obj_responds },
    { "conformsToProtocol:", (void *)obj_bool_yes },
    { "isProxy", (void *)obj_bool_no },
    { "zone", (void *)nil_object },
    { "length", (void *)obj_length_impl },
    { "lengthOfBytesUsingEncoding:", (void *)obj_length_impl },
    { "count", (void *)obj_count_impl },
    { "UTF8String", (void *)obj_utf8 },
    { "fileSystemRepresentation", (void *)obj_utf8 },
    { "absoluteString", (void *)str_identity },
    { "path", (void *)str_identity },
    { "lastPathComponent", (void *)str_last_path_component },
    { "pathExtension", (void *)str_path_extension },
    { "stringByDeletingLastPathComponent", (void *)str_delete_last_component },
    { "stringByDeletingPathExtension", (void *)str_delete_path_extension },
    { "stringByAppendingPathComponent:", (void *)str_append_component },
    { "stringByAppendingString:", (void *)str_append_string },
    { "stringByAppendingPathExtension:", (void *)str_append_string },
    { "componentsSeparatedByString:", (void *)str_components_separated },
    { "substringToIndex:", (void *)str_substring_to },
    { "substringFromIndex:", (void *)str_substring_from },
    { "lowercaseString", (void *)str_lowercase },
    { "uppercaseString", (void *)str_uppercase },
    { "hasPrefix:", (void *)str_has_prefix },
    { "hasSuffix:", (void *)str_has_suffix },
    { "isAbsolutePath", (void *)str_is_absolute },
    { "pathComponents", (void *)str_path_components },
    { "integerValue", (void *)obj_integer_value },
    { "intValue", (void *)obj_integer_value },
    { "longLongValue", (void *)obj_integer_value },
    { "unsignedIntegerValue", (void *)obj_integer_value },
    { "floatValue", (void *)obj_double_value },
    { "doubleValue", (void *)obj_double_value },
    { "boolValue", (void *)obj_bool_value },
    { "objectAtIndex:", (void *)arr_object_at },
    { "objectAtIndexedSubscript:", (void *)arr_object_at },
    { "firstObject", (void *)arr_first },
    { "lastObject", (void *)arr_nil_safe_last },
    { "addObject:", (void *)arr_add },
    { "addObjectsFromArray:", (void *)arr_add },
    { "componentsJoinedByString:", (void *)arr_joined },
    { "objectForKey:", (void *)dict_object_for_key },
    { "objectForKeyedSubscript:", (void *)dict_object_for_key },
    { "setObject:forKey:", (void *)dict_set_object },
    { "allKeys", (void *)dict_all_keys },
    { "allValues", (void *)dict_all_values },
    { "processInfo", (void *)process_info },
    { "arguments", (void *)process_arguments },
    { "environment", (void *)process_environment },
    { "operatingSystemVersion", (void *)operating_system_version },
    { "operatingSystemVersionString", (void *)operating_system_name },
    { "operatingSystemName", (void *)operating_system_name },
    { "globallyUniqueString", (void *)operating_system_name },
    { "defaultManager", (void *)file_manager },
    { "mainBundle", (void *)main_bundle },
    { "bundlePath", (void *)bundle_path },
    { "resourcePath", (void *)bundle_path },
    { "URLsForDirectory:inDomains:", (void *)urls_for_directory },
    { "URLForDirectory:inDomain:appropriateForURL:create:error:", (void *)urls_for_directory },
    { "contentsOfDirectoryAtPath:error:", (void *)contents_of_directory },
    { "isMacCatalystApp", (void *)bool_no },
    { "isiOSAppOnMac", (void *)bool_no },
    { "isiOSAppOnVision", (void *)bool_no },
    { NULL, NULL }
};

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
        m.imp = g_methods[k].imp;
        if (chm_objc_session_add_method(s, &m) != 0)
            break;
    }
    if (s)
        chm_session_log(s, "foundation: %d classes, %d selectors", g_class_count, k);
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
    for (i = 0; g_methods[i].sel; i++) {
        if (strcmp(g_methods[i].sel, sel) == 0)
            return g_methods[i].imp;
    }
    return NULL;
}

void chm_foundation_note_missing_selector(const char *cls_name, const char *sel)
{
    if (g_session && cls_name && sel)
        chm_session_log(g_session, "unimplemented -[%s %s]", cls_name, sel);
}
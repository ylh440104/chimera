#include <ctype.h>

#define FO_PREDICATE_PARTS 16

typedef struct {
    void *isa;
    unsigned count;
    void *keys[FO_PREDICATE_PARTS], *value;
} fo_predicate;

extern void chm_predicate_entry(void);

void *chm_predicate_list(void *cls, const char *sel, void *format, void *const *args)
{
    (void)sel;
    const char *p = fo_bytes(format), *end = p + strlen(p);
    if (!args) return NULL;
    for (;;) {
        while (p < end && isspace((unsigned char)*p)) p++;
        while (end > p && isspace((unsigned char)end[-1])) end--;
        if (end - p < 2 || *p != '(' || end[-1] != ')') break;
        p++; end--;
    }
    const char *path = p;
    size_t length = 0;
    if (end - p >= 2 && p[0] == '%' && p[1] == 'K') {
        path = fo_bytes(*args++); length = strlen(path); p += 2;
    } else {
        while (p < end && (isalnum((unsigned char)*p) || *p == '_' || *p == '.')) p++;
        length = (size_t)(p - path);
    }
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p == end || *p++ != '=') return NULL;
    if (p < end && *p == '=') p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (end - p != 2 || memcmp(p, "%@", 2) || !length || length > 255) return NULL;
    fo_predicate *pred = chm_foundation_alloc_class(cls, sizeof(*pred));
    if (!pred) return NULL;
    pred->value = *args;
    if (length == 4 && !strncmp(path, "SELF", 4)) return pred;
    char key[256];
    for (size_t i = 0; i < length;) {
        size_t start = i;
        if (!(isalpha((unsigned char)path[i]) || path[i] == '_')) return NULL;
        while (i < length && (isalnum((unsigned char)path[i]) || path[i] == '_')) i++;
        if (pred->count == FO_PREDICATE_PARTS) return NULL;
        memcpy(key, path + start, i - start); key[i - start] = 0;
        pred->keys[pred->count++] = fo_string_new(key);
        if (i < length && (path[i++] != '.' || i == length)) return NULL;
    }
    return pred;
}

static int predicate_evaluate(fo_predicate *pred, const char *sel, void *obj)
{
    (void)sel;
    if (!pred || !chm_foundation_isa_is(pred, "NSPredicate")) return 0;
    for (unsigned i = 0; obj && i < pred->count; i++) {
        void *key = pred->keys[i];
        if (fo_is_dict(obj)) obj = d_value(obj, NULL, key);
        else obj = chm_objc_send1(obj, "valueForKey:", key);
    }
    return o_is_equal(obj, NULL, pred->value);
}

static void *predicate_filter(void *self, const char *sel, void *pred)
{
    (void)sel;
    if (!fo_is_array(self) || !pred) return NULL;
    int (*evaluate)(void *, const char *, void *) = chm_objc_find_imp(pred, "evaluateWithObject:");
    if (!evaluate) return NULL;
    fo_array *source = self, *output = fo_array_new();
    if (!output) return NULL;
    for (int64_t i = 0; i < source->count; i++) {
        void *v = source->items[i];
        if (evaluate(pred, "evaluateWithObject:", v) && !a_push(output, v)) return NULL;
    }
    return output;
}

static void *mutable_string_capacity(void *cls, const char *sel, size_t capacity)
{
    (void)sel; (void)capacity;
    return chm_foundation_alloc_class(cls, sizeof(fo_string));
}

static const chm_host_method filter_methods[] = {
    {"NSPredicate", "predicateWithFormat:", "@@:@", chm_predicate_entry, 1},
    {"NSPredicate", "evaluateWithObject:", "B@:@", predicate_evaluate, 0},
    {"NSArray", "filteredArrayUsingPredicate:", "@@:@", predicate_filter, 0},
    {"NSMutableString", "stringWithCapacity:", "@@:Q", mutable_string_capacity, 1},
    {"NSMutableString", "appendFormat:", "v@:@", chm_ns_append_format_entry, 0}
};
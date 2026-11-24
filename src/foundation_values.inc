#include <sys/syscall.h>
#include <signal.h>
#include "chm_runtime.h"

typedef struct { void *isa; unsigned char bytes[16]; } fo_uuid;
typedef struct { void *isa; uint64_t mutex[8]; void *name; } fo_lock;
typedef struct { unsigned long location, length; } fo_range;
typedef struct fo_indexes {
    void *object;
    fo_range *ranges;
    size_t count;
    struct fo_indexes *next;
} fo_indexes;
static fo_indexes *index_objects;

static void *uuid_bytes(fo_uuid *self, const char *sel, const unsigned char *bytes)
{
    (void)sel;
    if (!self || !bytes) return NULL;
    memcpy(self->bytes, bytes, 16);
    return self;
}

static void *uuid_init(fo_uuid *self, const char *sel)
{
    (void)sel;
    size_t done = 0;
    while (done < 16) {
        ssize_t count = syscall(SYS_getrandom, self->bytes + done, 16 - done, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return NULL;
        done += (size_t)count;
    }
    self->bytes[6] = (self->bytes[6] & 15) | 64;
    self->bytes[8] = (self->bytes[8] & 63) | 128;
    return self;
}

static void *uuid_new(void *cls, const char *sel)
{
    fo_uuid *uuid = chm_foundation_alloc_class(cls, sizeof(*uuid));
    return uuid ? uuid_init(uuid, sel) : NULL;
}

static int uuid_hex(char character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static void *uuid_string_init(fo_uuid *self, const char *sel, void *string)
{
    (void)sel;
    const char *text = fo_bytes(string);
    if (strlen(text) != 36) return NULL;
    unsigned char bytes[16];
    size_t position = 0;
    for (size_t i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) if (text[position++] != '-') return NULL;
        int high = uuid_hex(text[position++]), low = uuid_hex(text[position++]);
        if (high < 0 || low < 0) return NULL;
        bytes[i] = (unsigned char)((high << 4) | low);
    }
    memcpy(self->bytes, bytes, 16);
    return self;
}

static void uuid_get(fo_uuid *self, const char *sel, unsigned char *bytes)
{
    (void)sel;
    if (bytes) memcpy(bytes, self->bytes, 16);
}

static void *uuid_string(fo_uuid *self, const char *sel)
{
    (void)sel;
    static const char digits[] = "0123456789ABCDEF";
    char text[37]; size_t position = 0;
    for (size_t i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) text[position++] = '-';
        text[position++] = digits[self->bytes[i] >> 4];
        text[position++] = digits[self->bytes[i] & 15];
    }
    text[position] = 0;
    return fo_string_new(text);
}

static signed char uuid_equal(fo_uuid *self, const char *sel, void *other)
{
    (void)sel;
    return chm_foundation_isa_is(other, "NSUUID") && !memcmp(self->bytes, ((fo_uuid *)other)->bytes, 16);
}
static unsigned long uuid_hash(fo_uuid *self, const char *sel)
{
    (void)sel;
    unsigned long hash = 5381;
    for (size_t i = 0; i < 16; i++) hash = hash * 33 ^ self->bytes[i];
    return hash;
}

static void *lock_init(fo_lock *self, const char *sel)
{
    (void)sel;
    if (!self->mutex[0]) self->mutex[0] = chm_foundation_isa_is(self, "NSRecursiveLock") ? 0x32AAABA2 : 0x32AAABA7;
    return self;
}
static int lock_operation(fo_lock *self, const char *symbol)
{
    int (*operation)(void *) = chm_runtime_lookup(symbol);
    return operation ? operation(self->mutex) : 22;
}
static void lock_take(fo_lock *self, const char *sel)
{
    (void)sel;
    int result = lock_operation(self, "_pthread_mutex_lock");
    if (result) { chm_session_log(g_session, "NSLock lock failed: %d", result); raise(SIGABRT); }
}
static void lock_drop(fo_lock *self, const char *sel)
{
    (void)sel;
    int result = lock_operation(self, "_pthread_mutex_unlock");
    if (result) { chm_session_log(g_session, "NSLock unlock failed: %d", result); raise(SIGABRT); }
}
static signed char lock_try(fo_lock *self, const char *sel)
{
    (void)sel;
    return !lock_operation(self, "_pthread_mutex_trylock");
}
static void lock_name_set(fo_lock *self, const char *sel, void *name)
{
    (void)sel;
    self->name = name ? fo_string_new(fo_bytes(name)) : NULL;
}
static void *lock_name(fo_lock *self, const char *sel) { (void)sel; return self->name; }

static fo_indexes *indexes_find(void *object, int create)
{
    for (fo_indexes *set = index_objects; set; set = set->next) if (set->object == object) return set;
    if (!create) return NULL;
    fo_indexes *set = calloc(1, sizeof(*set));
    if (!set) return NULL;
    set->object = object;
    set->next = index_objects;
    index_objects = set;
    return set;
}

static int indexes_valid(fo_range range)
{
    return range.location <= (unsigned long)INT64_MAX && range.length <= (unsigned long)INT64_MAX - range.location;
}

static int indexes_change(void *object, fo_range range, int remove)
{
    if (!indexes_valid(range)) return 0;
    fo_indexes *set = indexes_find(object, 1);
    if (!set || set->count > 1024 * 1024) return 0;
    if (!range.length) return 1;
    fo_range *output = malloc((set->count + 1) * sizeof(*output));
    if (!output) return 0;
    size_t count = 0;
    unsigned long start = range.location, end = start + range.length;
    int inserted = 0;
    for (size_t i = 0; i < set->count; i++) {
        unsigned long a = set->ranges[i].location, b = a + set->ranges[i].length;
        if (remove) {
            if (b <= start || a >= end) output[count++] = set->ranges[i];
            else {
                if (a < start) output[count++] = (fo_range){a, start - a};
                if (b > end) output[count++] = (fo_range){end, b - end};
            }
        } else if (b < start) output[count++] = set->ranges[i];
        else if (a > end) {
            if (!inserted) { output[count++] = (fo_range){start, end - start}; inserted = 1; }
            output[count++] = set->ranges[i];
        } else { if (a < start) start = a; if (b > end) end = b; }
    }
    if (!remove && !inserted) output[count++] = (fo_range){start, end - start};
    free(set->ranges); set->ranges = output; set->count = count;
    return 1;
}

static void *indexes_range_init(void *self, const char *sel, fo_range range)
{
    (void)sel;
    fo_indexes *set = indexes_find(self, 1);
    if (!set || !indexes_valid(range)) return NULL;
    free(set->ranges); set->ranges = NULL; set->count = 0;
    return indexes_change(self, range, 0) ? self : NULL;
}
static void *indexes_index_init(void *self, const char *sel, unsigned long index) { return indexes_range_init(self, sel, (fo_range){index, 1}); }
static void *indexes_range(void *cls, const char *sel, fo_range range)
{
    void *object = chm_foundation_alloc_class(cls, 0);
    return object ? indexes_range_init(object, sel, range) : NULL;
}
static void *indexes_index(void *cls, const char *sel, unsigned long index) { return indexes_range(cls, sel, (fo_range){index, 1}); }
static void *indexes_empty(void *cls, const char *sel) { return indexes_range(cls, sel, (fo_range){0, 0}); }
static unsigned long indexes_count(void *self, const char *sel)
{
    (void)sel;
    fo_indexes *set = indexes_find(self, 0); unsigned long count = 0;
    if (set) for (size_t i = 0; i < set->count; i++) count += set->ranges[i].length;
    return count;
}
static signed char indexes_contains_range(void *self, const char *sel, fo_range range)
{
    (void)sel;
    if (!indexes_valid(range)) return 0;
    if (!range.length) return 1;
    fo_indexes *set = indexes_find(self, 0);
    if (set) for (size_t i = 0; i < set->count; i++)
        if (set->ranges[i].location <= range.location && set->ranges[i].location + set->ranges[i].length >= range.location + range.length) return 1;
    return 0;
}
static signed char indexes_contains(void *self, const char *sel, unsigned long index) { return indexes_contains_range(self, sel, (fo_range){index, 1}); }
static unsigned long indexes_first(void *self, const char *sel)
{
    fo_indexes *set = indexes_find(self, 0);
    if (!set || !set->count) return INT64_MAX;
    fo_range range = set->ranges[!strcmp(sel, "lastIndex") ? set->count - 1 : 0];
    return range.location + (!strcmp(sel, "lastIndex") ? range.length - 1 : 0);
}
static unsigned long indexes_next(void *self, const char *sel, unsigned long index)
{
    if (index >= (unsigned long)INT64_MAX) return INT64_MAX;
    int lower = strstr(sel, "Less") != NULL, equal = strstr(sel, "Equal") != NULL;
    if (!equal) {
        if (lower && !index) return INT64_MAX;
        index = lower ? index - 1 : index + 1;
    }
    fo_indexes *set = indexes_find(self, 0);
    if (!set) return INT64_MAX;
    for (size_t i = 0; i < set->count; i++) {
        fo_range range = set->ranges[lower ? set->count - i - 1 : i];
        unsigned long last = range.location + range.length - 1;
        if (lower && index >= range.location) return index < last ? index : last;
        if (!lower && index <= last) return index > range.location ? index : range.location;
    }
    return INT64_MAX;
}
static void indexes_mutate_range(void *self, const char *sel, fo_range range)
{
    if (!indexes_change(self, range, !strncmp(sel, "remove", 6))) collection_error("invalid index range");
}
static void indexes_mutate_index(void *self, const char *sel, unsigned long index) { indexes_mutate_range(self, sel, (fo_range){index, 1}); }
static void indexes_clear(void *self, const char *sel)
{
    (void)sel;
    fo_indexes *set = indexes_find(self, 0);
    if (set) { free(set->ranges); set->ranges = NULL; set->count = 0; }
}
static void *indexes_copy(void *self, const char *sel)
{
    void *object = fo_alloc(strstr(sel, "mutable") ? "NSMutableIndexSet" : "NSIndexSet", 0);
    fo_indexes *source = indexes_find(self, 0), *target = object ? indexes_find(object, 1) : NULL;
    if (!target) return NULL;
    if (source && source->count) {
        target->ranges = malloc(source->count * sizeof(*target->ranges));
        if (!target->ranges) return NULL;
        memcpy(target->ranges, source->ranges, source->count * sizeof(*target->ranges));
        target->count = source->count;
    }
    return object;
}
static void values_reset(void)
{
    while (index_objects) {
        fo_indexes *next = index_objects->next;
        free(index_objects->ranges); free(index_objects); index_objects = next;
    }
}

static const chm_host_method value_methods[] = {
    {"NSUUID", "init", "@@:", uuid_init, 0},
    {"NSUUID", "UUID", "@@:", uuid_new, 1},
    {"NSUUID", "initWithUUIDBytes:", "@@:^C", uuid_bytes, 0},
    {"NSUUID", "initWithUUIDString:", "@@:@", uuid_string_init, 0},
    {"NSUUID", "getUUIDBytes:", "v@:^C", uuid_get, 0},
    {"NSUUID", "UUIDString", "@@:", uuid_string, 0},
    {"NSUUID", "description", "@@:", uuid_string, 0},
    {"NSUUID", "isEqual:", "B@:@", uuid_equal, 0},
    {"NSUUID", "hash", "Q@:", uuid_hash, 0},
    {"NSLock", "init", "@@:", lock_init, 0},
    {"NSLock", "lock", "v@:", lock_take, 0},
    {"NSLock", "unlock", "v@:", lock_drop, 0},
    {"NSLock", "tryLock", "B@:", lock_try, 0},
    {"NSLock", "name", "@@:", lock_name, 0},
    {"NSLock", "setName:", "v@:@", lock_name_set, 0},
    {"NSIndexSet", "indexSet", "@@:", indexes_empty, 1},
    {"NSIndexSet", "indexSetWithIndex:", "@@:Q", indexes_index, 1},
    {"NSIndexSet", "indexSetWithIndexesInRange:", "@@:{_NSRange=QQ}", indexes_range, 1},
    {"NSIndexSet", "initWithIndex:", "@@:Q", indexes_index_init, 0},
    {"NSIndexSet", "initWithIndexesInRange:", "@@:{_NSRange=QQ}", indexes_range_init, 0},
    {"NSIndexSet", "count", "Q@:", indexes_count, 0},
    {"NSIndexSet", "containsIndex:", "B@:Q", indexes_contains, 0},
    {"NSIndexSet", "containsIndexesInRange:", "B@:{_NSRange=QQ}", indexes_contains_range, 0},
    {"NSIndexSet", "firstIndex", "Q@:", indexes_first, 0},
    {"NSIndexSet", "lastIndex", "Q@:", indexes_first, 0},
    {"NSIndexSet", "indexGreaterThanIndex:", "Q@:Q", indexes_next, 0},
    {"NSIndexSet", "indexGreaterThanOrEqualToIndex:", "Q@:Q", indexes_next, 0},
    {"NSIndexSet", "indexLessThanIndex:", "Q@:Q", indexes_next, 0},
    {"NSIndexSet", "indexLessThanOrEqualToIndex:", "Q@:Q", indexes_next, 0},
    {"NSIndexSet", "copy", "@@:", indexes_copy, 0},
    {"NSIndexSet", "mutableCopy", "@@:", indexes_copy, 0},
    {"NSMutableIndexSet", "addIndex:", "v@:Q", indexes_mutate_index, 0},
    {"NSMutableIndexSet", "removeIndex:", "v@:Q", indexes_mutate_index, 0},
    {"NSMutableIndexSet", "addIndexesInRange:", "v@:{_NSRange=QQ}", indexes_mutate_range, 0},
    {"NSMutableIndexSet", "removeIndexesInRange:", "v@:{_NSRange=QQ}", indexes_mutate_range, 0},
    {"NSMutableIndexSet", "removeAllIndexes", "v@:", indexes_clear, 0}
};
#include "foundation_json.inc"
#include "foundation_plist.inc"
#include "foundation_network.inc"
#include "foundation_defaults.inc"
#include "foundation_bundle.inc"

static long string_compare(void *self, const char *sel, void *other)
{
    (void)sel;
    if (fo_is_number(self) && fo_is_number(other)) {
        double a = o_double(self), b = o_double(other); return (a > b) - (a < b);
    }
    return o_compare(self, sel, other);
}
static void *array_sorted(void *self, const char *sel, const char *selector)
{
    if (!fo_is_array(self) || !selector) return NULL;
    fo_array *a = !strcmp(sel, "sortUsingSelector:") ? self : a_with_array(chm_foundation_lookup_class("NSArray"), NULL, self);
    if (!a) return NULL;
    for (int64_t i = 1; i < a->count; i++) {
        void *value = a->items[i]; int64_t j = i;
        long (*compare)(void *, const char *, void *) = chm_objc_find_imp(value, selector);
        if (!compare) return NULL;
        while (j > 0 && compare(value, selector, a->items[j - 1]) < 0) { a->items[j] = a->items[j - 1]; j--; }
        a->items[j] = value;
    }
    a->mutations++; return a;
}
static void *directory_contents(void *self, const char *sel, void *path, void **error)
{
    (void)self; (void)sel;
    if (error) *error = NULL;
    char mapped[2048];
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped))) goto failed;
    DIR *dir = opendir(mapped);
    if (!dir) goto failed;
    fo_array *a = fo_array_new(); struct dirent *entry; int ok = a != NULL;
    while (ok && (entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        ok = a_push(a, fo_string_new(entry->d_name));
    }
    closedir(dir);
    if (ok) return a;
failed:
    if (error) *error = make_error("NSCocoaErrorDomain", errno, "Cannot enumerate directory");
    return NULL;
}
static int file_exists_directory(void *self, const char *sel, void *path, signed char *directory)
{
    (void)self; (void)sel;
    if (directory) *directory = 0;
    char mapped[2048]; struct stat st;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)) || stat(mapped, &st)) return 0;
    if (directory) *directory = S_ISDIR(st.st_mode);
    return 1;
}
static int directory_change(void *self, const char *sel, void *path)
{
    (void)self; (void)sel;
    char mapped[2048]; struct stat st;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)) || !path_below(mapped, g_sandbox_root) || stat(mapped, &st) || !S_ISDIR(st.st_mode)) return 0;
    const char *rel = mapped + strlen(g_sandbox_root);
    if (strlen(rel) >= sizeof(g_guest_directory)) return 0;
    strcpy(g_guest_directory, *rel ? rel : "/"); return 1;
}
static void *directory_current(void *self, const char *sel) { (void)self; (void)sel; return fo_string_new(g_guest_directory); }
static int file_symlink(void *self, const char *sel, void *path, void *target, void **error)
{
    (void)self; (void)sel;
    if (error) *error = NULL;
    char from[2048], to[2048];
    if (chm_foundation_map_path_write(fo_bytes(path), from, sizeof(from)) && chm_foundation_map_path(fo_bytes(target), to, sizeof(to)) && !symlink(to, from)) return 1;
    if (error) *error = make_error("NSCocoaErrorDomain", errno, "Cannot create symbolic link");
    return 0;
}

#include "foundation_streams.inc"
#include "foundation_xml.inc"
static long json_write_stream(void *cls, const char *sel, void *object, void *stream, unsigned long options, void **error)
{
    fo_data *data = json_write(cls, sel, object, options, error);
    if (!data) return 0;
    size_t offset = 0;
    while (offset < (size_t)data->length) {
        long n = stream_write(stream, NULL, data->bytes + offset, (size_t)data->length - offset);
        if (n <= 0) { if (error) *error = make_error("NSCocoaErrorDomain", errno, "Cannot write JSON stream"); return 0; }
        offset += (size_t)n;
    }
    return (long)offset;
}

typedef struct notification { void *object, *name, *sender, *info; struct notification *next; } notification;
typedef struct observer { void *center, *object, *name, *sender; char *selector; unsigned long serial; int active; struct observer *next; } observer;
static notification *notifications;
static observer *observers;
static unsigned long observer_serial;
static void *notification_center(void) { return singleton("__notification_center", "NSNotificationCenter"); }
static void *notification_new(void *cls, const char *sel, void *name, void *sender, void *info)
{
    (void)cls; (void)sel;
    notification *n = calloc(1, sizeof(*n));
    if (!n || !name) { free(n); return NULL; }
    n->object = fo_alloc("NSNotification", 0); n->name = name; n->sender = sender; n->info = info;
    n->next = notifications; notifications = n; return n->object;
}
static void *notification_property(void *self, const char *sel)
{
    for (notification *n = notifications; n; n = n->next)
        if (n->object == self) return !strcmp(sel, "name") ? n->name : !strcmp(sel, "object") ? n->sender : n->info;
    return NULL;
}
static void notification_add(void *self, const char *sel, void *object, const char *selector, void *name, void *sender)
{
    (void)sel;
    if (!object || !selector) return;
    observer *o = calloc(1, sizeof(*o)); if (!o) return;
    o->selector = strdup(selector); if (!o->selector) { free(o); return; }
    o->center = self; o->object = object; o->name = name; o->sender = sender; o->active = 1;
    o->serial = ++observer_serial; o->next = observers; observers = o;
}
static void notification_remove_filtered(void *self, const char *sel, void *object, void *name, void *sender)
{
    (void)sel;
    for (observer *o = observers; o; o = o->next)
        if (o->center == self && o->object == object && (!name || o_is_equal(name, NULL, o->name)) && (!sender || sender == o->sender)) o->active = 0;
}
static void notification_remove(void *self, const char *sel, void *object) { notification_remove_filtered(self, sel, object, NULL, NULL); }
static void notification_post(void *self, const char *sel, void *object)
{
    (void)sel; notification *n = notifications;
    while (n && n->object != object) n = n->next;
    if (!n) return;
    unsigned long serial = observer_serial;
    for (observer *o = observers; o; o = o->next) {
        if (!o->active || o->serial > serial || o->center != self || (o->name && !o_is_equal(o->name, NULL, n->name)) || (o->sender && o->sender != n->sender)) continue;
        void (*fn)(void *, const char *, void *) = chm_objc_find_imp(o->object, o->selector);
        if (fn) fn(o->object, o->selector, object);
    }
}
static void notification_post_info(void *self, const char *sel, void *name, void *sender, void *info)
{
    notification_post(self, sel, notification_new(NULL, NULL, name, sender, info));
}
static void notification_post_name(void *self, const char *sel, void *name, void *sender) { notification_post_info(self, sel, name, sender, NULL); }

void chm_foundation_set_bundle(const char *root)
{
    if (root && strlen(root) < sizeof(g_bundle_root)) {
        char resolved[4096];
        if (realpath(root, resolved) && strlen(resolved) < sizeof(g_bundle_root)) strcpy(g_bundle_root, resolved);
        else strcpy(g_bundle_root, root);
    }
}
void *chm_foundation_bundle_info(void)
{
    return bundle_metadata(NULL);
}
static void *bundle_information(void *self, const char *sel)
{
    (void)sel; return bundle_metadata(self);
}
static void *bundle_info_value(void *self, const char *sel, void *key)
{
    (void)sel; return d_value(bundle_metadata(self), NULL, key);
}
static long string_compare_insensitive(void *self, const char *sel, void *other)
{
    (void)sel;
    int rc = strcasecmp(fo_bytes(self), fo_bytes(other)); return (rc > 0) - (rc < 0);
}
static int data_write_options(void *self, const char *sel, void *path, unsigned long options, void **error)
{
    if (error) *error = NULL;
    int ok = d_write_file(self, sel, path, (options & 1) != 0);
    if (!ok && error) *error = make_error("NSCocoaErrorDomain", errno, "Cannot write data");
    return ok;
}
static void *bundle_path(void *self, const char *sel) { (void)sel; return fo_string_new(bundle_directory(self)); }
static void *bundle_resource(void *self, const char *sel, void *name, void *extension)
{
    (void)self; (void)sel;
    char path[2048], mapped[2048]; struct stat st;
    int n = snprintf(path, sizeof(path), "%s/%s%s%s", bundle_directory(self), fo_bytes(name), *fo_bytes(extension) ? "." : "", fo_bytes(extension));
    if (n < 0 || (size_t)n >= sizeof(path) || !chm_foundation_map_path(path, mapped, sizeof(mapped)) || stat(mapped, &st)) return NULL;
    return fo_string_new(mapped);
}
static void *preferred_languages(void)
{
    fo_array *a = fo_array_new(); if (a) a_push(a, fo_string_new("en")); return a;
}
static void *bundle_localized(void *self, const char *sel, void *key, void *value, void *table)
{
    (void)self; (void)sel;
    const char *filename = *fo_bytes(table) ? fo_bytes(table) : "Localizable";
    if (strchr(filename, '/') || strstr(filename, "..")) return value && *fo_bytes(value) ? value : key;
    char path[2048];
    const char *folders[] = {"en.lproj", "Base.lproj", ""};
    for (size_t i = 0; i < 3; i++) {
        int n = snprintf(path, sizeof(path), "%s/%s/%s.strings", bundle_directory(self), folders[i], filename);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        fo_data *data = read_whole_file(path); if (!data || !data->length) continue;
        void *dict = plist_read_data(data);
        if (fo_is_dict(dict)) { void *found = d_value(dict, NULL, key); if (found) return found; }
        json_reader r = {data->bytes, data->bytes + data->length, 0, 0};
        while (r.p < r.end) {
            json_space(&r);
            if (r.end - r.p >= 2 && r.p[0] == '/' && r.p[1] == '*') {
                r.p += 2; while (r.end - r.p >= 2 && !(r.p[0] == '*' && r.p[1] == '/')) r.p++;
                if (r.end - r.p >= 2) r.p += 2;
                continue;
            }
            void *k = json_string(&r); json_space(&r);
            if (!k || r.p == r.end || *r.p++ != '=') break;
            json_space(&r); void *v = json_string(&r); json_space(&r);
            if (!v || r.p == r.end || *r.p++ != ';') break;
            if (o_is_equal(key, NULL, k)) return v;
        }
    }
    return value && *fo_bytes(value) ? value : key;
}
static uint64_t process_memory(void *self, const char *sel)
{
    (void)self; (void)sel;
    long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0 ? (uint64_t)pages * (uint64_t)size : 0;
}
static void *device_current(void) { return singleton("__device", "UIDevice"); }
static void *device_model(void *self, const char *sel) { (void)self; (void)sel; return fo_string_new("Chimera Android"); }
static void *main_run_loop(void) { return singleton("__main_run_loop", "NSRunLoop"); }
static void *null_object(void) { return singleton("__null", "NSNull"); }
static void services_shutdown(void)
{
    network_shutdown();
    constant_strings_reset();
    bundles_reset();
    streams_reset();
    xml_reset();
    while (observers) { observer *next = observers->next; free(observers->selector); free(observers); observers = next; }
    while (notifications) { notification *next = notifications->next; free(notifications); notifications = next; }
    observer_serial = 0; g_bundle_root[0] = 0;
    default_values = NULL; registered_defaults = NULL;
}
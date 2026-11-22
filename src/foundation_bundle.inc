typedef struct resource_bundle { void *object; char *path; struct resource_bundle *next; } resource_bundle;
static resource_bundle *resource_bundles;
static void *bundle_metadata(void *object);

static const char *bundle_directory(void *object)
{
    for (resource_bundle *b = resource_bundles; b; b = b->next) if (b->object == object) return b->path;
    return g_bundle_root;
}

static void *bundle_at_path(void *cls, const char *sel, void *path)
{
    (void)cls; (void)sel;
    char mapped[2048]; struct stat st;
    if (!chm_foundation_map_path(fo_bytes(path), mapped, sizeof(mapped)) || stat(mapped, &st) || !S_ISDIR(st.st_mode)) return NULL;
    for (resource_bundle *b = resource_bundles; b; b = b->next) if (!strcmp(b->path, mapped)) return b->object;
    resource_bundle *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->object = fo_alloc("NSBundle", 0); b->path = strdup(mapped);
    if (!b->object || !b->path) { free(b->path); free(b); return NULL; }
    b->next = resource_bundles; resource_bundles = b;
    return b->object;
}

static void *bundle_metadata(void *object)
{
    char path[2048];
    int n = snprintf(path, sizeof(path), "%s/Info.plist", bundle_directory(object));
    if (n < 0 || (size_t)n >= sizeof(path)) return NULL;
    void *info = plist_read_data(read_whole_file(path));
    return fo_is_dict(info) ? info : NULL;
}

static void *bundle_by_identifier(void *cls, const char *sel, void *identifier)
{
    (void)cls; (void)sel;
    void *key = fo_string_new("CFBundleIdentifier");
    if (o_is_equal(identifier, NULL, d_value(bundle_metadata(NULL), NULL, key))) return singleton("__main_bundle", "NSBundle");
    for (resource_bundle *b = resource_bundles; b; b = b->next)
        if (o_is_equal(identifier, NULL, d_value(bundle_metadata(b->object), NULL, key))) return b->object;
    return NULL;
}

static void bundles_reset(void)
{
    while (resource_bundles) { resource_bundle *next = resource_bundles->next; free(resource_bundles->path); free(resource_bundles); resource_bundles = next; }
}
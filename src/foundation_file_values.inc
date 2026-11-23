static const char *file_url_path(void *object)
{
    net_object *n = net_find(object);
    if (!n || !n->address || strncmp(n->address, "file:///", 8)) return NULL;
    return n->address + 7;
}

static int file_url_is_file(void *self, const char *sel)
{
    (void)sel;
    return file_url_path(self) != NULL;
}

static void *file_url_append(void *self, const char *sel, void *component)
{
    (void)sel;
    const char *base = file_url_path(self);
    if (!base || !component) return NULL;
    void *path = s_append_component(fo_string_new(base), NULL, component);
    return path ? url_new(NULL, "fileURLWithPath:", path) : NULL;
}

static int file_url_resource(void *self, const char *sel, void **value, void *key, void **error)
{
    (void)sel;
    const char *path = file_url_path(self);
    const char *name = fo_bytes(key);
    char mapped[2048];
    struct stat st;
    if (error) *error = NULL;
    if (value) *value = NULL;
    if (!path || !value || !chm_foundation_map_path(path, mapped, sizeof(mapped)) || stat(mapped, &st)) {
        if (error) *error = make_error("NSCocoaErrorDomain", 4, "Cannot read file attributes");
        return 0;
    }
    if (!strcmp(name, "NSURLFileSizeKey")) *value = n_unsigned(NULL, NULL, st.st_size);
    else if (!strcmp(name, "NSURLFileAllocatedSizeKey") || !strcmp(name, "NSURLTotalFileAllocatedSizeKey"))
        *value = n_unsigned(NULL, NULL, (uint64_t)st.st_blocks * 512);
    else if (!strcmp(name, "NSURLIsRegularFileKey")) *value = n_bool(NULL, NULL, S_ISREG(st.st_mode));
    else if (!strcmp(name, "NSURLIsDirectoryKey")) *value = n_bool(NULL, NULL, S_ISDIR(st.st_mode));
    else if (!strcmp(name, "NSURLContentModificationDateKey")) *value = file_date(&st);
    else {
        if (error) *error = make_error("NSCocoaErrorDomain", 3328, "Unsupported file resource key");
        return 0;
    }
    return *value != NULL;
}

static void *file_url_data(void *cls, const char *sel, void *url)
{
    (void)cls; (void)sel;
    const char *path = file_url_path(url);
    return path ? f_contents_at_path(NULL, NULL, fo_string_new(path)) : NULL;
}

static int file_url_write(void *self, const char *sel, void *url, signed char atomic)
{
    (void)sel;
    const char *path = file_url_path(url);
    return path && d_write_file(self, NULL, fo_string_new(path), atomic);
}

static const chm_host_method file_value_methods[] = {
    {"NSDate", "dateWithTimeIntervalSince1970:", "@@:d", date_unix, 1},
    {"NSDate", "timeIntervalSince1970", "d@:", date_interval, 0},
    {"NSDate", "timeIntervalSinceReferenceDate", "d@:", date_interval, 0},
    {"NSURL", "isFileURL", "B@:", file_url_is_file, 0},
    {"NSURL", "URLByAppendingPathComponent:", "@@:@", file_url_append, 0},
    {"NSURL", "getResourceValue:forKey:error:", "B@:^@@^@", file_url_resource, 0},
    {"NSData", "dataWithContentsOfURL:", "@@:@", file_url_data, 1},
    {"NSData", "writeToURL:atomically:", "B@:@B", file_url_write, 0}
};

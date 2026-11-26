#include <openssl/sha.h>
#include <sys/file.h>

typedef struct cached_response {
    void *object, *response, *data, *user_info;
    unsigned long policy;
    struct cached_response *next;
} cached_response;

typedef struct cache_entry {
    char *key;
    void *value;
    size_t cost;
    uint64_t used;
    struct cache_entry *next;
} cache_entry;

typedef struct url_cache {
    void *object;
    size_t memory_capacity, disk_capacity, memory_used;
    uint64_t serial;
    int directory;
    cache_entry *entries;
    char *disk_path;
    struct url_cache *next;
} url_cache;

static cached_response *cached_responses;
static url_cache *url_caches;
static void *shared_url_cache;

static cached_response *cached_find(void *object)
{
    for (cached_response *value = cached_responses; value; value = value->next)
        if (value->object == object) return value;
    return NULL;
}

static url_cache *cache_find(void *object)
{
    for (url_cache *value = url_caches; value; value = value->next)
        if (value->object == object) return value;
    return NULL;
}

static void cached_destroy(void *object)
{
    cached_response **link = &cached_responses;
    while (*link && (*link)->object != object) link = &(*link)->next;
    if (!*link) return;
    cached_response *value = *link;
    *link = value->next;
    cf_owned_release(value->response);
    cf_owned_release(value->data);
    cf_owned_release(value->user_info);
    free(value);
}

static void *cached_init(void *self, const char *selector, void *response, void *data, void *user_info, unsigned long policy)
{
    (void)selector;
    const void *bytes;
    size_t length;
    if (!self || !fo_isa_is(self, "NSCachedURLResponse") || cached_find(self) || policy > 2 ||
        !fo_isa_is(response, "NSURLResponse") || !net_find(response) || (user_info && !fo_is_dict(user_info)) ||
        !chm_foundation_data_bytes(data, &bytes, &length)) return NULL;
    cached_response *value = calloc(1, sizeof(*value));
    if (!value) return NULL;
    value->response = url_response_copy(response, NULL);
    value->data = chm_foundation_data(bytes, length);
    value->user_info = user_info ? url_dictionary_copy(user_info) : NULL;
    if (!value->response || !value->data || (user_info && !value->user_info) || !cf_owned_add(self, NULL, NULL)) {
        cf_owned_release(value->response); cf_owned_release(value->data); cf_owned_release(value->user_info); free(value); return NULL;
    }
    cf_owned_find(self)->destroy = cached_destroy;
    value->object = self;
    value->policy = policy;
    value->next = cached_responses;
    cached_responses = value;
    return self;
}

static void *cached_init_simple(void *self, const char *selector, void *response, void *data)
{
    return cached_init(self, selector, response, data, NULL, 0);
}

static void *cached_property(void *self, const char *selector)
{
    cached_response *value = cached_find(self);
    if (!value) return NULL;
    if (!strcmp(selector, "data")) return value->data;
    if (!strcmp(selector, "response")) return value->response;
    return value->user_info;
}

static unsigned long cached_policy(void *self, const char *selector)
{
    (void)selector;
    cached_response *value = cached_find(self);
    return value ? value->policy : 2;
}

static void *cached_copy(void *self, const char *selector, ...)
{
    (void)selector;
    return cached_find(self) ? cf_owned_retain(self) : NULL;
}

static void cache_entry_dispose(cache_entry *entry)
{
    cf_owned_release(entry->value);
    free(entry->key);
    free(entry);
}

static void cache_destroy(void *object)
{
    url_cache **link = &url_caches;
    while (*link && (*link)->object != object) link = &(*link)->next;
    if (!*link) return;
    url_cache *value = *link;
    *link = value->next;
    while (value->entries) {
        cache_entry *entry = value->entries;
        value->entries = entry->next;
        cache_entry_dispose(entry);
    }
    if (value->directory >= 0) close(value->directory);
    free(value->disk_path);
    if (shared_url_cache == object) shared_url_cache = NULL;
    free(value);
}

static void *cache_init(void *self, const char *selector, size_t memory, size_t disk, void *path)
{
    (void)selector;
    if (!self || !fo_isa_is(self, "NSURLCache") || cache_find(self) || (path && !fo_is_string(path) && !fo_is_cfstring(path))) return NULL;
    url_cache *value = calloc(1, sizeof(*value));
    if (!value) return NULL;
    value->directory = -1;
    char guest[2048], mapped[2048];
    const char *requested = path ? fo_bytes(path) : "ChimeraURLCache";
    int n = requested[0] == '/' ? snprintf(guest, sizeof(guest), "%s", requested) :
        snprintf(guest, sizeof(guest), "/var/mobile/Library/Caches/%s", requested);
    if (n < 0 || (size_t)n >= sizeof(guest)) { free(value); return NULL; }
    value->disk_path = strdup(guest);
    if (!value->disk_path) { free(value); return NULL; }
    if (disk && chm_foundation_map_mutable(guest, mapped, sizeof(mapped)) && !mkdir_p(mapped))
        value->directory = open(mapped, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (!cf_owned_add(self, NULL, NULL)) { if (value->directory >= 0) close(value->directory); free(value->disk_path); free(value); return NULL; }
    cf_owned_find(self)->destroy = cache_destroy;
    value->object = self;
    value->memory_capacity = memory;
    value->disk_capacity = disk;
    value->next = url_caches;
    url_caches = value;
    return self;
}

static void *cache_init_default(void *self, const char *selector)
{
    return cache_init(self, selector, 4 * 1024 * 1024, 20 * 1024 * 1024, NULL);
}

static void *cache_shared(void *cls, const char *selector)
{
    (void)cls;
    if (!shared_url_cache) shared_url_cache = cache_init_default(fo_alloc("NSURLCache", 0), selector);
    return shared_url_cache;
}

static void cache_set_shared(void *cls, const char *selector, void *object)
{
    (void)cls; (void)selector;
    if (object && !cache_find(object)) return;
    void *old = shared_url_cache;
    shared_url_cache = cf_owned_retain(object);
    cf_owned_release(old);
}

static const char *cache_request_key(void *object)
{
    if (!fo_isa_is(object, "NSURLRequest")) return NULL;
    net_object *request = net_find(object), *url = request ? net_find(request->url) : NULL;
    return url && url->address && *url->address ? url->address : NULL;
}

static void cache_filename(const char *key, char name[80])
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)key, strlen(key), digest);
    memcpy(name, "chm-url-", 8);
    for (size_t i = 0; i < sizeof(digest); i++) snprintf(name + 8 + i * 2, 3, "%02x", digest[i]);
    memcpy(name + 72, ".cache", 7);
}

static int cache_is_file(const char *name)
{
    if (strlen(name) != 78 || strncmp(name, "chm-url-", 8) || strcmp(name + 72, ".cache")) return 0;
    for (size_t i = 8; i < 72; i++) if (!isxdigit((unsigned char)name[i])) return 0;
    return 1;
}

static size_t cache_disk_usage(url_cache *cache, char oldest[80], const char *skip)
{
    if (oldest) oldest[0] = 0;
    int fd = cache->directory < 0 ? -1 : openat(cache->directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *directory = fd < 0 ? NULL : fdopendir(fd);
    if (!directory) { if (fd >= 0) close(fd); return 0; }
    size_t total = 0;
    struct timespec age = {0};
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!cache_is_file(entry->d_name) || (skip && !strcmp(skip, entry->d_name))) continue;
        struct stat info;
        if (fstatat(cache->directory, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) || !S_ISREG(info.st_mode) || info.st_size < 0) continue;
        if ((uint64_t)info.st_size > SIZE_MAX - total) total = SIZE_MAX;
        else total += (size_t)info.st_size;
        if (oldest && (!*oldest || info.st_mtim.tv_sec < age.tv_sec ||
            (info.st_mtim.tv_sec == age.tv_sec && info.st_mtim.tv_nsec < age.tv_nsec))) {
            strcpy(oldest, entry->d_name);
            age = info.st_mtim;
        }
    }
    closedir(directory);
    return total;
}

static void cache_record_destroy(void *object)
{
    fo_dict *record = object;
    for (int64_t i = 0; i < record->count; i++) {
        url_value_release(record->keys[i]);
        url_value_release(record->values[i]);
    }
    free(record->keys);
    free(record->values);
}

static int cache_record_put(fo_dict *record, const char *name, void *value)
{
    if (!value) return 0;
    if (!cf_owned_find(value)) {
        if (fo_is_array(value) || fo_is_dict(value)) {
            if (!url_value_adopt(value, 0)) { url_value_dispose(value); return 0; }
        } else {
            url_value_own(value);
            if (!cf_owned_find(value)) { url_value_dispose(value); return 0; }
        }
    }
    void *key = fo_string_new(name);
    if (!key) { cf_owned_release(value); return 0; }
    int ok = d_put(record, key, value);
    free(((fo_string *)key)->bytes);
    free(key);
    if (!ok) cf_owned_release(value);
    return ok;
}

static fo_data *cache_serialize(const char *key, cached_response *value)
{
    net_object *response = net_find(value->response), *url = response ? net_find(response->url) : NULL;
    if (!url || !url->address) return NULL;
    fo_dict *record = fo_dict_new();
    if (!record) return NULL;
    if (!cf_owned_add(record, NULL, NULL)) { collection_dispose(record); return NULL; }
    cf_owned_find(record)->destroy = cache_record_destroy;
    int ok = cache_record_put(record, "version", n_int(NULL, NULL, 1)) &&
        cache_record_put(record, "key", fo_string_new(key)) &&
        cache_record_put(record, "url", fo_string_new(url->address)) &&
        cache_record_put(record, "status", n_int(NULL, NULL, response->status)) &&
        cache_record_put(record, "length", n_signed(NULL, NULL, response->expected)) &&
        cache_record_put(record, "data", fo_data_new(((fo_data *)value->data)->bytes, ((fo_data *)value->data)->length));
    if (response->headers) ok = ok && cache_record_put(record, "headers", url_dictionary_copy(response->headers));
    if (response->address) ok = ok && cache_record_put(record, "mime", fo_string_new(response->address));
    if (response->method) ok = ok && cache_record_put(record, "encoding", fo_string_new(response->method));
    if (value->user_info) ok = ok && cache_record_put(record, "info", url_dictionary_copy(value->user_info));
    fo_data *result = ok ? bplist_write(record) : NULL;
    cf_owned_release(record);
    return result;
}

static void *cache_record_get(fo_dict *record, const char *name)
{
    for (int64_t i = 0; i < record->count; i++) if (!strcmp(fo_bytes(record->keys[i]), name)) return record->values[i];
    return NULL;
}

static void *cache_deserialize(fo_data *data, const char *key)
{
    void *parsed = plist_read_data(data);
    if (!fo_is_dict(parsed)) return NULL;
    fo_dict *record = url_value_adopt(parsed, 0);
    if (!record) return NULL;
    void *result = NULL, *response = NULL;
    void *version = cache_record_get(record, "version"), *stored_key = cache_record_get(record, "key");
    void *body = cache_record_get(record, "data"), *address = cache_record_get(record, "url");
    void *status = cache_record_get(record, "status"), *length = cache_record_get(record, "length");
    void *headers = cache_record_get(record, "headers"), *info = cache_record_get(record, "info");
    if (!fo_is_number(version) || o_integer(version) != 1 || !fo_is_string(stored_key) || strcmp(fo_bytes(stored_key), key) ||
        !fo_is_data(body) || !fo_is_string(address) || !fo_is_number(status) || !fo_is_number(length) ||
        (headers && !fo_is_dict(headers)) || (info && !fo_is_dict(info))) goto done;
    void *url = url_new(NULL, "URLWithString:", address);
    if (!url) goto done;
    long code = o_integer(status);
    if (code) response = url_response_http(fo_alloc("NSHTTPURLResponse", 0), NULL, url, code, NULL, headers);
    else response = url_response_basic(fo_alloc("NSURLResponse", 0), NULL, url, cache_record_get(record, "mime"), o_integer(length), cache_record_get(record, "encoding"));
    if (response) result = cached_init(fo_alloc("NSCachedURLResponse", 0), NULL, response, body, info, 0);
 done:
    cf_owned_release(response);
    cf_owned_release(record);
    return result;
}

static void cache_disk_store(url_cache *cache, const char *key, cached_response *value)
{
    if (cache->directory < 0 || !cache->disk_capacity || value->policy) return;
    fo_data *data = cache_serialize(key, value);
    if (!data) return;
    if (data->length <= 0 || (uint64_t)data->length > cache->disk_capacity || data->length > 32 * 1024 * 1024) goto done;
    char name[80], temporary[112], oldest[80];
    cache_filename(key, name);
    if (flock(cache->directory, LOCK_EX)) goto done;
    for (unsigned attempt = 0;; attempt++) {
        size_t used = cache_disk_usage(cache, oldest, name);
        if (used <= cache->disk_capacity - (size_t)data->length) break;
        if (attempt == 4096 || !*oldest || unlinkat(cache->directory, oldest, 0)) { flock(cache->directory, LOCK_UN); goto done; }
    }
    snprintf(temporary, sizeof(temporary), ".%s-%ld-%llu", name, (long)getpid(), (unsigned long long)++cache->serial);
    int fd = openat(cache->directory, temporary, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        size_t offset = 0;
        while (offset < (size_t)data->length) {
            ssize_t n = write(fd, data->bytes + offset, (size_t)data->length - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            offset += (size_t)n;
        }
        int ok = offset == (size_t)data->length && !fsync(fd);
        if (close(fd)) ok = 0;
        if (ok) ok = !renameat(cache->directory, temporary, cache->directory, name);
        if (!ok) unlinkat(cache->directory, temporary, 0);
    }
    flock(cache->directory, LOCK_UN);
 done:
    free(data->bytes); free(data);
}

static void *cache_disk_read(url_cache *cache, const char *key)
{
    if (cache->directory < 0) return NULL;
    char name[80]; cache_filename(key, name);
    int fd = openat(cache->directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > 32 * 1024 * 1024) { close(fd); return NULL; }
    fo_data data = {.magic = FO_DATA_MAGIC, .length = info.st_size, .bytes = malloc((size_t)info.st_size)};
    if (!data.bytes) { close(fd); return NULL; }
    size_t offset = 0;
    while (offset < (size_t)data.length) {
        ssize_t n = read(fd, data.bytes + offset, (size_t)data.length - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        offset += (size_t)n;
    }
    close(fd);
    void *result = offset == (size_t)data.length ? cache_deserialize(&data, key) : NULL;
    free(data.bytes);
    return result;
}

static void cache_memory_remove(url_cache *cache, const char *key)
{
    cache_entry **link = &cache->entries;
    while (*link) {
        cache_entry *entry = *link;
        if (key && strcmp(entry->key, key)) { link = &entry->next; continue; }
        *link = entry->next;
        cache->memory_used -= entry->cost;
        cache_entry_dispose(entry);
        if (key) return;
    }
}

static void cache_store(void *self, const char *selector, void *object, void *request)
{
    (void)selector;
    url_cache *cache = cache_find(self);
    cached_response *value = cached_find(object);
    const char *key = cache_request_key(request);
    if (!cache || !value || !key || value->policy == 2) return;
    cf_owned_retain(object);
    cache_memory_remove(cache, key);
    if (cache->directory >= 0) { char name[80]; cache_filename(key, name); unlinkat(cache->directory, name, 0); }
    size_t cost = (size_t)((fo_data *)value->data)->length;
    if (cache->memory_capacity && cost <= cache->memory_capacity) {
        while (cache->entries && cache->memory_used > cache->memory_capacity - cost) {
            cache_entry *oldest = cache->entries;
            for (cache_entry *entry = cache->entries; entry; entry = entry->next) if (entry->used < oldest->used) oldest = entry;
            cache_memory_remove(cache, oldest->key);
        }
        cache_entry *entry = calloc(1, sizeof(*entry));
        if (entry) {
            entry->key = strdup(key);
            if (!entry->key) free(entry);
            else {
                entry->value = cf_owned_retain(object); entry->cost = cost; entry->used = ++cache->serial;
                entry->next = cache->entries; cache->entries = entry; cache->memory_used += cost;
            }
        }
    }
    cache_disk_store(cache, key, value);
    cf_owned_release(object);
}

static void *cache_get(void *self, const char *selector, void *request)
{
    (void)selector;
    url_cache *cache = cache_find(self);
    const char *key = cache_request_key(request);
    if (!cache || !key) return NULL;
    for (cache_entry *entry = cache->entries; entry; entry = entry->next) {
        if (strcmp(entry->key, key)) continue;
        entry->used = ++cache->serial;
        return entry->value;
    }
    void *result = cache_disk_read(cache, key);
    if (result && cache->memory_capacity) {
        cached_response *value = cached_find(result);
        size_t cost = value ? (size_t)((fo_data *)value->data)->length : SIZE_MAX;
        if (value && cost <= cache->memory_capacity) {
            while (cache->entries && cache->memory_used > cache->memory_capacity - cost) {
                cache_entry *oldest = cache->entries;
                for (cache_entry *entry = cache->entries; entry; entry = entry->next) if (entry->used < oldest->used) oldest = entry;
                cache_memory_remove(cache, oldest->key);
            }
            cache_entry *entry = calloc(1, sizeof(*entry));
            if (entry && (entry->key = strdup(key))) {
                entry->value = cf_owned_retain(result);
                entry->cost = cost;
                entry->used = ++cache->serial;
                entry->next = cache->entries;
                cache->entries = entry;
                cache->memory_used += cost;
            } else {
                free(entry ? entry->key : NULL);
                free(entry);
            }
        }
    }
    return result;
}

static void cache_remove(void *self, const char *selector, void *request)
{
    (void)selector;
    url_cache *cache = cache_find(self);
    const char *key = cache_request_key(request);
    if (!cache || !key) return;
    cache_memory_remove(cache, key);
    if (cache->directory >= 0) { char name[80]; cache_filename(key, name); unlinkat(cache->directory, name, 0); }
}

static void cache_remove_all(void *self, const char *selector)
{
    (void)selector;
    url_cache *cache = cache_find(self);
    if (!cache) return;
    cache_memory_remove(cache, NULL);
    char name[80];
    for (unsigned i = 0; i < 65536; i++) {
        cache_disk_usage(cache, name, NULL);
        if (!*name || unlinkat(cache->directory, name, 0)) break;
    }
}

static size_t cache_size(void *self, const char *selector)
{
    url_cache *cache = cache_find(self);
    if (!cache) return 0;
    if (!strcmp(selector, "memoryCapacity")) return cache->memory_capacity;
    if (!strcmp(selector, "diskCapacity")) return cache->disk_capacity;
    if (!strcmp(selector, "currentMemoryUsage")) return cache->memory_used;
    return cache_disk_usage(cache, NULL, NULL);
}

static void cache_set_size(void *self, const char *selector, size_t size)
{
    url_cache *cache = cache_find(self);
    if (!cache) return;
    if (!strcmp(selector, "setMemoryCapacity:")) {
        cache->memory_capacity = size;
    } else {
        cache->disk_capacity = size;
        if (size && cache->directory < 0 && cache->disk_path) {
            char mapped[2048];
            if (chm_foundation_map_mutable(cache->disk_path, mapped, sizeof(mapped)) && !mkdir_p(mapped))
                cache->directory = open(mapped, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        if (cache->directory >= 0) {
            char oldest[80];
            for (unsigned attempt = 0; attempt < 65536; attempt++) {
                if (cache_disk_usage(cache, oldest, NULL) <= cache->disk_capacity || !*oldest || unlinkat(cache->directory, oldest, 0)) break;
            }
        }
    }
}

static const chm_host_method cache_methods[] = {
    {"NSCachedURLResponse", "initWithResponse:data:", "@@:@@", cached_init_simple, 0},
    {"NSCachedURLResponse", "initWithResponse:data:userInfo:storagePolicy:", "@@:@@@Q", cached_init, 0},
    {"NSCachedURLResponse", "response", "@@:", cached_property, 0},
    {"NSCachedURLResponse", "data", "@@:", cached_property, 0},
    {"NSCachedURLResponse", "userInfo", "@@:", cached_property, 0},
    {"NSCachedURLResponse", "storagePolicy", "Q@:", cached_policy, 0},
    {"NSCachedURLResponse", "copy", "@@:", cached_copy, 0},
    {"NSCachedURLResponse", "copyWithZone:", "@@:^v", cached_copy, 0},
    {"NSURLCache", "init", "@@:", cache_init_default, 0},
    {"NSURLCache", "initWithMemoryCapacity:diskCapacity:diskPath:", "@@:QQ@", cache_init, 0},
    {"NSURLCache", "sharedURLCache", "@@:", cache_shared, 1},
    {"NSURLCache", "setSharedURLCache:", "v@:@", cache_set_shared, 1},
    {"NSURLCache", "storeCachedResponse:forRequest:", "v@:@@", cache_store, 0},
    {"NSURLCache", "cachedResponseForRequest:", "@@:@", cache_get, 0},
    {"NSURLCache", "removeCachedResponseForRequest:", "v@:@", cache_remove, 0},
    {"NSURLCache", "removeAllCachedResponses", "v@:", cache_remove_all, 0},
    {"NSURLCache", "memoryCapacity", "Q@:", cache_size, 0},
    {"NSURLCache", "diskCapacity", "Q@:", cache_size, 0},
    {"NSURLCache", "currentMemoryUsage", "Q@:", cache_size, 0},
    {"NSURLCache", "currentDiskUsage", "Q@:", cache_size, 0},
    {"NSURLCache", "setMemoryCapacity:", "v@:Q", cache_set_size, 0},
    {"NSURLCache", "setDiskCapacity:", "v@:Q", cache_set_size, 0}
};
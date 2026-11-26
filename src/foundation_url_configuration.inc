static void url_configuration_destroy(void *object)
{
    net_object **link = &net_objects;
    while (*link && (*link)->object != object) link = &(*link)->next;
    if (!*link) return;
    net_object *value = *link;
    *link = value->next;
    cf_owned_release(value->url_cache);
    free(value);
}

static void *url_configuration_new(void *cls, const char *selector)
{
    (void)cls;
    net_object *value = net_new("NSURLSessionConfiguration");
    if (!value) return NULL;
    if (!cf_owned_add(value->object, NULL, NULL)) return NULL;
    cf_owned_find(value->object)->destroy = url_configuration_destroy;
    value->timeout = 60;
    value->cookies = value->cellular = 1;
    if (!strcmp(selector, "ephemeralSessionConfiguration"))
        value->url_cache = cache_init(fo_alloc("NSURLCache", 0), NULL, 4 * 1024 * 1024, 0, NULL);
    else value->url_cache = cf_owned_retain(cache_shared(NULL, NULL));
    return value->object;
}

static void url_configuration_cache(void *self, const char *selector, void *object)
{
    (void)selector;
    net_object *value = net_find(self);
    if (!value || (object && !cache_find(object))) return;
    void *old = value->url_cache;
    value->url_cache = cf_owned_retain(object);
    cf_owned_release(old);
}

static void *url_configuration_get_cache(void *self, const char *selector)
{
    (void)selector;
    net_object *value = net_find(self);
    return value ? value->url_cache : NULL;
}

static void *url_session_configuration(void *self, const char *selector)
{
    (void)selector;
    net_object *value = net_find(self);
    return value ? value->configuration : NULL;
}

static void *url_session_cache(void *self, const char *selector)
{
    (void)selector;
    net_object *value = net_find(self);
    return value ? value->url_cache : NULL;
}

static unsigned long url_configuration_policy(void *self, const char *selector)
{
    (void)selector;
    net_object *value = net_find(self);
    return value ? value->cache_policy : 0;
}

static void url_configuration_set_policy(void *self, const char *selector, unsigned long policy)
{
    (void)selector;
    net_object *value = net_find(self);
    if (value && policy <= 5) value->cache_policy = policy;
}

static void url_configuration_set_flag(void *self, const char *selector, signed char flag)
{
    net_object *value = net_find(self);
    if (!value) return;
    if (!strcmp(selector, "setHTTPShouldSetCookies:")) value->cookies = !!flag;
    else if (!strcmp(selector, "setHTTPShouldUsePipelining:")) value->pipelining = !!flag;
    else value->cellular = !!flag;
}

static signed char url_configuration_flag(void *self, const char *selector)
{
    net_object *value = net_find(self);
    if (!value) return 0;
    if (!strcmp(selector, "HTTPShouldSetCookies")) return value->cookies;
    if (!strcmp(selector, "HTTPShouldUsePipelining")) return value->pipelining;
    return value->cellular;
}

static double url_configuration_timeout(void *self, const char *selector)
{
    (void)selector;
    net_object *value = net_find(self);
    return value ? value->timeout : 0;
}

static void *url_configuration_copy(void *self, const char *selector, ...)
{
    (void)selector;
    net_object *source = net_find(self);
    if (!source) return NULL;
    void *result = url_configuration_new(NULL, "defaultSessionConfiguration");
    net_object *copy = net_find(result);
    if (!copy) return NULL;
    copy->timeout = source->timeout;
    copy->custom_timeout = source->custom_timeout;
    copy->cache_policy = source->cache_policy;
    copy->cookies = source->cookies;
    copy->pipelining = source->pipelining;
    copy->cellular = source->cellular;
    url_configuration_cache(result, NULL, source->url_cache);
    return result;
}

static const chm_host_method url_configuration_methods[] = {
    {"NSURLSessionConfiguration", "defaultSessionConfiguration", "@@:", url_configuration_new, 1},
    {"NSURLSessionConfiguration", "ephemeralSessionConfiguration", "@@:", url_configuration_new, 1},
    {"NSURLSessionConfiguration", "URLCache", "@@:", url_configuration_get_cache, 0},
    {"NSURLSession", "configuration", "@@:", url_session_configuration, 0},
    {"NSURLSession", "URLCache", "@@:", url_session_cache, 0},
    {"NSURLSession", "sessionWithConfiguration:", "@@:@", session_configured, 1},
    {"NSURLSessionConfiguration", "setURLCache:", "v@:@", url_configuration_cache, 0},
    {"NSURLSessionConfiguration", "requestCachePolicy", "Q@:", url_configuration_policy, 0},
    {"NSURLSessionConfiguration", "setRequestCachePolicy:", "v@:Q", url_configuration_set_policy, 0},
    {"NSURLSessionConfiguration", "HTTPShouldSetCookies", "c@:", url_configuration_flag, 0},
    {"NSURLSessionConfiguration", "HTTPShouldUsePipelining", "c@:", url_configuration_flag, 0},
    {"NSURLSessionConfiguration", "allowsCellularAccess", "c@:", url_configuration_flag, 0},
    {"NSURLSessionConfiguration", "setHTTPShouldSetCookies:", "v@:c", url_configuration_set_flag, 0},
    {"NSURLSessionConfiguration", "setHTTPShouldUsePipelining:", "v@:c", url_configuration_set_flag, 0},
    {"NSURLSessionConfiguration", "setAllowsCellularAccess:", "v@:c", url_configuration_set_flag, 0},
    {"NSURLSessionConfiguration", "timeoutIntervalForRequest", "d@:", url_configuration_timeout, 0},
    {"NSURLSessionConfiguration", "copy", "@@:", url_configuration_copy, 0},
    {"NSURLSessionConfiguration", "copyWithZone:", "@@:^v", url_configuration_copy, 0}
};
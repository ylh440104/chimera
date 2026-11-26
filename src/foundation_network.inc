typedef struct net_object {
    void *object, *url, *body, *headers, *handler;
    void *progress, *update, *destination, *manager, *response, *url_cache, *configuration;
    unsigned long cache_policy;
    int cookies, pipelining, cellular;
    char *temporary;
    int64_t expected;
    char *address, *method, *header_json;
    double timeout;
    uint64_t token;
    int state, status, cancelled, download, custom_timeout;
    struct net_object *next;
} net_object;
static net_object *net_objects;
static uint64_t network_serial;
static void *url_configuration_copy(void *self, const char *selector, ...);

static net_object *net_find(void *object)
{
    for (net_object *n = net_objects; n; n = n->next) if (n->object == object) return n;
    return NULL;
}
static net_object *net_new(const char *cls)
{
    net_object *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->object = fo_alloc(cls, 0); n->timeout = 30;
    if (!n->object) { free(n); return NULL; }
    n->next = net_objects; net_objects = n; return n;
}
static void *url_new(void *cls, const char *sel, void *string)
{
    (void)cls;
    const char *text = fo_bytes(string);
    if (!*text) return NULL;
    net_object *n = net_new("NSURL"); if (!n) return NULL;
    if (!strcmp(sel, "fileURLWithPath:")) {
        size_t size = strlen(text) + 8; n->address = malloc(size);
        if (n->address) snprintf(n->address, size, "file://%s", text);
    } else n->address = strdup(text);
    return n->address ? n->object : NULL;
}
static void *url_string(void *self, const char *sel)
{
    net_object *n = net_find(self);
    if (!n) return s_identity(self);
    const char *address = n->address;
    if (!address && n->url) return url_string(n->url, sel);
    if (!address) return NULL;
    if (!strcmp(sel, "path") && !strncmp(address, "file://", 7)) address += 7;
    return fo_string_new(address);
}
static void *request_new(void *cls, const char *sel, void *url)
{
    (void)cls; (void)sel;
    net_object *n = net_new("NSMutableURLRequest"); if (!n) return NULL;
    n->url = url; n->method = strdup("GET"); n->headers = fo_dict_new(); return n->object;
}
static void request_set(void *self, const char *sel, void *value)
{
    net_object *n = net_find(self); if (!n) return;
    if (!strcmp(sel, "setURL:")) n->url = value;
    else if (!strcmp(sel, "setHTTPBody:")) n->body = value;
    else if (!strcmp(sel, "setAllHTTPHeaderFields:")) n->headers = value;
    else { char *m = strdup(fo_bytes(value)); if (m) { free(n->method); n->method = m; } }
}
static void *request_get(void *self, const char *sel)
{
    net_object *n = net_find(self); if (!n) return NULL;
    if (!strcmp(sel, "URL")) return n->url;
    if (!strcmp(sel, "HTTPBody")) return n->body;
    if (!strcmp(sel, "allHTTPHeaderFields") || !strcmp(sel, "allHeaderFields")) return n->headers;
    return fo_string_new(n->method ? n->method : "GET");
}
static void request_header(void *self, const char *sel, void *value, void *field)
{
    (void)sel; net_object *n = net_find(self); if (n && fo_is_dict(n->headers)) d_set_subscript(n->headers, NULL, value, field);
}
static void request_timeout(void *self, const char *sel, double timeout)
{
    (void)sel; net_object *n = net_find(self);
    if (n && isfinite(timeout) && timeout > 0) { n->timeout = fmin(timeout, 300); n->custom_timeout = 1; }
}
static void *session_shared(void) { return singleton("__url_session", "NSURLSession"); }
static void *session_configuration(void)
{
    net_object *n = net_new("NSURLSessionConfiguration");
    return n ? n->object : NULL;
}
static void *session_configured(void *cls, const char *sel, void *config)
{
    (void)cls; (void)sel;
    net_object *s = net_new("NSURLSession"), *c = net_find(config);
    if (s && c) {
        s->configuration = url_configuration_copy(config, "copy");
        s->timeout = c->timeout;
        s->custom_timeout = c->custom_timeout;
        s->cache_policy = c->cache_policy;
        s->url_cache = cf_owned_retain(c->url_cache);
        s->cookies = c->cookies;
        s->pipelining = c->pipelining;
        s->cellular = c->cellular;
    }
    return s ? s->object : NULL;
}
static void *session_task(void *self, const char *sel, void *request, void *handler)
{
    (void)self;
    net_object *r = net_find(request), *n = net_new("NSURLSessionTask");
    if (!n) return NULL;
    n->token = ++network_serial; n->handler = chm_block_copy(handler);
    net_object *session = net_find(self);
    n->url_cache = cf_owned_retain(session ? session->url_cache : NULL);
    n->cache_policy = session ? session->cache_policy : 0;
    n->url = !strcmp(sel, "dataTaskWithURL:completionHandler:") ? request : r ? r->url : NULL;
    net_object *url = net_find(n->url);
    n->address = strdup(url && url->address ? url->address : "");
    n->method = strdup(r && r->method ? r->method : "GET");
    n->body = r ? r->body : NULL; n->timeout = r ? r->timeout : 30;
    n->headers = r && r->headers ? o_copy(r->headers, "copy") : fo_dict_new();
    n->state = 1;
    return n->object;
}
static void task_resume(void *self, const char *sel)
{
    (void)sel; net_object *n = net_find(self); if (n && n->state == 1) n->state = 0;
}
static void task_cancel(void *self, const char *sel)
{
    (void)sel; net_object *n = net_find(self);
    if (n && n->state != 3) {
        n->cancelled = 1;
        if (n->state != 4) n->state = 2;
    }
}
static long task_state(void *self, const char *sel)
{
    net_object *n = net_find(self); if (!n) return 0;
    return !strcmp(sel, "statusCode") ? n->status : !strcmp(sel, "taskIdentifier") ? (long)n->token : n->state == 4 ? 0 : n->state;
}

static int download_prepare(net_object *n);
static void download_discard(net_object *n);
static int download_finish(net_object *n, int status, const char *headers, const char *failure);

int chm_network_next(chm_network_request *request)
{
    if (!request) return 0;
    for (net_object *n = net_objects; n; n = n->next) {
        if (!n->token || (n->state != 0 && n->state != 2)) continue;
        int cancelled = n->state == 2;
        n->state = 4;
        fo_buffer b = {0};
        int fd = n->download && !cancelled ? download_prepare(n) : -1;
        if (n->download && !cancelled && fd < 0)
            chm_session_log(g_session, "download: temporary file unavailable errno=%d", errno);
        if (!json_encode(&b, n->headers, 0)) { free(b.data); b.data = strdup("{}"); }
        free(n->header_json); n->header_json = b.data;
        *request = (chm_network_request){n->token, n->address, n->method, n->header_json,
            fo_is_data(n->body) ? ((fo_data *)n->body)->bytes : NULL,
            fo_is_data(n->body) ? (size_t)((fo_data *)n->body)->length : 0,
            (int)(n->timeout * 1000), cancelled, !!n->download, fd};
        if (n->download) chm_session_log(g_session, "download: token=%llu %s",
            (unsigned long long)n->token, cancelled ? "cancelled" : "submitted");
        return 1;
    }
    return 0;
}

static int network_deliver(uint64_t token, int status, const void *bytes, size_t length, const char *headers, const char *failure)
{
    net_object *n = net_objects;
    while (n && n->token != token) n = n->next;
    if (!n || n->state == 3 || length > 32u * 1024u * 1024u) return 0;
    if (n->download) return download_finish(n, status, headers, failure);
    int cancelled = n->cancelled;
    n->state = 3;
    void *error = cancelled || (failure && *failure) ? make_error("NSURLErrorDomain", cancelled ? -999 : -1004, cancelled ? "Request cancelled" : failure) : NULL;
    void *data = !error ? fo_data_new(bytes, (int64_t)length) : NULL;
    net_object *response = !error ? net_new("NSHTTPURLResponse") : NULL;
    if (response) {
        response->status = status; response->url = n->url;
        response->headers = json_read(NULL, NULL, fo_data_new(headers, (int64_t)strlen(headers)), 0, NULL);
    }
    void *handler = n->handler; n->handler = NULL;
    struct { void *isa; int flags, reserved; void (*invoke)(void *, void *, void *, void *); } *block = handler;
    if (block && block->invoke) block->invoke(block, data, response ? response->object : NULL, error);
    chm_block_release(handler);
    return 1;
}
typedef struct { uint64_t token; int status; const void *bytes; size_t length; const char *headers, *failure; } network_result;
static int network_body(void *context)
{
    network_result *r = context;
    return network_deliver(r->token, r->status, r->bytes, r->length, r->headers, r->failure);
}
int chm_network_complete(uint64_t token, int status, const void *bytes, size_t length, const char *headers, const char *failure)
{
    if (!g_session || g_session->exit_code == -2) return -1;
    network_result result = {token, status, bytes, length, headers ? headers : "{}", failure};
    int rc = chm_session_callback(g_session, network_body, &result);
    if (rc == -2) g_session->exit_code = -2;
    return rc;
}
static void network_shutdown(void)
{
    while (net_objects) {
        net_object *value = net_objects;
        net_objects = value->next;
        download_discard(value);
        chm_block_release(value->update); chm_block_release(value->destination);
        chm_block_release(value->handler);
        cf_owned_release(value->configuration);
        cf_owned_release(value->url_cache);
        cf_owned_release(value->headers);
        cf_owned_release(value->response);
        free(value->address); free(value->method); free(value->header_json); free(value);
    }
}
typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, void *, void *, void *);
} tasks_block;

static void *session_configured_delegate(void *cls, const char *sel, void *config, void *delegate, void *queue)
{
    (void)cls;
    (void)sel;
    (void)queue;
    void *object = session_configured(NULL, "sessionWithConfiguration:", config);
    net_object *session = net_find(object);
    if (session) session->delegate = delegate;
    return object;
}

static void *session_delegate_object(void *self, const char *sel)
{
    (void)sel;
    net_object *session = net_find(self);
    return session ? session->delegate : NULL;
}

static void *session_data_task(void *self, const char *sel, void *request)
{
    net_object *session = net_find(self);
    int direct_url = !strcmp(sel, "dataTaskWithURL:");
    net_object *r = net_find(request);
    net_object *n = net_new("NSURLSessionDataTask");
    if (!n) return NULL;
    n->token = ++network_serial;
    n->session = self;
    n->delegate = session ? session->delegate : NULL;
    n->request = direct_url ? NULL : request;
    n->url = direct_url ? request : r ? r->url : NULL;
    net_object *url = net_find(n->url);
    n->address = strdup(url && url->address ? url->address : "");
    n->method = strdup(direct_url ? "GET" : r && r->method ? r->method : "GET");
    n->body = direct_url ? NULL : r ? r->body : NULL;
    n->timeout = direct_url ? 30 : r ? r->timeout : 30;
    n->headers = !direct_url && r && r->headers ? o_copy(r->headers, "copy") : fo_dict_new();
    n->url_cache = cf_owned_retain(session ? session->url_cache : NULL);
    n->cache_policy = session ? session->cache_policy : 0;
    n->state = 1;
    return n->object;
}

static void task_set_description(void *self, const char *sel, void *text)
{
    (void)sel;
    (void)text;
    net_object *n = net_find(self);
    if (!n) return;
}

static void *task_description(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return fo_string_new("");
}

static void *task_original_request(void *self, const char *sel)
{
    (void)sel;
    net_object *n = net_find(self);
    return n ? n->request : NULL;
}

static void *task_error_object(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return NULL;
}

static long task_byte_count(void *self, const char *sel)
{
    (void)self;
    if (!strcmp(sel, "countOfBytesExpectedToReceive") || !strcmp(sel, "countOfBytesExpectedToSend")) return -1;
    return 0;
}

static void session_invalidate(void *self, const char *sel, ...)
{
    (void)sel;
    net_object *session = net_find(self);
    if (!session) return;
    for (net_object *n = net_objects; n; n = n->next)
        if (n->session == self && n->token && n->state != 3) n->cancelled = 1;
}

static void session_get_tasks(void *self, const char *sel, void *handler)
{
    (void)self;
    (void)sel;
    tasks_block *block = handler;
    if (!block || !block->invoke) return;
    block->invoke(block, fo_array_new(), fo_array_new(), fo_array_new());
}

static const chm_host_method session_methods[] = {
    {"NSURLSession", "sessionWithConfiguration:delegate:delegateQueue:", "@@:@@@", session_configured_delegate, 1},
    {"NSURLSession", "delegate", "@@:", session_delegate_object, 0},
    {"NSURLSession", "dataTaskWithRequest:", "@@:@", session_data_task, 0},
    {"NSURLSession", "dataTaskWithURL:", "@@:@", session_data_task, 0},
    {"NSURLSession", "invalidateAndCancel", "v@:", session_invalidate, 0},
    {"NSURLSession", "finishTasksAndInvalidate", "v@:", session_invalidate, 0},
    {"NSURLSession", "getTasksWithCompletionHandler:", "v@:@?", session_get_tasks, 0},
    {"NSURLSessionTask", "taskDescription", "@@:", task_description, 0},
    {"NSURLSessionTask", "setTaskDescription:", "v@:@", task_set_description, 0},
    {"NSURLSessionTask", "originalRequest", "@@:", task_original_request, 0},
    {"NSURLSessionTask", "currentRequest", "@@:", task_original_request, 0},
    {"NSURLSessionTask", "error", "@@:", task_error_object, 0},
    {"NSURLSessionTask", "countOfBytesReceived", "q@:", task_byte_count, 0},
    {"NSURLSessionTask", "countOfBytesSent", "q@:", task_byte_count, 0},
    {"NSURLSessionTask", "countOfBytesExpectedToReceive", "q@:", task_byte_count, 0},
    {"NSURLSessionTask", "countOfBytesExpectedToSend", "q@:", task_byte_count, 0},
    {"NSURLSessionTask", "suspend", "v@:", task_cancel, 0}
};
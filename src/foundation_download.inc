typedef struct { void *isa; int flags, reserved; void (*invoke)(void *, void *); } progress_block;
typedef struct { void *isa; int flags, reserved; void *(*invoke)(void *, void *, void *); } destination_block;
typedef struct { void *isa; int flags, reserved; void (*invoke)(void *, void *, void *, void *); } completion_block;

static void *download_manager_init(void *self, const char *sel, void *configuration)
{
    (void)sel;
    if (!self) return NULL;
    net_object *manager = net_find(self), *config = net_find(configuration);
    if (!manager) {
        manager = calloc(1, sizeof(*manager));
        if (!manager) return NULL;
        manager->object = self; manager->next = net_objects; net_objects = manager;
    }
    manager->timeout = config ? config->timeout : 30;
    return self;
}

static void *download_task_new(void *manager, void *request, int direct_url, int af, void *update, void *destination, void *completion)
{
    net_object *session = net_find(manager), *req = net_find(request);
    net_object *url = direct_url ? req : req ? net_find(req->url) : NULL;
    if (!url || !url->address || !*url->address) return NULL;
    net_object *n = net_new("NSURLSessionDownloadTask");
    if (!n) return NULL;
    n->url = url->object; n->address = strdup(url->address);
    n->method = strdup(req->method ? req->method : "GET");
    n->headers = req->headers ? o_copy(req->headers, "copy") : fo_dict_new();
    n->body = req->body; n->timeout = req->custom_timeout ? req->timeout : session ? session->timeout : 30;
    n->progress = fo_alloc("NSProgress", 0); n->manager = manager;
    n->update = chm_block_copy(update); n->destination = chm_block_copy(destination); n->handler = chm_block_copy(completion);
    if (!n->address || !n->method || !n->headers || !n->progress || (update && !n->update) ||
        (destination && !n->destination) || (completion && !n->handler)) return NULL;
    n->download = af ? 2 : 1; n->state = 1; n->token = ++network_serial;
    progress_set_units(n->progress, "setTotalUnitCount:", -1);
    return n->object;
}
static void *af_download(void *self, const char *sel, void *req, void *update, void *dest, void *complete)
{
    (void)sel; return download_task_new(self, req, 0, 1, update, dest, complete);
}
static void *session_download(void *self, const char *sel, void *req, void *complete)
{
    return download_task_new(self, req, !strcmp(sel, "downloadTaskWithURL:completionHandler:"), 0, NULL, NULL, complete);
}
static void *download_progress(void *self, const char *sel, void *task)
{
    (void)sel; net_object *n = net_find(task);
    return n && n->manager == self ? n->progress : NULL;
}
static void *task_progress(void *self, const char *sel)
{
    (void)sel; net_object *n = net_find(self); return n ? n->progress : NULL;
}
static void *task_response(void *self, const char *sel)
{
    (void)sel; net_object *n = net_find(self); return n ? n->response : NULL;
}
static int64_t response_length(void *self, const char *sel)
{
    (void)sel; net_object *n = net_find(self); return n ? n->expected : -1;
}
static void manager_invalidate(void *self, const char *sel, signed char cancel, signed char reset)
{
    (void)sel; (void)reset;
    if (cancel) for (net_object *n = net_objects; n; n = n->next) if (n->manager == self) task_cancel(n->object, "cancel");
}
static void download_discard(net_object *n)
{
    if (n->temporary) { unlink(n->temporary); free(n->temporary); n->temporary = NULL; }
}
static int download_prepare(net_object *n)
{
    char path[2048];
    if (!chm_foundation_map_mutable("/var/mobile/tmp/chm-transfer-XXXXXX", path, sizeof(path))) return -1;
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || !(n->temporary = strdup(path))) {
        int error = errno;
        close(fd); unlink(path); errno = error;
        return -1;
    }
    return fd;
}
typedef struct { uint64_t token; int64_t done, total; } download_update;
static int download_update_body(void *context)
{
    download_update *u = context;
    net_object *n = net_objects;
    while (n && n->token != u->token) n = n->next;
    if (!n || !n->download || n->state != 4 || n->cancelled || u->done < 0) return 0;
    if (progress_flag(n->progress, "isCancelled")) { task_cancel(n->object, "cancel"); return 0; }
    n->expected = u->total;
    progress_set_units(n->progress, "setTotalUnitCount:", u->total);
    progress_set_units(n->progress, "setCompletedUnitCount:", u->done);
    progress_block *block = n->update;
    if (block && block->invoke) block->invoke(block, n->progress);
    return 1;
}
int chm_network_progress(uint64_t token, int64_t done, int64_t total)
{
    if (!g_session || g_session->exit_code < 0) return -1;
    download_update update = {token, done, total};
    int rc = chm_session_callback(g_session, download_update_body, &update);
    if (rc == -2) g_session->exit_code = rc;
    return rc;
}
static int download_finish(net_object *n, int status, const char *headers, const char *failure)
{
    n->state = 3;
    int cancelled = n->cancelled || progress_flag(n->progress, "isCancelled");
    void *error = cancelled ? make_error("NSURLErrorDomain", -999, "Request cancelled") :
        failure && *failure ? make_error("NSURLErrorDomain", -1004, failure) : NULL;
    net_object *response = status > 0 ? net_new("NSHTTPURLResponse") : NULL;
    struct stat st;
    int has_file = n->temporary && !lstat(n->temporary, &st) && S_ISREG(st.st_mode);
    if (response) {
        response->status = status; response->url = n->url; response->expected = has_file ? st.st_size : n->expected;
        response->headers = json_read(NULL, NULL, fo_data_new(headers, (int64_t)strlen(headers)), 0, NULL);
        n->response = response->object;
    }
    if (!error && !has_file) error = make_error("NSCocoaErrorDomain", 4, "Missing temporary download");
    if (!error && n->download == 2 && (status < 200 || status >= 300)) error = make_error("NSURLErrorDomain", -1011, "Unsuccessful HTTP response");
    void *location = !error ? url_new(NULL, "fileURLWithPath:", fo_string_new(n->temporary)) : NULL;
    destination_block *dest = n->destination;
    if (!error && dest) {
        void *target = dest->invoke(dest, location, n->response);
        const char *path = file_url_path(target);
        char mapped[2048];
        if (!path || !chm_foundation_map_mutable(path, mapped, sizeof(mapped))) {
            error = make_error("NSCocoaErrorDomain", 512, "Invalid download destination");
        } else if (link(n->temporary, mapped)) {
            error = make_error("NSCocoaErrorDomain", errno == EEXIST ? 516 : 512, strerror(errno));
        } else {
            download_discard(n);
            location = target;
        }
    }
    if (!error) {
        if (progress_units(n->progress, "totalUnitCount") <= 0) progress_set_units(n->progress, "setTotalUnitCount:", st.st_size);
        progress_set_units(n->progress, "setCompletedUnitCount:", st.st_size);
    }
    completion_block *complete = n->handler;
    if (complete && complete->invoke) {
        if (n->download == 2) complete->invoke(complete, n->response, error ? NULL : location, error);
        else complete->invoke(complete, error ? NULL : location, n->response, error);
    }
    download_discard(n);
    chm_block_release(n->handler); chm_block_release(n->update); chm_block_release(n->destination);
    n->handler = n->update = n->destination = NULL;
    chm_session_log(g_session, "download: token=%llu status=%d bytes=%lld result=%s", (unsigned long long)n->token, status, (long long)(has_file ? st.st_size : 0), error ? "error" : "delivered");
    return 1;
}
static const chm_host_method download_methods[] = {
    {"AFURLSessionManager","initWithSessionConfiguration:","@@:@",download_manager_init,0},
    {"AFURLSessionManager","downloadTaskWithRequest:progress:destination:completionHandler:","@@:@@@@",af_download,0},
    {"AFURLSessionManager","downloadProgressForTask:","@@:@",download_progress,0},
    {"AFURLSessionManager","invalidateSessionCancelingTasks:resetSession:","v@:BB",manager_invalidate,0},
    {"NSURLSessionConfiguration","setTimeoutIntervalForRequest:","v@:d",request_timeout,0},
    {"NSURLSession","downloadTaskWithURL:completionHandler:","@@:@@",session_download,0},
    {"NSURLSession","downloadTaskWithRequest:completionHandler:","@@:@@",session_download,0},
    {"NSURLSessionDownloadTask","progress","@@:",task_progress,0},
    {"NSURLSessionDownloadTask","response","@@:",task_response,0},
    {"NSURLResponse","expectedContentLength","q@:",response_length,0}
};
typedef struct progress_record progress_record;
typedef struct progress_watch {
    void *observer, *key, *context;
    unsigned long options;
    int active;
    struct progress_watch *next;
} progress_watch;
struct progress_record {
    void *object, *kind, *operation;
    int64_t total, done, weight;
    int cancelled;
    progress_record *parent, *children, *sibling, *next;
    progress_watch *watches;
};
static progress_record *progress_records;

static progress_record *progress_record_for(void *obj)
{
    if (!obj || !chm_objc_is_kind(obj, "NSProgress")) return NULL;
    for (progress_record *p = progress_records; p; p = p->next) if (p->object == obj) return p;
    progress_record *p = calloc(1, sizeof(*p));
    if (p) { p->object = obj; p->next = progress_records; progress_records = p; }
    return p;
}

static double progress_ratio(progress_record *p)
{
    if (!p || p->total <= 0) return 0;
    double done = p->done;
    for (progress_record *c = p->children; c; c = c->sibling) done += (double)c->weight * progress_ratio(c);
    return fmax(0, fmin(1, done / (double)p->total));
}
static double progress_fraction(void *self, const char *sel)
{
    (void)sel; return progress_ratio(progress_record_for(self));
}
static void progress_send(progress_record *p, progress_watch *w, double fraction)
{
    void (*call)(void *, const char *, void *, void *, void *, void *) = chm_objc_find_imp(w->observer, "observeValueForKeyPath:ofObject:change:context:");
    if (!call) return;
    void *changes = fo_dict_new();
    if (w->options & 1) d_put(changes, fo_string_new("new"), n_double(NULL, NULL, fraction));
    call(w->observer, "observeValueForKeyPath:ofObject:change:context:", w->key, p->object, changes, w->context);
}
static void progress_notify(progress_record *p)
{
    for (; p; p = p->parent) {
        if (!p->watches) continue;
        double f = progress_ratio(p);
        for (progress_watch *w = p->watches; w; w = w->next) if (w->active) progress_send(p, w, f);
    }
}
static void progress_observe(void *self, const char *sel, void *observer, void *key, unsigned long options, void *context)
{
    (void)sel; progress_record *p = progress_record_for(self);
    if (!p || !observer || strcmp(fo_bytes(key), "fractionCompleted")) return;
    progress_watch *w = calloc(1, sizeof(*w));
    if (!w) return;
    *w = (progress_watch){observer,key,context,options,1,p->watches}; p->watches = w;
    if (options & 4) progress_send(p, w, progress_ratio(p));
}
static void progress_remove_watch(void *self, void *observer, void *key, void *context, int match_context)
{
    progress_record *p = progress_record_for(self);
    for (progress_watch *w = p ? p->watches : NULL; w; w = w->next) {
        if (w->observer == observer && o_is_equal(w->key, NULL, key) && (!match_context || w->context == context)) w->active = 0;
    }
}
static void progress_unobserve(void *self, const char *sel, void *observer, void *key)
{
    (void)sel; progress_remove_watch(self, observer, key, NULL, 0);
}
static void progress_unobserve_context(void *self, const char *sel, void *observer, void *key, void *context)
{
    (void)sel; progress_remove_watch(self, observer, key, context, 1);
}
static void progress_set_units(void *self, const char *sel, int64_t units)
{
    progress_record *p = progress_record_for(self);
    if (!p) return;
    int64_t *slot = !strcmp(sel, "setTotalUnitCount:") ? &p->total : &p->done;
    if (*slot != units) { *slot = units; progress_notify(p); }
}
static int64_t progress_units(void *self, const char *sel)
{
    progress_record *p = progress_record_for(self);
    return !p ? 0 : !strcmp(sel, "totalUnitCount") ? p->total : p->done;
}
static void *progress_new(void *cls, const char *sel, int64_t total)
{
    (void)sel; void *p = chm_foundation_alloc_class(cls, 0);
    progress_set_units(p, "setTotalUnitCount:", total); return p;
}
static void progress_add(void *self, const char *sel, void *child, int64_t weight)
{
    (void)sel; progress_record *p = progress_record_for(self), *c = progress_record_for(child);
    if (!p || !c || c->parent || weight < 0) return;
    unsigned depth = 0;
    for (progress_record *n = p; n; n = n->parent) if (n == c || ++depth >= 64) return;
    c->parent = p; c->weight = weight; c->sibling = p->children; p->children = c;
    progress_notify(p);
}
static void progress_label(void *self, const char *sel, void *label)
{
    progress_record *p = progress_record_for(self);
    if (!p) return;
    if (!strcmp(sel, "setKind:")) p->kind = label; else p->operation = label;
}
static void *progress_get_label(void *self, const char *sel)
{
    progress_record *p = progress_record_for(self);
    return !p ? NULL : !strcmp(sel, "kind") ? p->kind : p->operation;
}
static void progress_cancel(void *self, const char *sel)
{
    (void)sel; progress_record *p = progress_record_for(self);
    if (p && !p->cancelled) { p->cancelled = 1; progress_notify(p); }
}
static int progress_flag(void *self, const char *sel)
{
    progress_record *p = progress_record_for(self);
    if (!strcmp(sel, "isFinished")) return progress_ratio(p) >= 1;
    for (; p; p = p->parent) if (p->cancelled) return 1;
    return 0;
}
static void progress_reset(void)
{
    while (progress_records) {
        progress_record *p = progress_records; progress_records = p->next;
        while (p->watches) { progress_watch *w = p->watches; p->watches = w->next; free(w); }
        free(p);
    }
}
static const chm_host_method progress_methods[] = {
    {"NSProgress","progressWithTotalUnitCount:","@@:q",progress_new,1},
    {"NSProgress","discreteProgressWithTotalUnitCount:","@@:q",progress_new,1},
    {"NSProgress","setTotalUnitCount:","v@:q",progress_set_units,0},
    {"NSProgress","setCompletedUnitCount:","v@:q",progress_set_units,0},
    {"NSProgress","totalUnitCount","q@:",progress_units,0},
    {"NSProgress","completedUnitCount","q@:",progress_units,0},
    {"NSProgress","fractionCompleted","d@:",progress_fraction,0},
    {"NSProgress","addChild:withPendingUnitCount:","v@:@q",progress_add,0},
    {"NSProgress","setKind:","v@:@",progress_label,0},
    {"NSProgress","setFileOperationKind:","v@:@",progress_label,0},
    {"NSProgress","kind","@@:",progress_get_label,0},
    {"NSProgress","fileOperationKind","@@:",progress_get_label,0},
    {"NSProgress","isFinished","B@:",progress_flag,0},
    {"NSProgress","isCancelled","B@:",progress_flag,0},
    {"NSProgress","cancel","v@:",progress_cancel,0},
    {"NSProgress","addObserver:forKeyPath:options:context:","v@:@@Q^v",progress_observe,0},
    {"NSProgress","removeObserver:forKeyPath:","v@:@@",progress_unobserve,0},
    {"NSProgress","removeObserver:forKeyPath:context:","v@:@@^v",progress_unobserve_context,0}
};
typedef struct ui_clock_link {
    void *object, *target;
    const char *selector;
    double time, interval, last;
    int active, paused, invalid, rate;
    struct ui_clock_link *next;
} ui_clock_link;
static ui_clock_link *clock_links;

static ui_clock_link *clock_link(void *self)
{
    for (ui_clock_link *l = clock_links; l; l = l->next) if (l->object == self) return l;
    return NULL;
}
static void *clock_create(void *cls, const char *sel, void *target, const char *action)
{
    (void)sel;
    if (!target || !action || !*action) return NULL;
    ui_clock_link *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->object = chm_foundation_alloc_class(cls, 0);
    if (!l->object) { free(l); return NULL; }
    l->target = target; l->selector = action; l->interval = 1.0 / 60;
    l->next = clock_links; clock_links = l; return l->object;
}
static void clock_schedule(void *self, const char *sel, void *loop, void *mode)
{
    ui_clock_link *l = clock_link(self);
    if (!l || l->invalid || !loop || !mode) return;
    l->active = !strcmp(sel, "addToRunLoop:forMode:"); l->last = 0;
}
static void clock_invalidate(void *self, const char *sel)
{
    (void)sel; ui_clock_link *l = clock_link(self);
    if (l) { l->invalid = 1; l->target = NULL; l->active = 0; }
}
static void clock_pause(void *self, const char *sel, signed char pause)
{
    (void)sel; ui_clock_link *l = clock_link(self); if (l) { l->paused = !!pause; l->last = 0; }
}
static int clock_paused(void *self, const char *sel)
{
    (void)sel; ui_clock_link *l = clock_link(self); return l && l->paused;
}
static void clock_rate(void *self, const char *sel, long rate)
{
    (void)sel; ui_clock_link *l = clock_link(self); if (l) l->rate = rate <= 0 ? 0 : rate > 1000 ? 1000 : (int)rate;
}
static long clock_get_rate(void *self, const char *sel)
{
    (void)sel; ui_clock_link *l = clock_link(self); return l ? l->rate : 0;
}
static double clock_value(void *self, const char *sel)
{
    ui_clock_link *l = clock_link(self);
    if (!l) return 0;
    return !strcmp(sel, "duration") ? l->interval : !strcmp(sel, "timestamp") ? l->time : l->time + l->interval;
}
typedef struct { double timestamp, period; } frame_time;
static int clock_tick(void *context)
{
    frame_time *t = context;
    int count = 0;
    for (ui_clock_link *l = clock_links; l; l = l->next) {
        if (!l->active || l->invalid || l->paused || !l->target) continue;
        if (l->last && (t->timestamp <= l->last || (l->rate && t->timestamp - l->last + 0.0001 < 1.0 / l->rate))) continue;
        l->last = l->time = t->timestamp; l->interval = t->period;
        void (*callback)(void *, const char *, void *) = chm_objc_find_imp(l->target, l->selector);
        if (callback) { callback(l->target, l->selector, l->object); count++; }
    }
    return count;
}
int chm_ui_vsync(double timestamp, double period)
{
    if (!g_sess || g_sess->exit_code < 0 || !isfinite(timestamp) || !isfinite(period) || period <= 0) return -1;
    frame_time t = {timestamp, period};
    int rc = chm_session_callback(g_sess, clock_tick, &t);
    if (rc == -2) g_sess->exit_code = rc;
    return rc;
}
static void clock_reset(void)
{
    while (clock_links) { ui_clock_link *l = clock_links; clock_links = l->next; free(l); }
}
static const chm_host_method clock_methods[] = {
    {"CADisplayLink","displayLinkWithTarget:selector:","@@:@:",clock_create,1},
    {"CADisplayLink","addToRunLoop:forMode:","v@:@@",clock_schedule,0},
    {"CADisplayLink","removeFromRunLoop:forMode:","v@:@@",clock_schedule,0},
    {"CADisplayLink","invalidate","v@:",clock_invalidate,0},
    {"CADisplayLink","setPaused:","v@:B",clock_pause,0},
    {"CADisplayLink","isPaused","B@:",clock_paused,0},
    {"CADisplayLink","setPreferredFramesPerSecond:","v@:q",clock_rate,0},
    {"CADisplayLink","preferredFramesPerSecond","q@:",clock_get_rate,0},
    {"CADisplayLink","timestamp","d@:",clock_value,0},
    {"CADisplayLink","targetTimestamp","d@:",clock_value,0},
    {"CADisplayLink","duration","d@:",clock_value,0}
};
#include "chm_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_VIEW_MAX_KIDS 96
#define UI_TEXT_MAX 160

typedef struct ui_view {
    void *isa;
    double x;
    double y;
    double w;
    double h;
    double alpha;
    uint32_t bg;
    uint32_t tag;
    int hidden;
    int clips;
    int font_align;
    struct ui_view *parent;
    struct ui_view **kids;
    int nkids;
    int kid_cap;
    void *layer;
    void *owner;
    char text[UI_TEXT_MAX];
} ui_view;

typedef struct ui_vc {
    void *isa;
    void *view;
    void *title;
    void *presented;
} ui_vc;

typedef struct ui_app {
    void *isa;
    void *delegate;
} ui_app;

typedef struct ui_color {
    void *isa;
    double r;
    double g;
    double b;
    double a;
} ui_color;

static void *g_app;
static void *g_screen;
static chm_session *g_sess;
static ui_view *g_key_window;
static int g_vw = 720;
static int g_vh = 1280;
static double g_scale = 2.0;
static int g_running;

static uint32_t argb(double r, double g, double b, double a)
{
    uint32_t ia = (uint32_t)(a * 255.0 + 0.5);
    uint32_t ir = (uint32_t)(r * 255.0 + 0.5);
    uint32_t ig = (uint32_t)(g * 255.0 + 0.5);
    uint32_t ib = (uint32_t)(b * 255.0 + 0.5);

    if (ia > 255)
        ia = 255;
    if (ir > 255)
        ir = 255;
    if (ig > 255)
        ig = 255;
    if (ib > 255)
        ib = 255;
    return (ia << 24) | (ir << 16) | (ig << 8) | ib;
}

static ui_view *as_view(void *p)
{
    if (!p || !chm_foundation_isa_is(p, "UIView"))
        return NULL;
    return (ui_view *)p;
}

static ui_vc *as_vc(void *p)
{
    if (!p || !chm_foundation_isa_is(p, "UIViewController"))
        return NULL;
    return (ui_vc *)p;
}

static ui_color *as_color(void *p)
{
    if (!p || !chm_foundation_isa_is(p, "UIColor"))
        return NULL;
    return (ui_color *)p;
}

static void view_init(ui_view *v, double x, double y, double w, double h)
{
    v->x = x;
    v->y = y;
    v->w = w;
    v->h = h;
    v->alpha = 1.0;
    v->kid_cap = 8;
    v->kids = calloc((size_t)v->kid_cap, sizeof(ui_view *));
}

static void view_add(ui_view *p, ui_view *c)
{
    if (!p || !c || p->nkids >= UI_VIEW_MAX_KIDS)
        return;
    if (p->nkids >= p->kid_cap) {
        int cap = p->kid_cap * 2 > UI_VIEW_MAX_KIDS ? UI_VIEW_MAX_KIDS : p->kid_cap * 2;
        ui_view **nk = realloc(p->kids, (size_t)cap * sizeof(ui_view *));

        if (!nk)
            return;
        p->kids = nk;
        p->kid_cap = cap;
    }
    c->parent = p;
    p->kids[p->nkids++] = c;
}

static void view_remove(ui_view *c)
{
    ui_view *p;
    int i;

    if (!c || !c->parent)
        return;
    p = c->parent;
    for (i = 0; i < p->nkids; i++) {
        if (p->kids[i] != c)
            continue;
        memmove(&p->kids[i], &p->kids[i + 1], (size_t)(p->nkids - i - 1) * sizeof(ui_view *));
        p->nkids--;
        break;
    }
    c->parent = NULL;
}

static void *n_rect(double x, double y, double w, double h)
{
    double *r = malloc(4 * sizeof(double));

    if (!r)
        return NULL;
    r[0] = x;
    r[1] = y;
    r[2] = w;
    r[3] = h;
    return r;
}

static void *color_new(double r, double g, double b, double a)
{
    ui_color *c = (ui_color *)chm_foundation_alloc_obj("UIColor", sizeof(ui_color));

    if (!c)
        return NULL;
    c->r = r;
    c->g = g;
    c->b = b;
    c->a = a;
    return c;
}

static void *v_init(void *self, const char *sel)
{
    (void)sel;
    if (self && !((ui_view *)self)->kids)
        view_init((ui_view *)self, 0, 0, (double)g_vw, (double)g_vh);
    return self;
}

static void *v_init_frame(void *self, const char *sel, double x, double y, double w, double h)
{
    (void)sel;
    if (self)
        view_init((ui_view *)self, x, y, w, h);
    return self;
}

static void *v_frame(void *self, const char *sel)
{
    ui_view *v = as_view(self);

    (void)sel;
    if (!v)
        return NULL;
    return n_rect(v->x, v->y, v->w, v->h);
}

static void v_set_frame(void *self, const char *sel, double x, double y, double w, double h)
{
    ui_view *v = as_view(self);

    (void)sel;
    if (!v)
        return;
    v->x = x;
    v->y = y;
    v->w = w;
    v->h = h;
}

static void *v_bounds(void *self, const char *sel)
{
    ui_view *v = as_view(self);

    (void)sel;
    if (v)
        return n_rect(0, 0, v->w, v->h);
    return n_rect(0, 0, (double)g_vw, (double)g_vh);
}

static void *v_background(void *self, const char *sel)
{
    ui_view *v = as_view(self);
    ui_color *c;

    (void)sel;
    if (!v)
        return NULL;
    c = (ui_color *)color_new(0, 0, 0, 0);
    if (!c)
        return NULL;
    c->r = ((v->bg >> 16) & 0xff) / 255.0;
    c->g = ((v->bg >> 8) & 0xff) / 255.0;
    c->b = (v->bg & 0xff) / 255.0;
    c->a = ((v->bg >> 24) & 0xff) / 255.0;
    return c;
}

static void v_set_background(void *self, const char *sel, void *color)
{
    ui_view *v = as_view(self);
    ui_color *c = as_color(color);

    (void)sel;
    if (!v)
        return;
    v->bg = c ? argb(c->r, c->g, c->b, c->a) : 0;
}

static void v_add_subview(void *self, const char *sel, void *child)
{
    (void)sel;
    view_add(as_view(self), as_view(child));
}

static void v_remove_from_super(void *self, const char *sel)
{
    (void)sel;
    view_remove(as_view(self));
}

static void *v_subviews(void *self, const char *sel)
{
    ui_view *v = as_view(self);
    void *arr;
    int i;

    (void)sel;
    if (!v)
        return NULL;
    arr = chm_foundation_alloc_obj("NSMutableArray", 64);
    if (!arr)
        return NULL;
    for (i = 0; i < v->nkids; i++)
        chm_objc_send1(arr, "addObject:", v->kids[i]);
    return arr;
}

static void *v_superview(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) ? as_view(self)->parent : NULL;
}

static int v_hidden(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) ? as_view(self)->hidden : 0;
}

static void v_set_hidden(void *self, const char *sel, int hidden)
{
    (void)sel;
    if (as_view(self))
        as_view(self)->hidden = hidden ? 1 : 0;
}

static double v_alpha(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) ? as_view(self)->alpha : 1.0;
}

static void v_set_alpha(void *self, const char *sel, double a)
{
    (void)sel;
    if (as_view(self))
        as_view(self)->alpha = a;
}

static int64_t v_tag(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) ? (int64_t)as_view(self)->tag : 0;
}

static void v_set_tag(void *self, const char *sel, int64_t tag)
{
    (void)sel;
    if (as_view(self))
        as_view(self)->tag = (uint32_t)tag;
}

static void *v_layer(void *self, const char *sel)
{
    ui_view *v = as_view(self);

    (void)sel;
    if (!v)
        return NULL;
    if (!v->layer)
        v->layer = chm_foundation_alloc_obj("CALayer", 128);
    return v->layer;
}

static void v_set_needs_layout(void *self, const char *sel)
{
    (void)sel;
    (void)self;
}

static void v_layout_if_needed(void *self, const char *sel)
{
    (void)sel;
    (void)self;
}

static void v_size_to_fit(void *self, const char *sel)
{
    (void)sel;
    (void)self;
}

static int v_clips(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) ? as_view(self)->clips : 0;
}

static void v_set_clips(void *self, const char *sel, int c)
{
    (void)sel;
    if (as_view(self))
        as_view(self)->clips = c ? 1 : 0;
}

static void l_set_text(void *self, const char *sel, void *text)
{
    ui_view *v = as_view(self);
    const char *s;

    (void)sel;
    if (!v)
        return;
    s = text ? (const char *)chm_objc_send0(text, "UTF8String") : NULL;
    snprintf(v->text, sizeof(v->text), "%s", s ? s : "");
}

static void *l_text(void *self, const char *sel)
{
    ui_view *v = as_view(self);

    (void)sel;
    return v ? chm_foundation_string(v->text) : NULL;
}

static void l_set_alignment(void *self, const char *sel, int64_t a)
{
    (void)sel;
    if (as_view(self))
        as_view(self)->font_align = (int)a;
}

static void *wc_view(void *self, const char *sel)
{
    ui_vc *vc = as_vc(self);
    void *created;
    ui_view *v;

    (void)sel;
    if (!vc)
        return NULL;
    if (vc->view)
        return vc->view;
    created = chm_foundation_alloc_obj("UIView", 1024);
    if (!created)
        return NULL;
    v = as_view(created);
    view_init(v, 0, 0, (double)g_vw, (double)g_vh);
    v->owner = self;
    vc->view = created;
    v->bg = argb(0.11, 0.11, 0.12, 1);
    return vc->view;
}

static void wc_set_view(void *self, const char *sel, void *view)
{
    ui_vc *vc = as_vc(self);

    (void)sel;
    if (!vc)
        return;
    vc->view = view;
    if (as_view(view))
        as_view(view)->owner = self;
}

static void wc_view_did_load(void *self, const char *sel)
{
    (void)sel;
    (void)self;
}

static void wc_set_title(void *self, const char *sel, void *t)
{
    (void)sel;
    if (as_vc(self))
        as_vc(self)->title = t;
}

static void *wc_title(void *self, const char *sel)
{
    (void)sel;
    return as_vc(self) ? as_vc(self)->title : NULL;
}

static void wc_present(void *self, const char *sel, void *other, int animated, void *done)
{
    ui_vc *vc = as_vc(self);
    void *child;

    (void)sel;
    (void)animated;
    (void)done;
    if (!vc || !other)
        return;
    vc->presented = other;
    child = chm_objc_send0(other, "view");
    if (child && g_key_window)
        view_add(g_key_window, as_view(child));
}

static void wc_dismiss(void *self, const char *sel, int animated, void *done)
{
    ui_vc *vc = as_vc(self);
    void *child;

    (void)sel;
    (void)animated;
    (void)done;
    if (!vc || !vc->presented)
        return;
    child = chm_objc_send0(vc->presented, "view");
    view_remove(as_view(child));
    vc->presented = NULL;
}

static void *wc_presented(void *self, const char *sel)
{
    (void)sel;
    return as_vc(self) ? as_vc(self)->presented : NULL;
}

static void win_make_key(void *self, const char *sel)
{
    ui_view *v = as_view(self);

    (void)sel;
    if (!v)
        return;
    v->hidden = 0;
    g_key_window = v;
}

static int win_is_key(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) == g_key_window;
}

static void win_set_root_vc(void *self, const char *sel, void *vc)
{
    ui_view *v = as_view(self);
    void *child;
    ui_view *cv;

    (void)sel;
    if (!v || !vc)
        return;
    child = chm_objc_send0(vc, "view");
    if (!child)
        return;
    cv = as_view(child);
    cv->x = 0;
    cv->y = 0;
    cv->w = v->w;
    cv->h = v->h;
    view_add(v, cv);
    v->owner = vc;
}

static void *win_root_vc(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) ? as_view(self)->owner : NULL;
}

static int v_window_level(void *self, const char *sel)
{
    (void)sel;
    (void)self;
    return 0;
}

static void v_set_window_level(void *self, const char *sel, int64_t l)
{
    (void)sel;
    (void)self;
    (void)l;
}

static void *app_shared(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    if (!g_app) {
        g_app = chm_foundation_alloc_obj("UIApplication", 512);
        if (g_app)
            chm_objc_send0(g_app, "init");
    }
    return g_app;
}

static void *app_delegate(void *self, const char *sel)
{
    (void)sel;
    if (!self || !chm_foundation_isa_is(self, "UIApplication"))
        return NULL;
    return ((ui_app *)self)->delegate;
}

static void app_set_delegate(void *self, const char *sel, void *d)
{
    (void)sel;
    if (self && chm_foundation_isa_is(self, "UIApplication"))
        ((ui_app *)self)->delegate = d;
}

static void *app_key_window(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return g_key_window;
}

static void *app_windows(void *self, const char *sel)
{
    void *arr;

    (void)self;
    (void)sel;
    arr = chm_foundation_alloc_obj("NSMutableArray", 64);
    if (!arr)
        return NULL;
    if (g_key_window)
        chm_objc_send1(arr, "addObject:", g_key_window);
    return arr;
}

static void *color_rgba(void *cls, const char *sel, double r, double g, double b, double a)
{
    (void)cls;
    (void)sel;
    return color_new(r, g, b, a);
}

static void *color_white_alpha(void *cls, const char *sel, double w, double a)
{
    (void)cls;
    (void)sel;
    return color_new(w, w, w, a);
}

static void *color_black(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return color_new(0, 0, 0, 1);
}

static void *color_white(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return color_new(1, 1, 1, 1);
}

static void *color_clear(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return color_new(0, 0, 0, 0);
}

static void *color_system_bg(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return color_new(0.11, 0.11, 0.12, 1);
}

static void *color_system_blue(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return color_new(0, 0.478, 1, 1);
}

static void color_set_fill(void *self, const char *sel)
{
    (void)self;
    (void)sel;
}

static void *screen_shared(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    if (!g_screen)
        g_screen = chm_foundation_alloc_obj("UIScreen", 64);
    return g_screen;
}

static double screen_scale(void *cls, const char *sel)
{
    (void)cls;
    (void)sel;
    return g_scale;
}

static void *bundle_info(void *self, const char *sel)
{
    void *d;

    (void)sel;
    (void)self;
    d = chm_foundation_alloc_obj("NSMutableDictionary", 64);
    if (!d)
        return NULL;
    chm_objc_send2(d, "setObject:forKey:", chm_foundation_string("CMCLauncher"),
                   chm_foundation_string("CFBundleExecutable"));
    chm_objc_send2(d, "setObject:forKey:", chm_foundation_string("com.mojang.launcher"),
                   chm_foundation_string("CFBundleIdentifier"));
    chm_objc_send2(d, "setObject:forKey:", chm_foundation_string("1.0"),
                   chm_foundation_string("CFBundleShortVersionString"));
    chm_objc_send2(d, "setObject:forKey:", chm_foundation_string("1"),
                   chm_foundation_string("CFBundleVersion"));
    return d;
}

int chm_ui_main(int argc, char **argv, void *principal, void *delegate_name)
{
    void *app;
    void *delegate = NULL;
    const char *dn;

    (void)argc;
    (void)argv;
    (void)principal;

    fprintf(stderr, "[chimera] UIApplicationMain entered\n");
    app = app_shared(NULL, NULL);
    dn = delegate_name ? (const char *)chm_objc_send0(delegate_name, "UTF8String") : NULL;
    chm_session_log(g_sess, "ui: UIApplicationMain delegate=%s", dn ? dn : "(nil)");
    if (dn && dn[0]) {
        delegate = chm_objc_new_object(dn);
        if (delegate)
            app_set_delegate(app, NULL, delegate);
    }
    if (delegate) {
        chm_objc_send2(delegate, "application:didFinishLaunchingWithOptions:", app, NULL);
        g_running = 1;
        chm_session_log(g_sess, "ui: launched key_window=%s views=%d",
                        g_key_window ? "yes" : "no", chm_ui_view_count());
        chm_objc_send2(delegate, "applicationDidBecomeActive:", app, NULL);
        chm_objc_send2(delegate, "applicationWillResignActive:", app, NULL);
    } else {
        chm_session_log(g_sess, "ui: no delegate instance");
    }
    return 0;
}

int chm_ui_viewport(int w, int h)
{
    g_vw = w > 0 ? w : 720;
    g_vh = h > 0 ? h : 1280;
    return 0;
}

int chm_ui_running(void)
{
    return g_running;
}

static void blend_fill(uint32_t *buf, int bw, int bh, int x0, int y0, int x1, int y1, uint32_t c)
{
    int a = (int)((c >> 24) & 0xff);
    int sr = (int)((c >> 16) & 0xff);
    int sg = (int)((c >> 8) & 0xff);
    int sb = (int)(c & 0xff);
    int x;
    int y;

    if (a <= 0)
        return;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > bw)
        x1 = bw;
    if (y1 > bh)
        y1 = bh;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (a >= 255) {
        for (y = y0; y < y1; y++) {
            uint32_t *row = buf + (size_t)y * (size_t)bw;

            for (x = x0; x < x1; x++)
                row[x] = c;
        }
        return;
    }
    for (y = y0; y < y1; y++) {
        uint32_t *row = buf + (size_t)y * (size_t)bw;

        for (x = x0; x < x1; x++) {
            uint32_t d = row[x];
            int da = (int)((d >> 24) & 0xff);
            int oa = a + da * (255 - a) / 255;

            if (oa <= 0) {
                row[x] = 0;
                continue;
            }
            row[x] = ((uint32_t)oa << 24) |
                     ((uint32_t)((sr * a + (int)((d >> 16) & 0xff) * da * (255 - a) / 255) / oa)
                      << 16) |
                     ((uint32_t)((sg * a + (int)((d >> 8) & 0xff) * da * (255 - a) / 255) / oa) << 8) |
                     (uint32_t)((sb * a + (int)(d & 0xff) * da * (255 - a) / 255) / oa);
        }
    }
}

static void render_view(uint32_t *buf, int bw, int bh, ui_view *v, int px, int py, int cx, int cy)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int i;

    if (!v || v->hidden)
        return;
    x0 = px + (int)(v->x + 0.5);
    y0 = py + (int)(v->y + 0.5);
    x1 = x0 + (int)(v->w + 0.5);
    y1 = y0 + (int)(v->h + 0.5);
    if (v->clips) {
        if (x1 > cx)
            x1 = cx;
        if (y1 > cy)
            y1 = cy;
    }
    if (((v->bg >> 24) & 0xff) != 0)
        blend_fill(buf, bw, bh, x0, y0, x1, y1, v->bg);
    for (i = 0; i < v->nkids; i++)
        render_view(buf, bw, bh, v->kids[i], x0, y0, x1, y1);
}

int chm_ui_render(uint32_t *buf, int bw, int bh)
{
    if (!buf || bw <= 0 || bh <= 0)
        return -1;
    memset(buf, 0, (size_t)bw * (size_t)bh * sizeof(uint32_t));
    if (!g_key_window)
        return 0;
    render_view(buf, bw, bh, g_key_window, 0, 0, bw, bh);
    return 1;
}

int chm_ui_view_count(void)
{
    ui_view *stack[256];
    int sp = 0;
    int n = 0;

    if (!g_key_window)
        return 0;
    stack[sp++] = g_key_window;
    while (sp > 0) {
        ui_view *v = stack[--sp];
        int i;

        n++;
        for (i = 0; i < v->nkids && sp < 256; i++)
            stack[sp++] = v->kids[i];
    }
    return n;
}

static const struct {
    const char *sel;
    void *imp;
} g_ui_methods[] = {
    { "initWithFrame:", (void *)v_init_frame },
    { "initWithCoder:", (void *)v_init },
    { "frame", (void *)v_frame },
    { "setFrame:", (void *)v_set_frame },
    { "bounds", (void *)v_bounds },
    { "backgroundColor", (void *)v_background },
    { "setBackgroundColor:", (void *)v_set_background },
    { "addSubview:", (void *)v_add_subview },
    { "removeFromSuperview", (void *)v_remove_from_super },
    { "subviews", (void *)v_subviews },
    { "superview", (void *)v_superview },
    { "isHidden", (void *)v_hidden },
    { "setHidden:", (void *)v_set_hidden },
    { "alpha", (void *)v_alpha },
    { "setAlpha:", (void *)v_set_alpha },
    { "tag", (void *)v_tag },
    { "setTag:", (void *)v_set_tag },
    { "layer", (void *)v_layer },
    { "setNeedsLayout", (void *)v_set_needs_layout },
    { "layoutIfNeeded", (void *)v_layout_if_needed },
    { "sizeToFit", (void *)v_size_to_fit },
    { "clipsToBounds", (void *)v_clips },
    { "setClipsToBounds:", (void *)v_set_clips },
    { "setText:", (void *)l_set_text },
    { "text", (void *)l_text },
    { "setTextAlignment:", (void *)l_set_alignment },
    { "view", (void *)wc_view },
    { "setView:", (void *)wc_set_view },
    { "viewDidLoad", (void *)wc_view_did_load },
    { "setTitle:", (void *)wc_set_title },
    { "title", (void *)wc_title },
    { "presentViewController:animated:completion:", (void *)wc_present },
    { "dismissViewControllerAnimated:completion:", (void *)wc_dismiss },
    { "presentedViewController", (void *)wc_presented },
    { "makeKeyAndVisible", (void *)win_make_key },
    { "makeKeyWindow", (void *)win_make_key },
    { "isKeyWindow", (void *)win_is_key },
    { "setRootViewController:", (void *)win_set_root_vc },
    { "rootViewController", (void *)win_root_vc },
    { "windowLevel", (void *)v_window_level },
    { "setWindowLevel:", (void *)v_set_window_level },
    { "sharedApplication", (void *)app_shared },
    { "delegate", (void *)app_delegate },
    { "setDelegate:", (void *)app_set_delegate },
    { "keyWindow", (void *)app_key_window },
    { "windows", (void *)app_windows },
    { "colorWithRed:green:blue:alpha:", (void *)color_rgba },
    { "colorWithWhite:alpha:", (void *)color_white_alpha },
    { "colorWithDisplayP3Red:green:blue:alpha:", (void *)color_rgba },
    { "blackColor", (void *)color_black },
    { "whiteColor", (void *)color_white },
    { "clearColor", (void *)color_clear },
    { "systemBackgroundColor", (void *)color_system_bg },
    { "systemBlueColor", (void *)color_system_blue },
    { "setFill", (void *)color_set_fill },
    { "mainScreen", (void *)screen_shared },
    { "scale", (void *)screen_scale },
    { "nativeScale", (void *)screen_scale },
    { "infoDictionary", (void *)bundle_info },
    { "UIApplicationMain", (void *)chm_ui_main },
    { NULL, NULL }
};

void chm_ui_init(chm_session *s)
{
    int i;

    if (!s)
        return;
    g_sess = s;
    for (i = 0; g_ui_methods[i].sel; i++) {
        chm_method_reg m;

        m.cls = NULL;
        snprintf(m.sel, sizeof(m.sel), "%s", g_ui_methods[i].sel);
        m.imp = g_ui_methods[i].imp;
        if (chm_objc_session_add_method(s, &m) != 0)
            break;
    }
    app_shared(NULL, NULL);
    chm_session_log(s, "uikit: %d selectors, viewport %dx%d", i, g_vw, g_vh);
}
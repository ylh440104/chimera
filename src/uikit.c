#include "chm_scene.h"

#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define UI_TEXT_MAX 160

typedef struct { double x, y, w, h; } ui_rect;
typedef struct { double x, y; } ui_point;
typedef struct { double top, left, bottom, right; } ui_insets;

typedef struct ui_action {
    void *target;
    const char *selector;
    uint64_t events;
    struct ui_action *next;
} ui_action;

typedef struct ui_view {
    void *object;
    struct ui_view *next, *hash_next;
    void *scene;
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
    char *text;
    double font_size;
    uint32_t text_color;
    int number_of_lines;
    int translates;
    void *font;
    void *delegate;
    void *data_source;
    void *background_view;
    void *image;
    double progress;
    unsigned goal_mask;
    double goals[8];
    int interactive;
    int disabled;
    int highlighted;
    ui_action *actions;
    ui_point content_size, offset;
    ui_insets inset;
    int scrollable, scroll_disabled, notifying_scroll, dragging;
    int switch_on;
    struct ui_table *table;
    struct ui_cell *cell;
    void *alert_action;
    char *placeholder;
    int secure, keyboard_type, capitalization;
    int animating, show_when_stopped;
} ui_view;

static ui_view *focused_field;
static void dialogs_prepare(void);
static void dialogs_shutdown(void);
static int dialog_select(void *action);
static int send_control_events(ui_view *view, uint64_t events);

typedef struct ui_index {
    void *object;
    long section, row;
    struct ui_index *next;
} ui_index;

typedef struct {
    ui_view *view;
    void *index;
    double top, height;
    int header;
} ui_row;

typedef struct ui_table {
    ui_view *view;
    ui_row *rows;
    size_t count, capacity;
    long sections;
    long *row_counts;
    double row_height, content_height;
    int dirty, loading;
    ui_view *selected;
} ui_table;

typedef struct ui_cell {
    ui_view *view;
    void *content, *label, *detail, *icon, *accessory, *reuse;
    long style, accessory_type, selection_style;
    int selected;
    ui_table *table;
    void *index;
} ui_cell;

static ui_index *indexes;
static void table_reload(void *self, const char *sel);
static void tables_prepare(ui_view *root);
static int table_select_cell(ui_view *view);
static ui_table *table_state(void *object);
static void tables_shutdown(void);

typedef struct ui_vc {
    void *object;
    struct ui_vc *next;
    void *view;
    void *title;
    void *presented;
    int loading;
    int loaded;
    void **children;
    size_t child_count;
    size_t selected;
    struct ui_vc *parent;
    struct ui_vc *active_child;
    void *tab_item;
    void *navigation_item;
    void *bar;
    int syncing;
    int appeared;
    int bar_hidden;
    void *split_delegate;
    long split_mode, split_behavior, table_style;
    double split_fraction, split_minimum, split_maximum;
    int split_widths_ready, split_laying_out;
    struct ui_vc *presenting;
    struct ui_vc *previous_modal;
} ui_vc;

static ui_vc *g_modal;

typedef struct ui_item {
    void *object;
    void *title;
    void *image;
    void *selected_image;
    void *standard_appearance;
    void *scroll_appearance;
    void *right_item;
    void *left_item;
    void *target;
    const char *action;
    void *handler, *owner, *button;
    long style;
    int disabled;
    uint32_t background;
    struct ui_item *next;
} ui_item;

enum ui_attr { LEFT, RIGHT, TOP, BOTTOM, WIDTH, HEIGHT, CENTER_X, CENTER_Y };
typedef struct ui_anchor {
    void *object;
    ui_view *view;
    enum ui_attr attr;
    struct ui_anchor *next;
} ui_anchor;
typedef struct ui_constraint {
    void *object;
    ui_anchor *first;
    ui_anchor *second;
    double constant;
    double multiplier;
    int active;
    struct ui_constraint *next;
} ui_constraint;
typedef struct ui_guide {
    void *object;
    ui_view *view;
    struct ui_guide *next;
} ui_guide;

static ui_anchor *anchors;
static ui_constraint *constraints;
static ui_guide *guides;
static int layout_busy;
static int layout_dirty = 1;
static void ui_layout(void);
static void table_update(ui_view *table);
static ui_item *items;
static void *wc_view(void *self, const char *sel);
static void container_sync(ui_vc *vc);
static void container_layout(ui_vc *vc);
static void controller_appearance(ui_vc *vc, int appearing, int animated);

static ui_view *views;
static ui_view *view_index[2048];
static ui_vc *controllers;

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
    if (!p) return NULL;
    size_t bucket = ((uintptr_t)p >> 4) & 2047;
    for (ui_view *v = view_index[bucket]; v; v = v->hash_next)
        if (v->object == p) return v;
    if (!chm_objc_is_kind(p, "UIView")) return NULL;
    ui_view *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->object = p;
    v->alpha = 1.0;
    v->font_size = 17;
    v->text_color = 0xffffffffu;
    v->number_of_lines = 1;
    v->translates = 1;
    v->scrollable = chm_objc_is_kind(p, "UIScrollView");
    v->clips = v->scrollable;
    v->interactive = !chm_objc_is_kind(p, "UILabel") && !chm_objc_is_kind(p, "UIImageView");
    v->next = views;
    views = v;
    v->hash_next = view_index[bucket]; view_index[bucket] = v;
    return v;
}

static ui_vc *as_vc(void *p)
{
    if (!p || !chm_objc_is_kind(p, "UIViewController")) return NULL;
    for (ui_vc *v = controllers; v; v = v->next)
        if (v->object == p) return v;
    ui_vc *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->object = p;
    v->next = controllers;
    controllers = v;
    return v;
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
    layout_dirty = 1;
}

static void view_remove(ui_view *c);

static int view_add(ui_view *p, ui_view *c)
{
    if (!p || !c) return 0;
    for (ui_view *v = p; v; v = v->parent)
        if (v == c) return 0;
    if (c->parent != p && p->nkids == p->kid_cap) {
        if (p->kid_cap > INT_MAX / 2) return 0;
        int cap = p->kid_cap ? p->kid_cap * 2 : 8;
        ui_view **kids = realloc(p->kids, (size_t)cap * sizeof(*kids));
        if (!kids) return 0;
        p->kids = kids;
        p->kid_cap = cap;
    }
    view_remove(c);
    c->parent = p;
    p->kids[p->nkids++] = c;
    layout_dirty = 1;
    return 1;
}

#include "ui_scroll.inc"
#include "ui_geometry.inc"

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

static ui_rect n_rect(double x, double y, double w, double h)
{
    return (ui_rect){x, y, w, h};
}

typedef struct ui_font {
    void *object;
    double size;
    struct ui_font *next;
} ui_font;
static ui_font *fonts;

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
    ui_view *v = as_view(self);
    if (v) view_init(v, 0, 0, g_vw, g_vh);
    return self;
}

static void *v_init_frame(void *self, const char *sel, ui_rect rect)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v) return NULL;
    view_init(v, rect.x, rect.y, rect.w, rect.h);
    return self;
}

static void *win_init_scene(void *self, const char *sel, void *scene)
{
    if (!chm_objc_is_kind(self, "UIWindow") || !chm_objc_is_kind(scene, "UIWindowScene")) return NULL;
    ui_view *v = as_view(self);
    if (!v) return NULL;
    (void)sel;
    v->scene = scene;
    view_init(v, 0, 0, g_vw, g_vh);
    return self;
}

static void *win_scene(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v ? v->scene : NULL;
}

static ui_rect v_frame(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v ? n_rect(v->x, v->y, v->w, v->h) : n_rect(0, 0, 0, 0);
}

static void v_set_frame(void *self, const char *sel, ui_rect rect)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v) return;
    v->x = rect.x;
    v->y = rect.y;
    v->w = rect.w;
    v->h = rect.h;
    if (v->scrollable) v->offset = scroll_limit(v, v->offset);
    layout_dirty = 1;
    container_layout(as_vc(v->owner));
}

static ui_rect v_bounds(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v ? n_rect(v->offset.x, v->offset.y, v->w, v->h) : n_rect(0, 0, g_vw, g_vh);
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
        chm_objc_send1(arr, "addObject:", v->kids[i]->object);
    return arr;
}

static void *v_superview(void *self, const char *sel)
{
    (void)sel;
    ui_view *v = as_view(self);
    return v && v->parent ? v->parent->object : NULL;
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
    layout_dirty = 1;
}

static void v_layout_if_needed(void *self, const char *sel)
{
    (void)sel;
    (void)self;
    ui_layout();
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
    char *copy = s ? strdup(s) : NULL;
    if (s && !copy) return;
    free(v->text);
    v->text = copy;
    layout_dirty = 1;
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

static void wc_load_view(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc || vc->view) return;
    int table = chm_objc_is_kind(self, "UITableViewController");
    void *object = chm_foundation_alloc_obj(table ? "UITableView" : "UIView", 0);
    ui_view *v = as_view(object);
    if (!v) return;
    view_init(v, 0, 0, g_vw, g_vh);
    if (table) {
        v->clips = 1;
        v->bg = vc->table_style ? 0xff1c1c1eu : 0xff000000u;
        v->delegate = self;
        v->data_source = self;
        if (!table_state(object)) return;
    }
    v->owner = self;
    vc->view = object;
}

static void *wc_view(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc) return NULL;
    if (vc->loading) return vc->view;
    if (vc->loaded) {
        container_sync(vc);
        return vc->view;
    }
    vc->loading = 1;
    if (!vc->view) {
        const char *load = "loadView";
        void (*fn)(void *, const char *) = chm_objc_find_imp(self, load);
        if (fn) fn(self, load);
    }
    if (vc->view) {
        vc->loaded = 1;
        const char *ready = "viewDidLoad";
        void (*fn)(void *, const char *) = chm_objc_find_imp(self, ready);
        if (fn) fn(self, ready);
    }
    vc->loading = 0;
    container_sync(vc);
    return vc->view;
}

static void wc_set_view(void *self, const char *sel, void *view)
{
    ui_vc *vc = as_vc(self);

    (void)sel;
    if (!vc)
        return;
    vc->view = view;
    if (!view) vc->loaded = 0;
    if (as_view(view))
        as_view(view)->owner = self;
}

static void *table_controller_init(void *self, const char *sel, long style)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc || style < 0 || style > 2) return NULL;
    vc->table_style = style;
    return self;
}

static void *table_controller_view(void *self, const char *sel)
{
    return wc_view(self, sel);
}

static void table_controller_set_view(void *self, const char *sel, void *view)
{
    if (view && !chm_objc_is_kind(view, "UITableView")) return;
    wc_set_view(self, sel, view);
    if (view) {
        ui_view *v = as_view(view);
        if (v) { v->delegate = self; v->data_source = self; }
        table_state(view);
    }
}

static void wc_view_did_load(void *self, const char *sel)
{
    (void)sel;
    (void)self;
}

static int container_kind(ui_vc *vc)
{
    if (!vc) return 0;
    if (chm_objc_is_kind(vc->object, "UINavigationController")) return 1;
    if (chm_objc_is_kind(vc->object, "UITabBarController")) return 2;
    if (chm_objc_is_kind(vc->object, "UISplitViewController")) return 3;
    return 0;
}

static ui_item *item_state(void *object)
{
    if (!object) return NULL;
    for (ui_item *item = items; item; item = item->next)
        if (item->object == object) return item;
    ui_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->object = object;
    item->next = items;
    items = item;
    return item;
}

static void controller_appearance(ui_vc *vc, int appearing, int animated)
{
    if (!vc || vc->appeared == appearing) return;
    const char *will = appearing ? "viewWillAppear:" : "viewWillDisappear:";
    const char *did = appearing ? "viewDidAppear:" : "viewDidDisappear:";
    void (*fn)(void *, const char *, signed char) = chm_objc_find_imp(vc->object, will);
    if (fn) fn(vc->object, will, animated != 0);
    vc->appeared = appearing;
    if (container_kind(vc) == 3) {
        for (size_t i = 0; i < vc->child_count; i++) {
            ui_vc *child = as_vc(vc->children[i]);
            ui_view *view = child ? as_view(child->view) : NULL;
            if (view && view->parent == as_view(vc->view) && !view->hidden)
                controller_appearance(child, appearing, animated);
        }
    } else controller_appearance(vc->active_child, appearing, animated);
    fn = chm_objc_find_imp(vc->object, did);
    if (fn) fn(vc->object, did, animated != 0);
}

#include "ui_split.inc"

static void *container_bar(ui_vc *vc)
{
    int kind = container_kind(vc);
    if (kind != 1 && kind != 2) return NULL;
    if (!vc->bar) {
        vc->bar = chm_foundation_alloc_obj(kind == 1 ? "UINavigationBar" : "UITabBar", 0);
        ui_view *bar = as_view(vc->bar);
        if (!bar) return NULL;
        bar->bg = argb(0.11, 0.11, 0.12, 1);
    }
    return vc->bar;
}

static void container_layout(ui_vc *vc)
{
    int kind = container_kind(vc);
    if (!kind || !vc->view) return;
    if (kind == 3) { split_layout(vc); return; }
    ui_view *root = as_view(vc->view);
    ui_view *bar = as_view(container_bar(vc));
    if (!root || !bar) return;
    double height = vc->bar_hidden ? 0 : (kind == 1 ? 44.0 : 49.0);
    if (height > root->h) height = root->h > 0 ? root->h : 0;
    bar->x = 0;
    bar->y = kind == 1 ? 0 : root->h - height;
    bar->w = root->w;
    bar->h = height;
    bar->hidden = vc->bar_hidden;
    if (vc->active_child && vc->active_child->view) {
        ui_view *child = as_view(vc->active_child->view);
        if (child) {
            child->x = 0;
            child->y = kind == 1 ? height : 0;
            child->w = root->w;
            child->h = root->h - height;
            container_layout(vc->active_child);
        }
    }
    if (!view_add(root, bar)) chm_session_log(g_sess, "ui: could not mount container bar");
}

static void container_sync(ui_vc *vc)
{
    int kind = container_kind(vc);
    if (!kind || !vc->view || vc->loading || vc->syncing) return;
    vc->syncing = 1;
    if (kind == 3) {
        split_sync(vc);
        vc->syncing = 0;
        return;
    }
    size_t index = kind == 1 && vc->child_count ? vc->child_count - 1 : vc->selected;
    ui_vc *next = index < vc->child_count ? as_vc(vc->children[index]) : NULL;
    if (next != vc->active_child) {
        if (vc->active_child) {
            if (vc->appeared) controller_appearance(vc->active_child, 0, 0);
            view_remove(as_view(vc->active_child->view));
        }
        vc->active_child = NULL;
        if (next) {
            void *child = chm_objc_send0(next->object, "view");
            if (view_add(as_view(vc->view), as_view(child))) {
                vc->active_child = next;
                container_layout(vc);
                if (vc->appeared) controller_appearance(next, 1, 0);
                chm_session_log(g_sess, "ui: container=%s selected=%zu child=%s",
                                chm_objc_class_name(*(void **)vc->object), index,
                                chm_objc_class_name(*(void **)next->object));
            } else chm_session_log(g_sess, "ui: container child has no mountable view");
        }
    }
    container_layout(vc);
    vc->syncing = 0;
}

static int container_set(ui_vc *vc, void *const *objects, size_t count)
{
    if (!container_kind(vc) || (count && !objects) || count > SIZE_MAX / sizeof(void *)) return 0;
    if (container_kind(vc) == 3 && count > 2) return 0;
    for (size_t i = 0; i < count; i++) {
        ui_vc *child = as_vc(objects[i]);
        if (!child || (child->parent && child->parent != vc)) return 0;
        for (ui_vc *parent = vc; parent; parent = parent->parent)
            if (parent == child) return 0;
        for (size_t j = 0; j < i; j++) if (objects[i] == objects[j]) return 0;
    }
    void **copy = count ? malloc(count * sizeof(*copy)) : NULL;
    if (count && !copy) return 0;
    if (count) memcpy(copy, objects, count * sizeof(*copy));
    void *selected = vc->selected < vc->child_count ? vc->children[vc->selected] : NULL;
    for (size_t i = 0; i < vc->child_count; i++) {
        ui_vc *child = as_vc(vc->children[i]);
        if (container_kind(vc) == 3 && child) {
            int retained = 0;
            for (size_t j = 0; j < count; j++) retained |= copy[j] == child->object;
            if (!retained) {
                controller_appearance(child, 0, 0);
                view_remove(as_view(child->view));
            }
        }
        if (child && child->parent == vc) child->parent = NULL;
    }
    free(vc->children);
    vc->children = copy;
    vc->child_count = count;
    vc->selected = 0;
    for (size_t i = 0; i < count; i++) {
        as_vc(copy[i])->parent = vc;
        if (copy[i] == selected) vc->selected = i;
    }
    container_sync(vc);
    return 1;
}

static void *nav_init_root(void *self, const char *sel, void *root)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    return container_kind(vc) == 1 && container_set(vc, &root, 1) ? self : NULL;
}

static void container_set_array(void *self, const char *sel, void *array)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!container_kind(vc) || (array && !chm_objc_is_kind(array, "NSArray"))) return;
    size_t (*get_count)(void *, const char *) = chm_objc_find_imp(array, "count");
    size_t count = get_count ? get_count(array, "count") : 0;
    if (count > SIZE_MAX / sizeof(void *)) return;
    void **objects = count ? malloc(count * sizeof(*objects)) : NULL;
    if (count && !objects) return;
    void *(*get)(void *, const char *, size_t) = chm_objc_find_imp(array, "objectAtIndex:");
    for (size_t i = 0; i < count; i++) objects[i] = get ? get(array, "objectAtIndex:", i) : NULL;
    if (!container_set(vc, objects, count)) chm_session_log(g_sess, "ui: invalid container children");
    free(objects);
}

static void *container_children(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc) return NULL;
    void *array = chm_foundation_alloc_obj("NSMutableArray", 0);
    for (size_t i = 0; array && i < vc->child_count; i++) chm_objc_send1(array, "addObject:", vc->children[i]);
    return array;
}

static size_t tab_index(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    return vc ? vc->selected : 0;
}

static void tab_select(void *self, const char *sel, size_t index)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (container_kind(vc) != 2 || index >= vc->child_count) return;
    vc->selected = index;
    container_sync(vc);
}

static void *container_selected(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc || !vc->child_count) return NULL;
    size_t index = container_kind(vc) == 1 ? vc->child_count - 1 : vc->selected;
    return index < vc->child_count ? vc->children[index] : NULL;
}

static void tab_select_object(void *self, const char *sel, void *object)
{
    ui_vc *vc = as_vc(self);
    for (size_t i = 0; vc && i < vc->child_count; i++)
        if (vc->children[i] == object) { tab_select(self, sel, i); return; }
}

static void nav_push(void *self, const char *sel, void *object, signed char animated)
{
    (void)sel;
    (void)animated;
    ui_vc *vc = as_vc(self);
    if (container_kind(vc) != 1 || vc->child_count >= SIZE_MAX / sizeof(void *) - 1) return;
    void **objects = malloc((vc->child_count + 1) * sizeof(*objects));
    if (!objects) return;
    if (vc->child_count) memcpy(objects, vc->children, vc->child_count * sizeof(*objects));
    objects[vc->child_count] = object;
    if (!container_set(vc, objects, vc->child_count + 1)) chm_session_log(g_sess, "ui: invalid navigation push");
    free(objects);
}

static void *nav_pop(void *self, const char *sel, signed char animated)
{
    (void)sel;
    (void)animated;
    ui_vc *vc = as_vc(self);
    if (container_kind(vc) != 1 || vc->child_count <= 1) return NULL;
    void *last = vc->children[vc->child_count - 1];
    return container_set(vc, vc->children, vc->child_count - 1) ? last : NULL;
}

static void *controller_parent(void *self, const char *sel)
{
    ui_vc *vc = as_vc(self);
    ui_vc *parent = vc ? vc->parent : NULL;
    while (parent) {
        if (!strcmp(sel, "parentViewController") ||
            (!strcmp(sel, "navigationController") && container_kind(parent) == 1) ||
            (!strcmp(sel, "tabBarController") && container_kind(parent) == 2) ||
            (!strcmp(sel, "splitViewController") && container_kind(parent) == 3)) return parent->object;
        parent = parent->parent;
    }
    return NULL;
}

static void *controller_bar(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    void *bar = container_bar(vc);
    if (bar && vc->view) container_layout(vc);
    return bar;
}

static void nav_hide_bar(void *self, const char *sel, signed char hidden, signed char animated)
{
    (void)sel; (void)animated;
    ui_vc *vc = as_vc(self);
    if (container_kind(vc) != 1) return;
    vc->bar_hidden = hidden != 0;
    container_layout(vc);
}

static int is_bar_item(void *obj)
{
    return chm_objc_is_kind(obj, "UITabBarItem") || chm_objc_is_kind(obj, "UINavigationItem") ||
           chm_objc_is_kind(obj, "UIBarButtonItem") || chm_objc_is_kind(obj, "UIAlertAction");
}

static void wc_set_title(void *self, const char *sel, void *title)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (vc) vc->title = title;
    else if (is_bar_item(self)) {
        ui_item *item = item_state(self);
        if (item) item->title = title;
    }
}

static void *wc_title(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (vc) return vc->title;
    ui_item *item = is_bar_item(self) ? item_state(self) : NULL;
    return item ? item->title : NULL;
}

static void *tab_item_init(void *self, const char *sel, void *title, void *image, void *selected)
{
    (void)sel;
    if (!chm_objc_is_kind(self, "UITabBarItem")) return NULL;
    ui_item *item = item_state(self);
    if (!item) return NULL;
    item->title = title;
    item->image = image;
    item->selected_image = selected;
    return self;
}

static void *controller_item(void *self, const char *sel)
{
    ui_vc *vc = as_vc(self);
    if (!vc) return NULL;
    int navigation = !strcmp(sel, "navigationItem");
    void **slot = navigation ? &vc->navigation_item : &vc->tab_item;
    if (!*slot) {
        *slot = chm_foundation_alloc_obj(navigation ? "UINavigationItem" : "UITabBarItem", 0);
        ui_item *item = item_state(*slot);
        if (item) item->title = vc->title;
    }
    return *slot;
}

static void controller_set_item(void *self, const char *sel, void *item)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (vc && (!item || chm_objc_is_kind(item, "UITabBarItem"))) vc->tab_item = item;
}

static void *item_image(void *self, const char *sel)
{
    ui_item *item = is_bar_item(self) ? item_state(self) : NULL;
    if (!item) return NULL;
    return !strcmp(sel, "selectedImage") ? item->selected_image : item->image;
}

static void bar_set_appearance(void *self, const char *sel, void *appearance)
{
    ui_view *bar = as_view(self);
    if (!bar || (!chm_objc_is_kind(self, "UITabBar") && !chm_objc_is_kind(self, "UINavigationBar"))) return;
    ui_item *state = item_state(self);
    if (!state) return;
    if (!strcmp(sel, "setStandardAppearance:")) state->standard_appearance = appearance;
    else state->scroll_appearance = appearance;
    ui_item *style = appearance ? item_state(appearance) : NULL;
    if (style) bar->bg = style->background;
}

static void *bar_appearance(void *self, const char *sel)
{
    ui_item *state = item_state(self);
    if (!state) return NULL;
    return !strcmp(sel, "standardAppearance") ? state->standard_appearance : state->scroll_appearance;
}

static void appearance_default(void *self, const char *sel)
{
    (void)sel;
    if (!chm_objc_is_kind(self, "UITabBarAppearance")) return;
    ui_item *state = item_state(self);
    if (state) state->background = argb(0.11, 0.11, 0.12, 1);
}

static void container_set_array_animated(void *self, const char *sel, void *array, signed char animated)
{
    (void)animated;
    container_set_array(self, sel, array);
}

static void complete_transition(void *block)
{
    if (!block) return;
    struct { void *isa; int flags, reserved; void (*invoke)(void *); } *b = block;
    if (b->invoke) b->invoke(block);
}

static void wc_present(void *self, const char *sel, void *other, int animated, void *done)
{
    (void)sel;
    ui_vc *vc = as_vc(self), *next = as_vc(other);
    if (!g_key_window || !vc || !next || vc == next || vc->presented || next->parent || next->presenting) return;
    for (ui_vc *p = vc; p; p = p->parent) if (p == next) return;
    ui_view *child = as_view(chm_objc_send0(other, "view"));
    if (!child || !view_add(g_key_window, child)) return;
    child->x = child->y = 0;
    child->w = g_key_window->w;
    child->h = g_key_window->h;
    next->previous_modal = g_modal;
    next->presenting = vc;
    vc->presented = other;
    controller_appearance(g_modal ? g_modal : as_vc(g_key_window->owner), 0, animated);
    g_modal = next;
    container_layout(next);
    layout_dirty = 1;
    controller_appearance(next, 1, animated);
    chm_session_log(g_sess, "ui: presented %s", chm_objc_class_name(*(void **)other));
    complete_transition(done);
}

static void wc_dismiss(void *self, const char *sel, int animated, void *done)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc) return;
    ui_vc *wanted = vc->presented ? as_vc(vc->presented) : vc;
    while (wanted && !wanted->presenting) wanted = wanted->parent;
    if (!wanted) return;
    ui_vc *scan = g_modal;
    while (scan && scan != wanted) scan = scan->previous_modal;
    if (!scan) return;
    focused_field = NULL;
    for (;;) {
        ui_vc *top = g_modal;
        controller_appearance(top, 0, animated);
        view_remove(as_view(top->view));
        if (top->presenting) top->presenting->presented = NULL;
        top->presenting = NULL;
        g_modal = top->previous_modal;
        top->previous_modal = NULL;
        if (top == wanted) break;
    }
    controller_appearance(g_modal ? g_modal : as_vc(g_key_window->owner), 1, animated);
    layout_dirty = 1;
    chm_session_log(g_sess, "ui: dismissed modal");
    complete_transition(done);
}

static void *wc_presenting(void *self, const char *sel)
{
    (void)sel;
    for (ui_vc *vc = as_vc(self); vc; vc = vc->parent)
        if (vc->presenting) return vc->presenting->object;
    return NULL;
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
    if (!v || !chm_objc_is_kind(self, "UIWindow"))
        return;
    if (g_key_window && g_key_window != v && g_key_window->owner)
        controller_appearance(as_vc(g_key_window->owner), 0, 0);
    v->hidden = 0;
    g_key_window = v;
    if (v->owner) {
        ui_vc *vc = as_vc(v->owner);
        container_layout(vc);
        controller_appearance(vc, 1, 0);
    }
}

static int win_is_key(void *self, const char *sel)
{
    (void)sel;
    return as_view(self) == g_key_window;
}

static void win_set_root_vc(void *self, const char *sel, void *vc)
{
    (void)sel;
    ui_view *v = as_view(self);
    if (!v || !chm_objc_is_kind(self, "UIWindow")) return;
    ui_view *child = vc ? as_view(chm_objc_send0(vc, "view")) : NULL;
    if (vc && !child) return;
    if (v->owner) {
        ui_vc *previous = as_vc(v->owner);
        if (previous) {
            if (v == g_key_window) controller_appearance(previous, 0, 0);
            view_remove(as_view(previous->view));
        }
    }
    v->owner = NULL;
    if (!child) return;
    child->x = 0;
    child->y = 0;
    child->w = v->w;
    child->h = v->h;
    if (view_add(v, child)) {
        v->owner = vc;
        container_layout(as_vc(vc));
        if (v == g_key_window) controller_appearance(as_vc(vc), 1, 0);
    }
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
    if (self && chm_objc_is_kind(self, "UIApplication")) return ((ui_app *)self)->delegate;
    ui_view *v = as_view(self);
    return v ? v->delegate : NULL;
}

static void app_set_delegate(void *self, const char *sel, void *d)
{
    (void)sel;
    if (self && chm_objc_is_kind(self, "UIApplication")) ((ui_app *)self)->delegate = d;
    else {
        ui_view *v = as_view(self);
        if (v) v->delegate = d;
    }
}

static void *app_key_window(void *self, const char *sel)
{
    (void)self;
    (void)sel;
    return g_key_window ? g_key_window->object : NULL;
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
        chm_objc_send1(arr, "addObject:", g_key_window->object);
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
    (void)self; (void)sel;
    return chm_foundation_bundle_info();
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
    const char *finish = "application:didFinishLaunchingWithOptions:";
    signed char (*did_finish)(void *, const char *, void *, void *) = chm_objc_find_imp(delegate, finish);
    if (did_finish && !did_finish(delegate, finish, app, NULL)) {
        chm_session_log(g_sess, "ui: delegate declined launch");
        return 1;
    }
    if (dn && *dn && !delegate) {
        chm_session_log(g_sess, "ui: could not instantiate delegate %s", dn);
        return 1;
    }
    int connected = chm_scene_connect(g_sess, app, delegate);
    if (connected < 0) return 1;
    g_running = 1;
    if (!connected) {
        const char *active = "applicationDidBecomeActive:";
        void (*became_active)(void *, const char *, void *) = chm_objc_find_imp(delegate, active);
        if (became_active) became_active(delegate, active, app);
    }
    chm_session_log(g_sess, "ui: launched key_window=%s views=%d",
                    g_key_window ? "yes" : "no", chm_ui_view_count());
    return 0;
}

static void resize_window_controller(ui_vc *controller, int width, int height)
{
    ui_view *view = controller ? as_view(controller->view) : NULL;
    if (view && view->parent == g_key_window) {
        view->w = width;
        view->h = height;
    }
}

int chm_ui_viewport(int w, int h)
{
    int width = w > 0 ? w : 720;
    int height = h > 0 ? h : 1280;
    chm_guest_lock();
    if (g_vw != width || g_vh != height ||
        (g_key_window && (g_key_window->w != width || g_key_window->h != height ||
                          g_key_window->x != 0 || g_key_window->y != 0))) {
        g_vw = width;
        g_vh = height;
        if (g_key_window) {
            g_key_window->x = 0;
            g_key_window->y = 0;
            g_key_window->w = width;
            g_key_window->h = height;
            resize_window_controller(as_vc(g_key_window->owner), width, height);
            for (ui_vc *modal = g_modal; modal; modal = modal->previous_modal)
                resize_window_controller(modal, width, height);
        }
        layout_dirty = 1;
    }
    chm_guest_unlock();
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

#include "ui_symbol.inc"
#include "ui_draw.inc"

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

#include "ui_widgets.inc"
#include "ui_dialog.inc"
#include "ui_input.inc"
#include "ui_display_link.inc"

static const struct {
    const char *sel;
    void *imp;
} g_ui_methods[] = {
#include "ui_widget_methods.inc"
#include "ui_input_methods.inc"
#include "ui_dialog_methods.inc"
    { "initWithRootViewController:", (void *)nav_init_root },
    { "viewControllers", (void *)container_children },
    { "childViewControllers", (void *)container_children },
    { "setViewControllers:", (void *)container_set_array },
    { "setViewControllers:animated:", (void *)container_set_array_animated },
    { "selectedIndex", (void *)tab_index },
    { "setSelectedIndex:", (void *)tab_select },
    { "selectedViewController", (void *)container_selected },
    { "setSelectedViewController:", (void *)tab_select_object },
    { "topViewController", (void *)container_selected },
    { "visibleViewController", (void *)container_selected },
    { "pushViewController:animated:", (void *)nav_push },
    { "popViewControllerAnimated:", (void *)nav_pop },
    { "parentViewController", (void *)controller_parent },
    { "navigationController", (void *)controller_parent },
    { "tabBarController", (void *)controller_parent },
    { "tabBar", (void *)controller_bar },
    { "navigationBar", (void *)controller_bar },
    { "setNavigationBarHidden:animated:", (void *)nav_hide_bar },
    { "tabBarItem", (void *)controller_item },
    { "navigationItem", (void *)controller_item },
    { "setTabBarItem:", (void *)controller_set_item },
    { "initWithTitle:image:selectedImage:", (void *)tab_item_init },
    { "image", (void *)item_image },
    { "selectedImage", (void *)item_image },
    { "setStandardAppearance:", (void *)bar_set_appearance },
    { "setScrollEdgeAppearance:", (void *)bar_set_appearance },
    { "standardAppearance", (void *)bar_appearance },
    { "scrollEdgeAppearance", (void *)bar_appearance },
    { "configureWithDefaultBackground", (void *)appearance_default },
    { "initWithWindowScene:", (void *)win_init_scene },
    { "windowScene", (void *)win_scene },
    { "loadView", (void *)wc_load_view },
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

void chm_ui_shutdown(chm_session *s)
{
    if (!s || s != g_sess) return;
    pan_reset();
    input_reset();
    g_modal = NULL;
    dialogs_shutdown();
    clock_reset();
    tables_shutdown();
    symbols_reset();
    memset(view_index, 0, sizeof(view_index));
    while (views) {
        ui_view *next = views->next;
        free(views->kids);
        while (views->actions) {
            ui_action *next_action = views->actions->next;
            free((void *)views->actions->selector);
            free(views->actions);
            views->actions = next_action;
        }
        free(views->text);
        free(views->placeholder);
        free(views);
        views = next;
    }
    while (controllers) {
        ui_vc *next = controllers->next;
        free(controllers->children);
        free(controllers);
        controllers = next;
    }
    while (items) {
        ui_item *next = items->next;
        free(items);
        items = next;
    }
    while (anchors) { ui_anchor *next = anchors->next; free(anchors); anchors = next; }
    while (constraints) { ui_constraint *next = constraints->next; free(constraints); constraints = next; }
    while (guides) { ui_guide *next = guides->next; free(guides); guides = next; }
    while (fonts) { ui_font *next = fonts->next; free(fonts); fonts = next; }
    layout_dirty = 1;
    layout_busy = 0;
    chm_scene_reset();
    g_sess = NULL;
    g_app = NULL;
    g_screen = NULL;
    g_key_window = NULL;
    g_running = 0;
}

#include "ui_split_methods.inc"

void chm_ui_init(chm_session *s)
{
    int i;

    if (!s) return;
    chm_ui_shutdown(g_sess);
    g_sess = s;
    chm_objc_register_host(s, split_methods, sizeof(split_methods) / sizeof(*split_methods));
    chm_objc_register_host(s, clock_methods, sizeof(clock_methods) / sizeof(*clock_methods));
    chm_objc_register_host(s, image_methods, sizeof(image_methods) / sizeof(*image_methods));
    static const chm_host_method cell_image = {"UITableViewCell", "imageView", "@@:", cell_part, 0};
    chm_objc_register_host(s, &cell_image, 1);
    chm_scene_application_role = chm_foundation_string(CHM_APPLICATION_ROLE);
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
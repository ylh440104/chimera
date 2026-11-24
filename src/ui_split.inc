static void split_defaults(ui_vc *vc)
{
    if (vc->split_widths_ready) return;
    vc->split_fraction = 1.0 / 3.0;
    vc->split_minimum = 0;
    vc->split_maximum = 320;
    vc->split_widths_ready = 1;
}

static long split_effective_mode(ui_vc *vc)
{
    if (vc->split_mode) return vc->split_mode;
    if (vc->split_behavior == 1) return 2;
    if (vc->split_behavior == 2) return 3;
    ui_view *root = as_view(vc->view);
    return root && root->w >= 768 ? 2 : 1;
}

static void split_layout(ui_vc *vc)
{
    ui_view *root = as_view(vc->view);
    if (!root || vc->loading || vc->split_laying_out) return;
    vc->split_laying_out = 1;
    split_defaults(vc);
    long mode = split_effective_mode(vc);
    double width = fmax(0, root->w), height = fmax(0, root->h);
    double primary = fmin(width, fmax(vc->split_minimum, fmin(vc->split_maximum, width * vc->split_fraction)));
    for (size_t pass = vc->child_count; pass > 0; pass--) {
        size_t i = pass - 1;
        if (i >= vc->child_count) continue;
        ui_vc *child = as_vc(vc->children[i]);
        if (!child) continue;
        if (vc->child_count == 2 && i == 0 && mode == 1) {
            controller_appearance(child, 0, 0);
            view_remove(as_view(child->view));
            continue;
        }
        ui_view *view = as_view(chm_objc_send0(child->object, "view"));
        if (!view || !view_add(root, view)) {
            chm_session_log(g_sess, "ui: split child %zu has no mountable view", i);
            continue;
        }
        view->x = vc->child_count == 2 && i == 1 && mode == 2 ? primary : 0;
        view->y = 0;
        view->w = vc->child_count == 1 ? width : i == 0 ? primary : width - view->x;
        view->h = height;
        container_layout(child);
        if (vc->appeared) controller_appearance(child, 1, 0);
    }
    vc->split_laying_out = 0;
    layout_dirty = 1;
}

static void split_sync(ui_vc *vc)
{
    vc->active_child = NULL;
    split_layout(vc);
}

static void *split_delegate(void *self, const char *sel)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    return vc ? vc->split_delegate : NULL;
}

static void split_set_delegate(void *self, const char *sel, void *delegate)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (vc) vc->split_delegate = delegate;
}

static long split_mode(void *self, const char *sel)
{
    ui_vc *vc = as_vc(self);
    if (!vc) return 0;
    return !strcmp(sel, "displayMode") ? split_effective_mode(vc) : vc->split_mode;
}

static void split_notify_mode(ui_vc *vc, long previous)
{
    long mode = split_effective_mode(vc);
    if (mode != previous && vc->split_delegate) {
        const char *sel = "splitViewController:willChangeToDisplayMode:";
        void (*callback)(void *, const char *, void *, long) = chm_objc_find_imp(vc->split_delegate, sel);
        if (callback) callback(vc->split_delegate, sel, vc->object, mode);
    }
    split_layout(vc);
}

static void split_set_mode(void *self, const char *sel, long mode)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc || mode < 0 || mode > 3) return;
    long previous = split_effective_mode(vc);
    vc->split_mode = mode;
    split_notify_mode(vc, previous);
}

static long split_behavior(void *self, const char *sel)
{
    ui_vc *vc = as_vc(self);
    if (!vc) return 0;
    if (!strcmp(sel, "preferredSplitBehavior")) return vc->split_behavior;
    return split_effective_mode(vc) == 3 ? 2 : 1;
}

static void split_set_behavior(void *self, const char *sel, long behavior)
{
    (void)sel;
    ui_vc *vc = as_vc(self);
    if (!vc || behavior < 0 || behavior > 2) return;
    long previous = split_effective_mode(vc);
    vc->split_behavior = behavior;
    split_notify_mode(vc, previous);
}

static double split_width(void *self, const char *sel)
{
    ui_vc *vc = as_vc(self);
    if (!vc) return 0;
    split_defaults(vc);
    if (!strcmp(sel, "preferredPrimaryColumnWidthFraction")) return vc->split_fraction;
    if (!strcmp(sel, "minimumPrimaryColumnWidth")) return vc->split_minimum;
    if (!strcmp(sel, "maximumPrimaryColumnWidth")) return vc->split_maximum;
    ui_view *root = as_view(vc->view);
    double width = root ? fmax(0, root->w) : 0;
    return fmin(width, fmax(vc->split_minimum, fmin(vc->split_maximum, width * vc->split_fraction)));
}

static void split_set_width(void *self, const char *sel, double width)
{
    ui_vc *vc = as_vc(self);
    if (!vc || !isfinite(width) || width < 0) return;
    split_defaults(vc);
    if (!strcmp(sel, "setPreferredPrimaryColumnWidthFraction:")) {
        if (width > 1) return;
        vc->split_fraction = width;
    } else if (!strcmp(sel, "setMinimumPrimaryColumnWidth:")) vc->split_minimum = width;
    else vc->split_maximum = width;
    split_layout(vc);
}

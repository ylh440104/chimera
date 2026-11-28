static void prop_flag(void *self, const char *sel, signed char flag)
{
    ui_view *v = as_view(self);
    if (!v) return;
    if (!strcmp(sel, "setUserInteractionEnabled:")) v->interactive = !!flag;
    else if (!strcmp(sel, "setHidden:")) v->hidden = !!flag;
    else v->clips = !!flag;
}

static signed char prop_get_flag(void *self, const char *sel)
{
    ui_view *v = as_view(self);
    if (!v) return 0;
    if (!strcmp(sel, "isUserInteractionEnabled")) return (signed char)v->interactive;
    return (signed char)v->hidden;
}

static void view_set_autoresizing(void *self, const char *sel, unsigned long mask)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[4] = (long)mask;
}

static unsigned long view_autoresizing(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (unsigned long)value->integers[4] : 0;
}

static void view_set_tint(void *self, const char *sel, void *color)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->values[3] = 0;
    if (value) value->third = color;
}

static void *view_tint(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->third : NULL;
}

static void view_set_content_mode(void *self, const char *sel, long mode)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[5] = mode;
}

static long view_content_mode(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->integers[5] : 0;
}

static void label_set_scale(void *self, const char *sel, signed char flag)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[6] = !!flag;
}

static signed char label_scale(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (signed char)value->integers[6] : 0;
}

static void label_set_factor(void *self, const char *sel, double factor)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->values[7] = factor;
}

static double label_factor(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value && value->values[7] ? value->values[7] : 1;
}

static void layer_set_number(void *self, const char *sel, double number)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setCornerRadius:")) value->values[0] = number;
    else if (!strcmp(sel, "setBorderWidth:")) value->values[1] = number;
    else if (!strcmp(sel, "setShadowOpacity:")) value->values[2] = number;
    else value->values[3] = number;
}

static double layer_number(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return 0;
    if (!strcmp(sel, "cornerRadius")) return value->values[0];
    if (!strcmp(sel, "borderWidth")) return value->values[1];
    if (!strcmp(sel, "shadowOpacity")) return value->values[2];
    return value->values[3];
}

static void layer_set_color(void *self, const char *sel, void *color)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setBorderColor:")) value->first = color;
    else value->second = color;
}

static void *layer_color(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return NULL;
    return !strcmp(sel, "borderColor") ? value->first : value->second;
}

static void layer_set_flag(void *self, const char *sel, signed char flag)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return;
    value->integers[7] = !!flag;
}

static signed char layer_flag(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (signed char)value->integers[7] : 0;
}

static void toolbar_set_items(void *self, const char *sel, void *items)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return;
    value->first = items;
    ui_view *v = as_view(self);
    if (v) layout_dirty = 1;
}

static void *toolbar_items(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void text_field_part(void *self, const char *sel, void *object)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setRightView:")) value->first = object;
    else if (!strcmp(sel, "setInputView:")) value->second = object;
    else value->third = object;
}

static void *text_field_get(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return NULL;
    if (!strcmp(sel, "rightView")) return value->first;
    if (!strcmp(sel, "inputView")) return value->second;
    return value->third;
}

static void button_set_edge_insets(void *self, const char *sel, ui_insets inset)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return;
    value->values[4] = inset.top;
    value->values[5] = inset.left;
    value->values[6] = inset.bottom;
    value->values[7] = inset.right;
}

static ui_insets button_edge_insets(void *self, const char *sel)
{
    (void)sel;
    ui_insets inset = {0, 0, 0, 0};
    launch_object *value = launch_find(self);
    if (!value) return inset;
    inset.top = value->values[4];
    inset.left = value->values[5];
    inset.bottom = value->values[6];
    inset.right = value->values[7];
    return inset;
}

static void *button_part(void *self, const char *sel)
{
    (void)sel;
    void *label = view_child(self, "UILabel");
    if (label) return label;
    ui_view *v = as_view(self);
    if (!v) return NULL;
    void *object = chm_foundation_alloc_obj("UILabel", 0);
    ui_view *kid = as_view(object);
    if (!kid || !view_add(v, kid)) return NULL;
    kid->interactive = 0;
    layout_dirty = 1;
    return object;
}

static void button_set_alignment(void *self, const char *sel, long alignment)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[6] = alignment;
}

static long button_alignment(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->integers[6] : 0;
}

static void button_set_attributed(void *self, const char *sel, void *text, unsigned long state)
{
    (void)sel;
    if (!text) return;
    const char *content = chm_foundation_cstr(chm_objc_send0(text, "string"));
    if (!content) return;
    ui_view *v = as_view(self);
    if (!v) return;
    char *copy = strdup(content);
    if (!copy) return;
    free(v->text);
    v->text = copy;
    launch_object *value = launch_record(self);
    if (value) value->integers[5] = (long)state;
    layout_dirty = 1;
}

static void scroll_behavior(void *self, const char *sel, long behavior)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[5] = behavior;
}

static long scroll_get_behavior(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->integers[5] : 0;
}

static void scroll_indicator(void *self, const char *sel, signed char visible)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setShowsHorizontalScrollIndicator:")) value->integers[3] = !!visible;
    else value->integers[4] = !!visible;
}

static signed char scroll_get_indicator(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return 1;
    long value_state = !strcmp(sel, "showsHorizontalScrollIndicator") ? value->integers[3] : value->integers[4];
    return (signed char)(value_state != 0);
}

static void app_set_idle(void *self, const char *sel, signed char disabled)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[0] = !!disabled;
}

static signed char app_idle(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (signed char)value->integers[0] : 0;
}

static void nav_set_toolbar_hidden(void *self, const char *sel, signed char hidden, signed char animated)
{
    (void)sel;
    (void)animated;
    launch_object *value = launch_record(self);
    if (value) value->integers[0] = !!hidden;
}

static signed char nav_toolbar_hidden(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? (signed char)value->integers[0] : 1;
}

static void *nav_toolbar(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return NULL;
    if (!value->first) {
        value->first = chm_foundation_alloc_obj("UIToolbar", 0);
        ui_view *bar = as_view(value->first);
        if (bar) {
            bar->h = 44;
            bar->w = g_vw;
            bar->y = g_vh - 44;
            layout_dirty = 1;
        }
    }
    return value->first;
}

static void nav_item_set_flag(void *self, const char *sel, signed char flag)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (value) value->integers[0] = !!flag;
}

static void alert_set_part(void *self, const char *sel, void *object)
{
    launch_object *value = launch_record(self);
    if (!value) return;
    if (!strcmp(sel, "setSourceView:")) value->first = object;
    else value->second = object;
}

static void *alert_get_part(void *self, const char *sel)
{
    launch_object *value = launch_find(self);
    if (!value) return NULL;
    return !strcmp(sel, "sourceView") ? value->first : NULL;
}

static void alert_set_rect(void *self, const char *sel, ui_rect rect)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return;
    value->values[0] = rect.x;
    value->values[1] = rect.y;
    value->values[2] = rect.w;
    value->values[3] = rect.h;
}

static void *split_mode_button(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_record(self);
    if (!value) return NULL;
    if (!value->first) value->first = chm_foundation_alloc_obj("UIBarButtonItem", 0);
    return value->first;
}

static void *bar_item_custom(void *self, const char *sel, void *view)
{
    (void)sel;
    if (!view) return NULL;
    launch_object *value = launch_record(self);
    if (!value) return NULL;
    value->first = view;
    ui_view *item = as_view(self);
    (void)item;
    return self;
}

static void *bar_item_view(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value ? value->first : NULL;
}

static void *font_with_size(void *cls, const char *sel, double size)
{
    (void)cls;
    (void)sel;
    if (!isfinite(size) || size <= 0 || size > 512) return NULL;
    void *font = chm_foundation_alloc_obj("UIFont", 0);
    launch_object *value = launch_record(font);
    if (!value) return NULL;
    value->values[0] = size;
    return font;
}

static double toolkit_font_size(void *self, const char *sel)
{
    (void)sel;
    launch_object *value = launch_find(self);
    return value && value->values[0] ? value->values[0] : 17;
}

static const chm_host_method toolkit_methods[] = {
    {"UIView", "setAutoresizingMask:", "v@:Q", view_set_autoresizing, 0},
    {"UIView", "autoresizingMask", "Q@:", view_autoresizing, 0},
    {"UIView", "setTintColor:", "v@:@", view_set_tint, 0},
    {"UIView", "tintColor", "@@:", view_tint, 0},
    {"UIView", "setContentMode:", "v@:q", view_set_content_mode, 0},
    {"UIView", "contentMode", "q@:", view_content_mode, 0},
    {"UIView", "setOpaque:", "v@:c", prop_flag, 0},
    {"UIView", "isOpaque", "c@:", prop_get_flag, 0},
    {"UIView", "setUserInteractionEnabled:", "v@:c", prop_flag, 0},
    {"UIView", "isUserInteractionEnabled", "c@:", prop_get_flag, 0},
    {"UILabel", "setAdjustsFontSizeToFitWidth:", "v@:c", label_set_scale, 0},
    {"UILabel", "adjustsFontSizeToFitWidth", "c@:", label_scale, 0},
    {"UILabel", "setMinimumScaleFactor:", "v@:d", label_set_factor, 0},
    {"UILabel", "minimumScaleFactor", "d@:", label_factor, 0},
    {"UILabel", "setLineBreakMode:", "v@:q", scroll_behavior, 0},
    {"UILabel", "lineBreakMode", "q@:", scroll_get_behavior, 0},
    {"CALayer", "setCornerRadius:", "v@:d", layer_set_number, 0},
    {"CALayer", "cornerRadius", "d@:", layer_number, 0},
    {"CALayer", "setBorderWidth:", "v@:d", layer_set_number, 0},
    {"CALayer", "borderWidth", "d@:", layer_number, 0},
    {"CALayer", "setShadowOpacity:", "v@:d", layer_set_number, 0},
    {"CALayer", "shadowOpacity", "d@:", layer_number, 0},
    {"CALayer", "setShadowRadius:", "v@:d", layer_set_number, 0},
    {"CALayer", "shadowRadius", "d@:", layer_number, 0},
    {"CALayer", "setBorderColor:", "v@:^v", layer_set_color, 0},
    {"CALayer", "borderColor", "^v@:", layer_color, 0},
    {"CALayer", "setShadowColor:", "v@:^v", layer_set_color, 0},
    {"CALayer", "shadowColor", "^v@:", layer_color, 0},
    {"CALayer", "setMasksToBounds:", "v@:c", layer_set_flag, 0},
    {"CALayer", "masksToBounds", "c@:", layer_flag, 0},
    {"UIToolbar", "setItems:", "v@:@", toolbar_set_items, 0},
    {"UIToolbar", "items", "@@:", toolbar_items, 0},
    {"UITextField", "setRightView:", "v@:@", text_field_part, 0},
    {"UITextField", "rightView", "@@:", text_field_get, 0},
    {"UITextField", "setRightViewMode:", "v@:q", scroll_behavior, 0},
    {"UITextField", "setInputView:", "v@:@", text_field_part, 0},
    {"UITextField", "inputView", "@@:", text_field_get, 0},
    {"UITextField", "setInputAccessoryView:", "v@:@", text_field_part, 0},
    {"UIButton", "setTitleEdgeInsets:", "v@:{UIEdgeInsets=dddd}", button_set_edge_insets, 0},
    {"UIButton", "titleEdgeInsets", "{UIEdgeInsets=dddd}@:", button_edge_insets, 0},
    {"UIButton", "setImageEdgeInsets:", "v@:{UIEdgeInsets=dddd}", button_set_edge_insets, 0},
    {"UIButton", "imageEdgeInsets", "{UIEdgeInsets=dddd}@:", button_edge_insets, 0},
    {"UIButton", "setContentHorizontalAlignment:", "v@:q", button_set_alignment, 0},
    {"UIButton", "contentHorizontalAlignment", "q@:", button_alignment, 0},
    {"UIButton", "setAttributedTitle:forState:", "v@:@Q", button_set_attributed, 0},
    {"UIButton", "imageView", "@@:", button_part, 0},
    {"UIButton", "titleLabel", "@@:", button_part, 0},
    {"UIBarButtonItem", "initWithCustomView:", "@@:@", bar_item_custom, 0},
    {"UIBarButtonItem", "customView", "@@:", bar_item_view, 0},
    {"UIScrollView", "setContentInsetAdjustmentBehavior:", "v@:q", scroll_behavior, 0},
    {"UIScrollView", "contentInsetAdjustmentBehavior", "q@:", scroll_get_behavior, 0},
    {"UIScrollView", "setShowsHorizontalScrollIndicator:", "v@:c", scroll_indicator, 0},
    {"UIScrollView", "showsHorizontalScrollIndicator", "c@:", scroll_get_indicator, 0},
    {"UIScrollView", "setShowsVerticalScrollIndicator:", "v@:c", scroll_indicator, 0},
    {"UIScrollView", "showsVerticalScrollIndicator", "c@:", scroll_get_indicator, 0},
    {"UIApplication", "setIdleTimerDisabled:", "v@:c", app_set_idle, 0},
    {"UIApplication", "isIdleTimerDisabled", "c@:", app_idle, 0},
    {"UINavigationController", "setToolbarHidden:", "v@:c", nav_set_toolbar_hidden, 0},
    {"UINavigationController", "setToolbarHidden:animated:", "v@:cc", nav_set_toolbar_hidden, 0},
    {"UINavigationController", "isToolbarHidden", "c@:", nav_toolbar_hidden, 0},
    {"UINavigationController", "toolbar", "@@:", nav_toolbar, 0},
    {"UINavigationItem", "setLeftItemsSupplementBackButton:", "v@:c", nav_item_set_flag, 0},
    {"UIAlertController", "setSourceView:", "v@:@", alert_set_part, 0},
    {"UIAlertController", "sourceView", "@@:", alert_get_part, 0},
    {"UIAlertController", "setSourceRect:", "v@:{CGRect={CGPoint=dd}{CGSize=dd}}", alert_set_rect, 0},
    {"UISplitViewController", "displayModeButtonItem", "@@:", split_mode_button, 0},
    {"UIFont", "fontWithSize:", "@@:d", font_with_size, 1},
    {"UIFont", "pointSize", "d@:", toolkit_font_size, 0}
};
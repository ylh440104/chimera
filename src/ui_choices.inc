typedef struct {
    char *title;
    void *image;
    double width;
    int disabled;
} ui_segment;

typedef struct ui_segments {
    ui_view *view;
    ui_segment *items;
    size_t count;
    long selected;
    int momentary;
    uint32_t tint;
} ui_segments;

typedef struct {
    long count, selected;
    double width, height;
    void **rows;
} picker_component;

typedef struct ui_picker {
    ui_view *view;
    picker_component *components;
    size_t count;
    int dirty, loading;
} ui_picker;

typedef struct ui_header {
    ui_view *view;
    void *content, *label, *detail, *background, *reuse, *table;
    long section;
    int footer;
} ui_header;

typedef struct header_registration {
    void *table, *cls;
    char *identifier;
    struct header_registration *next;
} header_registration;

static header_registration *header_registrations;

static ui_segments *segments_state(void *object)
{
    if (!chm_objc_is_kind(object, "UISegmentedControl")) return NULL;
    ui_view *view = as_view(object);
    if (!view) return NULL;
    if (!view->segments) {
        view->segments = calloc(1, sizeof(*view->segments));
        if (!view->segments) return NULL;
        view->segments->view = view;
        view->segments->selected = -1;
        view->segments->tint = 0xff0a84ffu;
        view->clips = 1;
        if (view->h <= 0) view->h = 32;
    }
    return view->segments;
}

static void segments_clear(ui_segments *state)
{
    for (size_t i = 0; i < state->count; i++) free(state->items[i].title);
    free(state->items);
    state->items = NULL;
    state->count = 0;
    state->selected = -1;
}

static int segment_insert(ui_segments *state, void *value, size_t index, int image)
{
    if (!state || index > state->count || state->count >= 1024) return 0;
    char *title = !image && value ? strdup(chm_foundation_cstr(value)) : NULL;
    if (!image && value && !title) return 0;
    ui_segment *items = realloc(state->items, (state->count + 1) * sizeof(*items));
    if (!items) { free(title); return 0; }
    state->items = items;
    memmove(items + index + 1, items + index, (state->count - index) * sizeof(*items));
    items[index] = (ui_segment){.title = title, .image = image ? value : NULL};
    state->count++;
    if (state->selected >= (long)index) state->selected++;
    layout_dirty = 1;
    return 1;
}

static void *segments_init(void *self, const char *sel, void *items)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    if (!state || (items && !chm_objc_is_kind(items, "NSArray"))) return NULL;
    size_t count = (size_t)chm_objc_send0(items, "count");
    if (count > 1024) return NULL;
    segments_clear(state);
    for (size_t i = 0; i < count; i++) {
        void *value = chm_objc_send1(items, "objectAtIndex:", (void *)i);
        int image = chm_objc_is_kind(value, "UIImage");
        if ((!image && !chm_objc_is_kind(value, "NSString")) || !segment_insert(state, value, i, image)) { segments_clear(state); return NULL; }
    }
    if (state->view->w <= 0) state->view->w = count * 80;
    return self;
}

static void *segments_frame(void *self, const char *sel, ui_rect frame)
{
    return v_init_frame(self, sel, frame) && segments_state(self) ? self : NULL;
}

static void segments_insert(void *self, const char *sel, void *value, unsigned long index, signed char animated)
{
    (void)animated;
    segment_insert(segments_state(self), value, index, !strcmp(sel, "insertSegmentWithImage:atIndex:animated:"));
}

static void segments_remove(void *self, const char *sel, unsigned long index, signed char animated)
{
    (void)sel; (void)animated;
    ui_segments *state = segments_state(self);
    if (!state || index >= state->count) return;
    free(state->items[index].title);
    memmove(state->items + index, state->items + index + 1, (state->count - index - 1) * sizeof(*state->items));
    state->count--;
    if (state->selected == (long)index) state->selected = -1;
    else if (state->selected > (long)index) state->selected--;
    layout_dirty = 1;
}

static void segments_remove_all(void *self, const char *sel)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    if (state) segments_clear(state);
    layout_dirty = 1;
}

static unsigned long segments_count(void *self, const char *sel)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    return state ? state->count : 0;
}

static void segments_selected_set(void *self, const char *sel, long index)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    if (state && (index == -1 || (index >= 0 && (size_t)index < state->count))) state->selected = index;
}

static long segments_selected(void *self, const char *sel)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    return state ? state->selected : -1;
}

static void segment_value_set(void *self, const char *sel, void *value, unsigned long index)
{
    ui_segments *state = segments_state(self);
    if (!state || index >= state->count) return;
    ui_segment *item = &state->items[index];
    if (!strcmp(sel, "setImage:forSegmentAtIndex:")) { free(item->title); item->title = NULL; item->image = value; }
    else {
        char *copy = value ? strdup(chm_foundation_cstr(value)) : NULL;
        if (value && !copy) return;
        free(item->title); item->title = copy; item->image = NULL;
    }
}

static void *segment_value(void *self, const char *sel, unsigned long index)
{
    ui_segments *state = segments_state(self);
    if (!state || index >= state->count) return NULL;
    ui_segment *item = &state->items[index];
    return !strcmp(sel, "imageForSegmentAtIndex:") ? item->image : item->title ? chm_foundation_string(item->title) : NULL;
}

static void segment_enabled_set(void *self, const char *sel, signed char enabled, unsigned long index)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    if (state && index < state->count) state->items[index].disabled = !enabled;
}

static signed char segment_enabled(void *self, const char *sel, unsigned long index)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    return state && index < state->count && !state->items[index].disabled;
}

static void segment_width_set(void *self, const char *sel, double width, unsigned long index)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    if (state && index < state->count && isfinite(width) && width >= 0) state->items[index].width = width;
}

static double segment_width(void *self, const char *sel, unsigned long index)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    return state && index < state->count ? state->items[index].width : 0;
}

static void segments_momentary_set(void *self, const char *sel, signed char value)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    if (state) state->momentary = value != 0;
}

static signed char segments_momentary(void *self, const char *sel)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    return state && state->momentary;
}

static void segments_tint_set(void *self, const char *sel, void *color)
{
    (void)sel;
    ui_segments *state = segments_state(self);
    ui_color *value = as_color(color);
    if (state) state->tint = value ? argb(value->r, value->g, value->b, value->a) : 0xff0a84ffu;
}

static double segment_span(ui_segments *state, size_t index)
{
    double fixed = 0;
    size_t automatic = 0;
    for (size_t i = 0; i < state->count; i++) {
        if (state->items[i].width > 0) fixed += state->items[i].width;
        else automatic++;
    }
    double width = fmax(0, state->view->w);
    double natural = state->items[index].width;
    if (fixed > width) return fixed > 0 ? natural * width / fixed : 0;
    if (!automatic) return fixed > 0 ? natural * width / fixed : 0;
    return natural > 0 ? natural : (width - fixed) / automatic;
}

static long segment_hit(ui_segments *state, double x)
{
    if (!state || state->view->disabled || x < 0 || x >= state->view->w) return -1;
    double edge = 0;
    for (size_t i = 0; i < state->count; i++) {
        edge += segment_span(state, i);
        if (x < edge) return state->items[i].disabled ? -1 : (long)i;
    }
    return -1;
}

static int segment_activate(ui_segments *state, long index)
{
    if (!state || index < 0 || (size_t)index >= state->count || state->view->disabled || state->items[index].disabled) return 0;
    if (!state->momentary && state->selected == index) return 0;
    state->selected = index;
    send_control_events(state->view, 1u << 12);
    if (state->momentary) state->selected = -1;
    return 1;
}

static ui_picker *picker_state(void *object)
{
    if (!chm_objc_is_kind(object, "UIPickerView")) return NULL;
    ui_view *view = as_view(object);
    if (!view) return NULL;
    if (!view->picker) {
        view->picker = calloc(1, sizeof(*view->picker));
        if (!view->picker) return NULL;
        view->picker->view = view;
        view->picker->dirty = 1;
        view->clips = 1;
        if (view->h <= 0) view->h = 216;
        if (view->w <= 0) view->w = 320;
    }
    return view->picker;
}

static void picker_clear(ui_picker *state)
{
    for (size_t i = 0; i < state->count; i++) {
        picker_component *component = &state->components[i];
        for (long row = 0; row < component->count; row++) view_remove(as_view(component->rows[row]));
        free(component->rows);
    }
    free(state->components);
    state->components = NULL;
    state->count = 0;
}

static void picker_reload(void *self, const char *sel)
{
    (void)sel;
    ui_picker *state = picker_state(self);
    if (!state) return;
    if (state->loading) { state->dirty = 1; return; }
    state->loading = 1;
    state->dirty = 0;
    void *source = state->view->data_source, *delegate = state->view->delegate;
    const char *count_sel = "numberOfComponentsInPickerView:", *rows_sel = "pickerView:numberOfRowsInComponent:";
    long (*count_method)(void *, const char *, void *) = chm_objc_find_imp(source, count_sel);
    long (*rows_method)(void *, const char *, void *, long) = chm_objc_find_imp(source, rows_sel);
    double (*height_method)(void *, const char *, void *, long) = chm_objc_find_imp(delegate, "pickerView:rowHeightForComponent:");
    double (*width_method)(void *, const char *, void *, long) = chm_objc_find_imp(delegate, "pickerView:widthForComponent:");
    long count = count_method ? count_method(source, count_sel, self) : 0;
    if (count < 0 || count > 32) count = 0;
    picker_component *components = count ? calloc((size_t)count, sizeof(*components)) : NULL;
    if (count && !components) { state->loading = 0; return; }
    size_t total = 0;
    for (long i = 0; i < count; i++) {
        picker_component *component = &components[i];
        long rows = rows_method ? rows_method(source, rows_sel, self, i) : 0;
        if (rows < 0 || rows > 4096 || total + (size_t)rows > 8192) rows = 0;
        total += (size_t)rows;
        component->rows = rows ? calloc((size_t)rows, sizeof(*component->rows)) : NULL;
        if (rows && !component->rows) rows = 0;
        component->count = rows;
        long selected = (size_t)i < state->count ? state->components[i].selected : 0;
        component->selected = !rows ? -1 : selected < 0 ? 0 : selected >= rows ? rows - 1 : selected;
        component->height = height_method ? height_method(delegate, "pickerView:rowHeightForComponent:", self, i) : 32;
        component->width = width_method ? width_method(delegate, "pickerView:widthForComponent:", self, i) : 0;
        if (!isfinite(component->height) || component->height < 1 || component->height > 4096) component->height = 32;
        if (!isfinite(component->width) || component->width < 0) component->width = 0;
    }
    picker_clear(state);
    state->components = components;
    state->count = (size_t)count;
    state->loading = 0;
    layout_dirty = 1;
}

static void picker_reload_component(void *self, const char *sel, long component)
{
    ui_picker *state = picker_state(self);
    if (!state) return;
    if (state->loading) { state->dirty = 1; return; }
    if (state->dirty) { picker_reload(self, sel); return; }
    if (component < 0 || (size_t)component >= state->count) return;
    state->loading = 1;
    void *source = state->view->data_source, *delegate = state->view->delegate;
    const char *rows_sel = "pickerView:numberOfRowsInComponent:";
    long (*rows_method)(void *, const char *, void *, long) = chm_objc_find_imp(source, rows_sel);
    long rows = rows_method ? rows_method(source, rows_sel, self, component) : 0;
    size_t total = 0;
    for (size_t i = 0; i < state->count; i++) if (i != (size_t)component) total += (size_t)state->components[i].count;
    if (rows < 0 || rows > 4096 || total + (size_t)rows > 8192) rows = 0;
    void **objects = rows ? calloc((size_t)rows, sizeof(*objects)) : NULL;
    if (rows && !objects) { state->loading = 0; return; }
    picker_component *entry = &state->components[component];
    for (long row = 0; row < entry->count; row++) view_remove(as_view(entry->rows[row]));
    free(entry->rows);
    entry->rows = objects;
    entry->count = rows;
    entry->selected = !rows ? -1 : entry->selected < 0 ? 0 : entry->selected >= rows ? rows - 1 : entry->selected;
    const char *height_sel = "pickerView:rowHeightForComponent:", *width_sel = "pickerView:widthForComponent:";
    double (*height_method)(void *, const char *, void *, long) = chm_objc_find_imp(delegate, height_sel);
    double (*width_method)(void *, const char *, void *, long) = chm_objc_find_imp(delegate, width_sel);
    entry->height = height_method ? height_method(delegate, height_sel, self, component) : 32;
    entry->width = width_method ? width_method(delegate, width_sel, self, component) : 0;
    if (!isfinite(entry->height) || entry->height < 1 || entry->height > 4096) entry->height = 32;
    if (!isfinite(entry->width) || entry->width < 0) entry->width = 0;
    state->loading = 0;
    layout_dirty = 1;
}

static void *picker_init(void *self, const char *sel, ui_rect rect)
{
    return v_init_frame(self, sel, rect) && picker_state(self) ? self : NULL;
}

static void picker_source_set(void *self, const char *sel, void *value)
{
    ui_picker *state = picker_state(self);
    if (!state) return;
    if (!strcmp(sel, "setDataSource:")) state->view->data_source = value;
    else state->view->delegate = value;
    state->dirty = 1;
}

static void *picker_source(void *self, const char *sel)
{
    ui_picker *state = picker_state(self);
    return state ? !strcmp(sel, "dataSource") ? state->view->data_source : state->view->delegate : NULL;
}

static long picker_count(void *self, const char *sel)
{
    ui_picker *state = picker_state(self);
    if (state && state->dirty) picker_reload(self, sel);
    return state ? (long)state->count : 0;
}

static long picker_rows(void *self, const char *sel, long component)
{
    ui_picker *state = picker_state(self);
    if (state && state->dirty) picker_reload(self, sel);
    return state && component >= 0 && (size_t)component < state->count ? state->components[component].count : 0;
}

static long picker_selected(void *self, const char *sel, long component)
{
    ui_picker *state = picker_state(self);
    if (state && state->dirty) picker_reload(self, sel);
    return state && component >= 0 && (size_t)component < state->count ? state->components[component].selected : -1;
}

static int picker_choose(ui_picker *state, long row, long component, int notify)
{
    if (!state || component < 0 || (size_t)component >= state->count) return 0;
    picker_component *entry = &state->components[component];
    if (row < 0 || row >= entry->count || entry->selected == row) return 0;
    entry->selected = row;
    layout_dirty = 1;
    if (notify) {
        const char *sel = "pickerView:didSelectRow:inComponent:";
        void (*callback)(void *, const char *, void *, long, long) = chm_objc_find_imp(state->view->delegate, sel);
        if (callback) callback(state->view->delegate, sel, state->view->object, row, component);
    }
    return 1;
}

static void picker_select(void *self, const char *sel, long row, long component, signed char animated)
{
    (void)animated;
    ui_picker *state = picker_state(self);
    if (state && state->dirty) picker_reload(self, sel);
    picker_choose(state, row, component, 0);
}

static double picker_width(ui_picker *state, size_t index)
{
    double fixed = 0;
    size_t automatic = 0;
    for (size_t i = 0; i < state->count; i++) {
        if (state->components[i].width) fixed += state->components[i].width;
        else automatic++;
    }
    double width = fmax(0, state->view->w), natural = state->components[index].width;
    if (fixed > width || !automatic) return fixed > 0 ? natural * width / fixed : 0;
    return natural ? natural : (width - fixed) / automatic;
}

static ui_point picker_row_size(void *self, const char *sel, long component)
{
    ui_picker *state = picker_state(self);
    if (state && state->dirty) picker_reload(self, sel);
    return state && component >= 0 && (size_t)component < state->count ? (ui_point){picker_width(state, (size_t)component), state->components[component].height} : (ui_point){0, 0};
}

static void *picker_row_view(ui_picker *state, long row, size_t index)
{
    picker_component *component = &state->components[index];
    if (row < 0 || row >= component->count) return NULL;
    if (component->rows[row]) return component->rows[row];
    int loading = state->loading;
    state->loading = 1;
    void *delegate = state->view->delegate;
    const char *sel = "pickerView:viewForRow:forComponent:reusingView:";
    void *(*custom)(void *, const char *, void *, long, long, void *) = chm_objc_find_imp(delegate, sel);
    void *object = custom ? custom(delegate, sel, state->view->object, row, (long)index, NULL) : NULL;
    if (!object) {
        sel = "pickerView:titleForRow:forComponent:";
        void *(*title)(void *, const char *, void *, long, long) = chm_objc_find_imp(delegate, sel);
        void *text = title ? title(delegate, sel, state->view->object, row, (long)index) : NULL;
        object = chm_foundation_alloc_obj("UILabel", 0);
        l_set_text(object, "setText:", text);
        ui_view *label = as_view(object);
        if (label) label->font_align = 1;
    }
    ui_view *view = as_view(object);
    state->loading = loading;
    if (!view || (view->parent && view->parent != state->view)) return NULL;
    for (size_t i = 0; i < state->count; i++)
        for (long other = 0; other < state->components[i].count; other++)
            if (state->components[i].rows[other] == object) return NULL;
    component->rows[row] = object;
    return object;
}

static void *picker_view_for_row(void *self, const char *sel, long row, long component)
{
    ui_picker *state = picker_state(self);
    if (state && state->dirty) picker_reload(self, sel);
    if (!state || component < 0 || (size_t)component >= state->count) return NULL;
    return picker_row_view(state, row, (size_t)component);
}

static int picker_hit(ui_picker *state, double x, double y, long *component, long *row)
{
    if (!state || x < 0 || x >= state->view->w || y < 0 || y >= state->view->h) return 0;
    double edge = 0;
    for (size_t i = 0; i < state->count; i++) {
        edge += picker_width(state, i);
        if (x >= edge) continue;
        picker_component *entry = &state->components[i];
        if (!entry->count) return 0;
        long picked = entry->selected + (long)floor((y - state->view->h / 2) / entry->height + 0.5);
        if (picked < 0 || picked >= entry->count) return 0;
        *component = (long)i;
        *row = picked;
        return 1;
    }
    return 0;
}

static void picker_layout(ui_picker *state)
{
    if (!state || state->dirty || state->loading) return;
    state->loading = 1;
    double x = 0;
    for (size_t i = 0; i < state->count; i++) {
        picker_component *entry = &state->components[i];
        double width = picker_width(state, i), height = entry->height;
        long visible = (long)fmin(64, ceil(state->view->h / height / 2) + 1);
        long first = entry->selected - visible, last = entry->selected + visible;
        for (long row = 0; row < entry->count; row++) {
            if (row < first || row > last) { view_remove(as_view(entry->rows[row])); continue; }
            ui_view *view = as_view(picker_row_view(state, row, i));
            if (!view || !view_add(state->view, view)) continue;
            view->x = x;
            view->y = state->view->h / 2 + (row - entry->selected - 0.5) * height;
            view->w = width;
            view->h = height;
            view->alpha = row == entry->selected ? 1 : 0.55;
        }
        x += width;
    }
    state->loading = 0;
}

static ui_header *header_state(void *object)
{
    if (!chm_objc_is_kind(object, "UITableViewHeaderFooterView")) return NULL;
    ui_view *view = as_view(object);
    if (!view) return NULL;
    if (!view->header) {
        view->header = calloc(1, sizeof(*view->header));
        if (!view->header) return NULL;
        view->header->view = view;
        view->header->section = -1;
        view->clips = 1;
    }
    return view->header;
}

static void *header_part(void *self, const char *sel)
{
    ui_header *state = header_state(self);
    if (!state) return NULL;
    int content = !strcmp(sel, "contentView"), detail = !strcmp(sel, "detailTextLabel");
    void **slot = content ? &state->content : detail ? &state->detail : &state->label;
    if (!*slot) {
        *slot = chm_foundation_alloc_obj(content ? "UIView" : "UILabel", 0);
        ui_view *view = as_view(*slot);
        if (!view) return NULL;
        if (!content) { view->font_size = detail ? 12 : 15; view->text_color = 0xffb3b3bdu; }
        if (!view_add(content ? state->view : as_view(header_part(self, "contentView")), view)) return NULL;
    }
    return *slot;
}

static void *header_init(void *self, const char *sel, void *reuse)
{
    (void)sel;
    ui_header *state = header_state(self);
    if (!state || !header_part(self, "contentView")) return NULL;
    state->reuse = reuse;
    return self;
}

static void *header_reuse(void *self, const char *sel)
{
    (void)sel;
    ui_header *state = header_state(self);
    return state ? state->reuse : NULL;
}

static void header_prepare(void *self, const char *sel)
{
    (void)sel;
    ui_header *state = header_state(self);
    if (state) { state->view->alpha = 1; state->section = -1; }
}

static void header_background_set(void *self, const char *sel, void *object)
{
    (void)sel;
    ui_header *state = header_state(self);
    ui_view *view = as_view(object);
    if (!state || (object && !view)) return;
    if (view && !view_add(state->view, view)) return;
    if (object != state->background) view_remove(as_view(state->background));
    state->background = object;
    if (view && state->view->nkids > 1) {
        memmove(state->view->kids + 1, state->view->kids, (size_t)(state->view->nkids - 1) * sizeof(*state->view->kids));
        state->view->kids[0] = view;
    }
}

static void *header_background(void *self, const char *sel)
{
    (void)sel;
    ui_header *state = header_state(self);
    return state ? state->background : NULL;
}

static void header_register(void *self, const char *sel, void *cls, void *identifier)
{
    (void)sel;
    if (!chm_objc_is_kind(self, "UITableView") || !identifier) return;
    const char *name = chm_foundation_cstr(identifier);
    for (header_registration *entry = header_registrations; entry; entry = entry->next) {
        if (entry->table == self && !strcmp(entry->identifier, name)) { entry->cls = cls; return; }
    }
    header_registration *entry = calloc(1, sizeof(*entry));
    if (!entry) return;
    entry->identifier = strdup(name);
    if (!entry->identifier) { free(entry); return; }
    entry->cls = cls; entry->table = self; entry->next = header_registrations; header_registrations = entry;
}

static void *header_dequeue(void *self, const char *sel, void *identifier)
{
    (void)sel;
    if (!chm_objc_is_kind(self, "UITableView") || !identifier) return NULL;
    const char *name = chm_foundation_cstr(identifier);
    for (ui_view *view = views; view; view = view->next) {
        ui_header *state = view->header;
        if (state && !view->parent && state->table == self && state->reuse && !strcmp(chm_foundation_cstr(state->reuse), name)) {
            state->table = NULL;
            chm_objc_send0(view->object, "prepareForReuse");
            return view->object;
        }
    }
    for (header_registration *entry = header_registrations; entry; entry = entry->next) {
        if (entry->table != self || strcmp(entry->identifier, name) || !entry->cls) continue;
        void *object = chm_objc_send0(entry->cls, "alloc");
        return chm_objc_send1(object, "initWithReuseIdentifier:", identifier);
    }
    return NULL;
}

static void header_layout(ui_header *state)
{
    if (!state) return;
    ui_view *content = as_view(state->content), *background = as_view(state->background);
    if (background) { background->x = background->y = 0; background->w = state->view->w; background->h = state->view->h; }
    if (content) { content->x = content->y = 0; content->w = state->view->w; content->h = state->view->h; }
    ui_view *label = as_view(state->label), *detail = as_view(state->detail);
    double height = state->view->h, split = detail && detail->text && *detail->text ? height * 0.6 : height;
    if (label) { label->x = 16; label->y = 0; label->w = fmax(0, state->view->w - 32); label->h = split; }
    if (detail) { detail->x = 16; detail->y = split; detail->w = fmax(0, state->view->w - 32); detail->h = height - split; }
}

static void choice_layout(ui_view *view)
{
    if (view->header) header_layout(view->header);
    if (view->picker) picker_layout(view->picker);
}

static void choice_prepare(ui_view *view)
{
    if (!view || view->hidden) return;
    if (chm_objc_is_kind(view->object, "UISegmentedControl")) segments_state(view->object);
    if (chm_objc_is_kind(view->object, "UIPickerView")) picker_state(view->object);
    if (view->picker && view->picker->dirty) picker_reload(view->object, "reloadAllComponents");
    choice_layout(view);
    for (int i = 0; i < view->nkids; i++) choice_prepare(view->kids[i]);
}

static void choice_paint(chm_draw_fn draw, void *context, ui_view *view, const chm_draw_command *box, double alpha)
{
    if (view->segments) {
        ui_segments *state = view->segments;
        double x = box->x;
        for (size_t i = 0; i < state->count; i++) {
            chm_draw_command segment = *box;
            segment.kind = 2;
            segment.x = x; segment.width = segment_span(state, i); x += segment.width;
            double opacity = alpha * (view->disabled || state->items[i].disabled ? 0.35 : 1);
            segment.color = paint_color(state->selected == (long)i ? state->tint : 0xff3a3a3cu, opacity);
            if (segment.width <= 0) continue;
            draw(context, &segment);
            segment.x += 4; segment.width = fmax(0, segment.width - 8);
            if (state->items[i].image) paint_symbol(draw, context, state->items[i].image, segment, paint_color(0xffffffffu, opacity));
            else emit_text(draw, context, &segment, state->items[i].title, 15, 1, 1, paint_color(0xffffffffu, opacity));
        }
    }
    if (view->picker) {
        ui_picker *state = view->picker;
        double x = box->x;
        for (size_t i = 0; i < state->count; i++) {
            chm_draw_command band = *box;
            band.x = x; band.width = picker_width(state, i); x += band.width;
            band.height = fmin(view->h, state->components[i].height);
            band.y += (view->h - band.height) / 2;
            band.color = paint_color(0x5548484au, alpha);
            if (band.width > 0 && state->components[i].count) draw(context, &band);
        }
    }
}

static void choices_reset(void)
{
    for (ui_view *view = views; view; view = view->next) {
        if (view->segments) { segments_clear(view->segments); free(view->segments); view->segments = NULL; }
        if (view->picker) { picker_clear(view->picker); free(view->picker); view->picker = NULL; }
        free(view->header); view->header = NULL;
    }
    while (header_registrations) {
        header_registration *next = header_registrations->next;
        free(header_registrations->identifier); free(header_registrations); header_registrations = next;
    }
}

static const chm_host_method choice_methods[] = {
    {"UISegmentedControl", "initWithItems:", "@@:@", segments_init, 0},
    {"UISegmentedControl", "initWithFrame:", "@@:{CGRect={CGPoint=dd}{CGSize=dd}}", segments_frame, 0},
    {"UISegmentedControl", "numberOfSegments", "Q@:", segments_count, 0},
    {"UISegmentedControl", "selectedSegmentIndex", "q@:", segments_selected, 0},
    {"UISegmentedControl", "setSelectedSegmentIndex:", "v@:q", segments_selected_set, 0},
    {"UISegmentedControl", "insertSegmentWithTitle:atIndex:animated:", "v@:@QB", segments_insert, 0},
    {"UISegmentedControl", "insertSegmentWithImage:atIndex:animated:", "v@:@QB", segments_insert, 0},
    {"UISegmentedControl", "removeSegmentAtIndex:animated:", "v@:QB", segments_remove, 0},
    {"UISegmentedControl", "removeAllSegments", "v@:", segments_remove_all, 0},
    {"UISegmentedControl", "setTitle:forSegmentAtIndex:", "v@:@Q", segment_value_set, 0},
    {"UISegmentedControl", "setImage:forSegmentAtIndex:", "v@:@Q", segment_value_set, 0},
    {"UISegmentedControl", "titleForSegmentAtIndex:", "@@:Q", segment_value, 0},
    {"UISegmentedControl", "imageForSegmentAtIndex:", "@@:Q", segment_value, 0},
    {"UISegmentedControl", "setEnabled:forSegmentAtIndex:", "v@:BQ", segment_enabled_set, 0},
    {"UISegmentedControl", "isEnabledForSegmentAtIndex:", "B@:Q", segment_enabled, 0},
    {"UISegmentedControl", "setWidth:forSegmentAtIndex:", "v@:dQ", segment_width_set, 0},
    {"UISegmentedControl", "widthForSegmentAtIndex:", "d@:Q", segment_width, 0},
    {"UISegmentedControl", "setMomentary:", "v@:B", segments_momentary_set, 0},
    {"UISegmentedControl", "isMomentary", "B@:", segments_momentary, 0},
    {"UISegmentedControl", "setSelectedSegmentTintColor:", "v@:@", segments_tint_set, 0},
    {"UIPickerView", "initWithFrame:", "@@:{CGRect={CGPoint=dd}{CGSize=dd}}", picker_init, 0},
    {"UIPickerView", "setDataSource:", "v@:@", picker_source_set, 0},
    {"UIPickerView", "dataSource", "@@:", picker_source, 0},
    {"UIPickerView", "setDelegate:", "v@:@", picker_source_set, 0},
    {"UIPickerView", "delegate", "@@:", picker_source, 0},
    {"UIPickerView", "reloadAllComponents", "v@:", picker_reload, 0},
    {"UIPickerView", "reloadComponent:", "v@:q", picker_reload_component, 0},
    {"UIPickerView", "numberOfComponents", "q@:", picker_count, 0},
    {"UIPickerView", "numberOfRowsInComponent:", "q@:q", picker_rows, 0},
    {"UIPickerView", "selectedRowInComponent:", "q@:q", picker_selected, 0},
    {"UIPickerView", "selectRow:inComponent:animated:", "v@:qqB", picker_select, 0},
    {"UIPickerView", "rowSizeForComponent:", "{CGSize=dd}@:q", picker_row_size, 0},
    {"UIPickerView", "viewForRow:forComponent:", "@@:qq", picker_view_for_row, 0},
    {"UITableViewHeaderFooterView", "initWithReuseIdentifier:", "@@:@", header_init, 0},
    {"UITableViewHeaderFooterView", "reuseIdentifier", "@@:", header_reuse, 0},
    {"UITableViewHeaderFooterView", "contentView", "@@:", header_part, 0},
    {"UITableViewHeaderFooterView", "textLabel", "@@:", header_part, 0},
    {"UITableViewHeaderFooterView", "detailTextLabel", "@@:", header_part, 0},
    {"UITableViewHeaderFooterView", "backgroundView", "@@:", header_background, 0},
    {"UITableViewHeaderFooterView", "setBackgroundView:", "v@:@", header_background_set, 0},
    {"UITableViewHeaderFooterView", "prepareForReuse", "v@:", header_prepare, 0},
    {"UITableView", "registerClass:forHeaderFooterViewReuseIdentifier:", "v@:@@", header_register, 0},
    {"UITableView", "dequeueReusableHeaderFooterViewWithIdentifier:", "@@:@", header_dequeue, 0}
};
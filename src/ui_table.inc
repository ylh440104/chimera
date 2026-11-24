static ui_index *index_state(void *object)
{
    for (ui_index *p = indexes; p; p = p->next) if (p->object == object) return p;
    return NULL;
}

static void *index_new(void *cls, const char *sel, long row, long section)
{
    (void)sel;
    if (cls != chm_foundation_lookup_class("NSIndexPath") || row < 0 || section < 0) return NULL;
    for (ui_index *p = indexes; p; p = p->next)
        if (p->row == row && p->section == section) return p->object;
    ui_index *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->object = chm_foundation_alloc_obj("NSIndexPath", 0);
    if (!p->object) { free(p); return NULL; }
    p->row = row; p->section = section; p->next = indexes; indexes = p;
    return p->object;
}

static long index_part(void *self, const char *sel)
{
    ui_index *p = index_state(self);
    return p ? (!strcmp(sel, "section") ? p->section : p->row) : 0;
}

static ui_table *table_state(void *object)
{
    if (!chm_objc_is_kind(object, "UITableView")) return NULL;
    ui_view *v = as_view(object);
    if (!v) return NULL;
    if (!v->table) {
        v->table = calloc(1, sizeof(*v->table));
        if (!v->table) return NULL;
        v->table->view = v; v->table->row_height = 44; v->table->dirty = 1;
    }
    return v->table;
}

static ui_cell *cell_state(void *object)
{
    if (!chm_objc_is_kind(object, "UITableViewCell")) return NULL;
    ui_view *v = as_view(object);
    if (!v) return NULL;
    if (!v->cell) {
        v->cell = calloc(1, sizeof(*v->cell));
        if (!v->cell) return NULL;
        v->cell->view = v; v->cell->selection_style = 1;
        v->bg = 0xff2c2c2eu; v->clips = 1;
    }
    return v->cell;
}

static void *cell_part(void *self, const char *sel)
{
    ui_cell *c = cell_state(self);
    if (!c) return NULL;
    int content = !strcmp(sel, "contentView"), detail = !strcmp(sel, "detailTextLabel");
    if (detail && !c->style) return NULL;
    int icon = !strcmp(sel, "imageView");
    void **slot = content ? &c->content : detail ? &c->detail : icon ? &c->icon : &c->label;
    if (!*slot) {
        *slot = chm_foundation_alloc_obj(content ? "UIView" : icon ? "UIImageView" : "UILabel", 0);
        ui_view *v = as_view(*slot);
        if (!v) return NULL;
        if (!content && !icon) {
            v->text_color = detail ? 0xffb3b3bdu : 0xffffffffu;
            v->font_size = detail ? 15 : 17;
            v->font_align = detail && c->style != 3 ? 2 : 0;
        }
        ui_view *parent = content ? c->view : as_view(cell_part(self, "contentView"));
        if (!view_add(parent, v)) return NULL;
    }
    return *slot;
}

static void *cell_init(void *self, const char *sel, long style, void *reuse)
{
    (void)sel;
    ui_cell *c = cell_state(self);
    if (!c) return NULL;
    c->style = style; c->reuse = reuse;
    if (!cell_part(self, "contentView")) return NULL;
    return self;
}

static void *cell_reuse(void *self, const char *sel)
{
    (void)sel; ui_cell *c = cell_state(self); return c ? c->reuse : NULL;
}

static void cell_set_accessory(void *self, const char *sel, void *accessory)
{
    (void)sel;
    ui_cell *c = cell_state(self);
    ui_view *v = as_view(accessory);
    if (!c || (accessory && !v)) return;
    ui_view *old_parent = v ? v->parent : NULL;
    if (v && !view_add(c->view, v)) return;
    if (old_parent && old_parent != c->view && old_parent->cell && old_parent->cell->accessory == accessory)
        old_parent->cell->accessory = NULL;
    ui_view *previous = as_view(c->accessory);
    if (accessory != c->accessory && previous && previous->parent == c->view) view_remove(previous);
    c->accessory = accessory;
    layout_dirty = 1;
}

static void *cell_accessory(void *self, const char *sel)
{
    (void)sel; ui_cell *c = cell_state(self); return c ? c->accessory : NULL;
}

static void cell_set_type(void *self, const char *sel, long type)
{
    ui_cell *c = cell_state(self);
    if (!c) return;
    if (!strcmp(sel, "setSelectionStyle:")) c->selection_style = type;
    else c->accessory_type = type;
    layout_dirty = 1;
}

static long cell_type(void *self, const char *sel)
{
    ui_cell *c = cell_state(self);
    return c ? (!strcmp(sel, "selectionStyle") ? c->selection_style : c->accessory_type) : 0;
}

static void cell_selected(void *self, const char *sel, signed char selected, signed char animated)
{
    (void)sel; (void)animated;
    ui_cell *c = cell_state(self);
    if (c) c->selected = !!selected;
}

static int cell_is_selected(void *self, const char *sel)
{
    (void)sel; ui_cell *c = cell_state(self); return c && c->selected;
}

static void cell_layout(ui_cell *c)
{
    double w = c->view->w, h = c->view->h, end = fmax(0, w - 16);
    ui_view *accessory = as_view(c->accessory);
    if (accessory) {
        double aw = accessory->w > 0 ? accessory->w : 51;
        double ah = accessory->h > 0 ? accessory->h : 31;
        accessory->w = fmin(aw, end); accessory->h = fmin(ah, h);
        accessory->x = end - accessory->w; accessory->y = (h - accessory->h) / 2;
        end = fmax(0, accessory->x - 12);
    } else if (c->accessory_type) end = fmax(0, end - 26);
    ui_view *content = as_view(c->content);
    if (content) { content->x = content->y = 0; content->w = end; content->h = h; }
    ui_view *label = as_view(c->label), *detail = as_view(c->detail);
    double start = 16;
    ui_view *icon = as_view(c->icon);
    if (icon) {
        ui_point size = image_size(icon->image, NULL);
        double side = fmax(0, fmin(end - start, fmin(h - 4, fmax(size.x, size.y))));
        icon->x = start; icon->y = (h - side) / 2; icon->w = icon->h = side;
        if (side > 0) start += side + 12;
    }
    double span = fmax(0, end - start);
    if (label) { label->x = start; label->y = 0; label->w = span; label->h = h; }
    if (detail) {
        if (c->style == 3) {
            if (label) label->h = h * 0.55;
            detail->x = start; detail->y = h * 0.55; detail->w = span; detail->h = h * 0.45;
        } else {
            double split = span * 0.55;
            if (label) label->w = fmax(0, split - 8);
            detail->x = start + split; detail->y = 0; detail->w = span - split; detail->h = h;
        }
    }
}

static void *table_dequeue(void *self, const char *sel, void *identifier)
{
    (void)sel;
    ui_table *t = table_state(self);
    if (!t || !identifier) return NULL;
    const char *name = chm_foundation_cstr(identifier);
    for (ui_view *v = views; v; v = v->next) {
        ui_cell *c = v->cell;
        if (c && c->table == t && !v->parent && c->reuse && !strcmp(chm_foundation_cstr(c->reuse), name)) {
            c->table = NULL; c->index = NULL; c->selected = 0;
            void (*prepare)(void *, const char *) = chm_objc_find_imp(v->object, "prepareForReuse");
            if (prepare) prepare(v->object, "prepareForReuse");
            return v->object;
        }
    }
    return NULL;
}

static void cell_prepare(void *self, const char *sel)
{
    (void)sel; ui_cell *c = cell_state(self); if (c) c->selected = 0;
}

static int table_append(ui_table *t, ui_view *view, void *index, double height, int header)
{
    if (!view || !isfinite(height) || height <= 0 || t->count >= 8192) return 0;
    if (t->count == t->capacity) {
        size_t capacity = t->capacity ? t->capacity * 2 : 16;
        ui_row *rows = realloc(t->rows, capacity * sizeof(*rows));
        if (!rows) return 0;
        t->rows = rows; t->capacity = capacity;
    }
    if (!view_add(t->view, view)) return 0;
    t->rows[t->count++] = (ui_row){view, index, t->content_height, height, header};
    t->content_height += height;
    return 1;
}

static void table_clear(ui_table *t)
{
    for (size_t i = 0; i < t->count; ++i) {
        ui_view *v = t->rows[i].view;
        view_remove(v);
        if (v->cell) { v->cell->index = NULL; v->cell->selected = 0; }
    }
    t->count = 0; t->sections = 0; t->content_height = 0; t->selected = NULL;
    free(t->row_counts); t->row_counts = NULL;
}

static int table_supplement(ui_table *table, long section, int footer)
{
    void *self = table->view->object, *delegate = table->view->delegate, *source = table->view->data_source;
    const char *view_sel = footer ? "tableView:viewForFooterInSection:" : "tableView:viewForHeaderInSection:";
    const char *title_sel = footer ? "tableView:titleForFooterInSection:" : "tableView:titleForHeaderInSection:";
    const char *height_sel = footer ? "tableView:heightForFooterInSection:" : "tableView:heightForHeaderInSection:";
    void *(*custom)(void *, const char *, void *, long) = chm_objc_find_imp(delegate, view_sel);
    void *(*title_method)(void *, const char *, void *, long) = chm_objc_find_imp(source, title_sel);
    double (*height_method)(void *, const char *, void *, long) = chm_objc_find_imp(delegate, height_sel);
    void *object = custom ? custom(delegate, view_sel, self, section) : NULL;
    int plain = 0;
    if (!object) {
        void *title = title_method ? title_method(source, title_sel, self, section) : NULL;
        if (!title || !*chm_foundation_cstr(title)) return 1;
        object = chm_foundation_alloc_obj("UILabel", 0);
        l_set_text(object, "setText:", title);
        ui_view *label = as_view(object);
        if (!label) return 0;
        label->font_size = 13; label->text_color = 0xffb3b3bdu;
        plain = 1;
    }
    ui_view *view = as_view(object);
    if (!view || view->parent) return 0;
    double height = height_method ? height_method(delegate, height_sel, self, section) : 32;
    if (height == 0) return 1;
    if (!isfinite(height) || height < 0) height = 32;
    ui_header *header = header_state(object);
    if (header) { header->table = self; header->section = section; header->footer = footer; }
    if (!table_append(table, view, NULL, height, plain ? 1 : 2)) return 0;
    const char *will_sel = footer ? "tableView:willDisplayFooterView:forSection:" : "tableView:willDisplayHeaderView:forSection:";
    void (*will)(void *, const char *, void *, void *, long) = chm_objc_find_imp(delegate, will_sel);
    if (will) will(delegate, will_sel, self, object, section);
    return 1;
}

static void table_reload(void *self, const char *sel)
{
    (void)sel;
    ui_table *t = table_state(self);
    if (!t) return;
    if (t->loading) { t->dirty = 1; return; }
    t->loading = 1; t->dirty = 0;
    table_clear(t);
    void *source = t->view->data_source, *delegate = t->view->delegate;
    long (*sections)(void *, const char *, void *) = chm_objc_find_imp(source, "numberOfSectionsInTableView:");
    long (*rows)(void *, const char *, void *, long) = chm_objc_find_imp(source, "tableView:numberOfRowsInSection:");
    void *(*cell)(void *, const char *, void *, void *) = chm_objc_find_imp(source, "tableView:cellForRowAtIndexPath:");
    double (*row_height)(void *, const char *, void *, void *) = chm_objc_find_imp(delegate, "tableView:heightForRowAtIndexPath:");
    long n = source ? (sections ? sections(source, "numberOfSectionsInTableView:", self) : 1) : 0;
    if (n < 0 || n > 1024) goto failed;
    t->row_counts = n ? calloc((size_t)n, sizeof(*t->row_counts)) : NULL;
    if (n && !t->row_counts) goto failed;
    t->sections = n;
    size_t total = 0;
    for (long section = 0; section < n; ++section) {
        long count = rows ? rows(source, "tableView:numberOfRowsInSection:", self, section) : 0;
        if (count < 0 || count > 4096 || total + (size_t)count > 4096) goto failed;
        t->row_counts[section] = count; total += (size_t)count;
        if (!table_supplement(t, section, 0)) goto failed;
        for (long row = 0; row < count; ++row) {
            void *index = index_new(chm_foundation_lookup_class("NSIndexPath"), NULL, row, section);
            if (!index || !cell) goto failed;
            void *object = cell(source, "tableView:cellForRowAtIndexPath:", self, index);
            ui_cell *c = cell_state(object);
            if (!c || c->view->parent || (c->table && c->table != t)) goto failed;
            double height = row_height ? row_height(delegate, "tableView:heightForRowAtIndexPath:", self, index) : t->row_height;
            if (!isfinite(height) || height <= 0) height = 44;
            c->table = t; c->index = index;
            if (!table_append(t, c->view, index, height, 0)) goto failed;
        }
        if (!table_supplement(t, section, 1)) goto failed;
    }
    chm_session_log(g_sess, "ui: table sections=%ld rows=%zu source=%s", n, total,
                    source ? chm_objc_class_name(*(void **)source) : "nil");
    t->loading = 0;
    table_update(t->view); layout_dirty = 1;
    return;
failed:
    table_clear(t); t->loading = 0;
    chm_session_log(g_sess, "ui: table data source rejected or allocation failed");
    layout_dirty = 1;
}

static void table_update(ui_view *view)
{
    ui_view *background = as_view(view->background_view);
    if (background && background->parent == view) {
        background->x = background->y = 0; background->w = view->w; background->h = view->h;
    }
    ui_table *t = view->table;
    if (!t || t->loading) return;
    view->offset = scroll_limit(view, view->offset);
    for (size_t i = 0; i < t->count; ++i) {
        ui_row *row = &t->rows[i];
        row->view->x = row->header == 1 ? 16 : 0;
        row->view->y = row->top;
        row->view->w = fmax(0, view->w - (row->header == 1 ? 32 : 0));
        row->view->h = row->height;
        if (row->view->header) header_layout(row->view->header);
        if (row->view->cell && row->top + row->height > view->offset.y && row->top < view->offset.y + view->h)
            cell_layout(row->view->cell);
    }
}

static void tables_prepare(ui_view *root)
{
    if (!root || root->hidden) return;
    ui_table *t = root->table;
    if (t && t->dirty && !t->loading) table_reload(root->object, "reloadData");
    if (t) table_update(root);
    for (int i = 0; i < root->nkids; ++i) tables_prepare(root->kids[i]);
}

static int table_select_cell(ui_view *view)
{
    ui_cell *c = view ? view->cell : NULL;
    if (!c || !c->table || !c->index || !c->selection_style) return 0;
    ui_table *t = c->table;
    void *index = c->index;
    void *delegate = t->view->delegate;
    void *(*will)(void *, const char *, void *, void *) = chm_objc_find_imp(delegate, "tableView:willSelectRowAtIndexPath:");
    if (will) index = will(delegate, "tableView:willSelectRowAtIndexPath:", t->view->object, index);
    if (!index) return 0;
    ui_view *selected = NULL;
    for (size_t i = 0; i < t->count; i++) if (t->rows[i].index == index) selected = t->rows[i].view;
    if (!selected) return 0;
    if (t->selected && t->selected->cell) t->selected->cell->selected = 0;
    t->selected = selected; selected->cell->selected = 1;
    void (*did)(void *, const char *, void *, void *) = chm_objc_find_imp(delegate, "tableView:didSelectRowAtIndexPath:");
    ui_index *p = index_state(index);
    chm_session_log(g_sess, "input: table section=%ld row=%ld callback=%s", p ? p->section : -1, p ? p->row : -1, did ? "yes" : "no");
    if (did) did(delegate, "tableView:didSelectRowAtIndexPath:", t->view->object, index);
    return 1;
}

static void table_deselect(void *self, const char *sel, void *index, signed char animated)
{
    (void)sel; (void)animated;
    ui_table *t = table_state(self);
    for (size_t i = 0; t && i < t->count; ++i) {
        ui_row *row = &t->rows[i];
        if (row->index != index || !row->view->cell) continue;
        row->view->cell->selected = 0;
        if (t->selected == row->view) t->selected = NULL;
    }
}

static void *table_cell(void *self, const char *sel, void *index)
{
    (void)sel; ui_table *t = table_state(self);
    if (!index_state(index)) return NULL;
    for (size_t i = 0; t && i < t->count; ++i) if (t->rows[i].index == index) return t->rows[i].view->object;
    return NULL;
}

static long table_sections(void *self, const char *sel)
{
    (void)sel; ui_table *t = table_state(self);
    if (t && t->dirty) table_reload(self, "reloadData");
    return t ? t->sections : 0;
}

static long table_rows(void *self, const char *sel, long section)
{
    (void)sel; ui_table *t = table_state(self);
    if (t && t->dirty) table_reload(self, "reloadData");
    return t && section >= 0 && section < t->sections ? t->row_counts[section] : 0;
}

static void table_row_height(void *self, const char *sel, double height)
{
    (void)sel; ui_table *t = table_state(self);
    if (t && isfinite(height) && height > 0) { t->row_height = height; t->dirty = 1; }
}

static void switch_set(void *self, const char *sel, signed char on)
{
    (void)sel; ui_view *v = as_view(self);
    if (!v || !chm_objc_is_kind(self, "UISwitch")) return;
    v->switch_on = !!on;
    if (v->w <= 0) v->w = 51;
    if (v->h <= 0) v->h = 31;
}

static void switch_set_animated(void *self, const char *sel, signed char on, signed char animated)
{
    (void)animated; switch_set(self, sel, on);
}

static int switch_is_on(void *self, const char *sel)
{
    (void)sel; ui_view *v = as_view(self); return v && v->switch_on;
}

static void tables_shutdown(void)
{
    for (ui_view *v = views; v; v = v->next) {
        if (v->table) { free(v->table->rows); free(v->table->row_counts); free(v->table); v->table = NULL; }
        free(v->cell); v->cell = NULL;
    }
    while (indexes) { ui_index *next = indexes->next; free(indexes); indexes = next; }
}
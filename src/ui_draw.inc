static uint32_t paint_color(uint32_t color, double alpha)
{
    double a = isfinite(alpha) ? fmin(1, fmax(0, alpha)) : 0;
    return (color & 0x00ffffffu) | ((uint32_t)((color >> 24) * a + 0.5) << 24);
}

static void emit_text(chm_draw_fn draw, void *context, const chm_draw_command *box,
                      const char *text, double size, int alignment, int lines, uint32_t color)
{
    if (!text || !*text || box->width <= 0 || box->height <= 0 || !(color >> 24)) return;
    chm_draw_command c = *box;
    c.kind = 1;
    c.text = text;
    c.color = color;
    c.font_size = size;
    c.alignment = alignment;
    c.lines = lines;
    c.clip_left = fmax(c.clip_left, c.x);
    c.clip_top = fmax(c.clip_top, c.y);
    c.clip_right = fmin(c.clip_right, c.x + c.width);
    c.clip_bottom = fmin(c.clip_bottom, c.y + c.height);
    if (c.clip_left < c.clip_right && c.clip_top < c.clip_bottom) draw(context, &c);
}

static int paint_symbol(chm_draw_fn draw, void *context, void *object, chm_draw_command box, uint32_t tint)
{
    ui_symbol_image *img = symbol_image(object);
    if (!img || img->size.x <= 0 || img->size.y <= 0 || box.width <= 0 || box.height <= 0) return 0;
    double scale = fmin(box.width / img->size.x, box.height / img->size.y);
    double w = img->size.x * scale, h = img->size.y * scale;
    ui_rect frame = {box.x + (box.width - w) / 2, box.y + (box.height - h) / 2, w, h};
    ui_rect visible = {box.clip_left, box.clip_top, box.clip_right - box.clip_left, box.clip_bottom - box.clip_top};
    for (size_t i = 0; i < img->count; i++) {
        const ui_symbol_layer *layer = &img->layers[i];
        ui_rect rect = vector_project(frame, layer->rect);
        ui_rect clip = vector_clip(visible, vector_project(frame, layer->clip));
        if (clip.w <= 0 || clip.h <= 0) continue;
        chm_draw_command cmd = box;
        cmd.kind = 3;
        cmd.color = tint;
        cmd.text = layer->symbol->path;
        cmd.x = rect.x; cmd.y = rect.y; cmd.width = rect.w; cmd.height = rect.h;
        cmd.clip_left = clip.x; cmd.clip_top = clip.y;
        cmd.clip_right = clip.x + clip.w; cmd.clip_bottom = clip.y + clip.h;
        draw(context, &cmd);
    }
    return 1;
}

static void paint_bar(chm_draw_fn draw, void *context, ui_view *bar, const chm_draw_command *box, double alpha)
{
    for (ui_vc *vc = controllers; vc; vc = vc->next) {
        if (vc->bar != bar->object) continue;
        if (container_kind(vc) == 2 && vc->child_count) {
            double width = box->width / (double)vc->child_count;
            for (size_t i = 0; i < vc->child_count; i++) {
                ui_vc *child = as_vc(vc->children[i]);
                ui_item *item = child && child->tab_item ? item_state(child->tab_item) : NULL;
                void *title = item && item->title ? item->title : child ? child->title : NULL;
                chm_draw_command label = *box;
                label.x += i * width;
                label.width = width;
                void *image = item ? item->image : NULL;
                if (item && i == vc->selected && item->selected_image) image = item->selected_image;
                chm_draw_command icon = label;
                icon.height = fmin(26, label.height);
                uint32_t tint = paint_color(i == vc->selected ? 0xff007affu : 0xffb3b3bdu, alpha);
                if (image && paint_symbol(draw, context, image, icon, tint)) {
                    label.y += icon.height;
                    label.height -= icon.height;
                }
                emit_text(draw, context, &label, chm_foundation_cstr(title), 13, 1, 1,
                          paint_color(i == vc->selected ? 0xff007affu : 0xffb3b3bdu, alpha));
            }
        } else if (container_kind(vc) == 1 && vc->active_child) {
            ui_vc *child = vc->active_child;
            ui_item *item = child->navigation_item ? item_state(child->navigation_item) : NULL;
            void *title = item && item->title ? item->title : child->title;
            chm_draw_command label = *box;
            label.x += 48;
            label.width = fmax(0, label.width - 96);
            emit_text(draw, context, &label, chm_foundation_cstr(title), 17, 1, 1, paint_color(0xffffffffu, alpha));
            for (int right = 0; right < 2; right++) {
                void *button = item ? (right ? item->right_item : item->left_item) : NULL;
                ui_item *b = button ? item_state(button) : NULL;
                label = *box;
                label.width = fmin(48, box->width);
                if (right) label.x += box->width - label.width;
                if (b) emit_text(draw, context, &label, chm_foundation_cstr(b->title), 22, 1, 1, paint_color(b->disabled ? 0xff666666u : 0xff007affu, alpha));
                else if (!right && vc->child_count > 1) emit_text(draw, context, &label, "‹", 26, 1, 1, paint_color(0xff007affu, alpha));
            }
        }
        break;
    }
}

static void paint_view(chm_draw_fn draw, void *context, ui_view *v, double px, double py,
                       double left, double top, double right, double bottom, double alpha, unsigned depth)
{
    if (!v || v->hidden || depth > 512 || !isfinite(v->x) || !isfinite(v->y) ||
        !isfinite(v->w) || !isfinite(v->h) || !isfinite(v->alpha)) return;
    alpha *= fmin(1, fmax(0, v->alpha));
    if (alpha <= 0 || left >= right || top >= bottom) return;
    chm_draw_command c = {0};
    c.x = px + v->x; c.y = py + v->y; c.width = v->w; c.height = v->h;
    if (v->clips && (c.x >= right || c.y >= bottom || c.x + v->w <= left || c.y + v->h <= top)) return;
    c.clip_left = left; c.clip_top = top; c.clip_right = right; c.clip_bottom = bottom;
    c.color = paint_color(v->cell && v->cell->selected ? 0xff48484au : v->bg, alpha);
    if (v->w > 0 && v->h > 0 && (c.color >> 24)) draw(context, &c);
    if (v->cell && v->w > 0 && v->h > 0) {
        chm_draw_command separator = c;
        separator.x += 16; separator.y += v->h - 1;
        separator.width = fmax(0, v->w - 16); separator.height = 1;
        separator.color = paint_color(0xff48484au, alpha);
        draw(context, &separator);
        if (!v->cell->accessory && v->cell->accessory_type) {
            chm_draw_command marker = c;
            marker.x += fmax(0, v->w - 40); marker.width = fmin(32, v->w);
            emit_text(draw, context, &marker, v->cell->accessory_type == 3 ? "✓" : "›", 22, 1, 1,
                      paint_color(0xffb3b3bdu, alpha));
        }
    }
    if (chm_objc_is_kind(v->object, "UISwitch") && v->w > 0 && v->h > 0) {
        chm_draw_command track = c;
        double opacity = alpha * (v->disabled ? 0.4 : 1);
        track.kind = 2;
        track.color = paint_color(v->switch_on ? 0xff30d158u : 0xff55555au, opacity);
        draw(context, &track);
        double diameter = fmax(0, fmin(v->h - 4, v->w - 4));
        track.x += v->switch_on ? v->w - diameter - 2 : 2;
        track.y += (v->h - diameter) / 2;
        track.width = track.height = diameter;
        track.color = paint_color(0xffffffffu, opacity);
        draw(context, &track);
    }
    if (chm_objc_is_kind(v->object, "UIProgressView") && v->w > 0 && v->h > 0) {
        c.color = paint_color(0xff55555au, alpha); draw(context, &c);
        c.width *= v->progress;
        c.color = paint_color(0xff007affu, alpha);
        if (c.width > 0) draw(context, &c);
        c.width = v->w;
    }
    if (v->image) paint_symbol(draw, context, v->image, c, paint_color(0xff0a84ffu, alpha));
    const char *text = v->text;
    char masked[256];
    uint32_t text_color = v->text_color;
    if (chm_objc_is_kind(v->object, "UITextField")) {
        if (!text || !*text) { text = v->placeholder; text_color = 0xff888888u; }
        else if (v->secure) {
            size_t n = 0;
            for (const unsigned char *p = (const unsigned char *)text; *p && n < sizeof(masked) - 1; p++)
                if ((*p & 0xc0) != 0x80) masked[n++] = '*';
            masked[n] = 0; text = masked;
        }
    }
    if (chm_objc_is_kind(v->object, "UIActivityIndicatorView") && (v->animating || v->show_when_stopped)) text = "◌";
    emit_text(draw, context, &c, text, v->font_size, v->font_align, v->number_of_lines, paint_color(text_color, alpha));
    paint_bar(draw, context, v, &c, alpha);
    choice_paint(draw, context, v, &c, alpha);
    if (v->clips) {
        left = fmax(left, c.x); top = fmax(top, c.y);
        right = fmin(right, c.x + v->w); bottom = fmin(bottom, c.y + v->h);
    }
    for (int i = 0; i < v->nkids; i++) {
        ui_view *child = v->kids[i];
        ui_point shift = child_offset(v, child);
        paint_view(draw, context, child, c.x - shift.x, c.y - shift.y, left, top, right, bottom, alpha, depth + 1);
    }
}

static int prepare_draw(void *context)
{
    (void)context;
    container_layout(as_vc(g_key_window->owner));
    for (ui_vc *modal = g_modal; modal; modal = modal->previous_modal) container_layout(modal);
    tables_prepare(g_key_window);
    dialogs_prepare();
    layout_dirty = 1;
    ui_layout();
    choice_prepare(g_key_window);
    return 1;
}

typedef struct { chm_draw_fn draw; void *context; int width, height; } draw_args;
static int draw_body(void *context)
{
    draw_args *a = context;
    if (!g_key_window) return 0;
    prepare_draw(NULL);
    paint_view(a->draw, a->context, g_key_window, 0, 0, 0, 0, a->width, a->height, 1, 0);
    return 1;
}
int chm_ui_paint(chm_draw_fn draw, void *context, int width, int height)
{
    if (!draw || width <= 0 || height <= 0 || width > 16384 || height > 16384) return -1;
    if (!g_sess) return 0;
    if (g_sess->exit_code == -2) return -2;
    draw_args args = {draw, context, width, height};
    int rc = chm_session_callback(g_sess, draw_body, &args);
    if (rc == -2) g_sess->exit_code = -2;
    return rc;
}

typedef struct { uint32_t *pixels; int width, height; } pixel_target;

static void paint_pixels(void *context, const chm_draw_command *c)
{
    pixel_target *p = context;
    if (c->kind != 0) return;
    double left = fmax(0, fmax(c->x, c->clip_left)), top = fmax(0, fmax(c->y, c->clip_top));
    double right = fmin(p->width, fmin(c->x + c->width, c->clip_right));
    double bottom = fmin(p->height, fmin(c->y + c->height, c->clip_bottom));
    if (left >= right || top >= bottom) return;
    blend_fill(p->pixels, p->width, p->height, (int)ceil(left), (int)ceil(top), (int)ceil(right), (int)ceil(bottom), c->color);
}

int chm_ui_render(uint32_t *buf, int width, int height)
{
    if (!buf || width <= 0 || height <= 0 || width > 16384 || height > 16384) return -1;
    memset(buf, 0, (size_t)width * (size_t)height * sizeof(*buf));
    pixel_target target = {buf, width, height};
    return chm_ui_paint(paint_pixels, &target, width, height);
}
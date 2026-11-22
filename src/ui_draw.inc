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
    if (chm_objc_is_kind(v->object, "UIActivityIndicatorView") && v->animating) text = "◌";
    emit_text(draw, context, &c, text, v->font_size, v->font_align, v->number_of_lines, paint_color(text_color, alpha));
    paint_bar(draw, context, v, &c, alpha);
    if (v->clips) {
        left = fmax(left, c.x); top = fmax(top, c.y);
        right = fmin(right, c.x + v->w); bottom = fmin(bottom, c.y + v->h);
    }
    for (int i = 0; i < v->nkids; i++)
        paint_view(draw, context, v->kids[i], c.x, c.y, left, top, right, bottom, alpha, depth + 1);
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
    return 1;
}

int chm_ui_paint(chm_draw_fn draw, void *context, int width, int height)
{
    if (!draw || width <= 0 || height <= 0 || width > 16384 || height > 16384) return -1;
    if (!g_key_window || !g_sess) return 0;
    if (g_sess->exit_code == -2) return -2;
    int rc = chm_session_callback(g_sess, prepare_draw, NULL);
    if (rc < 0) { if (rc == -2) g_sess->exit_code = -2; return rc; }
    paint_view(draw, context, g_key_window, 0, 0, 0, 0, width, height, 1, 0);
    return 1;
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
typedef struct byte_formatter {
    void *object, *locale;
    long style;
    unsigned long units;
    signed char nonnumeric, adaptive, padding, count, unit, actual;
    struct byte_formatter *next;
} byte_formatter;

static byte_formatter *byte_formatters;

static byte_formatter byte_defaults(void)
{
    return (byte_formatter){.nonnumeric = 1, .adaptive = 1, .count = 1, .unit = 1};
}

static byte_formatter *byte_find(void *self)
{
    for (byte_formatter *entry = byte_formatters; entry; entry = entry->next) if (entry->object == self) return entry;
    if (!self) return NULL;
    byte_formatter *entry = malloc(sizeof(*entry));
    if (!entry) return NULL;
    *entry = byte_defaults();
    entry->object = self;
    entry->next = byte_formatters;
    byte_formatters = entry;
    return entry;
}

static void *byte_init(void *self, const char *sel)
{
    (void)sel;
    return byte_find(self) ? self : NULL;
}

static void byte_option_set(void *self, const char *sel, signed char flag)
{
    byte_formatter *entry = byte_find(self);
    if (!entry) return;
    signed char value = flag != 0;
    if (!strcmp(sel, "setAllowsNonnumericFormatting:")) entry->nonnumeric = value;
    else if (!strcmp(sel, "setAdaptive:")) entry->adaptive = value;
    else if (!strcmp(sel, "setZeroPadsFractionDigits:")) entry->padding = value;
    else if (!strcmp(sel, "setIncludesCount:")) entry->count = value;
    else if (!strcmp(sel, "setIncludesUnit:")) entry->unit = value;
    else entry->actual = value;
}

static signed char byte_option_get(void *self, const char *sel)
{
    byte_formatter *entry = byte_find(self);
    if (!entry) return 0;
    if (!strcmp(sel, "allowsNonnumericFormatting")) return entry->nonnumeric;
    if (!strcmp(sel, "isAdaptive")) return entry->adaptive;
    if (!strcmp(sel, "zeroPadsFractionDigits")) return entry->padding;
    if (!strcmp(sel, "includesCount")) return entry->count;
    if (!strcmp(sel, "includesUnit")) return entry->unit;
    return entry->actual;
}

static void byte_style_set(void *self, const char *sel, long style)
{
    (void)sel;
    byte_formatter *entry = byte_find(self);
    if (entry && style >= 0 && style <= 3) entry->style = style;
}

static long byte_style_get(void *self, const char *sel)
{
    (void)sel;
    byte_formatter *entry = byte_find(self);
    return entry ? entry->style : 0;
}

static void byte_units_set(void *self, const char *sel, unsigned long units)
{
    (void)sel;
    byte_formatter *entry = byte_find(self);
    if (entry) entry->units = units & 0x1ff;
}

static unsigned long byte_units_get(void *self, const char *sel)
{
    (void)sel;
    byte_formatter *entry = byte_find(self);
    return entry ? entry->units : 0;
}

static void byte_locale_set(void *self, const char *sel, void *locale)
{
    (void)sel;
    byte_formatter *entry = byte_find(self);
    region_value *value = region_find(locale);
    if (entry && (!locale || (value && value->kind == 1))) entry->locale = locale;
}

static void *byte_locale_get(void *self, const char *sel)
{
    (void)sel;
    byte_formatter *entry = byte_find(self);
    return entry && entry->locale ? entry->locale : locale_current(chm_foundation_lookup_class("NSLocale"), NULL);
}

static void *byte_number(byte_formatter *entry, double amount, int64_t exact, int unit, int integer)
{
    if (!icu_ready()) return NULL;
    region_value *locale = region_find(entry->locale);
    int32_t status = 0;
    void *formatter = icu.number_open(1, NULL, 0, locale ? locale->name : region_default_locale(), NULL, &status);
    if (!formatter || status > 0) { if (formatter) icu.number_close(formatter); return NULL; }
    int precision = unit <= 1 ? 0 : unit == 2 ? 1 : 2;
    icu.number_attribute(formatter, 1, 1);
    if (integer) icu.number_attribute(formatter, 6, 0);
    else if (entry->adaptive) {
        icu.number_attribute(formatter, 6, precision);
        icu.number_attribute(formatter, 7, entry->padding ? precision : 0);
    } else {
        icu.number_attribute(formatter, 16, 1);
        icu.number_attribute(formatter, 17, entry->padding ? 3 : 1);
        icu.number_attribute(formatter, 18, 3);
    }
    uint16_t text[256];
    int32_t length = integer ? icu.number_integer(formatter, exact, text, 256, NULL, &status) : icu.number_double(formatter, amount, text, 256, NULL, &status);
    icu.number_close(formatter);
    return status <= 0 && length >= 0 && length < 256 ? icu_string(text, length) : NULL;
}

static void *byte_format(byte_formatter *entry, int64_t count)
{
    if (!entry) return NULL;
    unsigned long units = entry->units ? entry->units : 0x7f;
    double base = entry->style == 1 || entry->style == 3 ? 1024 : 1000;
    double absolute = fabs((double)count), factor = 1, selected_factor = 1;
    int selected = -1;
    for (int unit = 0; unit <= 8; unit++, factor *= base) {
        if (!(units & (1ul << unit))) continue;
        if (selected < 0 || absolute >= factor) { selected = unit; selected_factor = factor; }
    }
    if (selected < 0) return NULL;
    static const char *names[] = {"bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    const char *unit_name = !selected && (count == 1 || count == -1) ? "byte" : names[selected];
    void *number;
    if (!count && entry->nonnumeric && entry->unit && entry->count) { number = fo_string_new("Zero"); selected = 1; unit_name = "KB"; }
    else number = byte_number(entry, (double)count / selected_factor, count, selected, selected == 0);
    if (!number) return NULL;
    fo_buffer text = {0};
    int ok = 1;
    if (entry->count) ok = buffer_add(&text, fo_bytes(number), strlen(fo_bytes(number)));
    if (ok && entry->unit) ok = (!entry->count || buffer_add(&text, " ", 1)) && buffer_add(&text, unit_name, strlen(unit_name));
    if (ok && selected && entry->actual && entry->count && entry->unit) {
        void *actual = byte_number(entry, 0, count, 0, 1);
        ok = actual && buffer_add(&text, " (", 2) && buffer_add(&text, fo_bytes(actual), strlen(fo_bytes(actual))) && buffer_add(&text, " bytes)", 7);
    }
    void *result = ok ? fo_string_new(text.data ? text.data : "") : NULL;
    free(text.data);
    return result;
}

static void *byte_string(void *self, const char *sel, int64_t count)
{
    (void)sel;
    return byte_format(byte_find(self), count);
}

static void *byte_string_class(void *cls, const char *sel, int64_t count, long style)
{
    (void)cls; (void)sel;
    if (style < 0 || style > 3) return NULL;
    byte_formatter entry = byte_defaults();
    entry.style = style;
    return byte_format(&entry, count);
}

static void byte_reset(void)
{
    while (byte_formatters) { byte_formatter *next = byte_formatters->next; free(byte_formatters); byte_formatters = next; }
}

static const chm_host_method byte_methods[] = {
    {"NSByteCountFormatter", "init", "@@:", byte_init, 0},
    {"NSByteCountFormatter", "stringFromByteCount:countStyle:", "@@:qq", byte_string_class, 1},
    {"NSByteCountFormatter", "stringFromByteCount:", "@@:q", byte_string, 0},
    {"NSByteCountFormatter", "setCountStyle:", "v@:q", byte_style_set, 0},
    {"NSByteCountFormatter", "countStyle", "q@:", byte_style_get, 0},
    {"NSByteCountFormatter", "setAllowedUnits:", "v@:Q", byte_units_set, 0},
    {"NSByteCountFormatter", "allowedUnits", "Q@:", byte_units_get, 0},
    {"NSByteCountFormatter", "setAllowsNonnumericFormatting:", "v@:B", byte_option_set, 0},
    {"NSByteCountFormatter", "allowsNonnumericFormatting", "B@:", byte_option_get, 0},
    {"NSByteCountFormatter", "setAdaptive:", "v@:B", byte_option_set, 0},
    {"NSByteCountFormatter", "isAdaptive", "B@:", byte_option_get, 0},
    {"NSByteCountFormatter", "setZeroPadsFractionDigits:", "v@:B", byte_option_set, 0},
    {"NSByteCountFormatter", "zeroPadsFractionDigits", "B@:", byte_option_get, 0},
    {"NSByteCountFormatter", "setIncludesCount:", "v@:B", byte_option_set, 0},
    {"NSByteCountFormatter", "includesCount", "B@:", byte_option_get, 0},
    {"NSByteCountFormatter", "setIncludesUnit:", "v@:B", byte_option_set, 0},
    {"NSByteCountFormatter", "includesUnit", "B@:", byte_option_get, 0},
    {"NSByteCountFormatter", "setIncludesActualByteCount:", "v@:B", byte_option_set, 0},
    {"NSByteCountFormatter", "includesActualByteCount", "B@:", byte_option_get, 0},
    {"NSByteCountFormatter", "setLocale:", "v@:@", byte_locale_set, 0},
    {"NSByteCountFormatter", "locale", "@@:", byte_locale_get, 0}
};
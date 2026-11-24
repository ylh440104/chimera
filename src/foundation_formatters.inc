typedef struct region_value {
    void *object;
    char *name;
    int kind;
    struct region_value *next;
} region_value;

typedef struct date_formatter {
    void *object, *locale, *zone;
    char *pattern;
    long date_style, time_style;
    signed char lenient;
    struct date_formatter *next;
} date_formatter;

static region_value *region_values;
static date_formatter *date_formatters;
static char region_locale[256], region_zone[256];

void chm_foundation_set_region(const char *locale, const char *zone)
{
    snprintf(region_locale, sizeof(region_locale), "%s", locale ? locale : "");
    snprintf(region_zone, sizeof(region_zone), "%s", zone ? zone : "");
}

static region_value *region_find(void *object)
{
    for (region_value *value = region_values; value; value = value->next) if (value->object == object) return value;
    return NULL;
}

static const char *region_default_locale(void)
{
    if (*region_locale) return region_locale;
    return icu_ready() ? icu.locale_default() : NULL;
}

static void *region_init(void *self, const char *sel, void *name)
{
    if (!self || region_find(self) || !name || !*fo_bytes(name) || strlen(fo_bytes(name)) >= 256) return NULL;
    int kind = !strcmp(sel, "initWithName:") ? 2 : 1;
    if (kind == 2) {
        int32_t length, status = 0;
        uint16_t *zone = icu_utf16(fo_bytes(name), &length), canonical[256];
        if (!zone) return NULL;
        int8_t system = 0;
        int32_t n = icu.zone_canonical(zone, length, canonical, 256, &system, &status);
        free(zone);
        if (status > 0 || n < 1 || n >= 256) return NULL;
        void *identifier = icu_string(canonical, n);
        if (!identifier || !strcmp(fo_bytes(identifier), "Etc/Unknown")) return NULL;
    }
    region_value *value = calloc(1, sizeof(*value));
    if (!value) return NULL;
    value->name = strdup(fo_bytes(name));
    if (!value->name) { free(value); return NULL; }
    value->object = self;
    value->kind = kind;
    value->next = region_values;
    region_values = value;
    return self;
}

static void *region_new(void *cls, const char *sel, void *name)
{
    void *object = chm_objc_send0(cls, "alloc");
    if (region_init(object, !strcmp(sel, "timeZoneWithName:") ? "initWithName:" : "initWithLocaleIdentifier:", name)) return object;
    free(object);
    return NULL;
}

static void *region_name(void *self, const char *sel)
{
    (void)sel;
    region_value *value = region_find(self);
    return value ? fo_string_new(value->name) : NULL;
}

static void *locale_current(void *cls, const char *sel)
{
    (void)sel;
    const char *name = region_default_locale();
    return name ? region_new(cls, "localeWithLocaleIdentifier:", fo_string_new(name)) : NULL;
}

static void *zone_local(void *cls, const char *sel)
{
    (void)sel;
    if (*region_zone) return region_new(cls, "timeZoneWithName:", fo_string_new(region_zone));
    if (!icu_ready()) return NULL;
    uint16_t zone[256];
    int32_t status = 0, length = icu.zone_default(zone, 256, &status);
    if (status > 0 || length < 1 || length >= 256) return NULL;
    return region_new(cls, "timeZoneWithName:", icu_string(zone, length));
}

static void *zone_offset_new(void *cls, const char *sel, long seconds)
{
    (void)sel;
    if (seconds < -64800 || seconds > 64800) return NULL;
    long magnitude = seconds < 0 ? -seconds : seconds;
    char name[48];
    snprintf(name, sizeof(name), "GMT%c%02ld:%02ld:%02ld", seconds < 0 ? '-' : '+', magnitude / 3600, magnitude / 60 % 60, magnitude % 60);
    return region_new(cls, "timeZoneWithName:", fo_string_new(name));
}

static double date_now_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now)) return NAN;
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void *date_now(void *cls, const char *sel)
{
    return date_unix(cls, sel, date_now_seconds());
}

static void *date_reference(void *cls, const char *sel, double seconds)
{
    return date_unix(cls, sel, seconds + 978307200.0);
}

static long zone_offset(void *self, const char *sel, void *date)
{
    double epoch = !strcmp(sel, "secondsFromGMT") ? date_now_seconds() : fo_isa_is(date, "NSDate") ? ((fo_date *)date)->epoch : NAN;
    region_value *value = region_find(self);
    if (!value || value->kind != 2 || !isfinite(epoch)) return 0;
    int32_t length, status = 0;
    uint16_t *zone = icu_utf16(value->name, &length);
    if (!zone) return 0;
    void *calendar = icu.calendar_open(zone, length, "en_US_POSIX", 1, &status);
    free(zone);
    if (!calendar || status > 0) { if (calendar) icu.calendar_close(calendar); return 0; }
    icu.calendar_millis(calendar, epoch * 1000, &status);
    int32_t offset = icu.calendar_get(calendar, 15, &status);
    int32_t daylight = icu.calendar_get(calendar, 16, &status);
    icu.calendar_close(calendar);
    return status <= 0 ? (offset + daylight) / 1000 : 0;
}

static long zone_now_offset(void *self, const char *sel)
{
    return zone_offset(self, sel, NULL);
}

static date_formatter *date_formatter_find(void *self, int create)
{
    for (date_formatter *entry = date_formatters; entry; entry = entry->next) if (entry->object == self) return entry;
    if (!create || !self) return NULL;
    date_formatter *entry = calloc(1, sizeof(*entry));
    if (!entry) return NULL;
    entry->object = self;
    entry->next = date_formatters;
    date_formatters = entry;
    return entry;
}

static void *date_formatter_init(void *self, const char *sel)
{
    (void)sel;
    return date_formatter_find(self, 1) ? self : NULL;
}

static void date_formatter_set(void *self, const char *sel, void *value)
{
    date_formatter *entry = date_formatter_find(self, 1);
    if (!entry) return;
    if (!strcmp(sel, "setDateFormat:")) {
        if (value && strlen(fo_bytes(value)) > 16384) return;
        char *pattern = value ? strdup(fo_bytes(value)) : NULL;
        if (value && !pattern) return;
        free(entry->pattern);
        entry->pattern = pattern;
    } else {
        region_value *region = region_find(value);
        int kind = !strcmp(sel, "setLocale:") ? 1 : 2;
        if (value && (!region || region->kind != kind)) return;
        if (kind == 1) entry->locale = value;
        else entry->zone = value;
    }
}

static void *date_formatter_get(void *self, const char *sel)
{
    date_formatter *entry = date_formatter_find(self, 1);
    if (!entry) return NULL;
    if (!strcmp(sel, "dateFormat")) return entry->pattern ? fo_string_new(entry->pattern) : NULL;
    if (!strcmp(sel, "locale")) return entry->locale ? entry->locale : locale_current(chm_foundation_lookup_class("NSLocale"), NULL);
    return entry->zone ? entry->zone : zone_local(chm_foundation_lookup_class("NSTimeZone"), NULL);
}

static void date_style_set(void *self, const char *sel, long style)
{
    date_formatter *entry = date_formatter_find(self, 1);
    if (!entry || style < 0 || style > 4) return;
    if (!strcmp(sel, "setDateStyle:")) entry->date_style = style;
    else entry->time_style = style;
    free(entry->pattern);
    entry->pattern = NULL;
}

static long date_style_get(void *self, const char *sel)
{
    date_formatter *entry = date_formatter_find(self, 1);
    return entry ? !strcmp(sel, "dateStyle") ? entry->date_style : entry->time_style : 0;
}

static void date_lenient_set(void *self, const char *sel, signed char lenient)
{
    (void)sel;
    date_formatter *entry = date_formatter_find(self, 1);
    if (entry) entry->lenient = lenient != 0;
}

static signed char date_lenient_get(void *self, const char *sel)
{
    (void)sel;
    date_formatter *entry = date_formatter_find(self, 1);
    return entry ? entry->lenient : 0;
}

static void *date_formatter_open(date_formatter *entry)
{
    if (!entry || !icu_ready()) return NULL;
    region_value *locale = region_find(entry->locale), *zone = region_find(entry->zone);
    int32_t pattern_length = 0, zone_length = 0, status = 0;
    uint16_t *pattern = entry->pattern ? icu_utf16(entry->pattern, &pattern_length) : NULL;
    const char *zone_name = zone ? zone->name : *region_zone ? region_zone : NULL;
    uint16_t *zone_text = zone_name ? icu_utf16(zone_name, &zone_length) : NULL;
    if ((entry->pattern && !pattern) || (zone_name && !zone_text)) { free(pattern); free(zone_text); return NULL; }
    int32_t date_style = entry->pattern ? -2 : entry->date_style ? 4 - (int32_t)entry->date_style : -1;
    int32_t time_style = entry->pattern ? -2 : entry->time_style ? 4 - (int32_t)entry->time_style : -1;
    void *formatter = icu.date_open(time_style, date_style, locale ? locale->name : region_default_locale(), zone_text, zone_length, pattern, pattern_length, &status);
    free(pattern);
    free(zone_text);
    if (status > 0) { if (formatter) icu.date_close(formatter); return NULL; }
    if (formatter) icu.date_lenient(formatter, entry->lenient);
    return formatter;
}

static void *date_format_string(void *self, const char *sel, void *date)
{
    (void)sel;
    if (!fo_isa_is(date, "NSDate") || !isfinite(((fo_date *)date)->epoch)) return NULL;
    date_formatter *entry = date_formatter_find(self, 1);
    if (!entry) return NULL;
    if ((!entry->pattern && !entry->date_style && !entry->time_style) || (entry->pattern && !*entry->pattern)) return fo_string_new("");
    void *formatter = date_formatter_open(entry);
    if (!formatter) { chm_session_log(g_session, "date formatter: ICU unavailable or invalid configuration"); return NULL; }
    int32_t status = 0;
    double milliseconds = ((fo_date *)date)->epoch * 1000;
    int32_t length = icu.date_format(formatter, milliseconds, NULL, 0, NULL, &status);
    void *result = NULL;
    if ((status <= 0 || status == 15) && length >= 0 && length < 65536) {
        uint16_t *text = calloc((size_t)length + 1, sizeof(*text));
        if (text) {
            status = 0;
            length = icu.date_format(formatter, milliseconds, text, length + 1, NULL, &status);
            if (status <= 0) result = icu_string(text, length);
            free(text);
        }
    }
    icu.date_close(formatter);
    return result;
}

static void *date_parse_string(void *self, const char *sel, void *string)
{
    (void)sel;
    if (!string) return NULL;
    date_formatter *entry = date_formatter_find(self, 1);
    void *formatter = date_formatter_open(entry);
    if (!formatter) return NULL;
    int32_t length, status = 0, position = 0;
    uint16_t *text = icu_utf16(fo_bytes(string), &length);
    void *result = NULL;
    if (text) {
        double milliseconds = icu.date_parse(formatter, text, length, &position, &status);
        if (status <= 0 && position == length && position > 0 && isfinite(milliseconds)) result = date_unix(NULL, NULL, milliseconds / 1000);
    }
    free(text);
    icu.date_close(formatter);
    return result;
}

static void formatters_reset(void)
{
    while (date_formatters) {
        date_formatter *next = date_formatters->next;
        free(date_formatters->pattern);
        free(date_formatters);
        date_formatters = next;
    }
    while (region_values) {
        region_value *next = region_values->next;
        free(region_values->name);
        free(region_values);
        region_values = next;
    }
    region_locale[0] = 0;
    region_zone[0] = 0;
}

static const chm_host_method date_formatter_methods[] = {
    {"NSDate", "date", "@@:", date_now, 1},
    {"NSDate", "dateWithTimeIntervalSinceReferenceDate:", "@@:d", date_reference, 1},
    {"NSLocale", "initWithLocaleIdentifier:", "@@:@", region_init, 0},
    {"NSLocale", "localeWithLocaleIdentifier:", "@@:@", region_new, 1},
    {"NSLocale", "currentLocale", "@@:", locale_current, 1},
    {"NSLocale", "autoupdatingCurrentLocale", "@@:", locale_current, 1},
    {"NSLocale", "localeIdentifier", "@@:", region_name, 0},
    {"NSTimeZone", "initWithName:", "@@:@", region_init, 0},
    {"NSTimeZone", "timeZoneWithName:", "@@:@", region_new, 1},
    {"NSTimeZone", "timeZoneForSecondsFromGMT:", "@@:q", zone_offset_new, 1},
    {"NSTimeZone", "localTimeZone", "@@:", zone_local, 1},
    {"NSTimeZone", "systemTimeZone", "@@:", zone_local, 1},
    {"NSTimeZone", "defaultTimeZone", "@@:", zone_local, 1},
    {"NSTimeZone", "name", "@@:", region_name, 0},
    {"NSTimeZone", "secondsFromGMT", "q@:", zone_now_offset, 0},
    {"NSTimeZone", "secondsFromGMTForDate:", "q@:@", zone_offset, 0},
    {"NSDateFormatter", "init", "@@:", date_formatter_init, 0},
    {"NSDateFormatter", "setDateFormat:", "v@:@", date_formatter_set, 0},
    {"NSDateFormatter", "dateFormat", "@@:", date_formatter_get, 0},
    {"NSDateFormatter", "setLocale:", "v@:@", date_formatter_set, 0},
    {"NSDateFormatter", "locale", "@@:", date_formatter_get, 0},
    {"NSDateFormatter", "setTimeZone:", "v@:@", date_formatter_set, 0},
    {"NSDateFormatter", "timeZone", "@@:", date_formatter_get, 0},
    {"NSDateFormatter", "setDateStyle:", "v@:q", date_style_set, 0},
    {"NSDateFormatter", "dateStyle", "q@:", date_style_get, 0},
    {"NSDateFormatter", "setTimeStyle:", "v@:q", date_style_set, 0},
    {"NSDateFormatter", "timeStyle", "q@:", date_style_get, 0},
    {"NSDateFormatter", "setLenient:", "v@:B", date_lenient_set, 0},
    {"NSDateFormatter", "isLenient", "B@:", date_lenient_get, 0},
    {"NSDateFormatter", "stringFromDate:", "@@:@", date_format_string, 0},
    {"NSDateFormatter", "dateFromString:", "@@:@", date_parse_string, 0}
};
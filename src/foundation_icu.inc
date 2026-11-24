#include <dlfcn.h>

typedef struct {
    void *library;
    int attempted;
    uint16_t *(*from_utf8)(uint16_t *, int32_t, int32_t *, const char *, int32_t, int32_t *);
    char *(*to_utf8)(char *, int32_t, int32_t *, const uint16_t *, int32_t, int32_t *);
    const char *(*locale_default)(void);
    int32_t (*zone_default)(uint16_t *, int32_t, int32_t *);
    int32_t (*zone_canonical)(const uint16_t *, int32_t, uint16_t *, int32_t, int8_t *, int32_t *);
    void *(*calendar_open)(const uint16_t *, int32_t, const char *, int32_t, int32_t *);
    void (*calendar_millis)(void *, double, int32_t *);
    int32_t (*calendar_get)(const void *, int32_t, int32_t *);
    void (*calendar_close)(void *);
    void *(*date_open)(int32_t, int32_t, const char *, const uint16_t *, int32_t, const uint16_t *, int32_t, int32_t *);
    int32_t (*date_format)(const void *, double, uint16_t *, int32_t, void *, int32_t *);
    double (*date_parse)(const void *, const uint16_t *, int32_t, int32_t *, int32_t *);
    void (*date_lenient)(void *, int8_t);
    void (*date_close)(void *);
    void *(*number_open)(int32_t, const uint16_t *, int32_t, const char *, void *, int32_t *);
    void (*number_attribute)(void *, int32_t, int32_t);
    int32_t (*number_double)(const void *, double, uint16_t *, int32_t, void *, int32_t *);
    int32_t (*number_integer)(const void *, int64_t, uint16_t *, int32_t, void *, int32_t *);
    void (*number_close)(void *);
    void *(*transform_open)(const uint16_t *, int32_t, int32_t, const uint16_t *, int32_t, void *, int32_t *);
    void (*transform_apply)(const void *, uint16_t *, int32_t *, int32_t, int32_t, int32_t *, int32_t *);
    void (*transform_close)(void *);
} fo_icu;

static fo_icu icu;

static void *icu_symbol(const char *name)
{
    void *address = dlsym(icu.library, name);
    char symbol[128];
    for (int version = 100; !address && version >= 50; version--) {
        snprintf(symbol, sizeof(symbol), "%s_%d", name, version);
        address = dlsym(icu.library, symbol);
    }
    return address;
}

static int icu_ready(void)
{
    if (icu.attempted) return icu.library != NULL;
    icu.attempted = 1;
    const char *libraries[] = {"libicu.so", "libicui18n.so", NULL};
    for (int i = 0; libraries[i] && !icu.library; i++) icu.library = dlopen(libraries[i], RTLD_NOW | RTLD_LOCAL);
    for (int version = 100; !icu.library && version >= 50; version--) {
        char library[64];
        snprintf(library, sizeof(library), "libicui18n.so.%d", version);
        icu.library = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    }
    if (!icu.library) return 0;
    icu.from_utf8 = icu_symbol("u_strFromUTF8");
    icu.to_utf8 = icu_symbol("u_strToUTF8");
    icu.locale_default = icu_symbol("uloc_getDefault");
    icu.zone_default = icu_symbol("ucal_getDefaultTimeZone");
    icu.zone_canonical = icu_symbol("ucal_getCanonicalTimeZoneID");
    icu.calendar_open = icu_symbol("ucal_open");
    icu.calendar_millis = icu_symbol("ucal_setMillis");
    icu.calendar_get = icu_symbol("ucal_get");
    icu.calendar_close = icu_symbol("ucal_close");
    icu.date_open = icu_symbol("udat_open");
    icu.date_format = icu_symbol("udat_format");
    icu.date_parse = icu_symbol("udat_parse");
    icu.date_lenient = icu_symbol("udat_setLenient");
    icu.date_close = icu_symbol("udat_close");
    icu.number_open = icu_symbol("unum_open");
    icu.number_attribute = icu_symbol("unum_setAttribute");
    icu.number_double = icu_symbol("unum_formatDouble");
    icu.number_integer = icu_symbol("unum_formatInt64");
    icu.number_close = icu_symbol("unum_close");
    icu.transform_open = icu_symbol("utrans_openU");
    icu.transform_apply = icu_symbol("utrans_transUChars");
    icu.transform_close = icu_symbol("utrans_close");
    if (!icu.from_utf8 || !icu.to_utf8 || !icu.locale_default || !icu.zone_default || !icu.zone_canonical ||
        !icu.calendar_open || !icu.calendar_millis || !icu.calendar_get || !icu.calendar_close || !icu.date_open ||
        !icu.date_format || !icu.date_parse || !icu.date_lenient || !icu.date_close || !icu.number_open ||
        !icu.number_attribute || !icu.number_double || !icu.number_integer || !icu.number_close) {
        dlclose(icu.library);
        icu.library = NULL;
        return 0;
    }
    return 1;
}

static uint16_t *icu_utf16(const char *text, int32_t *length)
{
    if (!text || !icu_ready() || strlen(text) > 1024 * 1024) return NULL;
    int32_t status = 0, count = 0;
    icu.from_utf8(NULL, 0, &count, text, -1, &status);
    if ((status > 0 && status != 15) || count < 0 || count > 1024 * 1024) return NULL;
    uint16_t *result = calloc((size_t)count + 1, sizeof(*result));
    if (!result) return NULL;
    status = 0;
    icu.from_utf8(result, count + 1, length, text, -1, &status);
    if (status > 0) { free(result); return NULL; }
    return result;
}

static void *icu_string(const uint16_t *text, int32_t length)
{
    if (!icu_ready() || length < 0 || length > 1024 * 1024) return NULL;
    int32_t status = 0, count = 0;
    icu.to_utf8(NULL, 0, &count, text, length, &status);
    if ((status > 0 && status != 15) || count < 0 || count > 4 * 1024 * 1024) return NULL;
    char *bytes = calloc((size_t)count + 1, 1);
    if (!bytes) return NULL;
    status = 0;
    icu.to_utf8(bytes, count + 1, NULL, text, length, &status);
    void *result = status <= 0 ? fo_string_new(bytes) : NULL;
    free(bytes);
    return result;
}
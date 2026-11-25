typedef struct {
    const char *identifier, *parents, *extensions, *mimes;
} ut_declaration;

static const ut_declaration ut_types[] = {
    {"public.item", "", "", ""},
    {"public.content", "", "", ""},
    {"public.data", "public.item", "", "application/octet-stream"},
    {"public.text", "public.data|public.content", "", ""},
    {"public.plain-text", "public.text", "txt|text", "text/plain"},
    {"public.utf8-plain-text", "public.plain-text", "", "text/plain;charset=utf-8|text/plain;charset=\"utf-8\""},
    {"public.image", "public.data|public.content", "", ""},
    {"public.archive", "", "", ""},
    {"public.executable", "", "", ""},
    {"public.composite-content", "public.content", "", ""},
    {"public.source-code", "public.plain-text", "", ""},
    {"public.script", "public.source-code", "", ""},
    {"public.audiovisual-content", "public.data|public.content", "", ""},
    {"public.audio", "public.audiovisual-content", "", ""},
    {"public.movie", "public.audiovisual-content", "", ""},
    {"public.mpeg-4-audio", "public.audio", "", ""},
    {"public.heif-standard", "public.image", "", ""},
    {"com.pkware.zip-archive", "public.data|public.archive", "", ""},
    {"org.gnu.gnu-tar-archive", "public.data|public.archive", "", ""},
    {"com.apple.mach-o-binary", "public.data|public.executable", "", ""},
    {"public.log", "public.text", "", ""},
    {"net.daringfireball.markdown", "public.plain-text", "md|mdown|markdown|text", "text/markdown|text/x-markdown|text/x-web-markdown"},
    {"public.html", "public.text", "html|htm|shtml|shtm", "text/html"},
    {"public.css", "public.text", "css", "text/css"},
    {"com.netscape.javascript-source", "public.script|public.executable", "js|jscript|javascript|mjs", "text/javascript"},
    {"public.json", "public.text", "json", "application/json"},
    {"public.xml", "public.text", "xml", "application/xml|text/xml"},
    {"com.apple.property-list", "public.data", "plist", ""},
    {"com.adobe.pdf", "public.data|public.composite-content", "pdf", "application/pdf"},
    {"public.png", "public.image", "png", "image/png"},
    {"public.jpeg", "public.image", "jpeg|jpg|jpe", "image/jpeg|image/jpg"},
    {"com.compuserve.gif", "public.image", "gif", "image/gif"},
    {"com.microsoft.bmp", "public.image", "bmp|dib", "image/bmp"},
    {"public.tiff", "public.image", "tiff|tif", "image/tiff"},
    {"org.webmproject.webp", "public.image", "webp", "image/webp"},
    {"public.heic", "public.heif-standard", "heic", "image/heic"},
    {"public.svg-image", "public.image|public.xml", "svg|svgz", "image/svg+xml"},
    {"com.microsoft.ico", "public.image", "ico", "image/vnd.microsoft.icon"},
    {"public.zip-archive", "com.pkware.zip-archive", "zip", "application/zip|application/x-zip-compressed"},
    {"org.gnu.gnu-zip-archive", "public.data|public.archive", "gz|gzip", "application/x-gzip|application/gzip"},
    {"public.tar-archive", "org.gnu.gnu-tar-archive", "tar", "application/x-tar|application/tar"},
    {"public.bzip2-archive", "public.data|public.archive", "bz2|bz", "application/x-bzip2|application/x-bzip|application/bzip2|application/bzip|application/x-bz2"},
    {"org.7-zip.7-zip-archive", "public.data|public.archive", "7z", "application/x-7z-compressed"},
    {"com.sun.java-archive", "public.zip-archive|public.executable", "jar", "application/java-archive"},
    {"com.sun.java-class", "public.data|public.executable", "class", ""},
    {"com.apple.itunes.ipa", "public.data|public.executable", "ipa", ""},
    {"com.apple.mach-o-dylib", "com.apple.mach-o-binary", "dylib", ""},
    {"public.mp3", "public.audio", "mp3|mpga", "audio/mpeg|audio/mpeg3|audio/mpg|audio/mp3|audio/x-mpeg|audio/x-mpeg3|audio/x-mpg|audio/x-mp3"},
    {"com.apple.m4a-audio", "public.mpeg-4-audio", "m4a", "audio/x-m4a"},
    {"com.microsoft.waveform-audio", "public.audio", "wav|wave|bwf", "audio/vnd.wave|audio/wav|audio/wave|audio/x-wav"},
    {"org.xiph.ogg-audio", "public.audio", "ogg|oga", "audio/ogg"},
    {"public.mpeg-4", "public.movie", "mp4|mpg4", "video/mp4|video/mp4v-es"},
    {"com.apple.quicktime-movie", "public.movie", "mov|qt", "video/quicktime"},
    {"com.apple.macbinary-archive", "public.data|public.archive", "bin", "application/macbinary|application/x-macbinary"},
    {"com.apple.log", "public.plain-text|public.log", "log", ""}
};

static const struct { const char *short_name, *name; } ut_abbreviations[] = {
    {"0", "UTTypeConformsTo"}, {"1", "public.filename-extension"},
    {"2", "com.apple.ostype"}, {"3", "public.mime-type"},
    {"4", "com.apple.nspboard-type"}, {"5", "public.url-scheme"},
    {"6", "public.data"}, {"7", "public.text"}, {"8", "public.plain-text"},
    {"9", "public.utf16-plain-text"}, {"A", "com.apple.traditional-mac-plain-text"},
    {"B", "public.image"}, {"C", "public.video"}, {"D", "public.audio"},
    {"E", "public.directory"}, {"F", "public.folder"}, {"13", "public.content"}
};

static const char ut_alphabet[] = "abcdefghkmnpqrstuvwxyz0123456789";

static int ut_equal(const char *bytes, size_t length, const char *text)
{
    return strlen(text) == length && !memcmp(bytes, text, length);
}

static char *ut_bytes(void *object, size_t *length, int lower)
{
    int32_t units_count, byte_count;
    uint16_t *units = cf_string_units(object, &units_count);
    if (!units) return NULL;
    char *bytes = cf_string_utf8(units, units_count, &byte_count);
    free(units);
    if (!bytes) return NULL;
    if (byte_count > 640000) { free(bytes); return NULL; }
    *length = (size_t)byte_count;
    if (lower) for (size_t i = 0; i < *length; i++)
        if (bytes[i] >= 'A' && bytes[i] <= 'Z') bytes[i] += 'a' - 'A';
    return bytes;
}

static void *ut_string(const char *bytes, size_t length)
{
    if (length > 1024 * 1024) return NULL;
    fo_string *string = fo_alloc("NSString", sizeof(*string));
    if (!string) return NULL;
    string->bytes = malloc(length + 1);
    if (!string->bytes) { free(string); return NULL; }
    memcpy(string->bytes, bytes, length);
    string->bytes[length] = 0;
    string->length = (int64_t)length;
    if (!cf_owned_add(string, NULL, NULL)) { free(string->bytes); free(string); return NULL; }
    return string;
}

static const ut_declaration *ut_find(const char *identifier, size_t length)
{
    for (size_t i = 0; i < sizeof(ut_types) / sizeof(*ut_types); i++)
        if (ut_equal(identifier, length, ut_types[i].identifier)) return &ut_types[i];
    return NULL;
}

static int ut_contains(const char *list, const char *text, size_t length)
{
    if (!length) return 0;
    while (*list) {
        size_t count = strcspn(list, "|");
        if (count == length && !memcmp(list, text, length)) return 1;
        list += count;
        if (*list) list++;
    }
    return 0;
}

static int ut_conforms(const ut_declaration *type, const char *parent, size_t length, unsigned depth)
{
    if (!type || depth > sizeof(ut_types) / sizeof(*ut_types)) return 0;
    if (ut_equal(parent, length, type->identifier)) return 1;
    const char *p = type->parents;
    while (*p) {
        size_t count = strcspn(p, "|");
        if ((count == length && !memcmp(p, parent, length)) ||
            ut_conforms(ut_find(p, count), parent, length, depth + 1)) return 1;
        p += count;
        if (*p) p++;
    }
    return 0;
}

static size_t ut_escape(char *output, const char *bytes, size_t length)
{
    for (size_t i = 0; i < sizeof(ut_abbreviations) / sizeof(*ut_abbreviations); i++) {
        if (ut_equal(bytes, length, ut_abbreviations[i].name)) {
            size_t n = strlen(ut_abbreviations[i].short_name);
            memcpy(output, ut_abbreviations[i].short_name, n);
            return n;
        }
    }
    size_t count = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (!c || c == ',' || c == ':' || c == '=' || c == '\\') output[count++] = '\\';
        output[count++] = (char)c;
    }
    return count;
}

static void *ut_dynamic(const char *tag_class, size_t class_length, const char *tag, size_t tag_length, const char *parent, size_t parent_length)
{
    size_t capacity = 2 * (class_length + tag_length + parent_length) + 16;
    char *plain = malloc(capacity);
    if (!plain) return NULL;
    size_t count = 0;
    if (parent) {
        memcpy(plain, "?0=", 3);
        count = 3 + ut_escape(plain + 3, parent, parent_length);
        plain[count++] = ':';
    }
    count += ut_escape(plain + count, tag_class, class_length);
    plain[count++] = '=';
    count += ut_escape(plain + count, tag, tag_length);
    size_t encoded_length = (count * 8 + 4) / 5 + 5;
    char *encoded = malloc(encoded_length + 1);
    if (!encoded) { free(plain); return NULL; }
    memcpy(encoded, "dyn.a", 5);
    size_t position = 5;
    uint32_t bits = 0;
    unsigned available = 0;
    for (size_t i = 0; i < count; i++) {
        bits = (bits << 8) | (unsigned char)plain[i];
        available += 8;
        while (available >= 5) {
            available -= 5;
            encoded[position++] = ut_alphabet[(bits >> available) & 31];
        }
    }
    if (available) encoded[position++] = ut_alphabet[(bits << (5 - available)) & 31];
    void *result = ut_string(encoded, position);
    free(encoded);
    free(plain);
    return result;
}

static char *ut_decode(const char *identifier, size_t length, size_t *decoded_length)
{
    if (length <= 5 || memcmp(identifier, "dyn.a", 5) || length > 640000) return NULL;
    char *decoded = malloc((length - 5) * 5 / 8 + 1);
    if (!decoded) return NULL;
    size_t count = 0;
    uint32_t bits = 0;
    unsigned available = 0;
    for (size_t i = 5; i < length; i++) {
        const char *digit = identifier[i] ? strchr(ut_alphabet, identifier[i]) : NULL;
        if (!digit) { free(decoded); return NULL; }
        bits = (bits << 5) | (unsigned)(digit - ut_alphabet);
        available += 5;
        if (available >= 8) {
            available -= 8;
            decoded[count++] = (char)(bits >> available);
        }
    }
    if (available >= 5 || (available && (bits & ((1u << available) - 1)))) { free(decoded); return NULL; }
    decoded[count] = 0;
    *decoded_length = count;
    return decoded;
}

static void ut_expand(char **bytes, size_t *length)
{
    for (size_t i = 0; i < sizeof(ut_abbreviations) / sizeof(*ut_abbreviations); i++) {
        if (ut_equal(*bytes, *length, ut_abbreviations[i].short_name)) {
            *bytes = (char *)ut_abbreviations[i].name;
            *length = strlen(*bytes);
            return;
        }
    }
}

static void *ut_dynamic_tag(const char *identifier, size_t length, const char *tag_class, size_t class_length)
{
    size_t count;
    char *plain = ut_decode(identifier, length, &count);
    if (!plain) return NULL;
    char *key_buffer = malloc(count + 1), *value_buffer = malloc(count + 1);
    if (!key_buffer || !value_buffer) { free(key_buffer); free(value_buffer); free(plain); return NULL; }
    size_t position = count && plain[0] == '?' ? 1 : 0;
    void *result = NULL;
    int valid = 1;
    while (position < count && valid) {
        size_t key_length = 0, value_length = 0;
        int separator = 0, first = 1;
        while (position < count) {
            unsigned char c = (unsigned char)plain[position++];
            if (c == '\\') {
                if (position == count) { valid = 0; break; }
                key_buffer[key_length++] = plain[position++];
            } else if (c == '=') { separator = 1; break; }
            else if (c == ':' || c == ',') { valid = 0; break; }
            else key_buffer[key_length++] = (char)c;
        }
        if (!valid || !separator || !key_length) { valid = 0; break; }
        while (position < count) {
            unsigned char c = (unsigned char)plain[position++];
            if (c == '\\') {
                if (position == count) { valid = 0; break; }
                c = (unsigned char)plain[position++];
                if (first) value_buffer[value_length++] = (char)c;
            } else if (c == ':') break;
            else if (c == ',') first = 0;
            else if (c == '=') { valid = 0; break; }
            else if (first) value_buffer[value_length++] = (char)c;
        }
        char *key = key_buffer, *value = value_buffer;
        ut_expand(&key, &key_length);
        ut_expand(&value, &value_length);
        if (valid && !result && value_length && key_length == class_length && !memcmp(key, tag_class, class_length))
            result = ut_string(value, value_length);
    }
    free(key_buffer);
    free(value_buffer);
    free(plain);
    if (!valid && result) { cf_owned_release(result); result = NULL; }
    return result;
}

static void *ut_create_identifier(void *tag_class_object, void *tag_object, void *parent_object)
{
    size_t class_length = 0, tag_length = 0, parent_length = 0;
    char *tag_class = ut_bytes(tag_class_object, &class_length, 1);
    char *tag = ut_bytes(tag_object, &tag_length, 1);
    char *parent = parent_object ? ut_bytes(parent_object, &parent_length, 1) : NULL;
    void *result = NULL;
    if (!tag_class || !class_length || !tag || (parent_object && !parent) ||
        class_length > 65536 || tag_length > 65536 || parent_length > 65536) goto done;
    int extension = ut_equal(tag_class, class_length, "public.filename-extension");
    int mime = ut_equal(tag_class, class_length, "public.mime-type");
    if (extension || mime) {
        for (size_t i = 0; i < sizeof(ut_types) / sizeof(*ut_types); i++) {
            const ut_declaration *type = &ut_types[i];
            if (!ut_contains(extension ? type->extensions : type->mimes, tag, tag_length)) continue;
            if (parent && !ut_conforms(type, parent, parent_length, 0)) continue;
            result = ut_string(type->identifier, strlen(type->identifier));
            goto done;
        }
    }
    result = ut_dynamic(tag_class, class_length, tag, tag_length, parent, parent_length);
 done:
    free(tag_class);
    free(tag);
    free(parent);
    return result;
}

static void *ut_copy_tag(void *identifier_object, void *tag_class_object)
{
    size_t length = 0, class_length = 0;
    char *identifier = ut_bytes(identifier_object, &length, 1);
    char *tag_class = ut_bytes(tag_class_object, &class_length, 1);
    void *result = NULL;
    if (!identifier || !tag_class || !class_length) goto done;
    const ut_declaration *type = ut_find(identifier, length);
    if (type) {
        const char *tags = ut_equal(tag_class, class_length, "public.filename-extension") ? type->extensions :
            ut_equal(tag_class, class_length, "public.mime-type") ? type->mimes : "";
        size_t n = strcspn(tags, "|");
        if (n) result = ut_string(tags, n);
    } else result = ut_dynamic_tag(identifier, length, tag_class, class_length);
 done:
    free(identifier);
    free(tag_class);
    return result;
}

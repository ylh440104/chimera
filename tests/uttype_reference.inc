static void *ut_ref_field(void *record, const char *name)
{
    void *value = chm_objc_send1(record, "objectForKey:", chm_foundation_string(name));
    return value && !chm_objc_is_kind(value, "NSNull") ? value : NULL;
}

static int ut_ref_compare(void *actual, void *expected, size_t line, const char *name)
{
    if (!actual && !expected) return 1;
    if (actual && expected && !strcmp(chm_foundation_cstr(actual), chm_foundation_cstr(expected))) return 1;
    fprintf(stderr, "Apple reference line %zu %s: actual=%s expected=%s\n", line, name,
            actual ? chm_foundation_cstr(actual) : "<nil>", expected ? chm_foundation_cstr(expected) : "<nil>");
    return 0;
}

static int ut_reference(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    void *cls = chm_foundation_lookup_class("NSJSONSerialization");
    void *(*parse)(void *, const char *, void *, unsigned long, void **) = chm_objc_find_imp(cls, "JSONObjectWithData:options:error:");
    void *(*create)(void *, void *, void *) = chm_foundation_cf_lookup("_UTTypeCreatePreferredIdentifierForTag");
    void *(*copy_tag)(void *, void *) = chm_foundation_cf_lookup("_UTTypeCopyPreferredTagWithClass");
    if (!parse || !create || !copy_tag) { fclose(file); return 0; }
    void *extension_class = chm_foundation_string("public.filename-extension");
    void *mime_class = chm_foundation_string("public.mime-type");
    char *line = NULL;
    size_t capacity = 0, number = 0, checked = 0, nul_cases = 0;
    int ok = 1;
    ssize_t length;
    while (ok && (length = getline(&line, &capacity, file)) >= 0) {
        number++;
        if (strstr(line, "\\u0000")) { nul_cases++; continue; }
        void *data = chm_foundation_data(line, (size_t)length), *error = NULL;
        void *record = data ? parse(cls, "JSONObjectWithData:options:error:", data, 0, &error) : NULL;
        chm_foundation_cf_release(data);
        if (!record || error) { fprintf(stderr, "Invalid reference JSON at line %zu\n", number); ok = 0; break; }
        const char *operation = chm_foundation_cstr(ut_ref_field(record, "operation"));
        if (!strcmp(operation, "environment")) continue;
        int from_tag = !strcmp(operation, "from_tag");
        if (!from_tag && strcmp(operation, "describe")) { ok = 0; break; }
        void *expected = ut_ref_field(record, "result");
        void *type = from_tag ? create(ut_ref_field(record, "tag_class"), ut_ref_field(record, "tag"), ut_ref_field(record, "constraint")) : ut_ref_field(record, "input");
        void *extension = type ? copy_tag(type, extension_class) : NULL;
        void *mime = type ? copy_tag(type, mime_class) : NULL;
        ok = ut_ref_compare(type, ut_ref_field(expected, "identifier"), number, "identifier") &
             ut_ref_compare(extension, ut_ref_field(expected, "extension"), number, "extension") &
             ut_ref_compare(mime, ut_ref_field(expected, "mime"), number, "mime");
        if (from_tag) chm_foundation_cf_release(type);
        chm_foundation_cf_release(extension);
        chm_foundation_cf_release(mime);
        checked++;
    }
    if (ferror(file)) ok = 0;
    free(line);
    fclose(file);
    if (checked != 122 || nul_cases != 1) {
        fprintf(stderr, "Reference coverage: checked=%zu embedded-NUL=%zu\n", checked, nul_cases);
        ok = 0;
    }
    if (ok) puts("UTType: 122 Apple reference queries match identifier, extension and MIME; embedded NUL checked by Darwin fixture");
    return ok;
}

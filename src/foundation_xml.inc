typedef struct xml_document {
    void *object, *delegate, *data, *stream, *error;
    unsigned long line, column;
    int active, aborted, namespaces, prefixes, external;
    struct xml_document *next;
} xml_document;

typedef struct {
    void *name, *local, *uri;
    fo_dict *attributes, *scope, *declarations;
    int started;
} xml_element;

static xml_document *xml_documents;

static xml_document *xml_find(void *self)
{
    for (xml_document *p = xml_documents; p; p = p->next) if (p->object == self) return p;
    return NULL;
}

static void *xml_init(void *self, const char *sel, void *source)
{
    if (!self || xml_find(self) || !source) return NULL;
    void *data = NULL, *stream = NULL;
    if (!strcmp(sel, "initWithData:")) {
        if (!fo_is_data(source)) return NULL;
        fo_data *bytes = source;
        data = fo_data_new(bytes->bytes, bytes->length);
        if (!data) return NULL;
    } else if (!strcmp(sel, "initWithStream:")) stream = source;
    else {
        net_object *url = net_find(source);
        char path[2048];
        if (!url || !url->address || strncmp(url->address, "file://", 7) || !chm_foundation_map_path(url->address + 7, path, sizeof(path))) return NULL;
        data = read_whole_file(path);
        if (!data) return NULL;
    }
    xml_document *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->object = self;
    p->data = data;
    p->stream = stream;
    p->line = 1;
    p->next = xml_documents;
    xml_documents = p;
    return self;
}

static void xml_delegate_set(void *self, const char *sel, void *delegate)
{
    (void)sel;
    xml_document *p = xml_find(self);
    if (p) p->delegate = delegate;
}

static void *xml_property(void *self, const char *sel)
{
    xml_document *p = xml_find(self);
    if (!p) return NULL;
    return !strcmp(sel, "delegate") ? p->delegate : p->error;
}

static unsigned long xml_position(void *self, const char *sel)
{
    xml_document *p = xml_find(self);
    return p ? !strcmp(sel, "lineNumber") ? p->line : p->column : 0;
}

static void xml_flag_set(void *self, const char *sel, signed char value)
{
    xml_document *p = xml_find(self);
    if (!p || p->active) return;
    if (!strcmp(sel, "setShouldProcessNamespaces:")) p->namespaces = value != 0;
    else if (!strcmp(sel, "setShouldReportNamespacePrefixes:")) p->prefixes = value != 0;
    else p->external = value != 0;
}

static signed char xml_flag(void *self, const char *sel)
{
    xml_document *p = xml_find(self);
    return p ? !strcmp(sel, "shouldProcessNamespaces") ? p->namespaces : !strcmp(sel, "shouldReportNamespacePrefixes") ? p->prefixes : p->external : 0;
}

static void xml_notify(xml_document *p, const char *sel, void *value, int argument)
{
    void *method = chm_objc_find_imp(p->delegate, sel);
    if (!method) return;
    if (argument) ((void (*)(void *, const char *, void *, void *))method)(p->delegate, sel, p->object, value);
    else ((void (*)(void *, const char *, void *))method)(p->delegate, sel, p->object);
}

static void xml_failure(xml_document *p, long code, const char *message)
{
    if (p->error) return;
    p->error = make_error("NSXMLParserErrorDomain", code, message);
    xml_notify(p, "parser:parseErrorOccurred:", p->error, 1);
}

static void xml_abort(void *self, const char *sel)
{
    (void)sel;
    xml_document *p = xml_find(self);
    if (p && p->active) p->aborted = 1;
}

static int xml_start(xml_document *p, xml_element *element)
{
    if (element->started) return !p->error && !p->aborted;
    element->started = 1;
    for (int64_t i = 0; i < element->attributes->count; i++) {
        const char *key = fo_bytes(element->attributes->keys[i]);
        const char *prefix = !strcmp(key, "xmlns") ? "" : !strncmp(key, "xmlns:", 6) ? key + 6 : NULL;
        if (prefix) {
            void *name = fo_string_new(prefix), *uri = element->attributes->values[i];
            if (!d_put(element->scope, name, uri) || !d_put(element->declarations, name, uri)) return 0;
        }
    }
    element->local = element->name;
    if (p->namespaces) {
        const char *qualified = fo_bytes(element->name), *colon = strchr(qualified, ':');
        char prefix[8192] = {0};
        if (colon) {
            size_t length = (size_t)(colon - qualified);
            if (!length || length >= sizeof(prefix) || !colon[1] || strchr(colon + 1, ':')) return 0;
            memcpy(prefix, qualified, length);
            element->local = fo_string_new(colon + 1);
        }
        element->uri = d_value(element->scope, NULL, fo_string_new(prefix));
        if (colon && (!element->uri || !*fo_bytes(element->uri))) return 0;
        if (!element->uri) element->uri = fo_string_new("");
        fo_dict *filtered = fo_dict_new();
        if (!filtered) return 0;
        for (int64_t i = 0; i < element->attributes->count; i++) {
            const char *key = fo_bytes(element->attributes->keys[i]);
            if (!strcmp(key, "xmlns") || !strncmp(key, "xmlns:", 6)) continue;
            if (!d_put(filtered, element->attributes->keys[i], element->attributes->values[i])) return 0;
        }
        element->attributes = filtered;
    }
    if (p->prefixes) {
        const char *sel = "parser:didStartMappingPrefix:toURI:";
        void (*callback)(void *, const char *, void *, void *, void *) = chm_objc_find_imp(p->delegate, sel);
        if (callback)
            for (int64_t i = 0; i < element->declarations->count && !p->aborted; i++)
                callback(p->delegate, sel, p->object, element->declarations->keys[i], element->declarations->values[i]);
    }
    if (p->aborted) return 0;
    const char *sel = "parser:didStartElement:namespaceURI:qualifiedName:attributes:";
    void (*callback)(void *, const char *, void *, void *, void *, void *, void *) = chm_objc_find_imp(p->delegate, sel);
    if (callback) callback(p->delegate, sel, p->object, element->local, element->uri, p->namespaces ? element->name : NULL, element->attributes);
    return !p->aborted && !p->error;
}

static void xml_end(xml_document *p, xml_element *element)
{
    const char *sel = "parser:didEndElement:namespaceURI:qualifiedName:";
    void (*callback)(void *, const char *, void *, void *, void *, void *) = chm_objc_find_imp(p->delegate, sel);
    if (callback) callback(p->delegate, sel, p->object, element->local, element->uri, p->namespaces ? element->name : NULL);
    if (p->prefixes && !p->aborted) {
        for (int64_t i = element->declarations->count; i > 0 && !p->aborted; i--)
            xml_notify(p, "parser:didEndMappingPrefix:", element->declarations->keys[i - 1], 1);
    }
}

static void xml_text(xml_document *p, fo_buffer *text)
{
    if (text->size) xml_notify(p, "parser:foundCharacters:", fo_string_new(text->data), 1);
    text->size = 0;
    if (text->data) text->data[0] = 0;
}

static signed char xml_parse(void *self, const char *sel)
{
    (void)sel;
    xml_document *p = xml_find(self);
    if (!p || p->active) return 0;
    p->active = 1;
    p->aborted = 0;
    p->error = NULL;
    p->line = 1;
    p->column = 0;
    fo_buffer input = {0};
    fo_data *data = p->data;
    if (p->external) { xml_failure(p, 26, "External entity resolution is unavailable"); goto finished; }
    if (p->stream) {
        long (*read_bytes)(void *, const char *, void *, unsigned long) = chm_objc_find_imp(p->stream, "read:maxLength:");
        if (!read_bytes) { xml_failure(p, 1, "Input stream does not support reading"); goto finished; }
        chm_objc_send0(p->stream, "open");
        unsigned char chunk[4096];
        for (;;) {
            long count = read_bytes(p->stream, "read:maxLength:", chunk, sizeof(chunk));
            if (!count) break;
            if (count < 0 || count > (long)sizeof(chunk) || input.size > 32 * 1024 * 1024 - (size_t)count || !buffer_add(&input, chunk, (size_t)count)) {
                xml_failure(p, 1, "Cannot read XML input stream"); break;
            }
        }
        chm_objc_send0(p->stream, "close");
        if (p->error) goto finished;
    }
    const unsigned char *bytes = p->stream ? (const unsigned char *)input.data : data ? data->bytes : NULL;
    size_t length = p->stream ? input.size : data && data->length > 0 ? (size_t)data->length : 0;
    if (!length || length > 32 * 1024 * 1024) { xml_failure(p, 4, "Empty or oversized XML document"); goto finished; }
    yxml_t parser;
    char parser_stack[65536];
    yxml_init(&parser, parser_stack, sizeof(parser_stack));
    xml_element elements[129] = {0};
    unsigned depth = 0, nodes = 0;
    void *attribute = NULL, *instruction = NULL;
    fo_buffer value = {0}, text = {0}, pi = {0};
    int failed = 0;
    xml_notify(p, "parserDidStartDocument:", NULL, 0);
    size_t offset = length >= 3 && !memcmp(bytes, "\xef\xbb\xbf", 3) ? 3 : 0;
    for (size_t i = offset; i < length && !p->aborted && !p->error && !failed; i++) {
        yxml_ret_t event = yxml_parse(&parser, bytes[i]);
        p->line = parser.line;
        p->column = parser.byte;
        if (event < 0) { failed = event; break; }
        if (event == YXML_ELEMSTART) {
            if (depth && !xml_start(p, &elements[depth])) { failed = 1; break; }
            xml_text(p, &text);
            if (p->aborted) break;
            if (depth == 128 || ++nodes > 1000000) { failed = 1; break; }
            xml_element *parent = &elements[depth], *element = &elements[++depth];
            memset(element, 0, sizeof(*element));
            element->name = fo_string_new(parser.elem);
            element->attributes = fo_dict_new();
            element->declarations = fo_dict_new();
            element->scope = parent->scope ? d_with_dictionary(chm_foundation_lookup_class("NSMutableDictionary"), NULL, parent->scope) : fo_dict_new();
            if (!element->name || !element->attributes || !element->scope || !element->declarations ||
                !d_put(element->scope, fo_string_new("xml"), fo_string_new("http://www.w3.org/XML/1998/namespace"))) failed = 1;
        } else if (event == YXML_ATTRSTART) {
            attribute = fo_string_new(parser.attr);
            value.size = 0;
            if (value.data) value.data[0] = 0;
            if (!depth || !attribute || d_value(elements[depth].attributes, NULL, attribute)) failed = 1;
        } else if (event == YXML_ATTRVAL) {
            if (!buffer_add(&value, parser.data, strlen(parser.data))) failed = 1;
        } else if (event == YXML_ATTREND) {
            if (!depth || !attribute || !d_put(elements[depth].attributes, attribute, fo_string_new(value.data ? value.data : ""))) failed = 1;
            attribute = NULL;
        } else if (event == YXML_CONTENT) {
            if (depth && !xml_start(p, &elements[depth])) { failed = 1; break; }
            if (!buffer_add(&text, parser.data, strlen(parser.data))) failed = 1;
        } else if (event == YXML_ELEMEND) {
            if (!depth || !xml_start(p, &elements[depth])) { failed = 1; break; }
            xml_text(p, &text);
            if (!p->aborted) xml_end(p, &elements[depth]);
            depth--;
        } else if (event == YXML_PISTART) {
            if (depth && !xml_start(p, &elements[depth])) { failed = 1; break; }
            xml_text(p, &text);
            instruction = fo_string_new(parser.pi);
            pi.size = 0;
            if (pi.data) pi.data[0] = 0;
        } else if (event == YXML_PICONTENT) {
            if (!buffer_add(&pi, parser.data, strlen(parser.data))) failed = 1;
        } else if (event == YXML_PIEND) {
            const char *method = "parser:foundProcessingInstructionWithTarget:data:";
            void (*callback)(void *, const char *, void *, void *, void *) = chm_objc_find_imp(p->delegate, method);
            if (callback) callback(p->delegate, method, p->object, instruction, fo_string_new(pi.data ? pi.data : ""));
        }
    }
    if (p->aborted) xml_failure(p, 512, "Parsing aborted by delegate");
    else if (failed || depth || yxml_eof(&parser) < 0) xml_failure(p, failed == YXML_ECLOSE ? 76 : failed == YXML_EREF ? 26 : 5, "Malformed XML document");
    else {
        xml_text(p, &text);
        if (!p->aborted && !p->error) xml_notify(p, "parserDidEndDocument:", NULL, 0);
        if (p->aborted) xml_failure(p, 512, "Parsing aborted by delegate");
    }
    free(value.data);
    free(text.data);
    free(pi.data);
finished:
    free(input.data);
    p->active = 0;
    return !p->error && !p->aborted;
}

static void xml_reset(void)
{
    while (xml_documents) {
        xml_document *next = xml_documents->next;
        if (xml_documents->data) {
            free(((fo_data *)xml_documents->data)->bytes);
            free(xml_documents->data);
        }
        free(xml_documents);
        xml_documents = next;
    }
}

static const chm_host_method xml_methods[] = {
    {"NSXMLParser", "initWithData:", "@@:@", xml_init, 0},
    {"NSXMLParser", "initWithStream:", "@@:@", xml_init, 0},
    {"NSXMLParser", "initWithContentsOfURL:", "@@:@", xml_init, 0},
    {"NSXMLParser", "setDelegate:", "v@:@", xml_delegate_set, 0},
    {"NSXMLParser", "delegate", "@@:", xml_property, 0},
    {"NSXMLParser", "parserError", "@@:", xml_property, 0},
    {"NSXMLParser", "lineNumber", "q@:", xml_position, 0},
    {"NSXMLParser", "columnNumber", "q@:", xml_position, 0},
    {"NSXMLParser", "parse", "B@:", xml_parse, 0},
    {"NSXMLParser", "abortParsing", "v@:", xml_abort, 0},
    {"NSXMLParser", "setShouldProcessNamespaces:", "v@:B", xml_flag_set, 0},
    {"NSXMLParser", "setShouldReportNamespacePrefixes:", "v@:B", xml_flag_set, 0},
    {"NSXMLParser", "setShouldResolveExternalEntities:", "v@:B", xml_flag_set, 0},
    {"NSXMLParser", "shouldProcessNamespaces", "B@:", xml_flag, 0},
    {"NSXMLParser", "shouldReportNamespacePrefixes", "B@:", xml_flag, 0},
    {"NSXMLParser", "shouldResolveExternalEntities", "B@:", xml_flag, 0}
};
static void url_value_release(void *object);

static void url_value_destroy(void *object)
{
    if (fo_is_string(object)) free(((fo_string *)object)->bytes);
    else if (fo_is_data(object)) free(((fo_data *)object)->bytes);
    else if (fo_is_array(object)) {
        fo_array *value = object;
        for (int64_t i = 0; i < value->count; i++) url_value_release(value->items[i]);
        free(value->items);
    } else if (fo_is_dict(object)) {
        fo_dict *value = object;
        for (int64_t i = 0; i < value->count; i++) {
            url_value_release(value->keys[i]);
            url_value_release(value->values[i]);
        }
        free(value->keys);
        free(value->values);
    }
}

static void url_value_own(void *object)
{
    if (!object || cf_owned_find(object)) return;
    if (fo_is_string(object) || fo_is_data(object) || fo_is_array(object) || fo_is_dict(object) ||
        fo_is_number(object) || fo_isa_is(object, "NSDate")) cf_owned_add(object, NULL, NULL);
}

static void url_value_dispose(void *object)
{
    if (!object) return;
    if (cf_owned_find(object)) {
        cf_owned_release(object);
        return;
    }
    if (fo_is_string(object)) free(((fo_string *)object)->bytes);
    else if (fo_is_data(object)) free(((fo_data *)object)->bytes);
    else if (fo_is_array(object)) free(((fo_array *)object)->items);
    else if (fo_is_dict(object)) { free(((fo_dict *)object)->keys); free(((fo_dict *)object)->values); }
    free(object);
}

static void url_value_release(void *object)
{
    if (!object) return;
    if (fo_is_cfstring(object)) return;
    if (cf_owned_find(object)) cf_owned_release(object);
    else url_value_dispose(object);
}

static void *url_value_adopt(void *object, unsigned depth)
{
    if (!object || depth > 32) return NULL;
    if (fo_is_cfstring(object)) return object;
    if (cf_owned_find(object)) return cf_owned_retain(object);
    if (!fo_is_string(object) && !fo_is_data(object) && !fo_is_array(object) && !fo_is_dict(object) &&
        !fo_is_number(object) && !fo_isa_is(object, "NSDate")) return NULL;
    url_value_own(object);
    if (!cf_owned_find(object)) return NULL;
    if (fo_is_array(object) || fo_is_dict(object)) cf_owned_find(object)->destroy = url_value_destroy;
    if (fo_is_dict(object)) {
        fo_dict *value = object;
        for (int64_t i = 0; i < value->count; i++) {
            if (!url_value_adopt(value->keys[i], depth + 1) || !url_value_adopt(value->values[i], depth + 1)) return NULL;
        }
    } else if (fo_is_array(object)) {
        fo_array *value = object;
        for (int64_t i = 0; i < value->count; i++) if (!url_value_adopt(value->items[i], depth + 1)) return NULL;
    }
    return object;
}

static void *url_value_copy(void *object, unsigned depth)
{
    if (!object || depth > 32) return NULL;
    if (fo_is_cfstring(object) || fo_is_string(object)) return cf_string_immutable_copy(NULL, object);
    if (fo_is_data(object)) {
        fo_data *data = object;
        return chm_foundation_data(data->bytes, (size_t)data->length);
    }
    if (fo_is_array(object)) {
        fo_array *source = object, *copy = fo_array_new();
        if (!copy) return NULL;
        if (!cf_owned_add(copy, NULL, NULL)) { collection_dispose(copy); return NULL; }
        cf_owned_find(copy)->destroy = url_value_destroy;
        if (!a_reserve(copy, (size_t)source->count)) { cf_owned_release(copy); return NULL; }
        for (int64_t i = 0; i < source->count; i++) {
            void *value = url_value_copy(source->items[i], depth + 1);
            if (!value) { cf_owned_release(copy); return NULL; }
            copy->items[copy->count++] = value;
        }
        return copy;
    }
    if (fo_is_dict(object)) {
        fo_dict *source = object, *copy = fo_dict_new();
        if (!copy) return NULL;
        if (!cf_owned_add(copy, NULL, NULL)) { collection_dispose(copy); return NULL; }
        cf_owned_find(copy)->destroy = url_value_destroy;
        if (!d_reserve(copy, (size_t)source->count)) { cf_owned_release(copy); return NULL; }
        for (int64_t i = 0; i < source->count; i++) {
            void *key = url_value_copy(source->keys[i], depth + 1);
            void *value = key ? url_value_copy(source->values[i], depth + 1) : NULL;
            if (!key || !value) { cf_owned_release(key); cf_owned_release(value); cf_owned_release(copy); return NULL; }
            copy->keys[copy->count] = key;
            copy->values[copy->count++] = value;
        }
        return copy;
    }
    if (fo_is_number(object)) {
        fo_number *source = object;
        fo_number *copy = fo_alloc("NSNumber", sizeof(*copy));
        if (!copy) return NULL;
        copy->type = source->type;
        copy->value = source->value;
        url_value_own(copy);
        return copy;
    }
    if (fo_isa_is(object, "NSDate")) {
        fo_date *source = object;
        fo_date *copy = fo_alloc("NSDate", sizeof(*copy));
        if (!copy) return NULL;
        copy->epoch = source->epoch;
        url_value_own(copy);
        return copy;
    }
    return NULL;
}

static void *url_dictionary_copy(void *object)
{
    return fo_is_dict(object) ? url_value_copy(object, 0) : NULL;
}

static const char *url_header(net_object *response, const char *name)
{
    if (!response || !fo_is_dict(response->headers)) return NULL;
    fo_dict *headers = response->headers;
    for (int64_t i = 0; i < headers->count; i++)
        if (!strcasecmp(fo_bytes(headers->keys[i]), name)) return fo_bytes(headers->values[i]);
    return NULL;
}

static void url_response_destroy(void *object)
{
    net_object **link = &net_objects;
    while (*link && (*link)->object != object) link = &(*link)->next;
    if (!*link) return;
    net_object *value = *link;
    *link = value->next;
    cf_owned_release(value->headers);
    cf_owned_release(value->url);
    free(value->address);
    free(value->method);
    free(value->header_json);
    free(value);
}

static net_object *url_response_attach(void *object)
{
    if (!object || !fo_isa_is(object, "NSURLResponse") || net_find(object)) return NULL;
    net_object *value = calloc(1, sizeof(*value));
    if (!value) return NULL;
    if (!cf_owned_add(object, NULL, NULL)) { free(value); return NULL; }
    cf_owned_find(object)->destroy = url_response_destroy;
    value->object = object;
    value->expected = -1;
    value->next = net_objects;
    net_objects = value;
    return value;
}

static void *url_response_http(void *self, const char *selector, void *url, long status, void *version, void *headers)
{
    (void)selector;
    (void)version;
    net_object *address = net_find(url);
    if (!address || !address->address || (headers && !fo_is_dict(headers)) || status < 100 || status > 599) return NULL;
    net_object *value = url_response_attach(self);
    if (!value) return NULL;
    value->url = cf_owned_retain(url);
    value->status = (int)status;
    value->headers = headers ? url_value_copy(headers, 0) : fo_dict_new();
    if (!value->headers) { cf_owned_release(self); return NULL; }
    const char *length = url_header(value, "Content-Length");
    if (length && *length) {
        char *end;
        errno = 0;
        long long parsed = strtoll(length, &end, 10);
        if (!errno && !*end && parsed >= 0) value->expected = parsed;
    }
    return self;
}

static void *url_response_basic(void *self, const char *selector, void *url, void *mime, long length, void *encoding)
{
    (void)selector;
    net_object *address = net_find(url);
    if (!address || !address->address) return NULL;
    net_object *value = url_response_attach(self);
    if (!value) return NULL;
    value->url = cf_owned_retain(url);
    value->expected = length;
    value->address = mime ? strdup(fo_bytes(mime)) : NULL;
    value->method = encoding ? strdup(fo_bytes(encoding)) : NULL;
    return self;
}

static void *url_response_text(void *self, const char *selector)
{
    net_object *value = net_find(self);
    if (!value) return NULL;
    if (!value->status) {
        const char *text = !strcmp(selector, "MIMEType") ? value->address : value->method;
        return text ? fo_string_new(text) : NULL;
    }
    const char *type = url_header(value, "Content-Type");
    if (!type) return NULL;
    if (!strcmp(selector, "MIMEType")) {
        const char *end = strchr(type, ';');
        size_t size = end ? (size_t)(end - type) : strlen(type);
        while (size && isspace((unsigned char)type[size - 1])) size--;
        char *text = strndup(type, size);
        if (!text) return NULL;
        for (char *p = text; *p; p++) *p = (char)tolower((unsigned char)*p);
        void *result = fo_string_new(text);
        free(text);
        return result;
    }
    const char *start = strcasestr(type, "charset=");
    if (!start) return NULL;
    start += 8;
    int quote = *start == '"';
    if (quote) start++;
    size_t size = strcspn(start, quote ? "\"" : "; \t\r\n");
    char *text = strndup(start, size);
    if (!text) return NULL;
    void *result = fo_string_new(text);
    free(text);
    return result;
}

static void *url_response_copy(void *self, const char *selector, ...)
{
    (void)selector;
    net_object *value = net_find(self);
    if (!value || !fo_isa_is(self, "NSURLResponse")) return NULL;
    void *object = fo_alloc(value->status ? "NSHTTPURLResponse" : "NSURLResponse", 0);
    if (!object) return NULL;
    if (value->status) return url_response_http(object, NULL, value->url, value->status, NULL, value->headers);
    return url_response_basic(object, NULL, value->url, value->address ? fo_string_new(value->address) : NULL,
        value->expected, value->method ? fo_string_new(value->method) : NULL);
}

static void *url_request_copy(void *self, const char *selector, ...)
{
    net_object *source = net_find(self);
    if (!source || !fo_isa_is(self, "NSURLRequest")) return NULL;
    const char *cls = !strncmp(selector, "mutableCopy", 11) ? "NSMutableURLRequest" : "NSURLRequest";
    net_object *copy = net_new(cls);
    if (!copy) return NULL;
    copy->url = source->url;
    copy->method = strdup(source->method ? source->method : "GET");
    copy->headers = source->headers ? o_copy(source->headers, "copy") : fo_dict_new();
    copy->body = fo_is_data(source->body) ? fo_data_new(((fo_data *)source->body)->bytes, ((fo_data *)source->body)->length) : NULL;
    copy->timeout = source->timeout;
    copy->custom_timeout = source->custom_timeout;
    return copy->object;
}

static const chm_host_method url_value_methods[] = {
    {"NSURLResponse", "initWithURL:MIMEType:expectedContentLength:textEncodingName:", "@@:@@q@", url_response_basic, 0},
    {"NSHTTPURLResponse", "initWithURL:statusCode:HTTPVersion:headerFields:", "@@:@q@@", url_response_http, 0},
    {"NSURLResponse", "MIMEType", "@@:", url_response_text, 0},
    {"NSURLResponse", "textEncodingName", "@@:", url_response_text, 0},
    {"NSURLResponse", "copy", "@@:", url_response_copy, 0},
    {"NSURLResponse", "copyWithZone:", "@@:^v", url_response_copy, 0},
    {"NSURLRequest", "copy", "@@:", url_request_copy, 0},
    {"NSURLRequest", "copyWithZone:", "@@:^v", url_request_copy, 0},
    {"NSURLRequest", "mutableCopy", "@@:", url_request_copy, 0},
    {"NSURLRequest", "mutableCopyWithZone:", "@@:^v", url_request_copy, 0}
};
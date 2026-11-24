typedef struct {
    const unsigned char *bytes;
    size_t length, table, objects, reference_size;
    size_t *offsets;
    void **cache;
    unsigned char *state;
    size_t work;
} binary_plist;

static int plist_be(const unsigned char *bytes, size_t length, size_t offset, size_t size, uint64_t *value)
{
    if (!size || size > 8 || offset > length || size > length - offset) return 0;
    uint64_t out = 0;
    for (size_t i = 0; i < size; i++) out = out << 8 | bytes[offset + i];
    *value = out;
    return 1;
}

static int bplist_length(binary_plist *plist, size_t *position, unsigned nibble, uint64_t *count)
{
    if (nibble != 15) { *count = nibble; return 1; }
    if (*position >= plist->table) return 0;
    unsigned marker = plist->bytes[(*position)++];
    if ((marker >> 4) != 1 || (marker & 15) > 3) return 0;
    size_t size = (size_t)1 << (marker & 15);
    if (!plist_be(plist->bytes, plist->table, *position, size, count)) return 0;
    *position += size;
    return 1;
}

static void *bplist_object(binary_plist *plist, uint64_t index, unsigned depth)
{
    if (index >= plist->objects || depth > 64 || ++plist->work > 1000000) return NULL;
    if (plist->state[index] == 2) return plist->cache[index];
    if (plist->state[index]) return NULL;
    plist->state[index] = 1;
    size_t position = plist->offsets[index];
    unsigned marker = plist->bytes[position++], type = marker >> 4, nibble = marker & 15;
    uint64_t count = 0, value = 0;
    void *object = NULL;
    if (marker == 8 || marker == 9) object = n_bool(NULL, NULL, marker == 9);
    else if (type == 1 && nibble <= 4) {
        size_t size = (size_t)1 << nibble;
        if (nibble == 4) {
            uint64_t high;
            if (!plist_be(plist->bytes, plist->table, position, 8, &high) || high ||
                !plist_be(plist->bytes, plist->table, position + 8, 8, &value)) return NULL;
            object = n_unsigned(NULL, NULL, value);
        } else if (plist_be(plist->bytes, plist->table, position, size, &value)) {
            object = nibble == 3 ? n_signed(NULL, NULL, (int64_t)value) : n_unsigned(NULL, NULL, value);
        }
    } else if ((type == 2 && (nibble == 2 || nibble == 3)) || marker == 0x33) {
        size_t size = (size_t)1 << nibble;
        if (!plist_be(plist->bytes, plist->table, position, size, &value)) return NULL;
        double real;
        if (size == 4) { uint32_t bits = (uint32_t)value; float number; memcpy(&number, &bits, 4); real = number; }
        else memcpy(&real, &value, 8);
        if (!isfinite(real)) return NULL;
        object = marker == 0x33 ? date_unix(NULL, NULL, real + 978307200.0) : n_double(NULL, NULL, real);
    } else if (type == 4 || type == 5 || type == 6 || type == 10 || type == 13) {
        if (!bplist_length(plist, &position, nibble, &count) || count > 32 * 1024 * 1024) return NULL;
        size_t multiplier = type == 6 ? 2 : type == 10 ? plist->reference_size : type == 13 ? plist->reference_size * 2 : 1;
        if (position > plist->table || count > (plist->table - position) / multiplier) return NULL;
        if (type == 4) object = fo_data_new(plist->bytes + position, (int64_t)count);
        else if (type == 5) {
            const unsigned char *text = plist->bytes + position;
            for (uint64_t i = 0; i < count; i++) if (!text[i] || text[i] > 127) return NULL;
            char *copy = malloc((size_t)count + 1);
            if (!copy) return NULL;
            memcpy(copy, text, (size_t)count); copy[count] = 0;
            object = fo_string_new(copy); free(copy);
        } else if (type == 6) {
            fo_buffer buffer = {0};
            for (uint64_t i = 0; i < count; i++) {
                unsigned code = (unsigned)plist->bytes[position + i * 2] << 8 | plist->bytes[position + i * 2 + 1];
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (++i == count) { free(buffer.data); return NULL; }
                    unsigned low = (unsigned)plist->bytes[position + i * 2] << 8 | plist->bytes[position + i * 2 + 1];
                    if (low < 0xdc00 || low > 0xdfff) { free(buffer.data); return NULL; }
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                }
                if (!buffer_unicode(&buffer, code)) { free(buffer.data); return NULL; }
            }
            object = fo_string_new(buffer.data ? buffer.data : ""); free(buffer.data);
        } else {
            if (count > 1000000) return NULL;
            object = type == 10 ? fo_array_new() : fo_dict_new();
            if (!object) return NULL;
            for (uint64_t i = 0; i < count; i++) {
                uint64_t reference;
                if (!plist_be(plist->bytes, plist->table, position + i * plist->reference_size, plist->reference_size, &reference)) return NULL;
                void *first = bplist_object(plist, reference, depth + 1);
                if (!first) return NULL;
                if (type == 10) { if (!a_push(object, first)) return NULL; }
                else {
                    if (!fo_is_string(first) || !plist_be(plist->bytes, plist->table, position + (count + i) * plist->reference_size, plist->reference_size, &reference)) return NULL;
                    void *second = bplist_object(plist, reference, depth + 1);
                    if (!second || !d_put(object, first, second)) return NULL;
                }
            }
        }
    }
    plist->cache[index] = object;
    plist->state[index] = 2;
    return object;
}

static void *bplist_read(fo_data *data)
{
    if (!data || data->length < 40 || data->length > 32 * 1024 * 1024 || memcmp(data->bytes, "bplist00", 8)) return NULL;
    size_t trailer = (size_t)data->length - 32;
    const unsigned char *bytes = data->bytes;
    unsigned offset_size = bytes[trailer + 6], reference_size = bytes[trailer + 7];
    uint64_t objects, root, table;
    if (!offset_size || offset_size > 8 || !reference_size || reference_size > 8 ||
        !plist_be(bytes, (size_t)data->length, trailer + 8, 8, &objects) || !objects || objects > 1000000 ||
        !plist_be(bytes, (size_t)data->length, trailer + 16, 8, &root) || root >= objects ||
        !plist_be(bytes, (size_t)data->length, trailer + 24, 8, &table) || table < 8 || table > trailer || objects > (trailer - table) / offset_size) return NULL;
    binary_plist plist = {.bytes = bytes, .length = (size_t)data->length, .table = (size_t)table, .objects = (size_t)objects, .reference_size = reference_size};
    plist.offsets = calloc(plist.objects, sizeof(*plist.offsets));
    plist.cache = calloc(plist.objects, sizeof(*plist.cache));
    plist.state = calloc(plist.objects, 1);
    void *out = NULL;
    if (!plist.offsets || !plist.cache || !plist.state) goto done;
    for (size_t i = 0; i < plist.objects; i++) {
        uint64_t offset;
        if (!plist_be(bytes, trailer, (size_t)table + i * offset_size, offset_size, &offset) || offset < 8 || offset >= table) goto done;
        plist.offsets[i] = (size_t)offset;
    }
    out = bplist_object(&plist, root, 0);
done:
    free(plist.offsets); free(plist.cache); free(plist.state);
    return out;
}

static int plist_put_be(fo_buffer *buffer, uint64_t value, size_t size)
{
    unsigned char bytes[8];
    for (size_t i = 0; i < size; i++) bytes[size - 1 - i] = (unsigned char)(value >> (i * 8));
    return buffer_add(buffer, bytes, size);
}
static int bplist_marker(fo_buffer *buffer, unsigned type, size_t count)
{
    unsigned char marker = (unsigned char)(type | (count < 15 ? count : 15));
    return buffer_add(buffer, &marker, 1) && (count < 15 || (buffer_add(buffer, "\x13", 1) && plist_put_be(buffer, count, 8)));
}

typedef struct { fo_buffer bytes, offsets; size_t objects; } bplist_writer;
static int bplist_write_object(bplist_writer *writer, void *object, unsigned depth, size_t *index)
{
    if (!object || depth > 64 || writer->objects >= 1000000) return 0;
    fo_buffer references = {0};
    int array = fo_is_array(object), dictionary = fo_is_dict(object);
    size_t count = array ? (size_t)((fo_array *)object)->count : dictionary ? (size_t)((fo_dict *)object)->count : 0;
    if (count > 1000000) return 0;
    if (array || dictionary) {
        for (size_t i = 0; i < count * (dictionary ? 2 : 1); i++) {
            void *child = array ? ((fo_array *)object)->items[i] : i < count ? ((fo_dict *)object)->keys[i] : ((fo_dict *)object)->values[i - count];
            if (dictionary && i < count && !fo_is_string(child) && !fo_is_cfstring(child)) goto fail;
            size_t reference;
            if (!bplist_write_object(writer, child, depth + 1, &reference) || !plist_put_be(&references, reference, 8)) goto fail;
        }
    }
    if (writer->objects >= 1000000 || !plist_put_be(&writer->offsets, writer->bytes.size, 8)) goto fail;
    *index = writer->objects++;
    fo_buffer *buffer = &writer->bytes;
    int ok = 0;
    if (array || dictionary) ok = bplist_marker(buffer, dictionary ? 0xd0 : 0xa0, count) && buffer_add(buffer, references.data, references.size);
    else if (fo_is_data(object)) {
        fo_data *data = object;
        ok = data->length >= 0 && bplist_marker(buffer, 0x40, (size_t)data->length) && buffer_add(buffer, data->bytes, (size_t)data->length);
    } else if (fo_is_number(object)) {
        fo_number *number = object;
        if (number->type == 'B') ok = buffer_add(buffer, number->value.signed_value ? "\x09" : "\x08", 1);
        else if (number->type == 'd') {
            uint64_t bits; memcpy(&bits, &number->value.real_value, 8);
            ok = isfinite(number->value.real_value) && buffer_add(buffer, "\x23", 1) && plist_put_be(buffer, bits, 8);
        } else {
            uint64_t value = number->type == 'Q' ? number->value.unsigned_value : (uint64_t)number->value.signed_value;
            if (number->type == 'Q' && value > INT64_MAX) ok = buffer_add(buffer, "\x14", 1) && plist_put_be(buffer, 0, 8) && plist_put_be(buffer, value, 8);
            else ok = buffer_add(buffer, "\x13", 1) && plist_put_be(buffer, value, 8);
        }
    } else if (fo_isa_is(object, "NSDate")) {
        double value = ((fo_date *)object)->epoch - 978307200.0;
        uint64_t bits; memcpy(&bits, &value, 8);
        ok = isfinite(value) && buffer_add(buffer, "\x33", 1) && plist_put_be(buffer, bits, 8);
    } else if (fo_is_string(object) || fo_is_cfstring(object)) {
        const unsigned char *text = (const unsigned char *)fo_bytes(object);
        int ascii = 1;
        for (const unsigned char *p = text; *p; p++) if (*p > 127) ascii = 0;
        if (ascii) ok = bplist_marker(buffer, 0x50, strlen((const char *)text)) && buffer_add(buffer, text, strlen((const char *)text));
        else {
            fo_buffer unicode = {0};
            for (const unsigned char *p = text; *p;) {
                unsigned first = *p++, code, extra;
                if (first < 128) { code = first; extra = 0; }
                else if (first >= 0xc2 && first <= 0xdf) { code = first & 31; extra = 1; }
                else if (first >= 0xe0 && first <= 0xef) { code = first & 15; extra = 2; }
                else if (first >= 0xf0 && first <= 0xf4) { code = first & 7; extra = 3; }
                else { free(unicode.data); goto fail; }
                for (unsigned i = 0; i < extra; i++) {
                    if ((*p & 0xc0) != 0x80) { free(unicode.data); goto fail; }
                    code = code << 6 | (*p++ & 63);
                }
                if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) || (extra == 3 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) { free(unicode.data); goto fail; }
                if (code < 0x10000) ok = plist_put_be(&unicode, code, 2);
                else ok = plist_put_be(&unicode, 0xd800 + ((code - 0x10000) >> 10), 2) && plist_put_be(&unicode, 0xdc00 + ((code - 0x10000) & 1023), 2);
                if (!ok) { free(unicode.data); goto fail; }
            }
            ok = bplist_marker(buffer, 0x60, unicode.size / 2) && buffer_add(buffer, unicode.data, unicode.size);
            free(unicode.data);
        }
    }
    free(references.data);
    return ok;
fail:
    free(references.data);
    return 0;
}

static void *bplist_write(void *object)
{
    bplist_writer writer = {0}; size_t root;
    void *out = NULL;
    if (!buffer_add(&writer.bytes, "bplist00", 8) || !bplist_write_object(&writer, object, 0, &root)) goto done;
    size_t table = writer.bytes.size;
    if (!buffer_add(&writer.bytes, writer.offsets.data, writer.offsets.size) ||
        !buffer_add(&writer.bytes, "\0\0\0\0\0\0\x08\x08", 8) || !plist_put_be(&writer.bytes, writer.objects, 8) ||
        !plist_put_be(&writer.bytes, root, 8) || !plist_put_be(&writer.bytes, table, 8)) goto done;
    out = fo_data_new(writer.bytes.data, (int64_t)writer.bytes.size);
done:
    free(writer.bytes.data); free(writer.offsets.data);
    return out;
}
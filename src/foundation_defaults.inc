static fo_dict *default_values;
static fo_dict *registered_defaults;

static int defaults_sync(void *self, const char *sel)
{
    (void)sel;
    return self == default_values && collection_write_file(default_values, NULL, fo_string_new("/Library/Preferences/preferences.plist"), 1);
}
static void *defaults_standard(void)
{
    if (!default_values) {
        void *data = f_contents_at_path(NULL, NULL, fo_string_new("/Library/Preferences/preferences.plist"));
        void *saved = plist_read_data(data);
        default_values = fo_is_dict(saved) ? saved : fo_dict_new();
        if (default_values) default_values->isa = chm_foundation_lookup_class("NSUserDefaults");
        registered_defaults = fo_dict_new();
    }
    return default_values;
}
static void *defaults_get(void *self, const char *sel, void *key)
{
    void *out = d_value(self, sel, key);
    if (!out && self == default_values) out = d_value(registered_defaults, sel, key);
    return out;
}
static void defaults_set(void *self, const char *sel, void *value, void *key)
{
    d_set_subscript(self, sel, value, key);
    if (self == default_values) defaults_sync(self, NULL);
}
static void defaults_remove(void *self, const char *sel, void *key)
{
    d_remove(self, sel, key);
    if (self == default_values) defaults_sync(self, NULL);
}
static void defaults_register(void *self, const char *sel, void *values)
{
    if (self == default_values) d_add_entries(registered_defaults, sel, values);
}
static void defaults_bool_set(void *self, const char *sel, signed char value, void *key) { defaults_set(self, sel, n_bool(NULL, NULL, value), key); }
static void defaults_integer_set(void *self, const char *sel, long value, void *key) { defaults_set(self, sel, n_signed(NULL, NULL, value), key); }
static long defaults_integer(void *self, const char *sel, void *key) { return o_integer(defaults_get(self, sel, key)); }
static int defaults_bool(void *self, const char *sel, void *key) { return o_bool(defaults_get(self, sel, key)); }
static void *defaults_dictionary(void *self, const char *sel)
{
    (void)sel;
    fo_dict *out = d_with_dictionary(chm_foundation_lookup_class("NSMutableDictionary"), NULL, registered_defaults);
    if (out) d_add_entries(out, NULL, self);
    return out;
}
static void array_add_all(void *self, const char *sel, void *other)
{
    (void)sel;
    if (!fo_is_array(self) || !fo_is_array(other)) return;
    fo_array *a = self, *b = other;
    int64_t count = b->count;
    if (!a_reserve(a, (size_t)a->count + (size_t)count)) return;
    memmove(a->items + a->count, b->items, (size_t)count * sizeof(void *));
    a->count += count; a->mutations++;
}
static void array_insert(void *self, const char *sel, void *object, unsigned long index)
{
    (void)sel;
    fo_array *a = self;
    if (!fo_is_array(a) || !object || index > (unsigned long)a->count || !a_reserve(a, (size_t)a->count + 1)) {
        collection_error("insertObject:atIndex:"); return;
    }
    memmove(a->items + index + 1, a->items + index, ((size_t)a->count - index) * sizeof(void *));
    a->items[index] = object; a->count++; a->mutations++;
}
static void array_remove_index(void *self, const char *sel, unsigned long index)
{
    (void)sel; fo_array *a = self;
    if (!fo_is_array(a) || index >= (unsigned long)a->count) { collection_error("removeObjectAtIndex:"); return; }
    memmove(a->items + index, a->items + index + 1, ((size_t)a->count - index - 1) * sizeof(void *));
    a->items[--a->count] = NULL; a->mutations++;
}
static void *array_adding(void *self, const char *sel, void *other)
{
    fo_array *out = a_with_array(chm_foundation_lookup_class("NSArray"), sel, self);
    if (out) array_add_all(out, NULL, other);
    return out;
}
static void *error_description(void *self, const char *sel)
{
    (void)sel; return d_value(self, NULL, fo_string_new("NSLocalizedDescription"));
}
static long error_code(void *self, const char *sel)
{
    (void)sel; return o_integer(d_value(self, NULL, fo_string_new("code")));
}
static void *error_domain(void *self, const char *sel)
{
    (void)sel; return d_value(self, NULL, fo_string_new("domain"));
}
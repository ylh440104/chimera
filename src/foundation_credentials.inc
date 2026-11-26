typedef struct url_credential {
    void *object, *user, *password, *trust;
    unsigned long persistence;
    struct url_credential *next;
} url_credential;

static url_credential *url_credentials;

static url_credential *credential_find(void *object)
{
    for (url_credential *value = url_credentials; value; value = value->next)
        if (value->object == object) return value;
    return NULL;
}

static void credential_destroy(void *object)
{
    url_credential **link = &url_credentials;
    while (*link && (*link)->object != object) link = &(*link)->next;
    if (!*link) return;
    url_credential *value = *link;
    *link = value->next;
    cf_owned_release(value->user);
    cf_owned_release(value->password);
    cf_owned_release(value->trust);
    free(value);
}

static url_credential *credential_attach(void *object)
{
    if (!object || !fo_isa_is(object, "NSURLCredential")) return NULL;
    url_credential *value = credential_find(object);
    if (value) return NULL;
    value = calloc(1, sizeof(*value));
    if (!value) return NULL;
    if (!cf_owned_add(object, NULL, NULL)) { free(value); return NULL; }
    cf_owned_find(object)->destroy = credential_destroy;
    value->object = object;
    value->next = url_credentials;
    url_credentials = value;
    return value;
}

static void *credential_init_password(void *self, const char *selector, void *user, void *password, unsigned long persistence)
{
    (void)selector;
    if (!self || persistence > 3 || (!fo_is_string(user) && !fo_is_cfstring(user)) ||
        (!fo_is_string(password) && !fo_is_cfstring(password))) return NULL;
    void *u = cf_string_immutable_copy(NULL, user), *p = cf_string_immutable_copy(NULL, password);
    url_credential *value = u && p ? credential_attach(self) : NULL;
    if (!value) { cf_owned_release(u); cf_owned_release(p); return NULL; }
    value->user = u;
    value->password = p;
    value->persistence = persistence;
    return self;
}

static void *credential_password(void *cls, const char *selector, void *user, void *password, unsigned long persistence)
{
    (void)cls;
    void *object = fo_alloc("NSURLCredential", 0);
    if (!credential_init_password(object, selector, user, password, persistence)) { free(object); return NULL; }
    return object;
}

static void *credential_init_trust(void *self, const char *selector, void *trust)
{
    (void)selector;
    if (!sec_find(trust, 3)) return NULL;
    url_credential *value = credential_attach(self);
    if (!value) return NULL;
    value->trust = cf_owned_retain(trust);
    value->persistence = 1;
    return self;
}

static void *credential_trust(void *cls, const char *selector, void *trust)
{
    (void)cls;
    void *object = fo_alloc("NSURLCredential", 0);
    if (!credential_init_trust(object, selector, trust)) { free(object); return NULL; }
    return object;
}

static void *credential_property(void *self, const char *selector)
{
    url_credential *value = credential_find(self);
    if (!value) return NULL;
    if (!strcmp(selector, "user")) return value->user;
    if (!strcmp(selector, "password")) return value->password;
    return NULL;
}

static unsigned long credential_persistence(void *self, const char *selector)
{
    (void)selector;
    url_credential *value = credential_find(self);
    return value ? value->persistence : 0;
}

static signed char credential_has_password(void *self, const char *selector)
{
    (void)selector;
    url_credential *value = credential_find(self);
    return value && value->password;
}

static void *credential_copy(void *self, const char *selector, ...)
{
    (void)selector;
    return credential_find(self) ? cf_owned_retain(self) : NULL;
}

static signed char credential_equal(void *self, const char *selector, void *other)
{
    (void)selector;
    if (self == other) return 1;
    url_credential *a = credential_find(self), *b = credential_find(other);
    if (!a || !b || a->persistence != b->persistence || a->trust != b->trust) return 0;
    fo_string *au = a->user, *bu = b->user, *ap = a->password, *bp = b->password;
    int users = au == bu || (au && bu && au->length == bu->length && !memcmp(au->bytes, bu->bytes, (size_t)au->length));
    int passwords = ap == bp || (ap && bp && ap->length == bp->length && !memcmp(ap->bytes, bp->bytes, (size_t)ap->length));
    return users && passwords;
}

static unsigned long credential_hash(void *self, const char *selector)
{
    (void)selector;
    url_credential *value = credential_find(self);
    return value ? cf_hash(value->user) ^ (cf_hash(value->password) << 1) ^ value->persistence ^ (uintptr_t)value->trust : 0;
}

static const chm_host_method credential_methods[] = {
    {"NSURLCredential", "credentialForTrust:", "@@:^v", credential_trust, 1},
    {"NSURLCredential", "initWithTrust:", "@@:^v", credential_init_trust, 0},
    {"NSURLCredential", "credentialWithUser:password:persistence:", "@@:@@Q", credential_password, 1},
    {"NSURLCredential", "initWithUser:password:persistence:", "@@:@@Q", credential_init_password, 0},
    {"NSURLCredential", "user", "@@:", credential_property, 0},
    {"NSURLCredential", "password", "@@:", credential_property, 0},
    {"NSURLCredential", "identity", "^v@:", credential_property, 0},
    {"NSURLCredential", "certificates", "@@:", credential_property, 0},
    {"NSURLCredential", "hasPassword", "c@:", credential_has_password, 0},
    {"NSURLCredential", "persistence", "Q@:", credential_persistence, 0},
    {"NSURLCredential", "copy", "@@:", credential_copy, 0},
    {"NSURLCredential", "copyWithZone:", "@@:^v", credential_copy, 0},
    {"NSURLCredential", "isEqual:", "c@:@", credential_equal, 0},
    {"NSURLCredential", "hash", "Q@:", credential_hash, 0}
};

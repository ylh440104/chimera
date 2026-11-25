#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

typedef struct sec_object sec_object;
typedef struct { sec_object **items; size_t count; } sec_list;
struct sec_object {
    void *isa;
    uint64_t magic;
    int kind, ssl, server, custom_anchors, evaluated;
    char *hostname;
    X509 *certificate;
    EVP_PKEY *key;
    unsigned char *der;
    size_t der_length;
    sec_list certificates, policies, anchors, chain;
};

static void sec_destroy(void *object);

static sec_object *sec_find(void *object, int kind)
{
    cf_owned *entry = cf_owned_find(object);
    if (!entry || entry->destroy != sec_destroy) return NULL;
    sec_object *value = object;
    return !kind || value->kind == kind ? value : NULL;
}

static unsigned long sec_type_id(void *object)
{
    sec_object *value = sec_find(object, 0);
    return value ? 1000u + (unsigned)value->kind : 0;
}

static void sec_list_clear(sec_list *list)
{
    sec_list old = *list;
    *list = (sec_list){0};
    for (size_t i = 0; i < old.count; i++) cf_owned_release(old.items[i]);
    free(old.items);
}

static void sec_destroy(void *object)
{
    sec_object *value = object;
    X509_free(value->certificate);
    EVP_PKEY_free(value->key);
    free(value->der);
    free(value->hostname);
    sec_list_clear(&value->certificates);
    sec_list_clear(&value->policies);
    sec_list_clear(&value->anchors);
    sec_list_clear(&value->chain);
}

static sec_object *sec_new(int kind)
{
    sec_object *value = fo_alloc("NSObject", sizeof(*value));
    if (!value) return NULL;
    if (!cf_owned_add(value, NULL, NULL)) { free(value); return NULL; }
    cf_owned_find(value)->destroy = sec_destroy;
    value->magic = 0x5345434f424aull;
    value->kind = kind;
    return value;
}

static int sec_equal(void *left, void *right)
{
    sec_object *a = sec_find(left, 0), *b = sec_find(right, 0);
    if (!a && !b) return -1;
    if (!a || !b || a->kind != b->kind) return 0;
    if (a->kind == 1 || a->kind == 4)
        return a->der_length == b->der_length && !memcmp(a->der, b->der, a->der_length);
    if (a->kind == 2)
        return a->ssl == b->ssl && a->server == b->server &&
            ((!a->hostname && !b->hostname) || (a->hostname && b->hostname && !strcmp(a->hostname, b->hostname)));
    return left == right;
}

static unsigned long sec_hash(void *object)
{
    sec_object *value = sec_find(object, 0);
    unsigned long hash = 5381;
    if (!value) return (unsigned long)(uintptr_t)object;
    if (value->kind == 2) {
        if (value->hostname) for (const unsigned char *p = (const void *)value->hostname; *p; p++) hash = hash * 33 ^ *p;
        return hash ^ ((unsigned long)value->ssl << 8) ^ (unsigned long)value->server;
    }
    if (value->kind != 1 && value->kind != 4) return (unsigned long)(uintptr_t)object;
    for (size_t i = 0; i < value->der_length; i++) hash = hash * 33 ^ value->der[i];
    return hash;
}

static sec_object *sec_certificate_from_x509(X509 *certificate)
{
    unsigned char *der = NULL;
    int length = i2d_X509(certificate, &der);
    if (length <= 0) return NULL;
    sec_object *value = sec_new(1);
    if (!value) { OPENSSL_free(der); return NULL; }
    value->der = malloc((size_t)length);
    if (!value->der || !X509_up_ref(certificate)) { OPENSSL_free(der); cf_owned_release(value); return NULL; }
    memcpy(value->der, der, (size_t)length);
    value->der_length = (size_t)length;
    value->certificate = certificate;
    OPENSSL_free(der);
    return value;
}

static void *sec_certificate_create(void *allocator, void *data)
{
    const void *bytes = NULL;
    size_t length = 0;
    if (allocator || !chm_foundation_data_bytes(data, &bytes, &length) || !length || length > 1024 * 1024) return NULL;
    const unsigned char *cursor = bytes;
    ERR_clear_error();
    X509 *certificate = d2i_X509(NULL, &cursor, (long)length);
    if (!certificate || cursor != (const unsigned char *)bytes + length) { X509_free(certificate); ERR_clear_error(); return NULL; }
    EVP_PKEY *key = X509_get_pubkey(certificate);
    if (!key) { X509_free(certificate); ERR_clear_error(); return NULL; }
    EVP_PKEY_free(key);
    sec_object *value = sec_certificate_from_x509(certificate);
    X509_free(certificate);
    ERR_clear_error();
    return value;
}

static void *sec_certificate_data(void *object)
{
    sec_object *value = sec_find(object, 1);
    return value ? chm_foundation_data(value->der, value->der_length) : NULL;
}

static void *sec_policy_basic(void) { return sec_new(2); }

static void *sec_policy_ssl(unsigned char server, void *hostname)
{
    char *name = NULL;
    if (hostname) {
        int32_t length = 0, bytes = 0;
        uint16_t *units = cf_string_units(hostname, &length);
        if (!units || length > 1024) { free(units); return NULL; }
        for (int32_t i = 0; i < length; i++) if (!units[i]) { free(units); return NULL; }
        name = cf_string_utf8(units, length, &bytes);
        free(units);
        if (!name) return NULL;
    }
    sec_object *policy = sec_new(2);
    if (!policy) { free(name); return NULL; }
    policy->ssl = 1;
    policy->server = !!server;
    policy->hostname = name;
    return policy;
}

static int sec_list_copy(sec_list *destination, void *source, int kind, int array_only, int allow_empty)
{
    size_t count = 1;
    void **items = &source;
    if (fo_is_array(source)) {
        fo_array *array = source;
        if (array->count < 0 || array->count > 256) return -50;
        count = (size_t)array->count;
        items = array->items;
    } else if (array_only) return -50;
    if (!count && !allow_empty) return -50;
    for (size_t i = 0; i < count; i++) if (!sec_find(items[i], kind)) return -50;
    sec_list result = {.items = count ? calloc(count, sizeof(*result.items)) : NULL, .count = count};
    if (count && !result.items) return -108;
    for (size_t i = 0; i < count; i++) result.items[i] = cf_owned_retain(items[i]);
    sec_list_clear(destination);
    *destination = result;
    return 0;
}

static void sec_invalidate(sec_object *trust)
{
    trust->evaluated = 0;
    sec_list_clear(&trust->chain);
}

static int sec_trust_create(void *certificates, void *policies, void **result)
{
    if (!result) return -50;
    *result = NULL;
    sec_object *trust = sec_new(3);
    if (!trust) return -108;
    int status = sec_list_copy(&trust->certificates, certificates, 1, 0, 0);
    void *default_policy = policies ? NULL : sec_policy_basic();
    if (!status) status = sec_list_copy(&trust->policies, policies ? policies : default_policy, 2, 0, 0);
    cf_owned_release(default_policy);
    if (status) { cf_owned_release(trust); return status; }
    *result = trust;
    return 0;
}

static int sec_trust_policies(void *object, void *policies)
{
    sec_object *trust = sec_find(object, 3);
    if (!trust) return -50;
    int status = sec_list_copy(&trust->policies, policies, 2, 0, 0);
    if (!status) sec_invalidate(trust);
    return status;
}

static int sec_trust_anchors(void *object, void *anchors)
{
    sec_object *trust = sec_find(object, 3);
    if (!trust) return -50;
    int status = anchors ? sec_list_copy(&trust->anchors, anchors, 1, 1, 1) : 0;
    if (!status) {
        if (!anchors) sec_list_clear(&trust->anchors);
        trust->custom_anchors = anchors != NULL;
        sec_invalidate(trust);
    }
    return status;
}

static int sec_store_directory(X509_STORE *store, const char *directory)
{
    DIR *dir = opendir(directory);
    if (!dir) return 0;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char path[2048];
        int n = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        BIO *file = BIO_new_file(path, "r");
        X509 *certificate = file ? PEM_read_bio_X509(file, NULL, NULL, NULL) : NULL;
        if (certificate && X509_STORE_add_cert(store, certificate)) count++;
        X509_free(certificate);
        BIO_free(file);
        ERR_clear_error();
    }
    closedir(dir);
    return count;
}

static int sec_system_anchors(X509_STORE *store)
{
#ifdef __ANDROID__
    int count = sec_store_directory(store, "/apex/com.android.conscrypt/cacerts");
    return count ? count : sec_store_directory(store, "/system/etc/security/cacerts");
#else
    int loaded = X509_STORE_load_file(store, "/etc/ssl/certs/ca-certificates.crt");
    return loaded ? loaded : sec_store_directory(store, "/etc/ssl/certs");
#endif
}

static int sec_chain_save(sec_object *trust, STACK_OF(X509) *chain)
{
    if (!chain) return 0;
    int count = sk_X509_num(chain);
    if (count <= 0 || count > 256) return 0;
    sec_list result = {.items = calloc((size_t)count, sizeof(*result.items))};
    if (!result.items) return 0;
    for (int i = 0; i < count; i++) {
        X509 *certificate = sk_X509_value(chain, i);
        sec_object *value = NULL;
        for (size_t j = 0; j < trust->certificates.count; j++)
            if (!X509_cmp(certificate, trust->certificates.items[j]->certificate)) { value = cf_owned_retain(trust->certificates.items[j]); break; }
        if (!value) value = sec_certificate_from_x509(certificate);
        if (!value) { sec_list_clear(&result); return 0; }
        result.items[result.count++] = value;
    }
    sec_list_clear(&trust->chain);
    trust->chain = result;
    return 1;
}

static int sec_ssl_lifetime(sec_object *trust, sec_object *policy)
{
    if (!policy->ssl || !policy->server) return 1;
    X509 *leaf = trust->certificates.items[0]->certificate;
    const ASN1_TIME *before = X509_get0_notBefore(leaf), *after = X509_get0_notAfter(leaf);
    int days = 0, seconds = 0;
    if (!ASN1_TIME_diff(&days, &seconds, before, after) || days < 0) return 0;
    time_t july2019 = 1561939200, september2020 = 1598918400, march2018 = 1519862400;
    long long duration = (long long)days * 86400 + seconds;
    if (trust->custom_anchors) {
        if (X509_cmp_time(before, &july2019) > 0 && duration > 825LL * 86400 + 3600) return 0;
    } else {
        if (X509_cmp_time(before, &september2020) > 0 && duration > 398LL * 86400) return 0;
        if (X509_cmp_time(before, &march2018) > 0 && duration > 825LL * 86400 + 3600) return 0;
    }
    return 1;
}
static int sec_trust_evaluate(void *object, uint32_t *result)
{
    if (result) *result = 0;
    sec_object *trust = sec_find(object, 3);
    if (!trust || !result || !trust->certificates.count || !trust->policies.count) return -50;
    ERR_clear_error();
    X509_STORE *store = X509_STORE_new();
    STACK_OF(X509) *untrusted = sk_X509_new_null();
    if (!store || !untrusted) { X509_STORE_free(store); sk_X509_free(untrusted); return -108; }
    int status = 0, accepted = 1, roots = 0;
    if (trust->custom_anchors) {
        for (size_t i = 0; i < trust->anchors.count; i++) {
            if (X509_STORE_add_cert(store, trust->anchors.items[i]->certificate)) roots++;
            else ERR_clear_error();
        }
    } else roots = sec_system_anchors(store);
    for (size_t i = 1; i < trust->certificates.count; i++)
        if (!sk_X509_push(untrusted, trust->certificates.items[i]->certificate)) { status = -108; break; }
    sec_invalidate(trust);
    for (size_t i = 0; !status && i < trust->policies.count; i++) {
        sec_object *policy = trust->policies.items[i];
        X509_STORE_CTX *context = X509_STORE_CTX_new();
        if (!context || !X509_STORE_CTX_init(context, store, trust->certificates.items[0]->certificate, untrusted)) {
            X509_STORE_CTX_free(context); status = -108; break;
        }
        X509_VERIFY_PARAM *parameter = X509_STORE_CTX_get0_param(context);
        X509_VERIFY_PARAM_set_depth(parameter, 32);
        X509_VERIFY_PARAM_set_auth_level(parameter, 2);
        if (trust->custom_anchors) X509_VERIFY_PARAM_set_flags(parameter, X509_V_FLAG_PARTIAL_CHAIN);
        if (policy->ssl) {
            if (!sec_ssl_lifetime(trust, policy)) accepted = 0;
            if (!X509_STORE_CTX_set_purpose(context, policy->server ? X509_PURPOSE_SSL_SERVER : X509_PURPOSE_SSL_CLIENT)) status = -50;
            if (policy->hostname) {
                unsigned char address[16];
                int ip = inet_pton(AF_INET, policy->hostname, address) == 1 || inet_pton(AF_INET6, policy->hostname, address) == 1;
                time_t july2019 = 1561939200;
                unsigned host_flags = X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS;
                if (policy->server && X509_cmp_time(X509_get0_notBefore(trust->certificates.items[0]->certificate), &july2019) > 0) host_flags |= X509_CHECK_FLAG_NEVER_CHECK_SUBJECT;
                X509_VERIFY_PARAM_set_hostflags(parameter, host_flags);
                if (!*policy->hostname) accepted = 0;
                else if (ip ? !X509_VERIFY_PARAM_set1_ip_asc(parameter, policy->hostname) :
                    !X509_VERIFY_PARAM_set1_host(parameter, policy->hostname, strlen(policy->hostname))) status = -50;
            }
        }
        int valid = status ? -1 : X509_verify_cert(context);
        if (valid != 1 || !roots) accepted = 0;
        if (valid < 0 && !status) status = -2070;
        if (!i && !sec_chain_save(trust, X509_STORE_CTX_get0_chain(context))) status = -108;
        X509_STORE_CTX_free(context);
    }
    X509_STORE_free(store);
    sk_X509_free(untrusted);
    ERR_clear_error();
    if (status) { sec_invalidate(trust); return status; }
    trust->evaluated = 1;
    *result = accepted ? 4 : 5;
    return 0;
}

static long sec_trust_count(void *object)
{
    sec_object *trust = sec_find(object, 3);
    if (!trust) return 0;
    if (!trust->evaluated) { uint32_t result; if (sec_trust_evaluate(trust, &result)) return 0; }
    return (long)trust->chain.count;
}

static void *sec_trust_certificate(void *object, long index)
{
    sec_object *trust = sec_find(object, 3);
    if (!trust || index < 0 || index >= sec_trust_count(trust)) return NULL;
    return trust->chain.items[index];
}

static void *sec_trust_key(void *object)
{
    sec_object *trust = sec_find(object, 3);
    if (!trust || !trust->certificates.count) return NULL;
    EVP_PKEY *key = X509_get_pubkey(trust->certificates.items[0]->certificate);
    if (!key) return NULL;
    unsigned char *der = NULL;
    int length = i2d_PUBKEY(key, &der);
    if (length <= 0) { EVP_PKEY_free(key); return NULL; }
    sec_object *value = sec_new(4);
    if (!value) { EVP_PKEY_free(key); OPENSSL_free(der); return NULL; }
    value->key = key;
    value->der = malloc((size_t)length);
    if (!value->der) { OPENSSL_free(der); cf_owned_release(value); return NULL; }
    memcpy(value->der, der, (size_t)length);
    value->der_length = (size_t)length;
    OPENSSL_free(der);
    return value;
}

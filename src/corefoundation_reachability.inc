#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>
#include <time.h>

typedef struct {
    long version;
    void *info;
    const void *(*retain)(const void *);
    void (*release)(const void *);
    void *(*description)(const void *);
} sc_context;

typedef struct sc_observer {
    size_t references;
    void (*callback)(void *, uint32_t, void *);
    sc_context context;
} sc_observer;

typedef struct sc_reachability {
    void *isa;
    uint64_t magic;
    size_t serial;
    char *name;
    struct sockaddr_storage address, resolved[32];
    size_t resolved_count;
    sc_observer *observer;
    unsigned scheduled;
    uint32_t delivered;
    int notified, resolving, resolved_valid;
    double next_poll, next_dns;
    struct sc_reachability *next;
} sc_reachability;

static sc_reachability *sc_targets;
static size_t sc_target_count;
static int sc_polling;

static double sc_clock(void)
{
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) ? 0 : (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static sc_reachability *sc_find(void *object)
{
    for (sc_reachability *r = sc_targets; r; r = r->next) if (r == object) return r;
    return NULL;
}

static void sc_observer_release(sc_observer *observer)
{
    if (!observer || --observer->references) return;
    if (observer->context.release) observer->context.release(observer->context.info);
    free(observer);
}

static void sc_destroy(void *object)
{
    sc_reachability *r = object;
    sc_reachability **link = &sc_targets;
    while (*link && *link != r) link = &(*link)->next;
    if (*link) { *link = r->next; sc_target_count--; }
    sc_observer *observer = r->observer;
    r->observer = NULL;
    free(r->name);
    sc_observer_release(observer);
}

static sc_reachability *sc_new(void)
{
    if (sc_target_count >= 256) return NULL;
    sc_reachability *r = fo_alloc("NSObject", sizeof(*r));
    if (!r) return NULL;
    if (!cf_owned_add(r, NULL, NULL)) { free(r); return NULL; }
    cf_owned_find(r)->destroy = sc_destroy;
    r->magic = 0x53435245414348ull;
    r->next = sc_targets;
    sc_targets = r;
    sc_target_count++;
    return r;
}

static int sc_literal(const char *name, struct sockaddr_storage *storage)
{
    struct sockaddr_in *v4 = (void *)storage;
    if (inet_pton(AF_INET, name, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons(9);
        return 1;
    }
    struct sockaddr_in6 *v6 = (void *)storage;
    if (inet_pton(AF_INET6, name, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(9);
        return 1;
    }
    memset(storage, 0, sizeof(*storage));
    return 0;
}

static void *sc_create_address(void *allocator, const unsigned char *address)
{
    if (allocator || !address) return NULL;
    unsigned length = address[0], family = address[1];
    if ((family != 2 || length != 16) && (family != 30 || length != 28)) return NULL;
    sc_reachability *r = sc_new();
    if (!r) return NULL;
    if (family == 2) {
        struct sockaddr_in *v4 = (void *)&r->address;
        v4->sin_family = AF_INET;
        v4->sin_port = htons(9);
        memcpy(&v4->sin_addr, address + 4, 4);
    } else {
        struct sockaddr_in6 *v6 = (void *)&r->address;
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(9);
        memcpy(&v6->sin6_addr, address + 8, 16);
        memcpy(&v6->sin6_scope_id, address + 24, 4);
    }
    return r;
}

static void *sc_create_name(void *allocator, const char *name)
{
    if (allocator || !name || !*name || strnlen(name, 1025) > 1024) return NULL;
    sc_reachability *r = sc_new();
    if (!r) return NULL;
    if (!sc_literal(name, &r->address)) {
        r->name = strdup(name);
        if (!r->name) { cf_owned_release(r); return NULL; }
    }
    return r;
}

static const unsigned char *sc_ip(const struct sockaddr *address, size_t *length)
{
    if (!address) return NULL;
    if (address->sa_family == AF_INET) {
        *length = 4;
        return (const void *)&((const struct sockaddr_in *)address)->sin_addr;
    }
    if (address->sa_family == AF_INET6) {
        *length = 16;
        return (const void *)&((const struct sockaddr_in6 *)address)->sin6_addr;
    }
    return NULL;
}

static int sc_zero(const struct sockaddr_storage *address)
{
    size_t length = 0;
    const unsigned char *bytes = sc_ip((const void *)address, &length);
    if (!bytes) return 0;
    for (size_t i = 0; i < length; i++) if (bytes[i]) return 0;
    return 1;
}

static int sc_cellular(const char *name)
{
    return name && (!strncmp(name, "rmnet", 5) || !strncmp(name, "ccmni", 5) ||
        !strncmp(name, "wwan", 4) || !strncmp(name, "pdp", 3));
}

static int sc_route(const struct sockaddr_storage *destination, uint32_t *flags)
{
    *flags = 0;
    int family = destination->ss_family;
    socklen_t size = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    int fd = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT;
    int status;
    do { status = connect(fd, (const struct sockaddr *)destination, size); } while (status && errno == EINTR);
    int error = errno;
    struct sockaddr_storage local = {0};
    socklen_t local_size = sizeof(local);
    if (!status) {
        status = getsockname(fd, (struct sockaddr *)&local, &local_size);
        if (status) error = errno;
    }
    close(fd);
    if (status) return error == ENETUNREACH || error == EHOSTUNREACH || error == EADDRNOTAVAIL || error == ENETDOWN;
    *flags = 2;
    size_t length = 0, source_length = 0;
    const unsigned char *target = sc_ip((const void *)destination, &length);
    const unsigned char *source = sc_ip((const void *)&local, &source_length);
    if (target && source && length == source_length && !memcmp(target, source, length)) *flags |= (1u << 16) | (1u << 17);
    if (family == AF_INET && target && target[0] == 127) *flags |= (1u << 16) | (1u << 17);
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces)) return 1;
    for (struct ifaddrs *i = interfaces; i; i = i->ifa_next) {
        if (!(i->ifa_flags & IFF_UP) || !i->ifa_addr || i->ifa_addr->sa_family != family) continue;
        size_t n = 0, mask_length = 0;
        const unsigned char *ip = sc_ip(i->ifa_addr, &n);
        const unsigned char *mask = sc_ip(i->ifa_netmask, &mask_length);
        if (!ip || n != length) continue;
        if (!memcmp(ip, target, n)) *flags |= 1u << 16;
        if (!source || source_length != n || memcmp(ip, source, n)) continue;
        if (sc_cellular(i->ifa_name)) *flags |= 1u << 18;
        if (mask && mask_length == n) {
            int direct = 1;
            for (size_t j = 0; j < n; j++) if ((ip[j] & mask[j]) != (target[j] & mask[j])) direct = 0;
            if (direct) *flags |= 1u << 17;
        }
    }
    freeifaddrs(interfaces);
    return 1;
}

static int sc_default_route(uint32_t *flags)
{
    struct sockaddr_storage address = {0};
    uint32_t v4 = 0, v6 = 0;
    sc_literal("192.0.2.1", &address);
    int ok4 = sc_route(&address, &v4);
    memset(&address, 0, sizeof(address));
    sc_literal("2001:db8::1", &address);
    int ok6 = sc_route(&address, &v6);
    *flags = ((v4 & 2) ? v4 : v6) & (2u | (1u << 18));
    return (ok4 && (v4 & 2)) || (ok6 && (v6 & 2)) || (ok4 && ok6);
}

static int sc_query(sc_reachability *r, uint32_t *flags)
{
    if (r->name) {
        if (!r->resolved_valid) return sc_default_route(flags);
        *flags = 0;
        int ok = r->resolved_count == 0;
        for (size_t i = 0; i < r->resolved_count; i++) {
            uint32_t candidate = 0;
            if (sc_route(&r->resolved[i], &candidate)) ok = 1;
            if (candidate & 2) { *flags = candidate; return 1; }
        }
        return ok;
    }
    return sc_zero(&r->address) ? sc_default_route(flags) : sc_route(&r->address, flags);
}

static unsigned char sc_get_flags(void *object, uint32_t *flags)
{
    sc_reachability *r = sc_find(object);
    uint32_t value;
    if (!r || !flags || !sc_query(r, &value)) return 0;
    *flags = value;
    return 1;
}

static unsigned char sc_set_callback(void *object, void (*callback)(void *, uint32_t, void *), const sc_context *context)
{
    sc_reachability *r = sc_find(object);
    if (!r || (context && context->version)) return 0;
    sc_observer *observer = NULL;
    if (callback) {
        observer = calloc(1, sizeof(*observer));
        if (!observer) return 0;
        observer->references = 1;
        observer->callback = callback;
        if (context) observer->context = *context;
    }
    cf_owned_retain(r);
    if (observer && observer->context.retain) observer->context.info = (void *)observer->context.retain(observer->context.info);
    sc_observer *previous = r->observer;
    r->observer = observer;
    r->notified = 0;
    r->next_poll = 0;
    sc_observer_release(previous);
    cf_owned_release(r);
    return 1;
}

static unsigned sc_mode(void *loop, void *mode)
{
    if (loop != main_run_loop() || (!fo_is_string(mode) && !fo_is_cfstring(mode))) return 0;
    const char *name = fo_bytes(mode);
    return !strcmp(name, "kCFRunLoopDefaultMode") || !strcmp(name, "NSDefaultRunLoopMode") ? 1 :
        !strcmp(name, "kCFRunLoopCommonModes") || !strcmp(name, "NSRunLoopCommonModes") ? 2 : 0;
}

static unsigned char sc_schedule(void *object, void *loop, void *mode)
{
    sc_reachability *r = sc_find(object);
    unsigned flag = sc_mode(loop, mode);
    if (!r || !flag || chm_dispatch_stopping()) return 0;
    if (!r->scheduled) { r->serial++; r->notified = 0; r->resolved_valid = 0; r->next_dns = 0; }
    r->scheduled |= flag;
    r->next_poll = 0;
    return 1;
}

static unsigned char sc_unschedule(void *object, void *loop, void *mode)
{
    sc_reachability *r = sc_find(object);
    unsigned flag = sc_mode(loop, mode);
    if (!r || !flag || !(r->scheduled & flag)) return 0;
    r->scheduled &= ~flag;
    if (!r->scheduled) { r->serial++; r->notified = 0; }
    return 1;
}

static void sc_resolve(void *context)
{
    sc_reachability *r = context;
    size_t serial = r->serial;
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM}, *answers = NULL;
    struct sockaddr_storage addresses[32];
    size_t count = 0;
    unsigned depth = chm_guest_suspend();
    int error = getaddrinfo(r->name, "9", &hints, &answers);
    for (struct addrinfo *a = answers; a && count < 32; a = a->ai_next) {
        if (!a->ai_addr || (a->ai_family != AF_INET && a->ai_family != AF_INET6) || a->ai_addrlen > sizeof(*addresses)) continue;
        memset(&addresses[count], 0, sizeof(*addresses));
        memcpy(&addresses[count++], a->ai_addr, a->ai_addrlen);
    }
    if (answers) freeaddrinfo(answers);
    chm_guest_resume(depth);
    r->resolving = 0;
    if (r->scheduled && r->serial == serial && !chm_dispatch_stopping()) {
        r->resolved_valid = !error || error == EAI_NONAME;
        r->resolved_count = count;
        if (count) memcpy(r->resolved, addresses, count * sizeof(*addresses));
        r->next_dns = sc_clock() + (error && error != EAI_NONAME ? 5 : 30);
        r->next_poll = 0;
    }
    cf_owned_release(r);
}

static int sc_poll_body(void *unused)
{
    (void)unused;
    sc_reachability *snapshot[256];
    size_t count = 0;
    double now = sc_clock();
    for (sc_reachability *r = sc_targets; r; r = r->next)
        if (r->scheduled && r->observer && now >= r->next_poll) snapshot[count++] = cf_owned_retain(r);
    int delivered = 0;
    for (size_t i = 0; i < count; i++) {
        sc_reachability *r = snapshot[i];
        if (r->scheduled && r->observer) {
            uint32_t flags = 0;
            r->next_poll = now + 1;
            if (sc_query(r, &flags) && (!r->notified || flags != r->delivered)) {
                sc_observer *observer = r->observer;
                observer->references++;
                r->notified = 1;
                r->delivered = flags;
                observer->callback(r, flags, observer->context.info);
                sc_observer_release(observer);
                delivered++;
            }
            if (r->scheduled && r->name && !r->resolving && now >= r->next_dns && !chm_dispatch_stopping()) {
                void *(*global_queue)(long, unsigned long) = chm_dispatch_lookup("_dispatch_get_global_queue");
                r->resolving = 1;
                cf_owned_retain(r);
                if (!global_queue || chm_dispatch_submit(global_queue(0, 0), r, sc_resolve)) {
                    r->resolving = 0;
                    cf_owned_release(r);
                }
            }
        }
        cf_owned_release(r);
    }
    return delivered;
}

size_t chm_reachability_pump(chm_session *session)
{
    chm_guest_lock();
    if (!session || session != g_session || !sc_targets || sc_polling || chm_dispatch_stopping() || session->exit_code < 0) {
        chm_guest_unlock();
        return 0;
    }
    sc_polling = 1;
    int result = chm_session_callback(session, sc_poll_body, NULL);
    sc_polling = 0;
    if (result < 0) session->exit_code = -2;
    chm_guest_unlock();
    return result > 0 ? (size_t)result : 0;
}

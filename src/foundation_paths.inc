static char g_sandbox_root[512] = "/tmp/chimera";
static char g_guest_directory[1024] = "/";
static char g_bundle_root[512];

static int path_below(const char *path, const char *root)
{
    size_t n = strlen(root);
    return n && !strncmp(path, root, n) && (!path[n] || path[n] == '/');
}

void chm_foundation_set_root(const char *root)
{
    if (!root || root[0] != '/' || strlen(root) >= sizeof(g_sandbox_root)) return;
    snprintf(g_sandbox_root, sizeof(g_sandbox_root), "%s", root);
    size_t n = strlen(g_sandbox_root);
    while (n > 1 && g_sandbox_root[n - 1] == '/') g_sandbox_root[--n] = 0;
    strcpy(g_guest_directory, "/");
}

int chm_foundation_map_path(const char *in, char *out, size_t cap)
{
    if (!in || !*in || !out || !cap) { errno = EINVAL; return 0; }
    const char *root = g_sandbox_root, *rel = in;
    char joined[2048], normalized[2048];
    if (path_below(in, g_sandbox_root)) rel = in + strlen(g_sandbox_root);
    else if (path_below(in, g_bundle_root)) { root = g_bundle_root; rel = in + strlen(root); }
    else if (!strncmp(in, "/private/var/", 13)) rel = in + 13;
    else if (!strncmp(in, "/var/", 5)) rel = in + 5;
    else if (!strcmp(in, "/var") || !strcmp(in, "/private/var")) rel = "";
    else if (!strncmp(in, "/tmp/", 5)) rel = in + 5;
    else if (*in != '/') {
        int n = snprintf(joined, sizeof(joined), "%s/%s", g_guest_directory, in);
        if (n < 0 || (size_t)n >= sizeof(joined)) { errno = ENAMETOOLONG; return 0; }
        rel = joined;
    }
    size_t length = 0;
    while (*rel) {
        while (*rel == '/') rel++;
        const char *end = strchr(rel, '/');
        size_t n = end ? (size_t)(end - rel) : strlen(rel);
        if (!n) break;
        if (n == 1 && rel[0] == '.') {}
        else if (n == 2 && rel[0] == '.' && rel[1] == '.') {
            if (!length) { errno = EACCES; return 0; }
            while (length && normalized[length - 1] != '/') length--;
            if (length) length--;
        } else {
            if (length + n + 2 > sizeof(normalized)) { errno = ENAMETOOLONG; return 0; }
            if (length) normalized[length++] = '/';
            memcpy(normalized + length, rel, n); length += n;
        }
        rel += n;
    }
    normalized[length] = 0;
    int written = snprintf(out, cap, "%s%s%s", root, length ? "/" : "", normalized);
    if (written < 0 || (size_t)written >= cap) { errno = ENAMETOOLONG; return 0; }
    char probe[2048], resolved[4096];
    if (strlen(out) >= sizeof(probe)) { errno = ENAMETOOLONG; return 0; }
    strcpy(probe, out);
    for (;;) {
        if (realpath(probe, resolved)) {
            char base[4096];
            if (realpath(root, base) && !path_below(resolved, base)) { errno = EACCES; return 0; }
            break;
        }
        char *slash = strrchr(probe, '/');
        if (!slash || slash == probe || strlen(probe) <= strlen(root)) break;
        *slash = 0;
    }
    return 1;
}

static int directory_one(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR;
        return -1;
    }
    if (errno != ENOENT) return -1;
    if (mkdir(path, 0700) == 0) return 0;
    if (errno != EEXIST || stat(path, &st)) return -1;
    if (S_ISDIR(st.st_mode)) return 0;
    errno = ENOTDIR;
    return -1;
}

static int mkdir_p(const char *path)
{
    char tmp[2048]; size_t n = strlen(path);
    if (!n || n >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    if (!path_below(path, g_sandbox_root)) { errno = EACCES; return -1; }
    if (directory_one(g_sandbox_root)) return -1;
    if (!strcmp(path, g_sandbox_root)) return 0;
    memcpy(tmp, path, n + 1);
    for (size_t i = strlen(g_sandbox_root) + 1; i < n; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = 0; int rc = directory_one(tmp); tmp[i] = '/';
        if (rc) return -1;
    }
    return directory_one(tmp);
}

int chm_foundation_map_mutable(const char *in, char *out, size_t cap)
{
    if (!chm_foundation_map_path(in, out, cap)) return 0;
    if (!path_below(out, g_sandbox_root) ||
        (path_below(g_bundle_root, g_sandbox_root) && path_below(out, g_bundle_root))) {
        errno = EACCES;
        return 0;
    }
    return 1;
}

int chm_foundation_prepare_home(void)
{
    const char *dirs[] = {"/var/mobile/Documents", "/var/mobile/Library/Caches", "/var/mobile/tmp"};
    if (directory_one(g_sandbox_root)) return 0;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(*dirs); ++i) {
        char path[1024];
        if (!chm_foundation_map_mutable(dirs[i], path, sizeof(path)) || mkdir_p(path)) return 0;
    }
    return 1;
}

int chm_foundation_map_path_write(const char *in, char *out, size_t cap)
{
    if (!chm_foundation_map_mutable(in, out, cap)) return 0;
    if (!strcmp(out, g_sandbox_root)) return 1;
    char parent[2048];
    if (strlen(out) >= sizeof(parent)) { errno = ENAMETOOLONG; return 0; }
    strcpy(parent, out);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent) return 1;
    *slash = 0;
    return mkdir_p(parent) == 0;
}
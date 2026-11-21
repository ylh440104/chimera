#include "chm_session.h"
#include "chm_runtime.h"
#include "chm_format.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHM_DYLIB_BASE UINT64_C(0x200000000)
#define CHM_DYLIB_ALIGN UINT64_C(0x4000)
#define CHM_LC_DYLD_EXPORTS_TRIE 0x80000033u
#define CHM_EXPORT_REEXPORT 0x08u

static uint64_t read_uleb(const uint8_t **pp, const uint8_t *end)
{
    const uint8_t *p = *pp;
    uint64_t v = 0;
    unsigned shift = 0;

    while (p < end) {
        uint8_t b = *p++;

        v |= (uint64_t)(b & 0x7fu) << shift;
        if (!(b & 0x80u))
            break;
        shift += 7;
    }
    *pp = p;
    return v;
}

static void add_export(chm_session *s, int img_idx, const char *name, uint64_t addr)
{
    if (!name || !name[0] || s->export_count >= CHM_MAX_EXPORTS)
        return;
    chm_export_sym *e = &s->exports[s->export_count++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->addr = addr;
    e->image_index = img_idx;
}

static void walk_trie(chm_session *s, int img_idx, const uint8_t *start, const uint8_t *end,
                      const uint8_t *node, const char *prefix, uint64_t base, int depth)
{
    const uint8_t *p = node;
    const uint8_t *term_end;
    uint64_t term;
    uint8_t children;
    int i;

    if (depth > 32 || p >= end)
        return;
    term = read_uleb(&p, end);
    if (term) {
        uint64_t flags;

        term_end = p + term;
        if (term_end > end)
            return;
        flags = read_uleb(&p, term_end);
        if ((flags & CHM_EXPORT_REEXPORT) == 0) {
            uint64_t off = read_uleb(&p, term_end);

            add_export(s, img_idx, prefix, base + off);
        }
        p = term_end;
    }
    if (p >= end)
        return;
    children = *p++;
    for (i = 0; i < children && p < end; i++) {
        const char *edge = (const char *)p;
        const uint8_t *nul = memchr(p, 0, (size_t)(end - p));
        char buf[192];
        uint64_t off;

        if (!nul)
            return;
        p = nul + 1;
        if (p >= end)
            return;
        off = read_uleb(&p, end);
        snprintf(buf, sizeof(buf), "%s%s", prefix, edge);
        walk_trie(s, img_idx, start, end, start + off, buf, base, depth + 1);
    }
}

static void collect_exports(chm_session *s, int img_idx)
{
    chm_image *img = &s->images[img_idx];
    const uint8_t *p = (const uint8_t *)img->file + sizeof(struct chm_mach_header);
    const uint8_t *end = (const uint8_t *)img->file + img->filesize;
    const uint8_t *start = NULL;
    const uint8_t *tend = NULL;
    uint64_t base = img->vm_min + img->slide;
    int before = s->export_count;
    uint32_t i;

    for (i = 0; i < img->hdr->ncmds; i++) {
        const struct chm_load_command *lc;

        if ((size_t)(end - p) < sizeof(*lc))
            break;
        lc = (const struct chm_load_command *)p;
        if (lc->cmdsize < sizeof(*lc) || (size_t)lc->cmdsize > (size_t)(end - p))
            break;
        if (lc->cmd == CHM_LC_DYLD_EXPORTS_TRIE) {
            const struct chm_linkedit_data_command *dc = (const struct chm_linkedit_data_command *)p;

            if ((uint64_t)dc->dataoff + dc->datasize <= img->filesize) {
                start = (const uint8_t *)img->file + dc->dataoff;
                tend = start + dc->datasize;
            }
            break;
        }
        p += lc->cmdsize;
    }
    if (!start && img->dyld_info && img->dyld_info->export_size) {
        const struct chm_dyld_info_command *di = img->dyld_info;

        if ((uint64_t)di->export_off + di->export_size <= img->filesize) {
            start = (const uint8_t *)img->file + di->export_off;
            tend = start + di->export_size;
        }
    }
    if (start && tend > start)
        walk_trie(s, img_idx, start, tend, start, "", base, 0);
    chm_session_log(s, "exports[%d]: %d symbols", img_idx, s->export_count - before);
}

static int find_bundled_dylib(const char *app_dir, const char *loader_dir, const char *want,
                              char *out, size_t cap)
{
    const char *base;
    char cand[1280];
    char fw_dir[768];
    DIR *dir;
    struct dirent *ent;

    if (strncmp(want, "@executable_path/", 17) == 0) {
        snprintf(cand, sizeof(cand), "%s/%.240s", app_dir, want + 17);
        if (access(cand, R_OK) == 0) {
            snprintf(out, cap, "%.*s", (int)cap - 1, cand);
            return 0;
        }
    }
    if (strncmp(want, "@loader_path/", 13) == 0) {
        snprintf(cand, sizeof(cand), "%s/%.240s", loader_dir, want + 13);
        if (access(cand, R_OK) == 0) {
            snprintf(out, cap, "%.*s", (int)cap - 1, cand);
            return 0;
        }
    }
    if (strncmp(want, "@rpath/", 7) == 0) {
        snprintf(cand, sizeof(cand), "%s/Frameworks/%.240s", app_dir, want + 7);
        if (access(cand, R_OK) == 0) {
            snprintf(out, cap, "%.*s", (int)cap - 1, cand);
            return 0;
        }
    }
    base = strrchr(want, '/');
    base = base ? base + 1 : want;
    snprintf(cand, sizeof(cand), "%s/Frameworks/%.180s", app_dir, base);
    if (access(cand, R_OK) == 0) {
        snprintf(out, cap, "%.*s", (int)cap - 1, cand);
        return 0;
    }
    snprintf(fw_dir, sizeof(fw_dir), "%s/Frameworks", app_dir);
    dir = opendir(fw_dir);
    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.' || !strstr(ent->d_name, ".framework"))
                continue;
            snprintf(cand, sizeof(cand), "%s/%.180s/%.180s", fw_dir, ent->d_name, base);
            if (access(cand, R_OK) == 0) {
                snprintf(out, cap, "%.*s", (int)cap - 1, cand);
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);
    }
    return -1;
}

static bool already_loaded(chm_session *s, const char *path)
{
    int i;

    for (i = 0; i < s->image_count; i++) {
        if (strcmp(s->image_paths[i], path) == 0)
            return true;
    }
    return false;
}

static void record_missing(chm_session *s, const char *name, bool weak)
{
    int i;

    for (i = 0; i < s->missing_count; i++) {
        if (strcmp(s->missing[i].name, name) == 0) {
            s->missing[i].hits++;
            return;
        }
    }
    if (s->missing_count < CHM_MAX_MISSING) {
        chm_missing_sym *m = &s->missing[s->missing_count++];

        snprintf(m->name, sizeof(m->name), "%s", name);
        m->hits = 1;
        m->weak = weak;
    }
}

static int session_resolve(const chm_image *img, int32_t ordinal, const char *name,
                           bool weak, uint64_t *out, void *ctx)
{
    chm_session *s = ctx;
    void *p;
    int i;

    if (ordinal == 0) {
        p = chm_sym((chm_image *)img, name);
        if (p) {
            *out = (uint64_t)(uintptr_t)p;
            return 1;
        }
    }
    for (i = 0; i < s->export_count; i++) {
        if (strcmp(s->exports[i].name, name) == 0) {
            *out = s->exports[i].addr;
            return 1;
        }
    }
    p = chm_objc_session_lookup(s, name);
    if (p) {
        *out = (uint64_t)(uintptr_t)p;
        return 1;
    }
    p = chm_dispatch_lookup(name);
    if (p) {
        *out = (uint64_t)(uintptr_t)p;
        return 1;
    }
    p = chm_frameworks_lookup(name);
    if (p) {
        *out = (uint64_t)(uintptr_t)p;
        return 1;
    }
    p = chm_runtime_lookup(name);
    if (p) {
        *out = (uint64_t)(uintptr_t)p;
        return 1;
    }
    record_missing(s, name, weak);
    *out = 0;
    return 0;
}

chm_session *chm_session_create(void)
{
    chm_session *s = calloc(1, sizeof(*s));

    if (!s)
        return NULL;
    s->exports = calloc(CHM_MAX_EXPORTS, sizeof(*s->exports));
    s->missing = calloc(CHM_MAX_MISSING, sizeof(*s->missing));
    s->classes = calloc(CHM_MAX_CLASSES, sizeof(*s->classes));
    s->methods = calloc(CHM_MAX_METHOD_REGS, sizeof(*s->methods));
    s->sels = calloc(CHM_MAX_SELS, sizeof(*s->sels));
    s->log_cap = 16384;
    s->log_buf = calloc(1, s->log_cap);
    if (!s->exports || !s->missing || !s->classes || !s->methods || !s->sels || !s->log_buf) {
        chm_session_destroy(s);
        return NULL;
    }
    chm_runtime_init();
    chm_libc_set_session(s);
    chm_objc_session_init(s);
    chm_foundation_init(s);
    chm_ui_init(s);
    return s;
}

void chm_session_destroy(chm_session *s)
{
    int i;

    if (!s)
        return;
    for (i = 0; i < s->image_count; i++)
        chm_close(&s->images[i]);
    if (s->sels) {
        for (i = 0; i < s->sel_count; i++)
            free(s->sels[i]);
        free(s->sels);
    }
    free(s->exports);
    free(s->missing);
    free(s->classes);
    free(s->methods);
    free(s->log_buf);
    chm_foundation_shutdown();
    free(s);
}

void chm_session_log(chm_session *s, const char *fmt, ...)
{
    va_list ap;
    char line[512];
    size_t n;

    if (!s || !s->log_buf)
        return;
    va_start(ap, fmt);
    chm_format(line, sizeof(line), fmt, ap);
    va_end(ap);
    n = strlen(line);
    if (s->log_len + n + 2 < s->log_cap) {
        memcpy(s->log_buf + s->log_len, line, n);
        s->log_len += n;
        s->log_buf[s->log_len++] = '\n';
        s->log_buf[s->log_len] = 0;
    }
}

static void dir_of(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');

    if (slash && (size_t)(slash - path) < cap) {
        size_t n = (size_t)(slash - path);

        memcpy(out, path, n);
        out[n] = 0;
    } else {
        snprintf(out, cap, ".");
    }
}

int chm_session_load(chm_session *s, const char *exe_path)
{
    uint64_t next_base = CHM_DYLIB_BASE;
    int idx;
    uint32_t d;

    snprintf(s->exe_path, sizeof(s->exe_path), "%s", exe_path);
    dir_of(exe_path, s->app_dir, sizeof(s->app_dir));

    if (chm_open(&s->images[0], exe_path)) {
        chm_session_log(s, "open main failed: %s", s->images[0].error);
        return -1;
    }
    snprintf(s->image_paths[0], sizeof(s->image_paths[0]), "%s", exe_path);
    s->image_count = 1;
    if (chm_map_default(&s->images[0])) {
        chm_session_log(s, "map main failed: %s", s->images[0].error);
        return -1;
    }

    for (idx = 0; idx < s->image_count; idx++) {
        char loader_dir[512];

        dir_of(s->image_paths[idx], loader_dir, sizeof(loader_dir));
        for (d = 0; d < s->images[idx].ndylibs && s->image_count < CHM_MAX_IMAGES; d++) {
            char path[512];
            int slot;
            uint64_t span;
            uint64_t slide;

            if (find_bundled_dylib(s->app_dir, loader_dir, s->images[idx].dylibs[d].name,
                                   path, sizeof(path)) != 0)
                continue;
            if (already_loaded(s, path))
                continue;
            slot = s->image_count;
            if (chm_open(&s->images[slot], path)) {
                chm_session_log(s, "open dylib %s failed: %s", path, s->images[slot].error);
                continue;
            }
            span = (s->images[slot].vm_max - s->images[slot].vm_min + CHM_DYLIB_ALIGN - 1) &
                   ~(CHM_DYLIB_ALIGN - 1);
            if (!span)
                span = CHM_DYLIB_ALIGN;
            slide = next_base - s->images[slot].vm_min;
            if (chm_map(&s->images[slot], slide)) {
                chm_session_log(s, "map dylib %s failed: %s", path, s->images[slot].error);
                chm_close(&s->images[slot]);
                continue;
            }
            snprintf(s->image_paths[slot], sizeof(s->image_paths[slot]), "%s", path);
            s->image_count++;
            next_base += span;
            chm_session_log(s, "loaded dylib[%d]: %s", slot, path);
        }
    }

    for (idx = 0; idx < s->image_count; idx++)
        collect_exports(s, idx);

    for (idx = s->image_count - 1; idx >= 0; idx--) {
        s->images[idx].resolver = session_resolve;
        s->images[idx].resolver_context = s;
        if (chm_bind_image(&s->images[idx]) || chm_protect(&s->images[idx])) {
            chm_session_log(s, "bind[%d] failed: %s", idx, s->images[idx].error);
            return -1;
        }
        chm_session_log(s, "bound[%d]: bind=%u missing=%u rebase=%u exec=%d",
                        idx, s->images[idx].nfixup_bind, s->images[idx].nfixup_missing,
                        s->images[idx].nfixup_rebase, s->images[idx].executable);
    }

    for (idx = s->image_count - 1; idx >= 0; idx--)
        chm_objc_session_scan(s, idx);
    chm_libc_publish_images(s);
    s->prepared = true;
    return 0;
}

static void setup_env(void)
{
    char root[512];
    char path[768];

    chm_foundation_map_path("/var/mobile", root, sizeof(root));
    setenv("HOME", root, 1);
    setenv("CMC_HOME", root, 1);
    setenv("TMPDIR", root, 1);
    snprintf(path, sizeof(path), "%s/Library/Caches", root);
    setenv("CFFIXED_USER_HOME", root, 1);
    setenv("XDG_CACHE_HOME", path, 1);
}

int chm_session_run(chm_session *s, int argc, char **argv)
{
    if (!s || !s->prepared || !s->images[0].executable)
        return -1;
    setup_env();
    s->exit_code = chm_session_execute(s, &s->images[0], argc, argv);
    return s->exit_code;
}

void chm_session_report(chm_session *s, char *out, size_t cap)
{
    int i;
    int shown = 0;

    if (!s || !out || !cap)
        return;
    out[0] = 0;
    snprintf(out, cap, "%s", s->log_buf);
    if (s->missing_count > 0) {
        size_t used = strlen(out);

        snprintf(out + used, cap - used, "missing distinct=%d:", s->missing_count);
        for (i = 0; i < s->missing_count && shown < 12; i++) {
            used = strlen(out);
            snprintf(out + used, cap - used, " %s", s->missing[i].name);
            shown++;
        }
        used = strlen(out);
        snprintf(out + used, cap - used, "\n");
    }
}
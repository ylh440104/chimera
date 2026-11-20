#include "chm.h"
#include "chm_int.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int chm_fixup_at(chm_image *img, uint64_t slide);

#define CHM_DYLIB_MAX 32
#define CHM_EXPORT_MAX 8192
#define CHM_DYLIB_BASE 0x200000000ull
#define CHM_DYLIB_ALIGN 0x4000ull
#define CHM_LC_DYLD_EXPORTS_TRIE 0x80000033u
#define CHM_EXPORT_REEXPORT 0x08u

typedef struct {
    char name[160];
    uint64_t addr;
} chm_export;

typedef struct {
    chm_image img;
    char install[256];
    char path[1024];
    uint64_t base;
    int ok;
    int exports;
} chm_dylib_slot;

static chm_dylib_slot g_slots[CHM_DYLIB_MAX];
static int g_slot_count;
static chm_export g_exports[CHM_EXPORT_MAX];
static int g_export_count;

static uint64_t chm_uleb(const uint8_t **pp, const uint8_t *end)
{
    const uint8_t *p = *pp;
    uint64_t v = 0;
    unsigned shift = 0;

    while (p < end) {
        uint8_t b = *p++;

        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80))
            break;
        shift += 7;
    }
    *pp = p;
    return v;
}

static void chm_export_add(const char *name, uint64_t addr)
{
    if (!name || !name[0])
        return;
    if (g_export_count >= CHM_EXPORT_MAX)
        return;
    snprintf(g_exports[g_export_count].name, sizeof(g_exports[g_export_count].name), "%s", name);
    g_exports[g_export_count].addr = addr;
    g_export_count++;
}

static void chm_trie_walk(const uint8_t *start, const uint8_t *end, const uint8_t *node,
                          const char *prefix, uint64_t base, int depth)
{
    const uint8_t *p = node;
    const uint8_t *terminal_end;
    uint64_t terminal;
    uint8_t children;
    int i;

    if (depth > 24 || p >= end)
        return;
    terminal = chm_uleb(&p, end);
    if (terminal) {
        uint64_t flags;

        terminal_end = p + terminal;
        flags = chm_uleb(&p, end);
        if ((flags & CHM_EXPORT_REEXPORT) == 0) {
            uint64_t addr = chm_uleb(&p, end);

            chm_export_add(prefix, base + addr);
        }
        p = terminal_end;
    }
    if (p >= end)
        return;
    children = *p++;
    for (i = 0; i < children && p < end; i++) {
        const char *edge = (const char *)p;
        char name[192];
        uint64_t off;

        p += strlen(edge) + 1;
        if (p >= end)
            return;
        off = chm_uleb(&p, end);
        snprintf(name, sizeof(name), "%s%s", prefix, edge);
        chm_trie_walk(start, end, start + off, name, base, depth + 1);
    }
}

static const struct chm_linkedit_data_command *chm_exports_trie(chm_image *img)
{
    const uint8_t *p = (const uint8_t *)img->file + sizeof(struct chm_mach_header);
    const uint8_t *end = (const uint8_t *)img->file + img->filesize;
    uint32_t i;

    for (i = 0; i < img->hdr->ncmds; i++) {
        const struct chm_load_command *lc;

        if ((size_t)(end - p) < sizeof(struct chm_load_command))
            break;
        lc = (const struct chm_load_command *)p;
        if (lc->cmdsize < sizeof(struct chm_load_command))
            break;
        if ((size_t)lc->cmdsize > (size_t)(end - p))
            break;
        if (lc->cmd == CHM_LC_DYLD_EXPORTS_TRIE)
            return (const struct chm_linkedit_data_command *)p;
        p += lc->cmdsize;
    }
    return NULL;
}

static int chm_dylib_exports(chm_dylib_slot *s)
{
    const struct chm_linkedit_data_command *trie = chm_exports_trie(&s->img);
    const uint8_t *start;
    const uint8_t *end;
    int before = g_export_count;

    if (trie) {
        if ((uint64_t)trie->dataoff + trie->datasize > s->img.filesize)
            return -1;
        start = (const uint8_t *)s->img.file + trie->dataoff;
        end = start + trie->datasize;
        chm_trie_walk(start, end, start, "", s->base, 0);
    } else if (s->img.dyld_info && s->img.dyld_info->export_size) {
        const struct chm_dyld_info_command *di = s->img.dyld_info;

        if ((uint64_t)di->export_off + di->export_size > s->img.filesize)
            return -1;
        start = (const uint8_t *)s->img.file + di->export_off;
        end = start + di->export_size;
        chm_trie_walk(start, end, start, "", s->base, 0);
    } else {
        return 0;
    }
    s->exports = g_export_count - before;
    return 0;
}

static int chm_is_file(const char *path)
{
    return access(path, R_OK) == 0;
}

static int chm_scan_frameworks(const char *app_dir, const char *base, char *out, size_t cap)
{
    char fw_dir[640];
    DIR *dir;
    struct dirent *ent;

    snprintf(fw_dir, sizeof(fw_dir), "%s/Frameworks", app_dir);
    dir = opendir(fw_dir);
    if (!dir)
        return -1;
    while ((ent = readdir(dir)) != NULL) {
        char cand[1024];

        if (ent->d_name[0] == '.')
            continue;
        if (!strstr(ent->d_name, ".framework"))
            continue;
        snprintf(cand, sizeof(cand), "%s/%.180s/%.180s", fw_dir, ent->d_name, base);
        if (chm_is_file(cand)) {
            snprintf(out, cap, "%.1000s", cand);
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

static int chm_find_dylib(const char *want, const char *app_dir, char *out, size_t cap)
{
    const char *slash = strrchr(want, '/');
    const char *base = slash ? slash + 1 : want;
    char cand[1024];

    snprintf(cand, sizeof(cand), "%s/Frameworks/%.180s", app_dir, base);
    if (chm_is_file(cand)) {
        snprintf(out, cap, "%.1000s", cand);
        return 0;
    }
    snprintf(cand, sizeof(cand), "%s/%.180s", app_dir, base);
    if (chm_is_file(cand)) {
        snprintf(out, cap, "%.1000s", cand);
        return 0;
    }
    return chm_scan_frameworks(app_dir, base, out, cap);
}

void *chm_dylib_lookup(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < g_export_count; i++) {
        if (strcmp(g_exports[i].name, name) == 0)
            return (void *)(uintptr_t)g_exports[i].addr;
    }
    return NULL;
}

int chm_dylib_export_count(void)
{
    return g_export_count;
}

int chm_dylib_load_all(chm_image *main_img, const char *exe_path, chm_report *r)
{
    char app_dir[512];
    const char *slash = strrchr(exe_path, '/');
    uint64_t next = CHM_DYLIB_BASE;
    int loaded = 0;
    int missing = 0;
    uint32_t i;

    if (slash && (size_t)(slash - exe_path) < sizeof(app_dir)) {
        size_t n = (size_t)(slash - exe_path);

        memcpy(app_dir, exe_path, n);
        app_dir[n] = 0;
    } else {
        snprintf(app_dir, sizeof(app_dir), ".");
    }

    for (i = 0; i < main_img->ndylibs && g_slot_count < CHM_DYLIB_MAX; i++) {
        char path[1024];
        chm_dylib_slot *s = &g_slots[g_slot_count];
        uint64_t size;

        if (chm_find_dylib(main_img->dylibs[i].name, app_dir, path, sizeof(path)) != 0) {
            missing++;
            continue;
        }
        memset(s, 0, sizeof(*s));
        snprintf(s->install, sizeof(s->install), "%s", main_img->dylibs[i].name);
        snprintf(s->path, sizeof(s->path), "%s", path);
        if (chm_open(&s->img, path) != 0) {
            missing++;
            continue;
        }
        size = (s->img.vm_max + CHM_DYLIB_ALIGN - 1) & ~(CHM_DYLIB_ALIGN - 1);
        if (size == 0)
            size = CHM_DYLIB_ALIGN;
        s->base = next;
        if (chm_map(&s->img, s->base) != 0) {
            chm_close(&s->img);
            missing++;
            continue;
        }
        next += size;
        chm_dylib_exports(s);
        g_slot_count++;
        loaded++;
    }

    for (i = 0; i < (uint32_t)g_slot_count; i++) {
        if (chm_fixup_at(&g_slots[i].img, g_slots[i].base) == 0)
            g_slots[i].ok = 1;
    }

    chm_report_add(r, "dylib: 装 %d, 缺 %d, 导出 %d", loaded, missing, g_export_count);
    for (i = 0; i < (uint32_t)g_slot_count && i < 4; i++) {
        const char *name = strrchr(g_slots[i].path, '/');

        name = name ? name + 1 : g_slots[i].path;
        chm_report_add(r, "dylib: %s 导出 %d %s", name, g_slots[i].exports,
                       g_slots[i].ok ? "ok" : "half");
    }
    return loaded;
}
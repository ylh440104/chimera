#include "chm.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void chm_name(char *dst, size_t cap, const char *src, size_t len)
{
    size_t n = len < cap - 1 ? len : cap - 1;

    memcpy(dst, src, n);
    dst[n] = 0;
}

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

static int64_t chm_sleb(const uint8_t **pp, const uint8_t *end)
{
    const uint8_t *p = *pp;
    int64_t v = 0;
    unsigned shift = 0;
    uint8_t b = 0;

    while (p < end) {
        b = *p++;
        v |= (int64_t)(b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80))
            break;
    }
    if ((b & 0x40) && shift < 64)
        v |= -((int64_t)1 << shift);
    *pp = p;
    return v;
}

static int chm_prot(uint32_t p)
{
    int prot = 0;

    if (p & 1)
        prot |= PROT_READ;
    if (p & 2)
        prot |= PROT_WRITE;
    if (p & 4)
        prot |= PROT_EXEC;
    return prot ? prot : PROT_READ;
}

int chm_open(chm_image *img, const char *path)
{
    int fd;
    struct stat st;
    const uint8_t *p;
    const uint8_t *end;
    uint32_t i;

    memset(img, 0, sizeof(*img));
    img->vm_min = UINT64_MAX;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return -1;
    }
    if (st.st_size < (off_t)sizeof(struct chm_mach_header)) {
        close(fd);
        return -1;
    }
    img->filesize = (size_t)st.st_size;
    img->file = mmap(NULL, img->filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (img->file == MAP_FAILED) {
        img->file = NULL;
        return -1;
    }
    img->hdr = (struct chm_mach_header *)img->file;
    if (img->hdr->magic != CHM_MH_MAGIC_64)
        return -1;
    if (img->hdr->cputype != CHM_CPU_ARM64)
        return -1;

    p = (const uint8_t *)img->file + sizeof(struct chm_mach_header);
    end = (const uint8_t *)img->file + img->filesize;

    for (i = 0; i < img->hdr->ncmds; i++) {
        const struct chm_load_command *lc;

        if ((size_t)(end - p) < sizeof(struct chm_load_command))
            return -1;
        lc = (const struct chm_load_command *)p;
        if (lc->cmdsize < sizeof(struct chm_load_command))
            return -1;
        if ((size_t)lc->cmdsize > (size_t)(end - p))
            return -1;

        switch (lc->cmd) {
        case CHM_LC_SEGMENT_64: {
            const struct chm_segment_command *sc = (const struct chm_segment_command *)p;
            chm_segment *s;
            uint32_t k;

            if (img->nsegs >= CHM_MAX_SEGS)
                return -1;
            if (sizeof(*sc) > lc->cmdsize)
                return -1;
            if (sizeof(*sc) + (size_t)sc->nsects * sizeof(struct chm_section_64) > lc->cmdsize)
                return -1;

            s = &img->segs[img->nsegs++];
            chm_name(s->name, sizeof(s->name), sc->segname, 16);
            s->vmaddr = sc->vmaddr;
            s->vmsize = sc->vmsize;
            s->fileoff = sc->fileoff;
            s->filesize = sc->filesize;
            s->initprot = sc->initprot;
            s->maxprot = sc->maxprot;
            s->flags = sc->flags;
            s->nsects = sc->nsects;

            if (sc->nsects) {
                const struct chm_section_64 *sec;
                const struct chm_section_64 *base =
                    (const struct chm_section_64 *)(p + sizeof(*sc));

                s->sects = calloc(sc->nsects, sizeof(chm_section));
                if (!s->sects)
                    return -1;
                for (k = 0; k < sc->nsects; k++) {
                    sec = &base[k];
                    chm_name(s->sects[k].sect, sizeof(s->sects[k].sect), sec->sectname, 16);
                    chm_name(s->sects[k].seg, sizeof(s->sects[k].seg), sec->segname, 16);
                    s->sects[k].addr = sec->addr;
                    s->sects[k].size = sec->size;
                    s->sects[k].offset = sec->offset;
                    s->sects[k].flags = sec->flags;
                }
            }
            if (strcmp(s->name, "__PAGEZERO") != 0 && s->vmsize) {
                if (s->vmaddr < img->vm_min)
                    img->vm_min = s->vmaddr;
                if (s->vmaddr + s->vmsize > img->vm_max)
                    img->vm_max = s->vmaddr + s->vmsize;
            }
            break;
        }
        case CHM_LC_SYMTAB: {
            const struct chm_symtab_command *st = (const struct chm_symtab_command *)p;
            const struct chm_nlist_64 *nl;
            uint32_t k;

            if (sizeof(*st) > lc->cmdsize)
                return -1;
            if ((uint64_t)st->symoff + (uint64_t)st->nsyms * sizeof(struct chm_nlist_64) > img->filesize)
                return -1;
            if ((uint64_t)st->stroff + st->strsize > img->filesize)
                return -1;

            img->strtab = (const char *)img->file + st->stroff;
            img->strsize = st->strsize;
            if (st->nsyms) {
                img->syms = calloc(st->nsyms, sizeof(chm_symbol));
                if (!img->syms)
                    return -1;
                nl = (const struct chm_nlist_64 *)((const uint8_t *)img->file + st->symoff);
                for (k = 0; k < st->nsyms; k++) {
                    if (nl[k].n_strx < img->strsize)
                        snprintf(img->syms[k].name, CHM_MAX_NAME, "%s", img->strtab + nl[k].n_strx);
                    img->syms[k].type = nl[k].n_type;
                    img->syms[k].sect = nl[k].n_sect;
                    img->syms[k].desc = nl[k].n_desc;
                    img->syms[k].value = nl[k].n_value;
                }
            }
            img->nsyms = st->nsyms;
            break;
        }
        case CHM_LC_LOAD_DYLIB:
        case CHM_LC_LOAD_WEAK_DYLIB:
        case CHM_LC_REEXPORT_DYLIB:
        case CHM_LC_LAZY_LOAD_DYLIB:
        case CHM_LC_LOAD_UPWARD_DYLIB: {
            const struct chm_dylib_command *dc = (const struct chm_dylib_command *)p;

            if (img->ndylibs < CHM_MAX_DYLIBS && dc->name_offset < lc->cmdsize) {
                chm_dylib *d = &img->dylibs[img->ndylibs];

                snprintf(d->name, CHM_MAX_NAME, "%s", (const char *)p + dc->name_offset);
                d->ordinal = img->ndylibs + 1;
                img->ndylibs++;
            }
            break;
        }
        case CHM_LC_DYLD_INFO:
        case CHM_LC_DYLD_INFO_ONLY:
            img->dyld_info = (struct chm_dyld_info_command *)(uintptr_t)p;
            break;
        case CHM_LC_DYLD_CHAINED_FIXUPS:
            img->chained = (struct chm_linkedit_data_command *)(uintptr_t)p;
            break;
        case CHM_LC_MAIN: {
            const struct chm_entry_point_command *ep = (const struct chm_entry_point_command *)p;

            img->entryoff = ep->entryoff;
            img->has_entry = 1;
            break;
        }
        case CHM_LC_BUILD_VERSION: {
            const struct chm_build_version_command *bv = (const struct chm_build_version_command *)p;

            img->platform = bv->platform;
            break;
        }
        default:
            break;
        }
        p += lc->cmdsize;
    }
    return 0;
}

void chm_close(chm_image *img)
{
    uint32_t i;

    for (i = 0; i < img->nsegs; i++) {
        free(img->segs[i].sects);
        img->segs[i].sects = NULL;
    }
    free(img->syms);
    img->syms = NULL;
    if (img->file) {
        munmap(img->file, img->filesize);
        img->file = NULL;
    }
}

void *chm_sym(chm_image *img, const char *name)
{
    uint32_t i;

    for (i = 0; i < img->nsyms; i++) {
        if ((img->syms[i].type & 0x0e) != 0x0e)
            continue;
        if (strcmp(img->syms[i].name, name) == 0)
            return (void *)(uintptr_t)(img->syms[i].value + img->slide);
    }
    return NULL;
}

int chm_map(chm_image *img, uint64_t slide)
{
    uint32_t i;

    img->slide = slide;
    for (i = 0; i < img->nsegs; i++) {
        chm_segment *s = &img->segs[i];
        void *want;
        void *got;

        if (!s->vmsize)
            continue;
        if (strcmp(s->name, "__PAGEZERO") == 0)
            continue;
        if (s->filesize > s->vmsize)
            return -1;
        if (s->fileoff + s->filesize > img->filesize)
            return -1;

        want = (void *)(uintptr_t)(s->vmaddr + slide);
        got = mmap(want, s->vmsize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (got == MAP_FAILED)
            return -1;
        if (got != want) {
            munmap(got, s->vmsize);
            return -1;
        }
        if (s->filesize)
            memcpy(got, (const uint8_t *)img->file + s->fileoff, (size_t)s->filesize);
        s->mapped = got;
    }
    return 0;
}

int chm_map_default(chm_image *img)
{
    if (chm_map(img, 0) == 0)
        return 0;
    return chm_map(img, 0x40000000ull);
}

int chm_protect(chm_image *img)
{
    uint32_t i;

    for (i = 0; i < img->nsegs; i++) {
        chm_segment *s = &img->segs[i];
        int prot;

        if (!s->mapped)
            continue;
        prot = chm_prot(s->initprot);
        if (mprotect(s->mapped, s->vmsize, prot) != 0)
            return -1;
    }
    return 0;
}

typedef struct {
    uint32_t name_offset;
    uint32_t lib_ordinal;
    int64_t addend;
} chm_import_desc;

static int chm_read_imports(const struct chm_chained_fixups_header *fh,
                            chm_import_desc **out, const char **symbols)
{
    const uint8_t *base = (const uint8_t *)fh;
    chm_import_desc *arr;
    uint32_t i;

    if (fh->symbols_format != 0)
        return -1;
    arr = calloc(fh->imports_count ? fh->imports_count : 1, sizeof(*arr));
    if (!arr)
        return -1;

    for (i = 0; i < fh->imports_count; i++) {
        if (fh->imports_format == CHM_IMPORT_FORMAT) {
            const uint32_t *e = (const uint32_t *)(base + fh->imports_offset) + i;

            arr[i].lib_ordinal = e[0] & 0xff;
            arr[i].name_offset = (e[0] >> 9) & 0x7fffff;
            arr[i].addend = 0;
        } else if (fh->imports_format == CHM_IMPORT_FORMAT_ADDEND) {
            const uint32_t *e = (const uint32_t *)(base + fh->imports_offset) + i * 2;

            arr[i].lib_ordinal = e[0] & 0xff;
            arr[i].name_offset = (e[0] >> 9) & 0x7fffff;
            arr[i].addend = (int32_t)e[1];
        } else if (fh->imports_format == CHM_IMPORT_FORMAT_ADDEND64) {
            const uint64_t *e = (const uint64_t *)(base + fh->imports_offset) + i * 2;

            arr[i].lib_ordinal = (uint32_t)(e[0] & 0xffff);
            arr[i].name_offset = (uint32_t)(e[0] >> 32);
            arr[i].addend = (int64_t)e[1];
        } else {
            free(arr);
            return -1;
        }
    }
    *out = arr;
    *symbols = (const char *)base + fh->symbols_offset;
    return 0;
}

static uint64_t chm_bind_value(chm_image *img, uint32_t index, uint32_t count,
                               const chm_import_desc *imports, const char *symbols,
                               int64_t addend)
{
    const char *name;
    void *sym;

    if (index >= count) {
        img->nfixup_missing++;
        return 0;
    }
    name = symbols + imports[index].name_offset;
    sym = chm_resolve_import(name);
    if (!sym) {
        fprintf(stderr, "chimera: missing import %s\n", name);
        img->nfixup_missing++;
        return 0;
    }
    img->nfixup_bind++;
    return (uint64_t)(uintptr_t)sym + (uint64_t)imports[index].addend + (uint64_t)addend;
}

static int chm_apply_chained(chm_image *img)
{
    const struct chm_chained_fixups_header *fh;
    const struct chm_chained_starts_in_image *si;
    chm_import_desc *imports = NULL;
    const char *symbols = NULL;
    uint32_t s;

    if (!img->chained)
        return 0;
    if (img->chained->dataoff + img->chained->datasize > img->filesize)
        return -1;

    fh = (const struct chm_chained_fixups_header *)((const uint8_t *)img->file + img->chained->dataoff);
    if (chm_read_imports(fh, &imports, &symbols) != 0)
        return -1;
    si = (const struct chm_chained_starts_in_image *)((const uint8_t *)fh + fh->starts_offset);

    for (s = 0; s < si->seg_count && s < img->nsegs; s++) {
        const uint32_t *offsets = (const uint32_t *)((const uint8_t *)si + sizeof(uint32_t));
        uint32_t off = offsets[s];
        const struct chm_chained_starts_in_segment *ss;
        uint8_t *base;
        uint32_t pg;

        if (!off)
            continue;
        ss = (const struct chm_chained_starts_in_segment *)((const uint8_t *)si + off);
        base = (uint8_t *)(uintptr_t)(img->segs[s].vmaddr + img->slide);

        for (pg = 0; pg < ss->page_count; pg++) {
            const uint16_t *starts =
                (const uint16_t *)((const uint8_t *)ss + offsetof(struct chm_chained_starts_in_segment, page_start));
            uint64_t delta = starts[pg];

            if (delta == 0xffff)
                continue;
            for (;;) {
                uint8_t *loc = base + (uint64_t)pg * ss->page_size + delta;
                uint64_t raw;
                uint64_t next = 0;
                uint64_t val = 0;

                memcpy(&raw, loc, 8);
                if (ss->pointer_format == CHM_PTR_64 || ss->pointer_format == CHM_PTR_64_OFFSET) {
                    next = CHM_B64_NEXT(raw);
                    if (CHM_B64_BIND(raw)) {
                        val = chm_bind_value(img, (uint32_t)CHM_B64_ORDINAL(raw),
                                             fh->imports_count, imports, symbols,
                                             CHM_B64_ADDEND(raw));
                    } else {
                        uint64_t target = CHM_B64_TARGET(raw);

                        if (ss->pointer_format == CHM_PTR_64_OFFSET)
                            target += img->slide;
                        val = target | (CHM_B64_HIGH8(raw) << 56);
                    }
                } else if (ss->pointer_format == CHM_PTR_ARM64E ||
                           ss->pointer_format == CHM_PTR_ARM64E_USERLAND ||
                           ss->pointer_format == CHM_PTR_ARM64E_USERLAND24) {
                    next = CHM_A64E_NEXT(raw);
                    if (CHM_A64E_BIND(raw)) {
                        val = chm_bind_value(img, (uint32_t)CHM_A64E_ORDINAL(raw),
                                             fh->imports_count, imports, symbols,
                                             CHM_A64E_AUTH(raw) ? 0 : CHM_A64E_ADDEND(raw));
                    } else if (CHM_A64E_AUTH(raw)) {
                        val = CHM_A64E_AUTH_TARGET(raw) + img->slide;
                    } else {
                        val = (CHM_A64E_TARGET(raw) + img->slide) | (CHM_A64E_HIGH8(raw) << 56);
                    }
                } else {
                    free(imports);
                    return -1;
                }
                memcpy(loc, &val, 8);
                if (!next)
                    break;
                delta += (uint64_t)next * 4;
            }
        }
    }
    free(imports);
    return 0;
}

static int chm_apply_bind(chm_image *img, uint32_t off, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)img->file + off;
    const uint8_t *end = p + size;
    uint8_t *addr = NULL;
    int64_t addend = 0;
    const char *sym = NULL;

    if ((uint64_t)off + size > img->filesize)
        return -1;

    while (p < end) {
        uint8_t byte = *p++;
        uint8_t op = byte & 0xf0;
        uint8_t imm = byte & 0x0f;

        switch (op) {
        case 0x00:
            return 0;
        case 0x10:
        case 0x20:
        case 0x30:
            if (op == 0x20)
                chm_uleb(&p, end);
            break;
        case 0x40:
            sym = (const char *)p;
            p += strlen(sym) + 1;
            break;
        case 0x50:
            break;
        case 0x60:
            addend = chm_sleb(&p, end);
            break;
        case 0x70:
            if (imm >= img->nsegs)
                return -1;
            addr = (uint8_t *)(uintptr_t)(img->segs[imm].vmaddr + img->slide) + chm_uleb(&p, end);
            break;
        case 0x80:
            if (!addr)
                return -1;
            addr += chm_uleb(&p, end);
            break;
        case 0x90:
        case 0xa0:
        case 0xb0:
        case 0xc0: {
            void *target = sym ? chm_resolve_import(sym) : NULL;
            uint64_t count = 1;
            uint64_t skip = 8;
            uint64_t n;

            if (op == 0xa0) {
                if (!addr)
                    return -1;
                addr += chm_uleb(&p, end);
            } else if (op == 0xb0) {
                if (!addr)
                    return -1;
                addr += (uint64_t)imm * 8;
            } else if (op == 0xc0) {
                count = chm_uleb(&p, end);
                skip = chm_uleb(&p, end);
            }
            if (!target) {
                fprintf(stderr, "chimera: missing import %s\n", sym ? sym : "?");
                img->nfixup_missing++;
                break;
            }
            for (n = 0; n < count; n++) {
                uint64_t v = (uint64_t)(uintptr_t)target + (uint64_t)addend;

                memcpy(addr, &v, 8);
                img->nfixup_bind++;
                if (op == 0xc0 && n + 1 < count)
                    addr += skip;
            }
            addend = 0;
            break;
        }
        default:
            return -1;
        }
    }
    return 0;
}

int chm_fixup(chm_image *img)
{
    if (chm_map_default(img) != 0)
        return -1;
    if (chm_apply_chained(img) != 0)
        return -1;
    if (img->dyld_info && img->dyld_info->bind_size) {
        if (chm_apply_bind(img, img->dyld_info->bind_off, img->dyld_info->bind_size) != 0)
            return -1;
    }
    if (chm_protect(img) != 0)
        return -1;
    return 0;
}

void chm_dump(chm_image *img, FILE *out)
{
    uint32_t i;
    uint32_t k;

    fprintf(out, "magic       0x%08x\n", img->hdr->magic);
    fprintf(out, "filetype    %u\n", img->hdr->filetype);
    fprintf(out, "flags       0x%08x\n", img->hdr->flags);
    fprintf(out, "platform    %u\n", img->platform);
    fprintf(out, "segments    %u\n", img->nsegs);
    fprintf(out, "vm range    0x%llx - 0x%llx\n",
            (unsigned long long)img->vm_min, (unsigned long long)img->vm_max);
    if (img->has_entry)
        fprintf(out, "entry       LC_MAIN +0x%llx\n", (unsigned long long)img->entryoff);
    else
        fprintf(out, "entry       none\n");

    for (i = 0; i < img->nsegs; i++) {
        chm_segment *s = &img->segs[i];

        fprintf(out, "seg %-12s vm=0x%llx vsize=0x%llx off=0x%llx size=0x%llx prot=%u/%u sects=%u\n",
                s->name,
                (unsigned long long)s->vmaddr,
                (unsigned long long)s->vmsize,
                (unsigned long long)s->fileoff,
                (unsigned long long)s->filesize,
                s->initprot, s->maxprot, s->nsects);
        for (k = 0; k < s->nsects; k++)
            fprintf(out, "  sect %-12s %-12s addr=0x%llx size=0x%llx\n",
                    s->sects[k].sect, s->sects[k].seg,
                    (unsigned long long)s->sects[k].addr,
                    (unsigned long long)s->sects[k].size);
    }
    for (i = 0; i < img->ndylibs; i++)
        fprintf(out, "dylib %u     %s\n", img->dylibs[i].ordinal, img->dylibs[i].name);
    fprintf(out, "symbols     %u\n", img->nsyms);
    fprintf(out, "chained     %s\n", img->chained ? "yes" : "no");
    fprintf(out, "dyld_info   %s\n", img->dyld_info ? "yes" : "no");
}
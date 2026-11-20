#define _GNU_SOURCE
#include "chm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define CHM_MAX_IMAGE_SIZE (UINT64_C(1) << 30)
#define CHM_MAX_SYMBOLS 1048576u

_Static_assert(sizeof(struct chm_mach_header) == 32, "mach header size");
_Static_assert(sizeof(struct chm_segment_command) == 72, "segment size");
_Static_assert(sizeof(struct chm_section_64) == 80, "section size");
_Static_assert(sizeof(struct chm_nlist_64) == 16, "symbol size");

int chm_fail(chm_image *img, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(img->error, sizeof(img->error), fmt, ap);
    va_end(ap);
    return -1;
}

static bool file_range(const chm_image *img, uint64_t off, uint64_t size)
{
    return off <= img->filesize && size <= img->filesize - off;
}

static int bounded_name(chm_image *img, char *out, size_t cap, const void *data, size_t size)
{
    const char *end = memchr(data, 0, size);
    if (!end)
        return chm_fail(img, "unterminated name");
    size_t n = (size_t)(end - (const char *)data);
    if (n >= cap)
        return chm_fail(img, "name exceeds %zu bytes", cap - 1);
    memcpy(out, data, n + 1);
    return 0;
}

static void fixed_name(char out[17], const char in[16])
{
    memcpy(out, in, 16);
    out[16] = 0;
}

static int parse_segment(chm_image *img, const uint8_t *p, uint32_t size)
{
    struct chm_segment_command sc;
    if (size < sizeof(sc))
        return chm_fail(img, "short segment command");
    memcpy(&sc, p, sizeof(sc));
    if (sc.nsects > (size - sizeof(sc)) / sizeof(struct chm_section_64))
        return chm_fail(img, "section table exceeds command");
    if (img->nsegs == CHM_MAX_SEGS)
        return chm_fail(img, "too many segments");
    if (sc.vmsize > UINT64_MAX - sc.vmaddr || sc.filesize > sc.vmsize ||
        !file_range(img, sc.fileoff, sc.filesize))
        return chm_fail(img, "invalid segment range");
    if ((sc.initprot & ~7u) || (sc.maxprot & ~7u) || (sc.initprot & ~sc.maxprot))
        return chm_fail(img, "invalid segment permissions");
    if (strncmp((const char *)sc.segname, "__PAGEZERO", 16) != 0 && (sc.vmaddr & 0xfffull))
        return chm_fail(img, "segment address is not page aligned");
    chm_segment *s = &img->segs[img->nsegs++];
    fixed_name(s->name, sc.segname);
    s->vmaddr = sc.vmaddr;
    s->vmsize = sc.vmsize;
    s->fileoff = sc.fileoff;
    s->filesize = sc.filesize;
    s->initprot = sc.initprot;
    s->maxprot = sc.maxprot;
    s->flags = sc.flags;
    s->nsects = sc.nsects;
    if (sc.nsects) {
        s->sects = calloc(sc.nsects, sizeof(*s->sects));
        if (!s->sects)
            return chm_fail(img, "section allocation failed");
    }
    for (uint32_t i = 0; i < sc.nsects; ++i) {
        struct chm_section_64 sec;
        memcpy(&sec, p + sizeof(sc) + i * sizeof(sec), sizeof(sec));
        if (sec.addr < sc.vmaddr || sec.addr - sc.vmaddr > sc.vmsize ||
            sec.size > sc.vmsize - (sec.addr - sc.vmaddr))
            return chm_fail(img, "section outside segment");
        uint32_t type = sec.flags & 0xffu;
        bool zerofill = type == 1 || type == 12 || type == 18;
        if (!zerofill && sec.size &&
            (!file_range(img, sec.offset, sec.size) || sec.offset < sc.fileoff ||
             sec.offset - sc.fileoff > sc.filesize ||
             sec.size > sc.filesize - (sec.offset - sc.fileoff)))
            return chm_fail(img, "section outside file-backed range");
        chm_section *dst = &s->sects[i];
        fixed_name(dst->sect, sec.sectname);
        fixed_name(dst->seg, sec.segname);
        dst->addr = sec.addr;
        dst->size = sec.size;
        dst->offset = sec.offset;
        dst->flags = sec.flags;
    }
    if (!strcmp(s->name, "__PAGEZERO")) {
        if (sc.filesize || sc.initprot || sc.vmaddr)
            return chm_fail(img, "invalid PAGEZERO segment");
    } else if (sc.vmsize) {
        if (sc.vmaddr < img->vm_min)
            img->vm_min = sc.vmaddr;
        if (sc.vmaddr + sc.vmsize > img->vm_max)
            img->vm_max = sc.vmaddr + sc.vmsize;
    }
    return 0;
}

static int parse_symbols(chm_image *img, const uint8_t *p, uint32_t size)
{
    struct chm_symtab_command st;
    if (size < sizeof(st) || img->strtab)
        return chm_fail(img, "invalid or duplicate symbol table");
    memcpy(&st, p, sizeof(st));
    if (st.nsyms > CHM_MAX_SYMBOLS ||
        !file_range(img, st.symoff, (uint64_t)st.nsyms * sizeof(struct chm_nlist_64)) ||
        !file_range(img, st.stroff, st.strsize))
        return chm_fail(img, "invalid symbol table range");
    img->strtab = (const char *)img->file + st.stroff;
    img->strsize = st.strsize;
    img->nsyms = st.nsyms;
    if (st.nsyms) {
        img->syms = calloc(st.nsyms, sizeof(*img->syms));
        if (!img->syms)
            return chm_fail(img, "symbol allocation failed");
    }
    for (uint32_t i = 0; i < st.nsyms; ++i) {
        struct chm_nlist_64 nl;
        memcpy(&nl, (const uint8_t *)img->file + st.symoff + i * sizeof(nl), sizeof(nl));
        chm_symbol *s = &img->syms[i];
        if (nl.n_strx >= st.strsize)
            return chm_fail(img, "symbol string offset out of range");
        const char *name = img->strtab + nl.n_strx;
        const char *end = memchr(name, 0, st.strsize - nl.n_strx);
        if (!end)
            return chm_fail(img, "unterminated symbol");
        size_t n = (size_t)(end - name);
        if (n >= sizeof(s->name))
            return chm_fail(img, "symbol name too long");
        memcpy(s->name, name, n + 1);
        s->type = nl.n_type;
        s->sect = nl.n_sect;
        s->desc = nl.n_desc;
        s->value = nl.n_value;
    }
    return 0;
}

static int parse_commands(chm_image *img)
{
    const struct chm_mach_header *h = img->hdr;
    if (h->magic != CHM_MH_MAGIC_64 || h->cputype != CHM_CPU_ARM64)
        return chm_fail(img, "only thin little-endian arm64 Mach-O is supported");
    if ((h->cpusubtype & 0xffffffu) == 2)
        return chm_fail(img, "arm64e/PAC is not supported");
    if (h->filetype != CHM_MH_EXECUTE && h->filetype != CHM_MH_DYLIB && h->filetype != CHM_MH_BUNDLE)
        return chm_fail(img, "unsupported Mach-O file type %u", h->filetype);
    if (!file_range(img, sizeof(*h), h->sizeofcmds) || h->ncmds > h->sizeofcmds / 8)
        return chm_fail(img, "invalid load command table");
    size_t off = sizeof(*h);
    size_t end = off + h->sizeofcmds;
    for (uint32_t i = 0; i < h->ncmds; ++i) {
        struct chm_load_command lc;
        if (end - off < sizeof(lc))
            return chm_fail(img, "truncated load command");
        const uint8_t *p = (const uint8_t *)img->file + off;
        memcpy(&lc, p, sizeof(lc));
        if (lc.cmdsize < sizeof(lc) || lc.cmdsize > end - off || lc.cmdsize % 8)
            return chm_fail(img, "invalid load command size");
        switch (lc.cmd) {
        case CHM_LC_SEGMENT_64:
            if (parse_segment(img, p, lc.cmdsize))
                return -1;
            break;
        case CHM_LC_SYMTAB:
            if (parse_symbols(img, p, lc.cmdsize))
                return -1;
            break;
        case CHM_LC_LOAD_DYLIB:
        case CHM_LC_LOAD_WEAK_DYLIB:
        case CHM_LC_REEXPORT_DYLIB:
        case CHM_LC_LAZY_LOAD_DYLIB:
        case CHM_LC_LOAD_UPWARD_DYLIB: {
            struct chm_dylib_command dc;
            if (lc.cmdsize < sizeof(dc) || img->ndylibs == CHM_MAX_DYLIBS)
                return chm_fail(img, "invalid or excessive dylib command");
            memcpy(&dc, p, sizeof(dc));
            if (dc.name_offset < sizeof(dc) || dc.name_offset >= lc.cmdsize)
                return chm_fail(img, "invalid dylib name offset");
            chm_dylib *d = &img->dylibs[img->ndylibs];
            if (bounded_name(img, d->name, sizeof(d->name), p + dc.name_offset, lc.cmdsize - dc.name_offset))
                return -1;
            d->ordinal = ++img->ndylibs;
            break;
        }
        case CHM_LC_DYLD_INFO:
        case CHM_LC_DYLD_INFO_ONLY: {
            struct chm_dyld_info_command di;
            if (lc.cmdsize < sizeof(di) || img->dyld_info)
                return chm_fail(img, "invalid or duplicate dyld info");
            memcpy(&di, p, sizeof(di));
            if (!file_range(img, di.rebase_off, di.rebase_size) ||
                !file_range(img, di.bind_off, di.bind_size) ||
                !file_range(img, di.weak_bind_off, di.weak_bind_size) ||
                !file_range(img, di.lazy_bind_off, di.lazy_bind_size) ||
                !file_range(img, di.export_off, di.export_size))
                return chm_fail(img, "dyld stream outside file");
            img->dyld_info = (struct chm_dyld_info_command *)(uintptr_t)p;
            break;
        }
        case CHM_LC_DYLD_CHAINED_FIXUPS: {
            struct chm_linkedit_data_command dc;
            if (lc.cmdsize < sizeof(dc) || img->chained)
                return chm_fail(img, "invalid or duplicate chained fixups");
            memcpy(&dc, p, sizeof(dc));
            if (!file_range(img, dc.dataoff, dc.datasize))
                return chm_fail(img, "chained fixups outside file");
            img->chained = (struct chm_linkedit_data_command *)(uintptr_t)p;
            break;
        }
        case CHM_LC_MAIN: {
            struct chm_entry_point_command ep;
            if (lc.cmdsize < sizeof(ep) || img->has_entry)
                return chm_fail(img, "invalid or duplicate entry");
            memcpy(&ep, p, sizeof(ep));
            img->entryoff = ep.entryoff;
            img->has_entry = 1;
            break;
        }
        case CHM_LC_BUILD_VERSION: {
            struct chm_build_version_command bv;
            if (lc.cmdsize < sizeof(bv))
                return chm_fail(img, "short build version");
            memcpy(&bv, p, sizeof(bv));
            if (bv.ntools > (lc.cmdsize - sizeof(bv)) / 8)
                return chm_fail(img, "build tools outside command");
            img->platform = bv.platform;
            break;
        }
        case 0x21:
        case 0x2c: {
            uint32_t v[6] = {0};
            size_t n = lc.cmd == 0x2c ? 24 : 20;
            if (lc.cmdsize < n)
                return chm_fail(img, "short encryption command");
            memcpy(v, p, n);
            if (!file_range(img, v[2], v[3]))
                return chm_fail(img, "encrypted range outside file");
            img->cryptid |= v[4];
            break;
        }
        default:
            break;
        }
        off += lc.cmdsize;
    }
    if (off != end || img->vm_min == UINT64_MAX || img->vm_max <= img->vm_min ||
        img->vm_max - img->vm_min > CHM_MAX_IMAGE_SIZE)
        return chm_fail(img, "invalid command or virtual address span");
    for (uint32_t i = 0; i < img->nsegs; ++i) {
        const chm_segment *a = &img->segs[i];
        if (!a->vmsize || !strcmp(a->name, "__PAGEZERO"))
            continue;
        for (uint32_t j = i + 1; j < img->nsegs; ++j) {
            const chm_segment *b = &img->segs[j];
            if (!b->vmsize || !strcmp(b->name, "__PAGEZERO"))
                continue;
            if (a->vmaddr < b->vmaddr + b->vmsize && b->vmaddr < a->vmaddr + a->vmsize)
                return chm_fail(img, "overlapping segments");
        }
    }
    return 0;
}

int chm_open(chm_image *img, const char *path)
{
    if (!img)
        return -1;
    memset(img, 0, sizeof(*img));
    img->vm_min = UINT64_MAX;
    if (!path)
        return chm_fail(img, "missing path");
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return chm_fail(img, "open: %s", strerror(errno));
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 32 || (uint64_t)st.st_size > CHM_MAX_IMAGE_SIZE) {
        close(fd);
        return chm_fail(img, "invalid file size or type");
    }
    img->filesize = (size_t)st.st_size;
    img->file = mmap(NULL, img->filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (img->file == MAP_FAILED) {
        img->file = NULL;
        return chm_fail(img, "file mapping failed");
    }
    img->hdr = img->file;
    if (parse_commands(img)) {
        chm_close(img);
        return -1;
    }
    return 0;
}

void chm_unmap(chm_image *img)
{
    if (img->reservation)
        munmap(img->reservation, img->reservation_size);
    img->reservation = NULL;
    img->reservation_size = 0;
    img->fixed = false;
    img->executable = false;
    for (uint32_t i = 0; i < img->nsegs; ++i)
        img->segs[i].mapped = NULL;
}

void chm_close(chm_image *img)
{
    if (!img)
        return;
    chm_unmap(img);
    for (uint32_t i = 0; i < img->nsegs; ++i) {
        free(img->segs[i].sects);
        img->segs[i].sects = NULL;
    }
    free(img->syms);
    img->syms = NULL;
    if (img->file)
        munmap(img->file, img->filesize);
    img->file = NULL;
    img->hdr = NULL;
    img->strtab = NULL;
    img->dyld_info = NULL;
    img->chained = NULL;
    img->nsegs = img->nsyms = 0;
}

int chm_map(chm_image *img, uint64_t slide)
{
    if (img->reservation)
        return slide == img->slide ? 0 : chm_fail(img, "cannot change mapped image slide");
    long native_page = sysconf(_SC_PAGESIZE);
    if (native_page <= 0)
        return chm_fail(img, "invalid host page size");
    uint64_t page = (uint64_t)native_page;
    uint64_t span = img->vm_max - img->vm_min;
    if (!img->file || img->vm_min == UINT64_MAX || img->vm_max <= img->vm_min ||
        span > CHM_MAX_IMAGE_SIZE || img->vm_min % page || slide % page ||
        img->vm_max > UINT64_MAX - slide - (page - 1))
        return chm_fail(img, "invalid mapping range or alignment");
    size_t size = (size_t)((span + page - 1) & ~(page - 1));
    uint64_t address = img->vm_min + slide;
    if (!address || address > UINTPTR_MAX - size)
        return chm_fail(img, "invalid mapping address");
    void *want = (void *)(uintptr_t)address;
    void *area = mmap(want, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (area == MAP_FAILED)
        return chm_fail(img, "reserve image: %s", strerror(errno));
    if (area != want) {
        munmap(area, size);
        return chm_fail(img, "kernel did not honor no-replace mapping");
    }
    img->reservation = area;
    img->reservation_size = size;
    img->slide = slide;
    for (uint32_t i = 0; i < img->nsegs; ++i) {
        chm_segment *s = &img->segs[i];
        if (!s->vmsize || !strcmp(s->name, "__PAGEZERO"))
            continue;
        if (s->vmaddr % page) {
            chm_fail(img, "segment %s is not host-page aligned", s->name);
            goto failed;
        }
        uint64_t rounded = (s->vmsize + page - 1) & ~(page - 1);
        for (uint32_t j = 0; j < img->nsegs; ++j) {
            const chm_segment *b = &img->segs[j];
            if (j != i && b->vmsize && b->vmaddr > s->vmaddr && b->vmaddr < s->vmaddr + rounded) {
                chm_fail(img, "segments share a host page");
                goto failed;
            }
        }
        void *target = (uint8_t *)area + (size_t)(s->vmaddr - img->vm_min);
        if (mprotect(target, (size_t)rounded, PROT_READ | PROT_WRITE)) {
            chm_fail(img, "enable segment writes: %s", strerror(errno));
            goto failed;
        }
        if (s->filesize)
            memcpy(target, (const uint8_t *)img->file + s->fileoff, (size_t)s->filesize);
        s->mapped = target;
    }
    img->error[0] = 0;
    return 0;
failed:
    chm_unmap(img);
    return -1;
}

int chm_map_default(chm_image *img)
{
    if (!chm_map(img, 0))
        return 0;
    return chm_map(img, UINT64_C(0x40000000));
}

int chm_bind_image(chm_image *img)
{
    if (!img->reservation)
        return chm_fail(img, "image is not mapped");
    if (img->fixed)
        return chm_fail(img, "fixups already applied");
    img->nfixup_bind = img->nfixup_missing = img->nfixup_rebase = img->nfixup_weak = 0;
    if (img->cryptid || chm_apply_fixups(img)) {
        if (img->cryptid)
            chm_fail(img, "encrypted image cannot be bound");
        chm_unmap(img);
        return -1;
    }
    img->fixed = true;
    return 0;
}

int chm_protect(chm_image *img)
{
    if (!img->reservation || !img->fixed)
        return chm_fail(img, "cannot protect an unbound image");
    for (uint32_t i = 0; i < img->nsegs; ++i) {
        chm_segment *s = &img->segs[i];
        if (!s->mapped)
            continue;
        uint32_t p = s->initprot;
        if ((p & 6u) == 6u) {
            chm_fail(img, "writable executable segment rejected");
            chm_unmap(img);
            return -1;
        }
        if (p & 4)
            __builtin___clear_cache((char *)s->mapped, (char *)s->mapped + s->vmsize);
        int prot = (p & 1 ? PROT_READ : 0) | (p & 2 ? PROT_WRITE : 0);
        if ((p & 4) && !img->nfixup_missing)
            prot |= PROT_EXEC;
        if (mprotect(s->mapped, (size_t)s->vmsize, prot)) {
            chm_fail(img, "protect segment: %s", strerror(errno));
            chm_unmap(img);
            return -1;
        }
    }
    img->executable = img->nfixup_missing == 0;
    return 0;
}

int chm_fixup_at(chm_image *img, uint64_t slide)
{
    if (chm_map(img, slide) || chm_bind_image(img) || chm_protect(img))
        return -1;
    return 0;
}

int chm_fixup(chm_image *img)
{
    if (chm_map_default(img) || chm_bind_image(img) || chm_protect(img))
        return -1;
    return 0;
}

void *chm_sym(chm_image *img, const char *name)
{
    if (!name || !img->reservation)
        return NULL;
    for (uint32_t i = 0; i < img->nsyms; ++i) {
        chm_symbol *s = &img->syms[i];
        if ((s->type & 0xe0) || strcmp(s->name, name))
            continue;
        if ((s->type & 0x0e) == 2)
            return (void *)(uintptr_t)s->value;
        if ((s->type & 0x0e) == 0x0e && s->value >= img->vm_min && s->value < img->vm_max)
            return (void *)(uintptr_t)(s->value + img->slide);
    }
    return NULL;
}

void chm_dump(chm_image *img, FILE *out)
{
    if (!img->hdr)
        return;
    fprintf(out, "platform %u, segments %u, dylibs %u, symbols %u\n", img->platform, img->nsegs, img->ndylibs, img->nsyms);
    fprintf(out, "entry 0x%llx, slide 0x%llx, encrypted %u\n", (unsigned long long)img->entryoff, (unsigned long long)img->slide, img->cryptid);
    for (uint32_t i = 0; i < img->nsegs; ++i) {
        const chm_segment *s = &img->segs[i];
        fprintf(out, "segment %u %s vm=0x%llx size=0x%llx prot=%u\n", i, s->name, (unsigned long long)s->vmaddr, (unsigned long long)s->vmsize, s->initprot);
        for (uint32_t j = 0; j < s->nsects; ++j)
            fprintf(out, "  %s addr=0x%llx size=0x%llx\n", s->sects[j].sect, (unsigned long long)s->sects[j].addr, (unsigned long long)s->sects[j].size);
    }
    for (uint32_t i = 0; i < img->ndylibs; ++i)
        fprintf(out, "dylib %u %s\n", i + 1, img->dylibs[i].name);
    fprintf(out, "bind=%u missing=%u weak-null=%u rebase=%u executable=%d\n", img->nfixup_bind, img->nfixup_missing, img->nfixup_weak, img->nfixup_rebase, img->executable);
}

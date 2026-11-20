#include "chm.h"

#include <limits.h>
#include <string.h>

#define CHM_FIXUP_LIMIT 4194304u

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    const uint8_t *begin;
    chm_image *img;
} chm_cursor;

typedef struct {
    uint32_t segment;
    uint64_t offset;
    int32_t ordinal;
    const char *symbol;
    int64_t addend;
    uint8_t type;
    bool have_segment;
    bool have_ordinal;
    bool weak;
} chm_bind_state;

static int stream(chm_image *img, uint32_t off, uint32_t size, chm_cursor *c)
{
    if (off > img->filesize || size > img->filesize - off)
        return chm_fail(img, "fixup stream outside file");
    c->p = (const uint8_t *)img->file + off;
    c->end = c->p + size;
    c->begin = c->p;
    c->img = img;
    return 0;
}

static int uleb(chm_cursor *c, uint64_t *value)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 10; ++i) {
        if (c->p == c->end)
            return chm_fail(c->img, "truncated ULEB128");
        uint8_t b = *c->p++;
        if (i == 9 && (b & 0xfe))
            return chm_fail(c->img, "ULEB128 overflow");
        v |= (uint64_t)(b & 0x7f) << (i * 7);
        if (!(b & 0x80)) {
            *value = v;
            return 0;
        }
    }
    return chm_fail(c->img, "ULEB128 too long");
}

static int sleb(chm_cursor *c, int64_t *value)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 10; ++i) {
        if (c->p == c->end)
            return chm_fail(c->img, "truncated SLEB128");
        uint8_t b = *c->p++;
        uint8_t payload = b & 0x7f;
        if (i == 9 && ((b & 0x80) || (payload != 0 && payload != 0x7f)))
            return chm_fail(c->img, "SLEB128 overflow");
        v |= (uint64_t)payload << (i * 7);
        if (!(b & 0x80)) {
            unsigned bits = (i + 1) * 7;
            if ((b & 0x40) && bits < 64)
                v |= UINT64_MAX << bits;
            memcpy(value, &v, sizeof(v));
            return 0;
        }
    }
    return chm_fail(c->img, "SLEB128 too long");
}

static int trailing_string(chm_cursor *c, const char **value)
{
    const uint8_t *end = memchr(c->p, 0, (size_t)(c->end - c->p));
    if (!end)
        return chm_fail(c->img, "unterminated bind symbol");
    if (end == c->p || (size_t)(end - c->p) >= CHM_MAX_NAME)
        return chm_fail(c->img, "invalid bind symbol length");
    *value = (const char *)c->p;
    c->p = end + 1;
    return 0;
}

static int advance(chm_image *img, uint32_t segment, uint64_t *offset, uint64_t amount)
{
    if (segment >= img->nsegs)
        return chm_fail(img, "invalid fixup segment %u", segment);
    uint64_t size = img->segs[segment].vmsize;
    uint64_t next = *offset + amount;
    if (next > size)
        return chm_fail(img, "fixup offset 0x%llx step 0x%llx leaves segment %u (size 0x%llx)",
                        (unsigned long long)*offset, (unsigned long long)amount, segment,
                        (unsigned long long)size);
    *offset = next;
    return 0;
}

static void *slot(chm_image *img, uint32_t segment, uint64_t offset)
{
    if (segment >= img->nsegs) {
        chm_fail(img, "invalid fixup segment %u", segment);
        return NULL;
    }
    chm_segment *s = &img->segs[segment];
    if (!s->mapped || !(s->initprot & 2) || (s->initprot & 4) ||
        offset > s->vmsize || s->vmsize - offset < 8) {
        chm_fail(img, "invalid pointer fixup in %s+0x%llx", s->name, (unsigned long long)offset);
        return NULL;
    }
    return (uint8_t *)s->mapped + offset;
}

static int add_signed(chm_image *img, uint64_t base, int64_t addend, uint64_t *out)
{
    if (addend >= 0) {
        if ((uint64_t)addend > UINT64_MAX - base)
            return chm_fail(img, "bind addend overflow");
        *out = base + (uint64_t)addend;
    } else {
        uint64_t magnitude = (uint64_t)(-(addend + 1)) + 1;
        if (base < magnitude)
            return chm_fail(img, "bind addend underflow");
        *out = base - magnitude;
    }
    return 0;
}

static void reset_state(chm_bind_state *s, int mode)
{
    memset(s, 0, sizeof(*s));
    s->type = 1;
    if (mode == 2) {
        s->ordinal = -3;
        s->have_ordinal = true;
    }
}

static int bind_one(chm_image *img, chm_bind_state *s, int mode)
{
    if (!s->have_segment || !s->have_ordinal || !s->symbol || s->type != 1)
        return chm_fail(img, "incomplete bind state or unsupported type");
    if (s->ordinal < -3 || (s->ordinal > 0 && (uint32_t)s->ordinal > img->ndylibs))
        return chm_fail(img, "invalid library ordinal %d", s->ordinal);
    if (img->nfixup_bind >= CHM_FIXUP_LIMIT)
        return chm_fail(img, "binding work limit exceeded");
    void *dst = slot(img, s->segment, s->offset);
    if (!dst)
        return -1;
    uint64_t value = 0;
    int found;
    if (img->resolver) {
        found = img->resolver(img, s->ordinal, s->symbol, s->weak, &value, img->resolver_context);
    } else if (s->ordinal == 0) {
        void *ptr = chm_sym(img, s->symbol);
        value = (uint64_t)(uintptr_t)ptr;
        found = ptr != NULL;
    } else {
        return chm_fail(img, "no symbol resolver configured");
    }
    if (found < 0)
        return chm_fail(img, "resolver failed for %s", s->symbol);
    if (found) {
        if (add_signed(img, value, s->addend, &value))
            return -1;
        memcpy(dst, &value, 8);
    } else if (mode == 2) {
        memcpy(&value, dst, 8);
    } else {
        memcpy(dst, &value, 8);
        if (s->weak)
            img->nfixup_weak++;
        else
            img->nfixup_missing++;
    }
    img->nfixup_bind++;
    if (img->observer)
        img->observer(img, s->segment, s->offset, s->ordinal, s->symbol, value,
                      found != 0, s->weak, img->observer_context);
    return 0;
}

static int bind_stream(chm_image *img, uint32_t off, uint32_t size, int mode)
{
    if (!size)
        return 0;
    chm_cursor c = {0};
    if (stream(img, off, size, &c))
        return -1;
    chm_bind_state s;
    reset_state(&s, mode);
    while (c.p < c.end) {
        uint8_t byte = *c.p++;
        uint8_t op = byte & 0xf0;
        uint8_t imm = byte & 15;
        uint64_t v = 0;
        if (op == 0) {
            if (imm)
                return chm_fail(img, "invalid bind DONE");
            reset_state(&s, mode);
            continue;
        }
        switch (op) {
        case 0x10:
            s.ordinal = imm;
            s.have_ordinal = true;
            break;
        case 0x20:
            if (uleb(&c, &v))
                return -1;
            if (v > INT32_MAX)
                return chm_fail(img, "library ordinal overflow");
            s.ordinal = (int32_t)v;
            s.have_ordinal = true;
            break;
        case 0x30:
            s.ordinal = imm ? (int32_t)imm - 16 : 0;
            s.have_ordinal = true;
            break;
        case 0x40:
            if (imm & ~9u)
                return chm_fail(img, "unsupported bind symbol flags");
            s.weak = (imm & 1) != 0;
            if (trailing_string(&c, &s.symbol))
                return -1;
            break;
        case 0x50:
            if (imm != 1)
                return chm_fail(img, "non-pointer bind type %u unsupported", imm);
            s.type = imm;
            break;
        case 0x60:
            if (sleb(&c, &s.addend))
                return -1;
            break;
        case 0x70:
            if (uleb(&c, &v))
                return -1;
            if (imm >= img->nsegs || v > img->segs[imm].vmsize)
                return chm_fail(img, "bind segment offset out of range");
            s.segment = imm;
            s.offset = v;
            s.have_segment = true;
            break;
        case 0x80:
            if (!s.have_segment)
                return chm_fail(img, "address advance without segment");
            if (uleb(&c, &v))
                return -1;
            if (advance(img, s.segment, &s.offset, v))
                return -1;
            break;
        case 0x90:
        case 0xa0:
        case 0xb0:
        case 0xc0: {
            uint64_t count = 1;
            uint64_t uskip = 0;
            if (op == 0xa0 && uleb(&c, &uskip))
                return -1;
            if (op == 0xb0)
                uskip = (uint64_t)imm * 8;
            if (op == 0xc0 && (uleb(&c, &count) || uleb(&c, &uskip)))
                return -1;
            if (count > CHM_FIXUP_LIMIT)
                return chm_fail(img, "bind repeat overflow");
            for (uint64_t i = 0; i < count; ++i) {
                if (bind_one(img, &s, mode) || advance(img, s.segment, &s.offset, uskip + 8))
                    return -1;
            }
            break;
        }
        default:
            return chm_fail(img, "unsupported bind opcode 0x%02x", byte);
        }
    }
    return 0;
}

static int rebase_one(chm_image *img, uint32_t segment, uint64_t offset, uint8_t type)
{
    if (type != 1)
        return chm_fail(img, "non-pointer rebase unsupported");
    if (img->nfixup_rebase >= CHM_FIXUP_LIMIT)
        return chm_fail(img, "rebase work limit exceeded");
    void *dst = slot(img, segment, offset);
    if (!dst)
        return -1;
    uint64_t value;
    memcpy(&value, dst, 8);
    if (value > UINT64_MAX - img->slide)
        return chm_fail(img, "rebase overflow");
    value += img->slide;
    memcpy(dst, &value, 8);
    img->nfixup_rebase++;
    return 0;
}

static int rebase_stream(chm_image *img, uint32_t off, uint32_t size)
{
    if (!size)
        return 0;
    chm_cursor c = {0};
    if (stream(img, off, size, &c))
        return -1;
    uint32_t segment = UINT32_MAX;
    uint64_t offset = 0;
    uint8_t type = 0;
    while (c.p < c.end) {
        uint8_t byte = *c.p++;
        uint8_t op = byte & 0xf0;
        uint8_t imm = byte & 15;
        uint64_t count = 0;
        uint64_t skip = 0;
        uint64_t v = 0;
        switch (op) {
        case 0x00:
            if (imm)
                return chm_fail(img, "invalid rebase DONE");
            while (c.p < c.end)
                if (*c.p++)
                    return chm_fail(img, "nonzero data after rebase DONE");
            return 0;
        case 0x10:
            if (imm != 1)
                return chm_fail(img, "non-pointer rebase type %u unsupported", imm);
            type = imm;
            continue;
        case 0x20:
            if (uleb(&c, &v))
                return -1;
            if (imm >= img->nsegs || v > img->segs[imm].vmsize)
                return chm_fail(img, "invalid rebase segment offset");
            segment = imm;
            offset = v;
            continue;
        case 0x30:
            if (uleb(&c, &v) || advance(img, segment, &offset, v))
                return -1;
            continue;
        case 0x40:
            if (advance(img, segment, &offset, (uint64_t)imm * 8))
                return -1;
            continue;
        case 0x50:
            count = imm;
            break;
        case 0x60:
            if (uleb(&c, &count))
                return -1;
            break;
        case 0x70:
            count = 1;
            if (uleb(&c, &skip))
                return -1;
            break;
        case 0x80:
            if (uleb(&c, &count) || uleb(&c, &skip))
                return -1;
            break;
        default:
            return chm_fail(img, "unsupported rebase opcode 0x%02x", byte);
        }
        if (count > CHM_FIXUP_LIMIT || skip > UINT64_MAX - 8)
            return chm_fail(img, "rebase repeat or skip overflow");
        for (uint64_t i = 0; i < count; ++i)
            if (rebase_one(img, segment, offset, type) || advance(img, segment, &offset, skip + 8))
                return -1;
    }
    return chm_fail(img, "rebase stream missing DONE");
}

#define CHM_MAX_CHAINED_IMPORTS 131072u
#define CHM_MAX_CHAINED_STARTS 64u

static int chained_import_read(chm_image *img, const struct chm_chained_fixups_header *fh,
                               const uint8_t *base, size_t len, uint32_t idx,
                               int32_t *ordinal, const char **name, int64_t *addend, bool *weak)
{
    uint32_t name_off = 0;
    *weak = false;
    *addend = 0;
    if (fh->imports_format == CHM_IMPORT_FORMAT) {
        if ((uint64_t)idx * 4 + 4 > len - fh->imports_offset)
            return chm_fail(img, "chained import out of range");
        uint32_t w;
        memcpy(&w, base + fh->imports_offset + idx * 4, 4);
        uint8_t ord = (uint8_t)(w & 0xffu);
        *ordinal = ord > 240 ? (int32_t)(int8_t)ord : (int32_t)ord;
        *weak = (w & 0x100u) != 0;
        name_off = w >> 9;
    } else if (fh->imports_format == CHM_IMPORT_FORMAT_ADDEND) {
        if ((uint64_t)idx * 8 + 8 > len - fh->imports_offset)
            return chm_fail(img, "chained addend import out of range");
        uint32_t w[2];
        memcpy(w, base + fh->imports_offset + idx * 8, 8);
        uint8_t ord = (uint8_t)(w[0] & 0xffu);
        *ordinal = ord > 240 ? (int32_t)(int8_t)ord : (int32_t)ord;
        *weak = (w[0] & 0x100u) != 0;
        name_off = w[0] >> 9;
        *addend = (int32_t)w[1];
    } else if (fh->imports_format == CHM_IMPORT_FORMAT_ADDEND64) {
        if ((uint64_t)idx * 16 + 16 > len - fh->imports_offset)
            return chm_fail(img, "chained addend64 import out of range");
        uint64_t w[2];
        memcpy(w, base + fh->imports_offset + idx * 16, 16);
        uint16_t ord = (uint16_t)(w[0] & 0xffffu);
        *ordinal = ord > 65500u ? (int32_t)(int16_t)ord : (int32_t)ord;
        *weak = (w[0] & 0x10000u) != 0;
        name_off = (uint32_t)(w[0] >> 32);
        memcpy(addend, &w[1], 8);
    } else {
        return chm_fail(img, "unsupported chained imports format %u", fh->imports_format);
    }
    if (fh->symbols_offset >= len || name_off >= len - fh->symbols_offset)
        return chm_fail(img, "chained symbol offset out of range");
    const char *s = (const char *)base + fh->symbols_offset + name_off;
    const char *end = memchr(s, 0, len - fh->symbols_offset - name_off);
    if (!end || (size_t)(end - s) >= CHM_MAX_NAME)
        return chm_fail(img, "invalid chained symbol name");
    *name = s;
    return 0;
}

static int apply_chained_fixups(chm_image *img)
{
    if (!img->chained || !img->chained->datasize)
        return 0;
    uint32_t off = img->chained->dataoff;
    uint32_t len = img->chained->datasize;
    if (off > img->filesize || len > img->filesize - off ||
        len < sizeof(struct chm_chained_fixups_header))
        return chm_fail(img, "invalid chained fixups table");
    const uint8_t *base = (const uint8_t *)img->file + off;
    struct chm_chained_fixups_header fh;
    memcpy(&fh, base, sizeof(fh));
    if (fh.fixups_version != 0 || fh.symbols_format != 0 ||
        fh.starts_offset >= len || fh.imports_offset > len ||
        fh.symbols_offset > len || fh.imports_count > CHM_MAX_CHAINED_IMPORTS)
        return chm_fail(img, "unsupported chained fixups header");
    if (len - fh.starts_offset < sizeof(uint32_t))
        return chm_fail(img, "truncated chained starts");
    uint32_t seg_count = 0;
    memcpy(&seg_count, base + fh.starts_offset, 4);
    if (seg_count > CHM_MAX_CHAINED_STARTS || seg_count > img->nsegs ||
        (uint64_t)seg_count * 4 > len - fh.starts_offset - 4)
        return chm_fail(img, "invalid chained segment count");
    for (uint32_t seg = 0; seg < seg_count; ++seg) {
        uint32_t seg_off = 0;
        memcpy(&seg_off, base + fh.starts_offset + 4 + seg * 4, 4);
        if (!seg_off)
            continue;
        if (seg_off > len - fh.starts_offset ||
            len - fh.starts_offset - seg_off < sizeof(struct chm_chained_starts_in_segment))
            return chm_fail(img, "chained segment info out of range");
        const uint8_t *sp = base + fh.starts_offset + seg_off;
        struct chm_chained_starts_in_segment ss;
        memcpy(&ss, sp, sizeof(ss));
        if (ss.page_size == 0 || (ss.page_size & (ss.page_size - 1)) ||
            (uint64_t)ss.page_count * 2 > len - fh.starts_offset - seg_off - offsetof(struct chm_chained_starts_in_segment, page_start))
            return chm_fail(img, "invalid chained page table");
        if (ss.pointer_format != CHM_PTR_64 && ss.pointer_format != CHM_PTR_64_OFFSET)
            return chm_fail(img, "unsupported chained pointer format %u", ss.pointer_format);
        for (uint16_t pg = 0; pg < ss.page_count; ++pg) {
            uint16_t start = 0;
            memcpy(&start, sp + offsetof(struct chm_chained_starts_in_segment, page_start) + pg * 2, 2);
            if (start == 0xffffu)
                continue;
            uint64_t cursor = (uint64_t)pg * ss.page_size + start;
            for (uint32_t steps = 0; steps < CHM_FIXUP_LIMIT; ++steps) {
                void *dst = slot(img, seg, cursor);
                if (!dst)
                    return -1;
                uint64_t raw = 0;
                memcpy(&raw, dst, 8);
                uint64_t next = CHM_B64_NEXT(raw);
                if (CHM_B64_BIND(raw)) {
                    uint32_t idx = (uint32_t)CHM_B64_ORDINAL(raw);
                    if (idx >= fh.imports_count)
                        return chm_fail(img, "chained import index out of range");
                    int32_t ord = 0;
                    const char *name = NULL;
                    int64_t addend = 0;
                    bool weak = false;
                    if (chained_import_read(img, &fh, base, len, idx, &ord, &name, &addend, &weak))
                        return -1;
                    int8_t imm_add = (int8_t)((raw >> 24) & 0xffu);
                    if (imm_add && !addend)
                        addend = imm_add;
                    chm_bind_state bs;
                    reset_state(&bs, 0);
                    bs.segment = seg;
                    bs.offset = cursor;
                    bs.ordinal = ord;
                    bs.symbol = name;
                    bs.addend = addend;
                    bs.weak = weak;
                    bs.have_segment = true;
                    bs.have_ordinal = true;
                    if (bind_one(img, &bs, 0))
                        return -1;
                } else {
                    uint64_t target = CHM_B64_TARGET(raw);
                    uint64_t val = ss.pointer_format == CHM_PTR_64_OFFSET ? img->vm_min + img->slide + target : target + img->slide;
                    val |= CHM_B64_HIGH8(raw) << 56;
                    memcpy(dst, &val, 8);
                    img->nfixup_rebase++;
                }
                if (!next)
                    break;
                if (cursor > UINT64_MAX - next * 4)
                    return chm_fail(img, "chained delta overflow");
                cursor += next * 4;
            }
        }
    }
    return 0;
}

int chm_apply_fixups(chm_image *img)
{
    if (apply_chained_fixups(img))
        return -1;
    if (!img->dyld_info)
        return 0;
    const struct chm_dyld_info_command *di = img->dyld_info;
    if (rebase_stream(img, di->rebase_off, di->rebase_size) ||
        bind_stream(img, di->bind_off, di->bind_size, 0) ||
        bind_stream(img, di->weak_bind_off, di->weak_bind_size, 2) ||
        bind_stream(img, di->lazy_bind_off, di->lazy_bind_size, 1))
        return -1;
    return 0;
}
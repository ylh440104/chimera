#include "chm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char name[64];
    uint64_t value;
    bool present;
} test_sym;

typedef struct {
    uint32_t seg;
    uint64_t off;
    int32_t ordinal;
    char name[64];
    uint64_t value;
    bool found;
    bool weak;
} test_obs;

typedef struct {
    test_sym syms[16];
    int nsyms;
    test_obs obs[32];
    int nobs;
} test_ctx;

static int test_resolve(const chm_image *img, int32_t ordinal, const char *name, bool weak, uint64_t *out, void *ctx)
{
    (void)img;
    (void)ordinal;
    (void)weak;
    test_ctx *t = ctx;
    for (int i = 0; i < t->nsyms; ++i) {
        if (!strcmp(t->syms[i].name, name) && t->syms[i].present) {
            *out = t->syms[i].value;
            return 1;
        }
    }
    return 0;
}

static void test_observe(const chm_image *img, uint32_t seg, uint64_t off, int32_t ordinal,
                         const char *name, uint64_t value, bool found, bool weak, void *ctx)
{
    (void)img;
    test_ctx *t = ctx;
    if (t->nobs >= 32)
        return;
    test_obs *o = &t->obs[t->nobs++];
    o->seg = seg;
    o->off = off;
    o->ordinal = ordinal;
    snprintf(o->name, sizeof(o->name), "%s", name);
    o->value = value;
    o->found = found;
    o->weak = weak;
}

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} buf_writer;

static void w_bytes(buf_writer *w, const void *p, size_t n)
{
    if (w->len + n > w->cap)
        exit(2);
    memcpy(w->buf + w->len, p, n);
    w->len += n;
}

static void w_u8(buf_writer *w, uint8_t v)
{
    w_bytes(w, &v, 1);
}

static void w_uleb(buf_writer *w, uint64_t v)
{
    do {
        uint8_t b = (uint8_t)(v & 0x7f);
        v >>= 7;
        if (v)
            b |= 0x80;
        w_u8(w, b);
    } while (v);
}

static void w_sleb(buf_writer *w, int64_t v)
{
    bool more = true;
    while (more) {
        uint8_t b = (uint8_t)(v & 0x7f);
        v >>= 7;
        if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)))
            more = false;
        else
            b |= 0x80;
        w_u8(w, b);
    }
}

static void w_str(buf_writer *w, const char *s)
{
    w_bytes(w, s, strlen(s) + 1);
}

static void write_seg(uint8_t *dst, const char *name, uint64_t vm, uint64_t vsize,
                      uint64_t foff, uint64_t fsize, uint32_t maxp, uint32_t initp)
{
    struct chm_segment_command sc;
    memset(&sc, 0, sizeof(sc));
    sc.cmd = CHM_LC_SEGMENT_64;
    sc.cmdsize = sizeof(sc);
    memcpy(sc.segname, name, strlen(name));
    sc.vmaddr = vm;
    sc.vmsize = vsize;
    sc.fileoff = foff;
    sc.filesize = fsize;
    sc.maxprot = maxp;
    sc.initprot = initp;
    memcpy(dst, &sc, sizeof(sc));
}

static int build_fixture_macho(const char *path, size_t page, const uint8_t *rebase, size_t rebase_sz,
                               const uint8_t *bind, size_t bind_sz, const uint8_t *weak, size_t weak_sz,
                               const uint8_t *lazy, size_t lazy_sz, uint64_t init_got0, uint64_t init_got1,
                               uint64_t init_rebase0, uint64_t init_rebase1, uint64_t init_weak_slot)
{
    size_t text_sz = page;
    size_t data_off = page;
    size_t data_sz = page;
    size_t link_off = page * 2;
    size_t link_sz = page;
    size_t total = page * 3;
    uint8_t *img = calloc(1, total);
    if (!img)
        return -1;

    uint64_t vm_base = UINT64_C(0x100000000);
    struct chm_mach_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = CHM_MH_MAGIC_64;
    hdr.cputype = CHM_CPU_ARM64;
    hdr.filetype = CHM_MH_EXECUTE;
    hdr.ncmds = 6;
    hdr.flags = CHM_MH_PIE;

    size_t off = sizeof(hdr);
    write_seg(img + off, "__PAGEZERO", 0, vm_base, 0, 0, 0, 0);
    off += sizeof(struct chm_segment_command);
    write_seg(img + off, "__TEXT", vm_base, text_sz, 0, text_sz, 5, 5);
    off += sizeof(struct chm_segment_command);
    write_seg(img + off, "__DATA", vm_base + data_off, data_sz, data_off, data_sz, 3, 3);
    off += sizeof(struct chm_segment_command);
    write_seg(img + off, "__LINKEDIT", vm_base + link_off, link_sz, link_off, link_sz, 1, 1);
    off += sizeof(struct chm_segment_command);

    size_t lpos = link_off;
    struct chm_dyld_info_command di;
    memset(&di, 0, sizeof(di));
    di.cmd = CHM_LC_DYLD_INFO_ONLY;
    di.cmdsize = sizeof(di);
    if (rebase_sz) {
        di.rebase_off = (uint32_t)lpos;
        di.rebase_size = (uint32_t)rebase_sz;
        memcpy(img + lpos, rebase, rebase_sz);
        lpos += rebase_sz;
    }
    if (bind_sz) {
        di.bind_off = (uint32_t)lpos;
        di.bind_size = (uint32_t)bind_sz;
        memcpy(img + lpos, bind, bind_sz);
        lpos += bind_sz;
    }
    if (weak_sz) {
        di.weak_bind_off = (uint32_t)lpos;
        di.weak_bind_size = (uint32_t)weak_sz;
        memcpy(img + lpos, weak, weak_sz);
        lpos += weak_sz;
    }
    if (lazy_sz) {
        di.lazy_bind_off = (uint32_t)lpos;
        di.lazy_bind_size = (uint32_t)lazy_sz;
        memcpy(img + lpos, lazy, lazy_sz);
        lpos += lazy_sz;
    }
    memcpy(img + off, &di, sizeof(di));
    off += sizeof(di);

    struct chm_dylib_command dc;
    memset(&dc, 0, sizeof(dc));
    const char *lib = "/usr/lib/libSystem.B.dylib";
    uint32_t dsz = (uint32_t)((sizeof(dc) + strlen(lib) + 1 + 7) & ~7u);
    dc.cmd = CHM_LC_LOAD_DYLIB;
    dc.cmdsize = dsz;
    dc.name_offset = sizeof(dc);
    memcpy(img + off, &dc, sizeof(dc));
    memcpy(img + off + sizeof(dc), lib, strlen(lib) + 1);
    off += dsz;

    hdr.sizeofcmds = (uint32_t)(off - sizeof(hdr));
    memcpy(img, &hdr, sizeof(hdr));

    memcpy(img + data_off + 0x00, &init_got0, 8);
    memcpy(img + data_off + 0x08, &init_got1, 8);
    memcpy(img + data_off + 0x30, &init_rebase0, 8);
    memcpy(img + data_off + 0x40, &init_rebase1, 8);
    memcpy(img + data_off + 0x50, &init_weak_slot, 8);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(img);
        return -1;
    }
    size_t nw = fwrite(img, 1, total, f);
    fclose(f);
    free(img);
    return nw == total ? 0 : -1;
}

static uint64_t read_slot(const chm_image *img, uint32_t seg, uint64_t off)
{
    uint64_t v = 0;
    memcpy(&v, (const uint8_t *)img->segs[seg].mapped + off, 8);
    return v;
}

static int test_full_fixup_semantics(size_t page)
{
    uint8_t rb[128];
    uint8_t bd[256];
    uint8_t wk[128];
    uint8_t lz[256];
    buf_writer wr = {rb, sizeof(rb), 0};
    w_u8(&wr, 0x11);
    w_u8(&wr, 0x22);
    w_uleb(&wr, 0x30);
    w_u8(&wr, 0x51);
    w_u8(&wr, 0x30);
    w_uleb(&wr, 8);
    w_u8(&wr, 0x51);
    w_u8(&wr, 0x00);

    buf_writer wb = {bd, sizeof(bd), 0};
    w_u8(&wb, 0x11);
    w_u8(&wb, 0x72);
    w_uleb(&wb, 0x00);
    w_u8(&wb, 0x40);
    w_str(&wb, "_mach_task_self_");
    w_u8(&wb, 0x90);
    w_u8(&wb, 0x40);
    w_str(&wb, "___stack_chk_guard");
    w_u8(&wb, 0x60);
    w_sleb(&wb, 4);
    w_u8(&wb, 0xa0);
    w_uleb(&wb, 8);
    w_u8(&wb, 0x40);
    w_str(&wb, "_after_addend");
    w_u8(&wb, 0xb1);
    w_u8(&wb, 0x41);
    w_str(&wb, "_weak_missing");
    w_u8(&wb, 0x90);
    w_u8(&wb, 0x00);

    buf_writer ww = {wk, sizeof(wk), 0};
    w_u8(&ww, 0x72);
    w_uleb(&ww, 0x50);
    w_u8(&ww, 0x40);
    w_str(&ww, "_weak_missing");
    w_u8(&ww, 0x90);
    w_u8(&ww, 0x00);

    buf_writer wl = {lz, sizeof(lz), 0};
    w_u8(&wl, 0x11);
    w_u8(&wl, 0x72);
    w_uleb(&wl, 0x60);
    w_u8(&wl, 0x40);
    w_str(&wl, "_lazy_a");
    w_u8(&wl, 0x90);
    w_u8(&wl, 0x00);
    w_u8(&wl, 0x11);
    w_u8(&wl, 0x72);
    w_uleb(&wl, 0x68);
    w_u8(&wl, 0x40);
    w_str(&wl, "_lazy_b");
    w_u8(&wl, 0x90);
    w_u8(&wl, 0x00);

    char path[] = "/tmp/chm_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 1;
    close(fd);

    if (build_fixture_macho(path, page, rb, wr.len, bd, wb.len, wk, ww.len, lz, wl.len,
                            0x1111, 0x2222, UINT64_C(0x100000100), UINT64_C(0x100000200), UINT64_C(0xdeadbeef))) {
        unlink(path);
        return 1;
    }

    chm_image img;
    if (chm_open(&img, path)) {
        unlink(path);
        fprintf(stderr, "open failed\n");
        return 1;
    }
    test_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strcpy(ctx.syms[0].name, "_mach_task_self_");
    ctx.syms[0].value = UINT64_C(0x5000);
    ctx.syms[0].present = true;
    strcpy(ctx.syms[1].name, "___stack_chk_guard");
    ctx.syms[1].value = UINT64_C(0x6000);
    ctx.syms[1].present = true;
    strcpy(ctx.syms[2].name, "_after_addend");
    ctx.syms[2].value = UINT64_C(0x7000);
    ctx.syms[2].present = true;
    strcpy(ctx.syms[3].name, "_lazy_a");
    ctx.syms[3].value = UINT64_C(0x8000);
    ctx.syms[3].present = true;
    strcpy(ctx.syms[4].name, "_lazy_b");
    ctx.syms[4].value = UINT64_C(0x9000);
    ctx.syms[4].present = true;
    ctx.nsyms = 5;

    img.resolver = test_resolve;
    img.resolver_context = &ctx;
    img.observer = test_observe;
    img.observer_context = &ctx;

    uint64_t slide = (uint64_t)page * 4;
    if (chm_fixup_at(&img, slide)) {
        fprintf(stderr, "fixup failed: %s\n", img.error);
        chm_close(&img);
        unlink(path);
        return 1;
    }

    int ok = 1;
    if (read_slot(&img, 2, 0x00) != UINT64_C(0x5000))
        ok = 0;
    if (read_slot(&img, 2, 0x08) != UINT64_C(0x6004))
        ok = 0;
    if (read_slot(&img, 2, 0x18) != UINT64_C(0x7004))
        ok = 0;
    if (read_slot(&img, 2, 0x28) != 0)
        ok = 0;
    if (read_slot(&img, 2, 0x30) != UINT64_C(0x100000100) + slide)
        ok = 0;
    if (read_slot(&img, 2, 0x40) != UINT64_C(0x100000200) + slide)
        ok = 0;
    if (read_slot(&img, 2, 0x50) != UINT64_C(0xdeadbeef))
        ok = 0;
    if (read_slot(&img, 2, 0x60) != UINT64_C(0x8000))
        ok = 0;
    if (read_slot(&img, 2, 0x68) != UINT64_C(0x9000))
        ok = 0;
    if (img.nfixup_bind != 7 || img.nfixup_weak != 1 || img.nfixup_missing != 0 || img.nfixup_rebase != 2 || !img.executable)
        ok = 0;

    chm_close(&img);
    unlink(path);
    if (!ok) {
        fprintf(stderr, "slot verification failed\n");
        return 1;
    }
    return 0;
}

static int test_missing_not_executable_and_bounds_rollback(size_t page)
{
    uint8_t bd[64];
    buf_writer wb = {bd, sizeof(bd), 0};
    w_u8(&wb, 0x11);
    w_u8(&wb, 0x72);
    w_uleb(&wb, 0x00);
    w_u8(&wb, 0x40);
    w_str(&wb, "_missing_sym");
    w_u8(&wb, 0x90);
    w_u8(&wb, 0x00);

    char path[] = "/tmp/chm_miss_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 1;
    close(fd);
    if (build_fixture_macho(path, page, NULL, 0, bd, wb.len, NULL, 0, NULL, 0, 0, 0, 0, 0, 0)) {
        unlink(path);
        return 1;
    }
    chm_image img;
    if (chm_open(&img, path)) {
        unlink(path);
        return 1;
    }
    test_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    img.resolver = test_resolve;
    img.resolver_context = &ctx;
    if (chm_fixup_at(&img, (uint64_t)page * 4)) {
        chm_close(&img);
        unlink(path);
        return 1;
    }
    int ok = img.nfixup_missing == 1 && !img.executable;
    chm_close(&img);
    unlink(path);
    if (!ok)
        return 1;

    uint8_t bad[64];
    buf_writer wbad = {bad, sizeof(bad), 0};
    w_u8(&wbad, 0x11);
    w_u8(&wbad, 0x72);
    w_uleb(&wbad, page - 4);
    w_u8(&wbad, 0x40);
    w_str(&wbad, "_oob");
    w_u8(&wbad, 0x90);
    w_u8(&wbad, 0x00);

    char path2[] = "/tmp/chm_oob_XXXXXX";
    fd = mkstemp(path2);
    if (fd < 0)
        return 1;
    close(fd);
    if (build_fixture_macho(path2, page, NULL, 0, bad, wbad.len, NULL, 0, NULL, 0, 0, 0, 0, 0, 0)) {
        unlink(path2);
        return 1;
    }
    if (chm_open(&img, path2)) {
        unlink(path2);
        return 1;
    }
    img.resolver = test_resolve;
    img.resolver_context = &ctx;
    int rc = chm_fixup_at(&img, (uint64_t)page * 8);
    ok = rc == -1 && img.reservation == NULL && img.segs[2].mapped == NULL && img.error[0] != 0;
    chm_close(&img);
    unlink(path2);
    return ok ? 0 : 1;
}

static int test_chained_fixups(size_t page)
{
    size_t total = page * 3;
    uint8_t *img = calloc(1, total);
    if (!img)
        return 1;
    uint64_t vm_base = UINT64_C(0x100000000);
    struct chm_mach_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = CHM_MH_MAGIC_64;
    hdr.cputype = CHM_CPU_ARM64;
    hdr.filetype = CHM_MH_EXECUTE;
    hdr.ncmds = 6;
    hdr.flags = CHM_MH_PIE;

    size_t off = sizeof(hdr);
    write_seg(img + off, "__PAGEZERO", 0, vm_base, 0, 0, 0, 0);
    off += sizeof(struct chm_segment_command);
    write_seg(img + off, "__TEXT", vm_base, page, 0, page, 5, 5);
    off += sizeof(struct chm_segment_command);
    write_seg(img + off, "__DATA", vm_base + page, page, page, page, 3, 3);
    off += sizeof(struct chm_segment_command);
    write_seg(img + off, "__LINKEDIT", vm_base + page * 2, page, page * 2, page, 1, 1);
    off += sizeof(struct chm_segment_command);

    size_t c_off = page * 2;
    struct chm_chained_fixups_header fh;
    memset(&fh, 0, sizeof(fh));
    fh.starts_offset = sizeof(fh);
    fh.imports_offset = fh.starts_offset + 4 + 16 + 24;
    fh.symbols_offset = fh.imports_offset + 8;
    fh.imports_count = 2;
    fh.imports_format = CHM_IMPORT_FORMAT;
    memcpy(img + c_off, &fh, sizeof(fh));

    uint32_t seg_count = 4;
    memcpy(img + c_off + fh.starts_offset, &seg_count, 4);
    uint32_t seg_offs[4] = {0, 0, 20, 0};
    memcpy(img + c_off + fh.starts_offset + 4, seg_offs, 16);

    struct chm_chained_starts_in_segment ss;
    memset(&ss, 0, sizeof(ss));
    ss.size = 24;
    ss.page_size = (uint16_t)page;
    ss.pointer_format = CHM_PTR_64_OFFSET;
    ss.segment_offset = page;
    ss.page_count = 1;
    ss.page_start[0] = 0;
    memcpy(img + c_off + fh.starts_offset + 20, &ss, sizeof(ss));

    uint32_t imp0 = 1 | (0 << 9);
    uint32_t imp1 = 1 | (9 << 9);
    memcpy(img + c_off + fh.imports_offset, &imp0, 4);
    memcpy(img + c_off + fh.imports_offset + 4, &imp1, 4);
    memcpy(img + c_off + fh.symbols_offset, "_c_first\0_c_second\0", 20);

    uint64_t p0 = (UINT64_C(1) << 63) | (UINT64_C(2) << 51) | 0;
    uint64_t p1 = (UINT64_C(1) << 63) | (UINT64_C(4) << 51) | (UINT64_C(8) << 24) | 1;
    uint64_t p2 = (UINT64_C(0x1234)) | (UINT64_C(0) << 51);
    memcpy(img + page + 0x00, &p0, 8);
    memcpy(img + page + 0x08, &p1, 8);
    memcpy(img + page + 0x18, &p2, 8);

    struct chm_linkedit_data_command lc;
    memset(&lc, 0, sizeof(lc));
    lc.cmd = CHM_LC_DYLD_CHAINED_FIXUPS;
    lc.cmdsize = sizeof(lc);
    lc.dataoff = (uint32_t)c_off;
    lc.datasize = fh.symbols_offset + 20;
    memcpy(img + off, &lc, sizeof(lc));
    off += sizeof(lc);

    struct chm_dylib_command dc;
    memset(&dc, 0, sizeof(dc));
    const char *lib = "/usr/lib/libSystem.B.dylib";
    uint32_t dsz = (uint32_t)((sizeof(dc) + strlen(lib) + 1 + 7) & ~7u);
    dc.cmd = CHM_LC_LOAD_DYLIB;
    dc.cmdsize = dsz;
    dc.name_offset = sizeof(dc);
    memcpy(img + off, &dc, sizeof(dc));
    memcpy(img + off + sizeof(dc), lib, strlen(lib) + 1);
    off += dsz;
    hdr.sizeofcmds = (uint32_t)(off - sizeof(hdr));
    memcpy(img, &hdr, sizeof(hdr));

    char path[] = "/tmp/chm_chain_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        free(img);
        return 1;
    }
    close(fd);
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(img);
        unlink(path);
        return 1;
    }
    fwrite(img, 1, total, f);
    fclose(f);
    free(img);

    chm_image m;
    memset(&m, 0, sizeof(m));
    if (chm_open(&m, path)) {
        fprintf(stderr, "chain open: %s\n", m.error);
        unlink(path);
        return 1;
    }
    test_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strcpy(ctx.syms[0].name, "_c_first");
    ctx.syms[0].value = UINT64_C(0xa000);
    ctx.syms[0].present = true;
    strcpy(ctx.syms[1].name, "_c_second");
    ctx.syms[1].value = UINT64_C(0xb000);
    ctx.syms[1].present = true;
    ctx.nsyms = 2;
    m.resolver = test_resolve;
    m.resolver_context = &ctx;

    uint64_t slide = (uint64_t)page * 5;
    if (chm_fixup_at(&m, slide)) {
        fprintf(stderr, "chained fixup failed: %s\n", m.error);
        chm_close(&m);
        unlink(path);
        return 1;
    }
    int ok = 1;
    fprintf(stderr, "chained s0=0x%llx s1=0x%llx s2=0x%llx bind=%u reb=%u exec=%d err=%s\n", (unsigned long long)read_slot(&m, 2, 0x00), (unsigned long long)read_slot(&m, 2, 0x08), (unsigned long long)read_slot(&m, 2, 0x18), m.nfixup_bind, m.nfixup_rebase, m.executable, m.error);
    ok = read_slot(&m, 2, 0x00) == UINT64_C(0xa000) &&
             read_slot(&m, 2, 0x08) == UINT64_C(0xb008) &&
             read_slot(&m, 2, 0x18) == vm_base + slide + UINT64_C(0x1234) &&
             m.nfixup_bind == 2 && m.nfixup_rebase == 1 && m.executable;
    if (!ok)
        fprintf(stderr, "chain slots: %llx %llx %llx bind=%u rebase=%u exec=%d\n",
                (unsigned long long)read_slot(&m, 2, 0x00),
                (unsigned long long)read_slot(&m, 2, 0x08),
                (unsigned long long)read_slot(&m, 2, 0x18),
                m.nfixup_bind, m.nfixup_rebase, m.executable);
    chm_close(&m);
    unlink(path);
    return ok ? 0 : 1;
}

int main(void)
{
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0)
        return 1;
    size_t page = (size_t)ps;
    if (test_full_fixup_semantics(page)) {
        fprintf(stderr, "fail: full\n");
        return 1;
    }
    if (test_missing_not_executable_and_bounds_rollback(page)) {
        fprintf(stderr, "fail: missing/rollback\n");
        return 1;
    }
    if (test_chained_fixups(page)) {
        fprintf(stderr, "fail: chained\n");
        return 1;
    }
    printf("test_fixups: all passed (page=%zu)\n", page);
    return 0;
}
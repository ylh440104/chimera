#include "chm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static size_t write_bytes(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    return w;
}

static int expect_reject(const char *label, const uint8_t *buf, size_t n)
{
    char path[] = "/tmp/chm_reject_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 1;
    close(fd);
    if (write_bytes(path, buf, n) != n) {
        unlink(path);
        return 1;
    }
    chm_image img;
    int rc = chm_open(&img, path);
    unlink(path);
    if (rc == 0) {
        fprintf(stderr, "accepted malformed input: %s\n", label);
        chm_close(&img);
        return 1;
    }
    if (!img.error[0]) {
        fprintf(stderr, "no diagnostic for: %s\n", label);
        return 1;
    }
    printf("rejected %-28s %s\n", label, img.error);
    return 0;
}

int main(void)
{
    uint8_t base[4096];
    memset(base, 0, sizeof(base));
    struct chm_mach_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = CHM_MH_MAGIC_64;
    hdr.cputype = CHM_CPU_ARM64;
    hdr.filetype = CHM_MH_EXECUTE;
    hdr.ncmds = 1;
    hdr.sizeofcmds = 72;
    memcpy(base, &hdr, sizeof(hdr));
    struct chm_segment_command sc;
    memset(&sc, 0, sizeof(sc));
    sc.cmd = CHM_LC_SEGMENT_64;
    sc.cmdsize = sizeof(sc);
    memcpy(sc.segname, "__TEXT", 6);
    sc.vmaddr = UINT64_C(0x100000000);
    sc.vmsize = UINT64_C(0x1000);
    sc.filesize = UINT64_C(0x1000);
    sc.maxprot = 5;
    sc.initprot = 5;
    memcpy(base + sizeof(hdr), &sc, sizeof(sc));

    int failures = 0;

    struct chm_mach_header bad = hdr;
    bad.magic = 0xfeedface;
    uint8_t tmp[4096];
    memcpy(tmp, base, sizeof(tmp));
    memcpy(tmp, &bad, sizeof(bad));
    failures += expect_reject("fat magic", tmp, sizeof(tmp));

    memcpy(tmp, base, sizeof(tmp));
    bad = hdr;
    bad.filetype = CHM_MH_OBJECT;
    memcpy(tmp, &bad, sizeof(bad));
    failures += expect_reject("object file type", tmp, sizeof(tmp));

    memcpy(tmp, base, sizeof(tmp));
    bad = hdr;
    bad.sizeofcmds = 0xff00;
    memcpy(tmp, &bad, sizeof(bad));
    failures += expect_reject("command table overflow", tmp, sizeof(tmp));

    memcpy(tmp, base, sizeof(tmp));
    struct chm_segment_command badseg = sc;
    badseg.filesize = UINT64_C(0x9000);
    memcpy(tmp + sizeof(hdr), &badseg, sizeof(badseg));
    failures += expect_reject("segment beyond file", tmp, sizeof(tmp));

    memcpy(tmp, base, sizeof(tmp));
    badseg = sc;
    badseg.initprot = 6;
    badseg.maxprot = 3;
    memcpy(tmp + sizeof(hdr), &badseg, sizeof(badseg));
    failures += expect_reject("write-exec segment", tmp, sizeof(tmp));

    memcpy(tmp, base, sizeof(tmp));
    badseg = sc;
    badseg.vmaddr = UINT64_C(0x100000100);
    memcpy(tmp + sizeof(hdr), &badseg, sizeof(badseg));
    failures += expect_reject("unaligned segment", tmp, sizeof(tmp));

    memcpy(tmp, base, sizeof(tmp));
    struct chm_load_command lc = {0x22, 48};
    memcpy(tmp, &hdr, sizeof(hdr));
    bad = hdr;
    bad.ncmds = 1;
    bad.sizeofcmds = 48;
    memcpy(tmp, &bad, sizeof(bad));
    memcpy(tmp + sizeof(hdr), &lc, sizeof(lc));
    failures += expect_reject("dyld info out of file", tmp, sizeof(tmp));

    uint8_t truncated[16];
    memset(truncated, 0, sizeof(truncated));
    failures += expect_reject("too short", truncated, sizeof(truncated));

    char path[] = "/tmp/chm_ok_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 1;
    close(fd);
    if (write_bytes(path, base, sizeof(base)) != sizeof(base)) {
        unlink(path);
        return 1;
    }
    chm_image ok;
    int rc = chm_open(&ok, path);
    unlink(path);
    if (rc != 0) {
        fprintf(stderr, "valid minimal image rejected: %s\n", ok.error);
        failures++;
    } else {
        printf("accepted %-28s segments=%u\n", "minimal valid image", ok.nsegs);
        chm_close(&ok);
    }

    if (failures) {
        fprintf(stderr, "test_reject: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_reject: all passed\n");
    return 0;
}
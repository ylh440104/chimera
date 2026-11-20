#ifndef CHM_H
#define CHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CHM_MAX_SEGS 32
#define CHM_MAX_DYLIBS 32
#define CHM_MAX_NAME 256

#define CHM_MH_MAGIC_64 0xfeedfacfu
#define CHM_CPU_ARM64 0x0100000cu

#define CHM_MH_OBJECT 0x1u
#define CHM_MH_EXECUTE 0x2u
#define CHM_MH_DYLIB 0x6u
#define CHM_MH_BUNDLE 0x8u
#define CHM_MH_PIE 0x00200000u

#define CHM_LC_SEGMENT_64 0x19u
#define CHM_LC_SYMTAB 0x02u
#define CHM_LC_LOAD_DYLIB 0x0cu
#define CHM_LC_ID_DYLIB 0x0du
#define CHM_LC_LOAD_WEAK_DYLIB 0x80000018u
#define CHM_LC_REEXPORT_DYLIB 0x8000001fu
#define CHM_LC_LAZY_LOAD_DYLIB 0x20u
#define CHM_LC_LOAD_UPWARD_DYLIB 0x80000023u
#define CHM_LC_DYLD_INFO 0x22u
#define CHM_LC_DYLD_INFO_ONLY 0x80000022u
#define CHM_LC_MAIN 0x80000028u
#define CHM_LC_DYLD_CHAINED_FIXUPS 0x80000034u
#define CHM_LC_BUILD_VERSION 0x32u
#define CHM_LC_UUID 0x1bu
#define CHM_LC_CODE_SIGNATURE 0x1du

#define CHM_PLATFORM_IOS 2u
#define CHM_PLATFORM_IOSSIMULATOR 7u

struct chm_mach_header {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct chm_load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct chm_segment_command {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct chm_section_64 {
    char sectname[16];
    char segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct chm_symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

struct chm_nlist_64 {
    uint32_t n_strx;
    uint8_t n_type;
    uint8_t n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};

struct chm_dylib_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t name_offset;
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compatibility_version;
};

struct chm_dyld_info_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t rebase_off;
    uint32_t rebase_size;
    uint32_t bind_off;
    uint32_t bind_size;
    uint32_t weak_bind_off;
    uint32_t weak_bind_size;
    uint32_t lazy_bind_off;
    uint32_t lazy_bind_size;
    uint32_t export_off;
    uint32_t export_size;
};

struct chm_entry_point_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
};

struct chm_linkedit_data_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t dataoff;
    uint32_t datasize;
};

struct chm_build_version_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint32_t ntools;
};

struct chm_chained_fixups_header {
    uint32_t fixups_version;
    uint32_t starts_offset;
    uint32_t imports_offset;
    uint32_t symbols_offset;
    uint32_t imports_count;
    uint32_t imports_format;
    uint32_t symbols_format;
};

struct chm_chained_starts_in_image {
    uint32_t seg_count;
    uint32_t seg_info_offset[1];
};

struct chm_chained_starts_in_segment {
    uint32_t size;
    uint16_t page_size;
    uint16_t pointer_format;
    uint64_t segment_offset;
    uint32_t max_valid_pointer;
    uint16_t page_count;
    uint16_t page_start[1];
};

enum chm_ptr_format {
    CHM_PTR_ARM64E = 1,
    CHM_PTR_64 = 2,
    CHM_PTR_32 = 3,
    CHM_PTR_32_CACHE = 4,
    CHM_PTR_32_FIRMWARE = 5,
    CHM_PTR_64_OFFSET = 6,
    CHM_PTR_ARM64E_KERNEL = 7,
    CHM_PTR_64_KERNEL_CACHE = 8,
    CHM_PTR_ARM64E_USERLAND = 9,
    CHM_PTR_ARM64E_FIRMWARE = 10,
    CHM_PTR_X86_64_KERNEL_CACHE = 11,
    CHM_PTR_ARM64E_USERLAND24 = 12
};

enum chm_import_format {
    CHM_IMPORT_FORMAT = 1,
    CHM_IMPORT_FORMAT_ADDEND = 2,
    CHM_IMPORT_FORMAT_ADDEND64 = 3
};

#define CHM_B64_TARGET(v) ((v) & 0xfffffffffull)
#define CHM_B64_HIGH8(v) (((v) >> 36) & 0xffull)
#define CHM_B64_NEXT(v) (((v) >> 51) & 0xfffull)
#define CHM_B64_BIND(v) (((v) >> 63) & 1ull)
#define CHM_B64_ORDINAL(v) ((v) & 0xffffffull)
#define CHM_B64_ADDEND(v) ((int64_t)((v) & 0xff000000ull) >> 24)

#define CHM_A64E_TARGET(v) ((v) & 0x7ffffffffffull)
#define CHM_A64E_HIGH8(v) (((v) >> 43) & 0xffull)
#define CHM_A64E_NEXT(v) (((v) >> 51) & 0x7ffull)
#define CHM_A64E_BIND(v) (((v) >> 62) & 1ull)
#define CHM_A64E_AUTH(v) (((v) >> 63) & 1ull)
#define CHM_A64E_ORDINAL(v) ((v) & 0xffffull)
#define CHM_A64E_ADDEND(v) ((int64_t)(((v) >> 32) & 0x7ffffull) << 45 >> 45)
#define CHM_A64E_AUTH_TARGET(v) ((v) & 0xffffffffull)

typedef struct {
    char sect[17];
    char seg[17];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t flags;
} chm_section;

typedef struct {
    char name[17];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t initprot;
    uint32_t maxprot;
    uint32_t flags;
    uint32_t nsects;
    chm_section *sects;
    void *mapped;
} chm_segment;

typedef struct {
    char name[CHM_MAX_NAME];
    uint8_t type;
    uint8_t sect;
    uint16_t desc;
    uint64_t value;
} chm_symbol;

typedef struct {
    char name[CHM_MAX_NAME];
    uint32_t ordinal;
} chm_dylib;

struct chm_image;

typedef int (*chm_bind_resolver)(const struct chm_image *, int32_t, const char *, bool, uint64_t *, void *);
typedef void (*chm_bind_observer)(const struct chm_image *, uint32_t, uint64_t, int32_t, const char *, uint64_t, bool, bool, void *);

typedef struct chm_image {
    void *file;
    size_t filesize;
    struct chm_mach_header *hdr;
    chm_segment segs[CHM_MAX_SEGS];
    uint32_t nsegs;
    chm_dylib dylibs[CHM_MAX_DYLIBS];
    uint32_t ndylibs;
    chm_symbol *syms;
    uint32_t nsyms;
    const char *strtab;
    uint32_t strsize;
    uint64_t slide;
    uint64_t entryoff;
    int has_entry;
    uint32_t platform;
    struct chm_dyld_info_command *dyld_info;
    struct chm_linkedit_data_command *chained;
    uint64_t vm_min;
    uint64_t vm_max;
    uint32_t nfixup_bind;
    uint32_t nfixup_missing;
    uint32_t nfixup_rebase;
    uint32_t nfixup_weak;
    uint32_t cryptid;
    void *reservation;
    size_t reservation_size;
    bool fixed;
    bool executable;
    chm_bind_resolver resolver;
    chm_bind_observer observer;
    void *resolver_context;
    void *observer_context;
    char error[256];
} chm_image;

int chm_open(chm_image *img, const char *path);
void chm_close(chm_image *img);
int chm_map(chm_image *img, uint64_t slide);
int chm_map_default(chm_image *img);
int chm_fixup(chm_image *img);
int chm_fixup_at(chm_image *img, uint64_t slide);
int chm_bind_image(chm_image *img);
int chm_protect(chm_image *img);
void chm_unmap(chm_image *img);
int chm_fail(chm_image *img, const char *fmt, ...);
int chm_apply_fixups(chm_image *img);
void *chm_resolve_import(const char *name);
void *chm_sym(chm_image *img, const char *name);
void chm_dump(chm_image *img, FILE *out);
int chm_run(chm_image *img, int argc, char **argv);

#endif
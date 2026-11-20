#ifndef CHM_INT_H
#define CHM_INT_H

#include <stddef.h>
#include <stdint.h>

#include "chm.h"

#define CHM_REPORT_MAX 4096

typedef struct {
    char text[CHM_REPORT_MAX];
} chm_report;

void chm_report_add(chm_report *r, const char *fmt, ...);
void chm_report_line(chm_report *r, const char *text);

void chm_objc_scan(chm_image *img, chm_report *r);
int chm_objc_class_count(void);
const char *chm_objc_class_at(int index);
void *chm_objc_getClass(const char *name);
void *chm_objc_getMetaClass(const char *name);
void *chm_objc_sel_registerName(const char *name);
void *chm_objc_lookup_imp(void *self, const char *sel);
void chm_objc_call_loads(chm_report *r);
void *chm_objc_resolve_import(const char *name);
void *chm_objc_block_isa(void);

void chm_dispatch_init(void);
void *chm_dispatch_resolve_import(const char *name);
int chm_dispatch_pending(void);
void chm_dispatch_pump(chm_report *r);

int chm_fw_load(chm_image *img, chm_report *r);

int chm_dylib_load_all(chm_image *main_img, const char *exe_path, chm_report *r);
void *chm_dylib_lookup(const char *name);
int chm_dylib_export_count(void);

int chm_ui_main(chm_image *img, int argc, char **argv, chm_report *r);

#endif
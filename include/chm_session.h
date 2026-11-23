#ifndef CHM_SESSION_H
#define CHM_SESSION_H

#include "chm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/stat.h>

#define CHM_MAX_IMAGES 32
#define CHM_MAX_EXPORTS 16384
#define CHM_MAX_MISSING 2048
#define CHM_MAX_CLASSES 2048
#define CHM_MAX_SELS 4096

typedef enum {
    CHM_SYM_NONE = 0,
    CHM_SYM_IMAGE_EXPORT,
    CHM_SYM_RUNTIME_FN,
    CHM_SYM_RUNTIME_DATA,
    CHM_SYM_OBJC_CLASS,
    CHM_SYM_OBJC_METACLASS
} chm_sym_origin;

typedef struct {
    char name[160];
    uint64_t addr;
    int32_t image_index;
} chm_export_sym;

typedef struct {
    char name[CHM_MAX_NAME];
    uint32_t hits;
    bool weak;
} chm_missing_sym;

typedef struct {
    char name[160];
    void *cls;
    void *meta;
    int32_t image_index;
} chm_objc_cls_entry;

#define CHM_MAX_METHOD_REGS 1024

typedef struct {
    const char *cls;
    char sel[96];
    void *imp;
} chm_method_reg;

typedef struct chm_session {
    chm_image images[CHM_MAX_IMAGES];
    char image_paths[CHM_MAX_IMAGES][512];
    int image_count;
    char app_dir[512];
    char exe_path[512];

    chm_export_sym *exports;
    int export_count;

    chm_missing_sym *missing;
    int missing_count;

    chm_objc_cls_entry *classes;
    int class_count;

    chm_method_reg *methods;
    int method_count;
    struct objc_method_store *objc_methods;

    char **sels;
    int sel_count;

    char *log_buf;
    size_t log_cap;
    size_t log_len;

    bool prepared;
    atomic_int exit_code;
    char fault[512];
} chm_session;

chm_session *chm_session_create(void);
void chm_session_destroy(chm_session *s);
void chm_session_log(chm_session *s, const char *fmt, ...);
int chm_session_load(chm_session *s, const char *exe_path);
int chm_session_run(chm_session *s, int argc, char **argv);
void chm_session_report(chm_session *s, char *out, size_t cap);

void chm_objc_session_init(chm_session *s);
void chm_objc_session_scan(chm_session *s, int img_idx);
typedef struct {
    const char *owner, *sel, *types;
    void *imp;
    int class_method;
} chm_host_method;
void chm_objc_register_host(chm_session *s, const chm_host_method *entries, size_t count);
void chm_objc_index_methods(chm_session *s);
void chm_objc_methods_reset(chm_session *s);
void *chm_objc_session_get_class(chm_session *s, const char *name);
void *chm_objc_session_get_metaclass(chm_session *s, const char *name);
void *chm_objc_session_lookup(chm_session *s, const char *name);
void *chm_objc_msgSend_bridge(void);
void *chm_objc_msgSendSuper2_bridge(void);
void *chm_objc_resolve_imp(void *self, const char *sel);
void *chm_objc_find_imp(void *self, const char *sel);
int chm_objc_is_kind(void *obj, const char *name);
void *chm_objc_resolve_super2_imp(void *super_ptr, const char *sel);
void chm_objc_dump_trace(chm_session *s);
const char *chm_objc_class_name(void *cls);
void *chm_objc_get_class(const char *name);

void *chm_block_copy(void *object);
void chm_block_release(void *object);
void chm_blocks_reset(void);
void chm_dispatch_reset(void);
void chm_dispatch_set_session(chm_session *session);
int chm_dispatch_worker(void);
int chm_dispatch_stopping(void);
void chm_guest_lock(void);
void chm_guest_unlock(void);
unsigned chm_guest_suspend(void);
void chm_guest_resume(unsigned depth);
void chm_dispatch_init(void);
void *chm_dispatch_lookup(const char *name);
size_t chm_dispatch_pump(chm_session *s);
size_t chm_dispatch_pending(void);

void chm_foundation_init(chm_session *s);
void chm_foundation_shutdown(void);
int chm_foundation_class_count(void);
void *chm_foundation_lookup_class(const char *name);
void *chm_foundation_lookup_metaclass(const char *name);
void *chm_foundation_class_ro(void *cls);
void *chm_foundation_alloc_class(void *cls, size_t min);
const char *chm_foundation_cstr(void *obj);
void *chm_foundation_lookup_method(const char *cls_name, const char *sel);
void chm_foundation_note_missing_selector(const char *cls_name, const char *sel);
void chm_foundation_set_root(const char *root);
void chm_foundation_set_bundle(const char *root);
void *chm_foundation_bundle_info(void);
typedef struct {
    uint64_t token;
    const char *url, *method, *headers;
    const void *body;
    size_t body_size;
    int timeout_ms, cancelled;
} chm_network_request;
int chm_network_next(chm_network_request *request);
int chm_network_complete(uint64_t token, int status, const void *bytes, size_t length, const char *headers, const char *failure);
int chm_foundation_map_path(const char *in, char *out, size_t cap);
int chm_foundation_map_mutable(const char *in, char *out, size_t cap);
int chm_foundation_map_path_write(const char *in, char *out, size_t cap);
int chm_foundation_prepare_home(void);
void *chm_foundation_home_directory(void);
void *chm_foundation_search_paths(unsigned long dir, unsigned long domains, signed char expand);
int chm_foundation_isa_is(void *obj, const char *cls_name);
void *chm_foundation_alloc_obj(const char *cls_name, size_t min);
void *chm_foundation_string(const char *bytes);
void *chm_foundation_constant(const char *symbol);

void *chm_objc_cfstr_isa(void);
void chm_objc_set_cfstr_isa(void *isa);

void *chm_objc_send0(void *obj, const char *sel);
void *chm_objc_send1(void *obj, const char *sel, void *a);
void *chm_objc_send2(void *obj, const char *sel, void *a, void *b);
void *chm_objc_send3(void *obj, const char *sel, void *a, void *b, void *c);
void *chm_objc_new_object(const char *cls_name);
int chm_objc_session_add_method(chm_session *s, const chm_method_reg *m);

void chm_libc_set_session(chm_session *s);
extern uint64_t g_stdout_obj;
extern uint64_t g_stderr_obj;
void chm_io_reset(void);
int chm_io_remap(int guest, int host, int reset_host);
int chm_io_close_number(int guest);
int chm_io_file_status(int guest, struct stat *status);
int64_t chm_io_seek_number(int guest, int64_t offset, int whence);
int chm_io_fd_flags(int guest);
int chm_io_set_fd_flags(int guest, int flags);
int chm_io_get_status(int guest);
int chm_io_set_status(int guest, int flags);
void chm_libc_publish_images(chm_session *s);

void *chm_frameworks_lookup(const char *name);
int chm_frameworks_count(void);

void chm_trap_install(void);
int chm_session_execute(chm_session *s, chm_image *img, int argc, char **argv);
int chm_session_callback(chm_session *s, int (*body)(void *), void *context);
uint64_t chm_trap_pc(void);
uint64_t chm_trap_lr(void);
int chm_trap_signal(void);
uint64_t chm_trap_fault_addr(void);

int chm_ui_main(int argc, char **argv, void *principal, void *delegate_name);

void chm_ui_init(chm_session *s);
void chm_ui_shutdown(chm_session *s);
int chm_ui_viewport(int w, int h);
int chm_ui_running(void);
typedef struct {
    int kind;
    const char *text;
    double x, y, width, height;
    double clip_left, clip_top, clip_right, clip_bottom;
    uint32_t color;
    double font_size;
    int alignment, lines;
} chm_draw_command;
typedef void (*chm_draw_fn)(void *context, const chm_draw_command *command);
int chm_ui_paint(chm_draw_fn draw, void *context, int width, int height);
int chm_ui_render(uint32_t *buf, int bw, int bh);
int chm_ui_view_count(void);
int chm_ui_pointer(int phase, double x, double y);
int chm_ui_pan(int phase, double x, double y);
int chm_ui_scroll(double x, double y, double delta);
uint64_t chm_ui_focused_field(void);
const char *chm_ui_field_text(void);
const char *chm_ui_field_placeholder(void);
int chm_ui_field_options(void);
int chm_ui_edit_text(uint64_t token, const char *text, int commit);

#endif
.text
.align 2
.globl chm_objc_msgSend_trampoline
.globl chm_objc_msgSendSuper2_trampoline

.macro SAVE_ALL
    stp x29, x30, [sp, #-256]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    stp q4, q5, [sp, #160]
    stp q6, q7, [sp, #192]
.endm

.macro RESTORE_ALL
    ldp q6, q7, [sp, #192]
    ldp q4, q5, [sp, #160]
    ldp q2, q3, [sp, #128]
    ldp q0, q1, [sp, #96]
    ldr x8, [sp, #80]
    ldp x6, x7, [sp, #64]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #32]
    ldp x0, x1, [sp, #16]
.endm

chm_objc_msgSend_trampoline:
    cbz x0, .Lnil
    SAVE_ALL
    bl chm_objc_resolve_imp
    mov x16, x0
    RESTORE_ALL
    b .Ldispatch

chm_objc_msgSendSuper2_trampoline:
    cbz x0, .Lnil
    ldr x9, [x0]
    cbz x9, .Lnil
    SAVE_ALL
    bl chm_objc_resolve_super2_imp
    mov x16, x0
    RESTORE_ALL
    ldr x0, [x0]
.Ldispatch:
    ldp x29, x30, [sp], #256
    cbz x16, .Lnil
    br x16

.Lnil:
    mov x0, xzr
    mov x1, xzr
    movi v0.16b, #0
    movi v1.16b, #0
    movi v2.16b, #0
    movi v3.16b, #0
    ret

.macro DARWIN_ENTRY name, next_arg, body
    .globl \name
    .hidden \name
    .type \name, %function
\name:
    mov \next_arg, sp
    b \body
    .size \name, .-\name
.endm

DARWIN_ENTRY chm_predicate_entry, x3, chm_predicate_list
DARWIN_ENTRY chm_ns_format_entry, x3, chm_ns_format_list
DARWIN_ENTRY chm_ns_append_format_entry, x3, chm_ns_append_format_list
DARWIN_ENTRY chm_printf_entry, x1, chm_printf_list
DARWIN_ENTRY chm_fprintf_entry, x2, chm_fprintf_list
DARWIN_ENTRY chm_open_entry, x2, chm_open_list
DARWIN_ENTRY chm_sscanf_entry, x2, chm_sscanf_list
DARWIN_ENTRY chm_sprintf_chk_entry, x4, chm_sprintf_chk_list

.section .note.GNU-stack,"",%progbits
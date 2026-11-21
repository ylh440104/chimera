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
    cbz x0, L_nil_ret
    SAVE_ALL
    bl chm_objc_resolve_imp2
    mov x16, x0
    mov x17, x1
    cbnz x17, L_variadic_dispatch
    RESTORE_ALL
    ldp x29, x30, [sp], #256
    cbz x16, L_nil_ret
    br x16

chm_objc_msgSendSuper2_trampoline:
    cbz x0, L_nil_ret
    SAVE_ALL
    bl chm_objc_resolve_super2_imp2
    mov x16, x0
    mov x17, x1
    cbnz x17, L_variadic_super2
    RESTORE_ALL
    ldr x0, [x0]
    ldp x29, x30, [sp], #256
    cbz x16, L_nil_ret
    br x16

L_variadic_super2:
    ldr x0, [x0]
    str x0, [sp, #16]
    b L_variadic_dispatch

L_variadic_dispatch:
    ldp x0, x1, [sp, #16]
    add x2, sp, #16
    add x3, sp, #96
    add x4, sp, #256
    mov x5, x16
    bl chm_objc_call_variadic
    ldp x29, x30, [sp], #256
    ret

L_nil_ret:
    mov x0, #0
    mov x1, #0
    fmov d0, xzr
    fmov d1, xzr
    ret
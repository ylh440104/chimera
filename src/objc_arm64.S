.text
.align 2
.globl chm_objc_msgSend_trampoline
.globl chm_objc_msgSendSuper2_trampoline

chm_objc_msgSend_trampoline:
    cbz x0, L_nil_ret
    stp x29, x30, [sp, #-160]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    bl chm_objc_resolve_imp
    mov x16, x0
    ldp q0, q1, [sp, #96]
    ldp q2, q3, [sp, #128]
    ldr x8, [sp, #80]
    ldp x6, x7, [sp, #64]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #32]
    ldp x0, x1, [sp, #16]
    ldp x29, x30, [sp], #160
    cbz x16, L_nil_ret
    br x16

chm_objc_msgSendSuper2_trampoline:
    cbz x0, L_nil_ret
    stp x29, x30, [sp, #-160]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    bl chm_objc_resolve_super2_imp
    mov x16, x0
    ldp q0, q1, [sp, #96]
    ldp q2, q3, [sp, #128]
    ldr x8, [sp, #80]
    ldp x6, x7, [sp, #64]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #32]
    ldp x0, x1, [sp, #16]
    ldr x0, [x0]
    ldp x29, x30, [sp], #160
    cbz x16, L_nil_ret
    br x16

L_nil_ret:
    mov x0, #0
    mov x1, #0
    fmov d0, xzr
    fmov d1, xzr
    ret
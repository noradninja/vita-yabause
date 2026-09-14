/*
 * Minimal ARM-state flight recorder for the Vita Ari64 runtime.
 *
 * This wrapper is intentionally ARM-only and avoids libc, filesystem I/O,
 * allocation, and Thumb interworking while recording the boundary around
 * sh2_recompile_block().  The linker redirects the linkage_arm.s call through
 * __wrap_sh2_recompile_block with --wrap=sh2_recompile_block.
 */

    .syntax unified
    .arm

    .section .bss
    .align 4
    .global vita_dynarec_recompile_recorder
    .type vita_dynarec_recompile_recorder, %object
    .size vita_dynarec_recompile_recorder, 160
vita_dynarec_recompile_recorder:
    .space 160

    .section .text
    .align 2

/*
 * Recorder layout (40 words / 160 bytes):
 *
 *   0  magic = 0x41524d52 ("ARMR")
 *   1  version = 1
 *   2  monotonically increasing snapshot sequence
 *   3  last checkpoint
 *
 * Each of two entries is 18 words / 72 bytes:
 *   0  checkpoint (1=before real compiler, 2=after real compiler)
 *   1  CPSR
 *   2  r0
 *   3  r1
 *   4  r2
 *   5  r3
 *   6  r4
 *   7  r5
 *   8  r6
 *   9  r7
 *  10  r8
 *  11  r9
 *  12  r10
 *  13  r11/fp
 *  14  r12
 *  15  SP before the six-word snapshot save frame
 *  16  LR before the snapshot call
 *  17  address of __real_sh2_recompile_block
 *
 * The caller saves r0-r3/r12/lr in a 24-byte frame before entering this
 * helper.  This leaf helper deliberately never touches r4-r11.
 */
    .global vita_dynarec_recompile_snapshot
    .type vita_dynarec_recompile_snapshot, %function
vita_dynarec_recompile_snapshot:
    ldr     r12, .Lrecorder_ptr
    ldr     r1, .Lrecorder_magic
    str     r1, [r12, #0]
    mov     r1, #1
    str     r1, [r12, #4]
    ldr     r1, [r12, #8]
    add     r1, r1, #1
    str     r1, [r12, #8]
    str     r0, [r12, #12]

    sub     r3, r0, #1
    add     r3, r3, r3, lsl #3      /* checkpoint-1 times 9 */
    lsl     r3, r3, #3              /* times 72 bytes */
    add     r12, r12, #16
    add     r12, r12, r3

    str     r0, [r12, #0]
    mrs     r1, cpsr
    str     r1, [r12, #4]

    ldr     r1, [sp, #0]
    str     r1, [r12, #8]
    ldr     r1, [sp, #4]
    str     r1, [r12, #12]
    ldr     r1, [sp, #8]
    str     r1, [r12, #16]
    ldr     r1, [sp, #12]
    str     r1, [r12, #20]

    str     r4,  [r12, #24]
    str     r5,  [r12, #28]
    str     r6,  [r12, #32]
    str     r7,  [r12, #36]
    str     r8,  [r12, #40]
    str     r9,  [r12, #44]
    str     r10, [r12, #48]
    str     r11, [r12, #52]

    ldr     r1, [sp, #16]
    str     r1, [r12, #56]
    add     r1, sp, #24
    str     r1, [r12, #60]
    ldr     r1, [sp, #20]
    str     r1, [r12, #64]
    ldr     r1, .Lreal_recompiler_ptr
    str     r1, [r12, #68]

    mov     pc, lr
    .size vita_dynarec_recompile_snapshot, .-vita_dynarec_recompile_snapshot

    .align 2
    .global __wrap_sh2_recompile_block
    .type __wrap_sh2_recompile_block, %function
__wrap_sh2_recompile_block:
    /* Preserve the linkage caller's r4 and LR across the real C function. */
    push    {r4, lr}

    /* Checkpoint 1: exact ARM-state boundary immediately before compilation. */
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, #1
    bl      vita_dynarec_recompile_snapshot
    pop     {r0, r1, r2, r3, r12, lr}

    bl      __real_sh2_recompile_block
    mov     r4, r0

    /* Checkpoint 2 exists only if sh2_recompile_block returns normally. */
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, #2
    bl      vita_dynarec_recompile_snapshot
    pop     {r0, r1, r2, r3, r12, lr}

    mov     r0, r4
    pop     {r4, pc}
    .size __wrap_sh2_recompile_block, .-__wrap_sh2_recompile_block

    .align 2
.Lrecorder_ptr:
    .word   vita_dynarec_recompile_recorder
.Lrecorder_magic:
    .word   0x41524d52
.Lreal_recompiler_ptr:
    .word   __real_sh2_recompile_block

    .section .note.GNU-stack,"",%progbits

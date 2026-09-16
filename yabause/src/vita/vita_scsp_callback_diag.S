/* Vita-only GSD_6 callback-boundary probe.
 *
 * The callback is required by AAPCS to preserve r4-r11 and SP.  Capture the
 * exact state on both sides of BLX, stop before returning to ScspExec if it
 * changes, and restore the saved native context only so the C failure recorder
 * can execute reliably.  This is diagnostic containment, not runtime repair.
 */

	.syntax unified
	.thumb
	.text
	.align 2

/* VitaScspStateRecorder.callback begins 40 bytes into the recorder. */
	.equ TRACE_BASE,              40
	.equ TRACE_TARGET,             0
	.equ TRACE_RETURN_VALUE,      20
	.equ TRACE_BEFORE_R4,         24
	.equ TRACE_BEFORE_SP,         56
	.equ TRACE_BEFORE_LR,         60
	.equ TRACE_EXPECTED_RETURN,   64
	.equ TRACE_AFTER_R4,          68
	.equ TRACE_AFTER_SP,         100
	.equ TRACE_AFTER_LR,         104

	.equ FAILURE_SP,             0x110
	.equ FAILURE_R4,             0x114
	.equ FAILURE_R5,             0x115
	.equ FAILURE_R6,             0x116
	.equ FAILURE_R7,             0x117
	.equ FAILURE_R8,             0x118
	.equ FAILURE_R9,             0x119
	.equ FAILURE_R10,            0x11a
	.equ FAILURE_R11,            0x11b

	.global vita_scsp_state_diag_call_get_audio_space
	.type vita_scsp_state_diag_call_get_audio_space, %function
	.thumb_func
vita_scsp_state_diag_call_get_audio_space:
	ldr	r12, =vita_scsp_state_recorder
	add	r12, r12, #TRACE_BASE
	add	r1, r12, #TRACE_BEFORE_R4
	stmia	r1, {r4-r11}
	mov	r1, sp
	str	r1, [r12, #TRACE_BEFORE_SP]
	str	lr, [r12, #TRACE_BEFORE_LR]
	str	r0, [r12, #TRACE_TARGET]
	adr	r1, .Lcallback_return
	orr	r1, r1, #1
	str	r1, [r12, #TRACE_EXPECTED_RETURN]

	/* Ten registers keep the public call boundary 8-byte aligned. */
	push	{r3-r11, lr}
	mov	r3, r0
	blx	r3

.Lcallback_return:
	ldr	r12, =vita_scsp_state_recorder
	add	r12, r12, #TRACE_BASE
	str	r0, [r12, #TRACE_RETURN_VALUE]
	add	r1, r12, #TRACE_AFTER_R4
	stmia	r1, {r4-r11}
	add	r1, sp, #40
	str	r1, [r12, #TRACE_AFTER_SP]
	str	lr, [r12, #TRACE_AFTER_LR]

	ldr	r2, [r12, #TRACE_BEFORE_SP]
	cmp	r1, r2
	bne	.Lfail_sp

	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 0)]
	cmp	r4, r1
	bne	.Lfail_r4
	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 4)]
	cmp	r5, r1
	bne	.Lfail_r5
	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 8)]
	cmp	r6, r1
	bne	.Lfail_r6
	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 12)]
	cmp	r7, r1
	bne	.Lfail_r7
	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 16)]
	cmp	r8, r1
	bne	.Lfail_r8
	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 20)]
	cmp	r9, r1
	bne	.Lfail_r9
	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 24)]
	cmp	r10, r1
	bne	.Lfail_r10
	ldr	r1, [r12, #(TRACE_BEFORE_R4 + 28)]
	cmp	r11, r1
	bne	.Lfail_r11

	pop	{r3-r11, lr}
	bx	lr

.Lfail_sp:
	movw	r0, #FAILURE_SP
	mov	r1, sp
	add	r1, r1, #40
	ldr	r2, [r12, #TRACE_BEFORE_SP]
	/* Re-establish the diagnostic frame before popping saved registers. */
	sub	r3, r2, #40
	mov	sp, r3
	b	.Lfail
.Lfail_r4:
	movw	r0, #FAILURE_R4
	mov	r1, r4
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 0)]
	b	.Lfail
.Lfail_r5:
	movw	r0, #FAILURE_R5
	mov	r1, r5
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 4)]
	b	.Lfail
.Lfail_r6:
	movw	r0, #FAILURE_R6
	mov	r1, r6
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 8)]
	b	.Lfail
.Lfail_r7:
	movw	r0, #FAILURE_R7
	mov	r1, r7
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 12)]
	b	.Lfail
.Lfail_r8:
	movw	r0, #FAILURE_R8
	mov	r1, r8
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 16)]
	b	.Lfail
.Lfail_r9:
	movw	r0, #FAILURE_R9
	mov	r1, r9
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 20)]
	b	.Lfail
.Lfail_r10:
	movw	r0, #FAILURE_R10
	mov	r1, r10
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 24)]
	b	.Lfail
.Lfail_r11:
	movw	r0, #FAILURE_R11
	mov	r1, r11
	ldr	r2, [r12, #(TRACE_BEFORE_R4 + 28)]

.Lfail:
	pop	{r3-r11, lr}
	ldr	r3, [r12, #TRACE_TARGET]
	b	vita_scsp_state_diag_callback_fail

	.size vita_scsp_state_diag_call_get_audio_space, .-vita_scsp_state_diag_call_get_audio_space

	.global vita_scsp_state_diag_trap
	.type vita_scsp_state_diag_trap, %function
	.thumb_func
vita_scsp_state_diag_trap:
	movw	r1, #0x18
	str	r0, [r1]
.Ltrap_spin:
	b	.Ltrap_spin
	.size vita_scsp_state_diag_trap, .-vita_scsp_state_diag_trap

	.section .note.GNU-stack,"",%progbits

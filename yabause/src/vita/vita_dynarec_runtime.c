#include <stdint.h>

#include "../memory.h"
#include "../sh2core.h"
#include "vita_dynarec_vm.h"

/* The Vita-generated Ari64 source keeps the bounded-smoke hooks compiled in so
 * it can share the same branch veneer and VM adaptation path. Real runtime
 * builds leave these controls permanently disabled. */
int vita_dynarec_exec_test_active = 0;
unsigned int vita_dynarec_exec_test_instruction_limit = 0;

/* Ari64's ARM backend predates several SH2-context arguments that modern
 * Yabause added. Generated code/linkage still use legacy calls such as
 * Read(addr), Write(addr,value), FRTExec(cycles), and WDTExec(cycles), while
 * the current core expects an SH2 * as the first argument.
 *
 * The real-runtime build links those entry points through --wrap. Calls that
 * already use the modern ABI pass through unchanged. Legacy Ari64 calls are
 * recognized because r0 is an emulated address/cycle count rather than one of
 * Yabause's SH2 context pointers. CurrentSH2 is maintained by the Ari64 frame
 * scheduler before entering generated master/slave execution. */
extern void *CurrentSH2;
extern unsigned char dynarec_local[];
extern unsigned int rccount;
extern unsigned int restore_candidate[];

u8 FASTCALL __real_MappedMemoryReadByteNocache(SH2_struct *sh, u32 addr);
u16 FASTCALL __real_MappedMemoryReadWordNocache(SH2_struct *sh, u32 addr);
u32 FASTCALL __real_MappedMemoryReadLongNocache(SH2_struct *sh, u32 addr);
void FASTCALL __real_MappedMemoryWriteByteNocache(SH2_struct *sh, u32 addr, u8 val);
void FASTCALL __real_MappedMemoryWriteWordNocache(SH2_struct *sh, u32 addr, u16 val);
void FASTCALL __real_MappedMemoryWriteLongNocache(SH2_struct *sh, u32 addr, u32 val);
void __real_FRTExec(SH2_struct *sh, u32 cycles);
void __real_WDTExec(SH2_struct *sh, u32 cycles);

/*
 * Non-intrusive Ari64 host-state flight recorder.
 *
 * The previous first-frame diagnostic performed fopen/fprintf/fflush/fclose
 * from inside scheduler wrappers. The failing hardware run showed that this
 * could itself become the crash site before control returned to linkage_arm.s.
 * Keep the replacement entirely in BSS and write it from tiny naked assembly
 * wrappers. There are no libc, filesystem, allocation, or C helper calls in
 * the recording path.
 *
 * Header (4 words):
 *   [0] magic "VDFR" (0x56444652)
 *   [1] version (1)
 *   [2] monotonically increasing snapshot sequence
 *   [3] last checkpoint id
 *
 * Nine fixed entries follow, 24 words / 96 bytes each. Entry N is at
 *   base + 16 + (N - 1) * 96
 * and contains:
 *   0  checkpoint id
 *   1  saved r4       10 live r4
 *   2  saved r5       11 live r5
 *   3  saved r6       12 live r6
 *   4  saved r7       13 live r7
 *   5  saved r8       14 live r8
 *   6  saved r9       15 live r9
 *   7  saved r10      16 live r10
 *   8  saved fp/r11   17 live fp/r11
 *   9  saved lr       18 live r12
 *                     19 live sp
 *                     20 live lr (scheduler caller return)
 *   21 rccount
 *   22 restore_candidate[rccount]
 *   23 restore_candidate[(rccount + 1) & 63]
 *
 * Checkpoints:
 *   1 VBlankOUT returned
 *   2/3 post-VBlank SmpcExec enter/return
 *   4/5 post-VBlank Cs2Exec enter/return
 *   6/7 post-VBlank M68KExec enter/return
 *   8/9 nextframe M68KSync enter/return
 */
#define VITA_DYNAREC_FLIGHT_ENTRY_WORDS 24u
#define VITA_DYNAREC_FLIGHT_ENTRY_COUNT 9u
#define VITA_DYNAREC_FLIGHT_HEADER_WORDS 4u
#define VITA_DYNAREC_FLIGHT_WORDS \
   (VITA_DYNAREC_FLIGHT_HEADER_WORDS + \
    VITA_DYNAREC_FLIGHT_ENTRY_WORDS * VITA_DYNAREC_FLIGHT_ENTRY_COUNT)

__attribute__((used, aligned(4)))
volatile uint32_t vita_dynarec_flight_recorder[VITA_DYNAREC_FLIGHT_WORDS];

__attribute__((used, aligned(4)))
volatile uint32_t vita_dynarec_flight_active;

static int vita_dynarec_is_yabause_context(SH2_struct *sh)
{
   return sh && (sh == MSH2 || sh == SSH2 || sh == SH1);
}

static SH2_struct *vita_dynarec_current_context(void)
{
   SH2_struct *sh = (SH2_struct *)CurrentSH2;
   if (sh == MSH2 || sh == SSH2)
      return sh;

   /* A legacy bridge call should only occur while Ari64 has selected one
    * Saturn SH2. Keep this deterministic and, importantly for this diagnostic,
    * do not enter stdio from a potentially corrupted dynarec host context. */
   return MSH2;
}

u8 FASTCALL __wrap_MappedMemoryReadByteNocache(SH2_struct *sh, u32 addr)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      addr = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   return __real_MappedMemoryReadByteNocache(sh, addr);
}

u16 FASTCALL __wrap_MappedMemoryReadWordNocache(SH2_struct *sh, u32 addr)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      addr = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   return __real_MappedMemoryReadWordNocache(sh, addr);
}

u32 FASTCALL __wrap_MappedMemoryReadLongNocache(SH2_struct *sh, u32 addr)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      addr = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   return __real_MappedMemoryReadLongNocache(sh, addr);
}

void FASTCALL __wrap_MappedMemoryWriteByteNocache(SH2_struct *sh, u32 addr, u8 val)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      val = (u8)addr;
      addr = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   __real_MappedMemoryWriteByteNocache(sh, addr, val);
}

void FASTCALL __wrap_MappedMemoryWriteWordNocache(SH2_struct *sh, u32 addr, u16 val)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      val = (u16)addr;
      addr = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   __real_MappedMemoryWriteWordNocache(sh, addr, val);
}

void FASTCALL __wrap_MappedMemoryWriteLongNocache(SH2_struct *sh, u32 addr, u32 val)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      val = addr;
      addr = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   __real_MappedMemoryWriteLongNocache(sh, addr, val);
}

void __wrap_FRTExec(SH2_struct *sh, u32 cycles)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      cycles = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   __real_FRTExec(sh, cycles);
}

void __wrap_WDTExec(SH2_struct *sh, u32 cycles)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      cycles = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   __real_WDTExec(sh, cycles);
}

#ifndef VITA_PROFILE
void __real_ScuExec(u32 timing);
void __real_M68KSync(void);
void __real_Vdp2HBlankIN(void);
void __real_Vdp2HBlankOUT(void);
void __real_ScspExec(void);
void __real_SmpcExec(s32 timing);
void __real_Cs2Exec(u32 timing);
void __real_M68KExec(s32 cycles);
void __real_Vdp2VBlankIN(void);
void __real_Vdp2VBlankOUT(void);
void __real_SmpcINTBACKEnd(void);
void __real_CheatDoPatches(void);

/* These scheduler calls are still linker-wrapped by the existing diagnostic
 * build configuration. Calls that are not part of the narrow post-VBlank
 * window are now transparent pass-throughs with no logging side effects. */
void __wrap_ScuExec(u32 timing)
{
   __real_ScuExec(timing);
}

void __wrap_Vdp2HBlankIN(void)
{
   __real_Vdp2HBlankIN();
}

void __wrap_Vdp2HBlankOUT(void)
{
   __real_Vdp2HBlankOUT();
}

void __wrap_ScspExec(void)
{
   __real_ScspExec();
}

void __wrap_Vdp2VBlankIN(void)
{
   __real_Vdp2VBlankIN();
}

void __wrap_SmpcINTBACKEnd(void)
{
   __real_SmpcINTBACKEnd();
}

void __wrap_CheatDoPatches(void)
{
   __real_CheatDoPatches();
}

/*
 * The five wrappers below are emitted as Thumb-2 assembly so the probe itself
 * cannot acquire a compiler prologue/epilogue that changes the live Ari64
 * callee-saved register set before we observe it. The snapshot helper uses
 * only caller-saved registers and preserves r4-r11 exactly.
 *
 * Each active wrapper keeps the scheduler's incoming r0-r3/r12/lr on a
 * 24-byte (8-byte aligned) stack frame. This gives the recorder the original
 * scheduler SP/LR while allowing the real helper to execute normally. If the
 * helper violates AAPCS by corrupting r4-r11, the return snapshot records the
 * corrupted values and the wrapper deliberately does NOT repair them.
 */
__asm__(
   ".syntax unified\n"
   ".thumb\n"
   ".align 2\n"

   ".global vita_dynarec_flight_snapshot\n"
   ".type vita_dynarec_flight_snapshot, %function\n"
   ".thumb_func\n"
   "vita_dynarec_flight_snapshot:\n"
   "    push.w {r0-r3, r12, lr}\n"
   "    ldr r12, =vita_dynarec_flight_recorder\n"
   "    movw r0, #0x4652\n"
   "    movt r0, #0x5644\n"
   "    str r0, [r12, #0]\n"
   "    movs r0, #1\n"
   "    str r0, [r12, #4]\n"
   "    ldr r0, [r12, #8]\n"
   "    add.w r0, r0, #1\n"
   "    str r0, [r12, #8]\n"
   "    ldr r0, [sp, #0]\n"
   "    str r0, [r12, #12]\n"
   "    sub.w r0, r0, #1\n"
   "    add.w r0, r0, r0, lsl #1\n"
   "    add.w r12, r12, #16\n"
   "    add.w r12, r12, r0, lsl #5\n"
   "    ldr r0, [sp, #0]\n"
   "    str r0, [r12, #0]\n"

   "    ldr r0, =dynarec_local\n"
   "    ldr r1, [r0, #28]\n"
   "    str r1, [r12, #4]\n"
   "    ldr r1, [r0, #32]\n"
   "    str r1, [r12, #8]\n"
   "    ldr r1, [r0, #36]\n"
   "    str r1, [r12, #12]\n"
   "    ldr r1, [r0, #40]\n"
   "    str r1, [r12, #16]\n"
   "    ldr r1, [r0, #44]\n"
   "    str r1, [r12, #20]\n"
   "    ldr r1, [r0, #48]\n"
   "    str r1, [r12, #24]\n"
   "    ldr r1, [r0, #52]\n"
   "    str r1, [r12, #28]\n"
   "    ldr r1, [r0, #56]\n"
   "    str r1, [r12, #32]\n"
   "    ldr r1, [r0, #60]\n"
   "    str r1, [r12, #36]\n"

   "    str r4, [r12, #40]\n"
   "    str r5, [r12, #44]\n"
   "    str r6, [r12, #48]\n"
   "    str r7, [r12, #52]\n"
   "    str r8, [r12, #56]\n"
   "    str r9, [r12, #60]\n"
   "    str r10, [r12, #64]\n"
   "    str r11, [r12, #68]\n"
   "    ldr r0, [sp, #12]\n"
   "    str r0, [r12, #72]\n"
   "    ldr r0, [sp, #8]\n"
   "    str r0, [r12, #76]\n"
   "    ldr r0, [sp, #4]\n"
   "    str r0, [r12, #80]\n"

   "    ldr r0, =rccount\n"
   "    ldr r1, [r0]\n"
   "    str r1, [r12, #84]\n"
   "    and.w r2, r1, #0x3f\n"
   "    ldr r0, =restore_candidate\n"
   "    ldr.w r3, [r0, r2, lsl #2]\n"
   "    str r3, [r12, #88]\n"
   "    add.w r2, r2, #1\n"
   "    and.w r2, r2, #0x3f\n"
   "    ldr.w r3, [r0, r2, lsl #2]\n"
   "    str r3, [r12, #92]\n"
   "    pop.w {r0-r3, r12, pc}\n"
   ".size vita_dynarec_flight_snapshot, .-vita_dynarec_flight_snapshot\n"

   ".macro VITA_ACTIVE_WRAPPER name, real, enter_id, return_id\n"
   "    .global \\name\n"
   "    .type \\name, %function\n"
   "    .thumb_func\n"
   "\\name:\n"
   "    ldr r12, =vita_dynarec_flight_active\n"
   "    ldr r12, [r12]\n"
   "    cbz r12, 99f\n"
   "    push.w {r0-r3, r12, lr}\n"
   "    ldr r1, [sp, #20]\n"
   "    add.w r2, sp, #24\n"
   "    ldr r3, [sp, #16]\n"
   "    movs r0, #\\enter_id\n"
   "    bl vita_dynarec_flight_snapshot\n"
   "    ldr r0, [sp, #0]\n"
   "    ldr r1, [sp, #4]\n"
   "    ldr r2, [sp, #8]\n"
   "    ldr r3, [sp, #12]\n"
   "    ldr r12, [sp, #16]\n"
   "    bl \\real\n"
   "    mov r3, r12\n"
   "    ldr r1, [sp, #20]\n"
   "    add.w r2, sp, #24\n"
   "    movs r0, #\\return_id\n"
   "    bl vita_dynarec_flight_snapshot\n"
   "    ldr lr, [sp, #20]\n"
   "    add.w sp, sp, #24\n"
   "    bx lr\n"
   "99:\n"
   "    b.w \\real\n"
   "    .size \\name, .-\\name\n"
   ".endm\n"

   "VITA_ACTIVE_WRAPPER __wrap_SmpcExec, __real_SmpcExec, 2, 3\n"
   "VITA_ACTIVE_WRAPPER __wrap_Cs2Exec, __real_Cs2Exec, 4, 5\n"
   "VITA_ACTIVE_WRAPPER __wrap_M68KExec, __real_M68KExec, 6, 7\n"

   ".global __wrap_Vdp2VBlankOUT\n"
   ".type __wrap_Vdp2VBlankOUT, %function\n"
   ".thumb_func\n"
   "__wrap_Vdp2VBlankOUT:\n"
   "    push.w {r0-r3, r12, lr}\n"
   "    ldr r0, [sp, #0]\n"
   "    ldr r1, [sp, #4]\n"
   "    ldr r2, [sp, #8]\n"
   "    ldr r3, [sp, #12]\n"
   "    ldr r12, [sp, #16]\n"
   "    bl __real_Vdp2VBlankOUT\n"
   "    mov r3, r12\n"
   "    ldr r1, [sp, #20]\n"
   "    add.w r2, sp, #24\n"
   "    movs r0, #1\n"
   "    bl vita_dynarec_flight_snapshot\n"
   "    ldr r12, =vita_dynarec_flight_active\n"
   "    movs r0, #1\n"
   "    str r0, [r12]\n"
   "    ldr lr, [sp, #20]\n"
   "    add.w sp, sp, #24\n"
   "    bx lr\n"
   ".size __wrap_Vdp2VBlankOUT, .-__wrap_Vdp2VBlankOUT\n"

   ".global __wrap_M68KSync\n"
   ".type __wrap_M68KSync, %function\n"
   ".thumb_func\n"
   "__wrap_M68KSync:\n"
   "    ldr r12, =vita_dynarec_flight_active\n"
   "    ldr r12, [r12]\n"
   "    cbz r12, 98f\n"
   "    push.w {r0-r3, r12, lr}\n"
   "    ldr r1, [sp, #20]\n"
   "    add.w r2, sp, #24\n"
   "    ldr r3, [sp, #16]\n"
   "    movs r0, #8\n"
   "    bl vita_dynarec_flight_snapshot\n"
   "    ldr r0, [sp, #0]\n"
   "    ldr r1, [sp, #4]\n"
   "    ldr r2, [sp, #8]\n"
   "    ldr r3, [sp, #12]\n"
   "    ldr r12, [sp, #16]\n"
   "    bl __real_M68KSync\n"
   "    mov r3, r12\n"
   "    ldr r1, [sp, #20]\n"
   "    add.w r2, sp, #24\n"
   "    movs r0, #9\n"
   "    bl vita_dynarec_flight_snapshot\n"
   "    ldr r12, =vita_dynarec_flight_active\n"
   "    movs r0, #0\n"
   "    str r0, [r12]\n"
   "    ldr lr, [sp, #20]\n"
   "    add.w sp, sp, #24\n"
   "    bx lr\n"
   "98:\n"
   "    b.w __real_M68KSync\n"
   ".size __wrap_M68KSync, .-__wrap_M68KSync\n"
   ".ltorg\n"
);
#endif

#ifdef VITA_USE_VITAGL
void __real_glGetIntegerv(unsigned int pname, int *params);

void __wrap_glGetIntegerv(unsigned int pname, int *params)
{
   __real_glGetIntegerv(pname, params);
}
#endif

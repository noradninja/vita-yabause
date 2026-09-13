#include <stdint.h>
#include <stdio.h>

#include "../memory.h"
#include "../sh2core.h"
#include "vita_dynarec_vm.h"

/* The Vita-generated Ari64 source keeps the bounded-smoke hooks compiled in so
 * it can share the same branch veneer and VM adaptation path.  Real runtime
 * builds leave these controls permanently disabled. */
int vita_dynarec_exec_test_active = 0;
unsigned int vita_dynarec_exec_test_instruction_limit = 0;

/* Ari64's ARM backend predates several SH2-context arguments that modern
 * Yabause added.  Generated code/linkage still use legacy calls such as
 * Read(addr), Write(addr,value), FRTExec(cycles), and WDTExec(cycles), while
 * the current core expects an SH2 * as the first argument.
 *
 * The real-runtime build links those entry points through --wrap. Calls that
 * already use the modern ABI pass through unchanged. Legacy Ari64 calls are
 * recognized because r0 is an emulated address/cycle count rather than one of
 * Yabause's SH2 context pointers. CurrentSH2 is maintained by the Ari64 frame
 * scheduler before entering generated master/slave execution. */
extern void *CurrentSH2;
extern int master_pc;
extern int slave_pc;
extern void *master_ip;
extern void *slave_ip;

#define DYNAREC_RUNTIME_LOG_PATH "ux0:data/yabause/dynarec-runtime.log"

u8 FASTCALL __real_MappedMemoryReadByteNocache(SH2_struct *sh, u32 addr);
u16 FASTCALL __real_MappedMemoryReadWordNocache(SH2_struct *sh, u32 addr);
u32 FASTCALL __real_MappedMemoryReadLongNocache(SH2_struct *sh, u32 addr);
void FASTCALL __real_MappedMemoryWriteByteNocache(SH2_struct *sh, u32 addr, u8 val);
void FASTCALL __real_MappedMemoryWriteWordNocache(SH2_struct *sh, u32 addr, u16 val);
void FASTCALL __real_MappedMemoryWriteLongNocache(SH2_struct *sh, u32 addr, u32 val);
void __real_FRTExec(SH2_struct *sh, u32 cycles);
void __real_WDTExec(SH2_struct *sh, u32 cycles);

static void vita_dynarec_trace(const char *stage, uintptr_t arg0, uintptr_t arg1)
{
   FILE *file = fopen(DYNAREC_RUNTIME_LOG_PATH, "a");
   if (!file)
      return;

   fprintf(file,
           "trace=%s current_sh2=%08x master_pc=%08x master_ip=%08x slave_pc=%08x slave_ip=%08x arg0=%08x arg1=%08x write_depth=%u\n",
           stage, (unsigned)(uintptr_t)CurrentSH2,
           (unsigned)master_pc, (unsigned)(uintptr_t)master_ip,
           (unsigned)slave_pc, (unsigned)(uintptr_t)slave_ip,
           (unsigned)arg0, (unsigned)arg1,
           vita_dynarec_vm_write_depth());
   fflush(file);
   fclose(file);
}

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
    * Saturn SH2. Keep this deterministic rather than dereferencing a stale
    * or unknown context if that scheduler contract is ever violated. */
   fprintf(stderr, "vita dynarec ABI bridge: CurrentSH2 is invalid (%p)\n", (void *)sh);
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
   vita_dynarec_trace("frt-enter", (uintptr_t)sh, cycles);
   __real_FRTExec(sh, cycles);
   vita_dynarec_trace("frt-return", (uintptr_t)sh, cycles);
}

void __wrap_WDTExec(SH2_struct *sh, u32 cycles)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      cycles = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   vita_dynarec_trace("wdt-enter", (uintptr_t)sh, cycles);
   __real_WDTExec(sh, cycles);
   vita_dynarec_trace("wdt-return", (uintptr_t)sh, cycles);
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

void __wrap_ScuExec(u32 timing)
{
   vita_dynarec_trace("scu-enter", timing, 0);
   __real_ScuExec(timing);
   vita_dynarec_trace("scu-return", timing, 0);
}

void __wrap_M68KSync(void)
{
   vita_dynarec_trace("m68k-sync-enter", 0, 0);
   __real_M68KSync();
   vita_dynarec_trace("m68k-sync-return", 0, 0);
}

void __wrap_Vdp2HBlankIN(void)
{
   vita_dynarec_trace("hblank-in-enter", 0, 0);
   __real_Vdp2HBlankIN();
   vita_dynarec_trace("hblank-in-return", 0, 0);
}

void __wrap_Vdp2HBlankOUT(void)
{
   vita_dynarec_trace("hblank-out-enter", 0, 0);
   __real_Vdp2HBlankOUT();
   vita_dynarec_trace("hblank-out-return", 0, 0);
}

void __wrap_ScspExec(void)
{
   vita_dynarec_trace("scsp-enter", 0, 0);
   __real_ScspExec();
   vita_dynarec_trace("scsp-return", 0, 0);
}

void __wrap_SmpcExec(s32 timing)
{
   vita_dynarec_trace("smpc-enter", (uintptr_t)(uint32_t)timing, 0);
   __real_SmpcExec(timing);
   vita_dynarec_trace("smpc-return", (uintptr_t)(uint32_t)timing, 0);
}

void __wrap_Cs2Exec(u32 timing)
{
   vita_dynarec_trace("cs2-enter", timing, 0);
   __real_Cs2Exec(timing);
   vita_dynarec_trace("cs2-return", timing, 0);
}

void __wrap_M68KExec(s32 cycles)
{
   vita_dynarec_trace("m68k-enter", (uintptr_t)(uint32_t)cycles, 0);
   __real_M68KExec(cycles);
   vita_dynarec_trace("m68k-return", (uintptr_t)(uint32_t)cycles, 0);
}

void __wrap_Vdp2VBlankIN(void)
{
   vita_dynarec_trace("vblank-in-enter", 0, 0);
   __real_Vdp2VBlankIN();
   vita_dynarec_trace("vblank-in-return", 0, 0);
}

void __wrap_Vdp2VBlankOUT(void)
{
   vita_dynarec_trace("vblank-out-enter", 0, 0);
   __real_Vdp2VBlankOUT();
   vita_dynarec_trace("vblank-out-return", 0, 0);
}

void __wrap_SmpcINTBACKEnd(void)
{
   vita_dynarec_trace("smpc-intback-enter", 0, 0);
   __real_SmpcINTBACKEnd();
   vita_dynarec_trace("smpc-intback-return", 0, 0);
}

void __wrap_CheatDoPatches(void)
{
   vita_dynarec_trace("cheat-patches-enter", 0, 0);
   __real_CheatDoPatches();
   vita_dynarec_trace("cheat-patches-return", 0, 0);
}
#endif

#ifdef VITA_USE_VITAGL
void __real_glGetIntegerv(unsigned int pname, int *params);

void __wrap_glGetIntegerv(unsigned int pname, int *params)
{
   vita_dynarec_trace("glGetIntegerv-enter", pname, (uintptr_t)params);
   __real_glGetIntegerv(pname, params);
   vita_dynarec_trace("glGetIntegerv-return", pname, (uintptr_t)params);
}
#endif

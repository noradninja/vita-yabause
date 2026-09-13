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

u8 FASTCALL __real_MappedMemoryReadByteNocache(SH2_struct *sh, u32 addr);
u16 FASTCALL __real_MappedMemoryReadWordNocache(SH2_struct *sh, u32 addr);
u32 FASTCALL __real_MappedMemoryReadLongNocache(SH2_struct *sh, u32 addr);
void FASTCALL __real_MappedMemoryWriteByteNocache(SH2_struct *sh, u32 addr, u8 val);
void FASTCALL __real_MappedMemoryWriteWordNocache(SH2_struct *sh, u32 addr, u16 val);
void FASTCALL __real_MappedMemoryWriteLongNocache(SH2_struct *sh, u32 addr, u32 val);
void __real_FRTExec(SH2_struct *sh, u32 cycles);
void __real_WDTExec(SH2_struct *sh, u32 cycles);

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

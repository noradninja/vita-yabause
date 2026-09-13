#include <stdint.h>
#include <stdio.h>

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
extern int master_pc;
extern int slave_pc;
extern void *master_ip;
extern void *slave_ip;
extern unsigned char dynarec_local[];
extern unsigned int rccount;
extern unsigned int restore_candidate[];

#define DYNAREC_RUNTIME_LOG_PATH "ux0:data/yabause/dynarec-runtime.log"
#define DYNAREC_SAVED_HOST_FP_OFFSET 56u
#define DYNAREC_SAVED_HOST_LR_OFFSET 60u

u8 FASTCALL __real_MappedMemoryReadByteNocache(SH2_struct *sh, u32 addr);
u16 FASTCALL __real_MappedMemoryReadWordNocache(SH2_struct *sh, u32 addr);
u32 FASTCALL __real_MappedMemoryReadLongNocache(SH2_struct *sh, u32 addr);
void FASTCALL __real_MappedMemoryWriteByteNocache(SH2_struct *sh, u32 addr, u8 val);
void FASTCALL __real_MappedMemoryWriteWordNocache(SH2_struct *sh, u32 addr, u16 val);
void FASTCALL __real_MappedMemoryWriteLongNocache(SH2_struct *sh, u32 addr, u32 val);
void __real_FRTExec(SH2_struct *sh, u32 cycles);
void __real_WDTExec(SH2_struct *sh, u32 cycles);

/* The first tracing revision synchronously opened/flushed/closed the log around
 * every scheduler helper call. On Vita that is expensive enough to make a
 * healthy first frame appear hung. Keep counters in memory and only touch the
 * filesystem at coarse scanline/VBlank checkpoints or inside the narrow
 * post-VBlank return window being investigated. */
static unsigned int trace_frt_calls;
static unsigned int trace_wdt_calls;
static unsigned int trace_hblank_in_calls;
static unsigned int trace_hblank_out_calls;
static unsigned int trace_scu_calls;
static unsigned int trace_m68k_sync_calls;
static unsigned int trace_scsp_calls;
static unsigned int trace_smpc_calls;
static unsigned int trace_cs2_calls;
static unsigned int trace_m68k_calls;
static unsigned int trace_vblank_in_calls;
static unsigned int trace_vblank_out_calls;
static int trace_post_vblank_active;

static uint32_t vita_dynarec_saved_word(unsigned int offset)
{
   const uint32_t *word = (const uint32_t *)(const void *)(dynarec_local + offset);
   return *word;
}

static void vita_dynarec_trace_checkpoint(const char *stage,
                                          uintptr_t arg0,
                                          uintptr_t arg1)
{
   unsigned int rc = rccount & 0x3fu;
   unsigned int next_rc = (rc + 1u) & 0x3fu;
   uint32_t saved_fp = vita_dynarec_saved_word(DYNAREC_SAVED_HOST_FP_OFFSET);
   uint32_t saved_lr = vita_dynarec_saved_word(DYNAREC_SAVED_HOST_LR_OFFSET);
   FILE *file = fopen(DYNAREC_RUNTIME_LOG_PATH, "a");
   if (!file)
      return;

   fprintf(file,
           "trace=%s current_sh2=%08x master_pc=%08x master_ip=%08x slave_pc=%08x slave_ip=%08x arg0=%08x arg1=%08x write_depth=%u frt=%u wdt=%u hbin=%u hbout=%u scu=%u m68ksync=%u scsp=%u smpc=%u cs2=%u m68k=%u vbin=%u vbout=%u saved_fp=%08x saved_lr=%08x rccount=%u restore_cur=%08x restore_next=%08x\n",
           stage, (unsigned)(uintptr_t)CurrentSH2,
           (unsigned)master_pc, (unsigned)(uintptr_t)master_ip,
           (unsigned)slave_pc, (unsigned)(uintptr_t)slave_ip,
           (unsigned)arg0, (unsigned)arg1,
           vita_dynarec_vm_write_depth(),
           trace_frt_calls, trace_wdt_calls,
           trace_hblank_in_calls, trace_hblank_out_calls,
           trace_scu_calls, trace_m68k_sync_calls, trace_scsp_calls,
           trace_smpc_calls, trace_cs2_calls, trace_m68k_calls,
           trace_vblank_in_calls, trace_vblank_out_calls,
           (unsigned)saved_fp, (unsigned)saved_lr, rc,
           restore_candidate[rc], restore_candidate[next_rc]);
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
   ++trace_frt_calls;
   __real_FRTExec(sh, cycles);
}

void __wrap_WDTExec(SH2_struct *sh, u32 cycles)
{
   if (!vita_dynarec_is_yabause_context(sh)) {
      cycles = (u32)(uintptr_t)sh;
      sh = vita_dynarec_current_context();
   }
   ++trace_wdt_calls;
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

void __wrap_ScuExec(u32 timing)
{
   ++trace_scu_calls;
   __real_ScuExec(timing);
}

void __wrap_M68KSync(void)
{
   ++trace_m68k_sync_calls;
   if (trace_post_vblank_active)
      vita_dynarec_trace_checkpoint("nextframe-m68ksync-enter", 0, 0);
   __real_M68KSync();
   if (trace_post_vblank_active) {
      vita_dynarec_trace_checkpoint("nextframe-m68ksync-return", 0, 0);
      trace_post_vblank_active = 0;
   }
}

void __wrap_Vdp2HBlankIN(void)
{
   ++trace_hblank_in_calls;
   __real_Vdp2HBlankIN();

   if (trace_hblank_in_calls == 1 || (trace_hblank_in_calls & 31u) == 0)
      vita_dynarec_trace_checkpoint("scanline-checkpoint",
                                    trace_hblank_in_calls,
                                    trace_hblank_out_calls);
}

void __wrap_Vdp2HBlankOUT(void)
{
   ++trace_hblank_out_calls;
   __real_Vdp2HBlankOUT();
}

void __wrap_ScspExec(void)
{
   ++trace_scsp_calls;
   __real_ScspExec();
}

void __wrap_SmpcExec(s32 timing)
{
   ++trace_smpc_calls;
   if (trace_post_vblank_active)
      vita_dynarec_trace_checkpoint("post-vblank-smpc-enter", (uintptr_t)timing, 0);
   __real_SmpcExec(timing);
   if (trace_post_vblank_active)
      vita_dynarec_trace_checkpoint("post-vblank-smpc-return", (uintptr_t)timing, 0);
}

void __wrap_Cs2Exec(u32 timing)
{
   ++trace_cs2_calls;
   if (trace_post_vblank_active)
      vita_dynarec_trace_checkpoint("post-vblank-cs2-enter", timing, 0);
   __real_Cs2Exec(timing);
   if (trace_post_vblank_active)
      vita_dynarec_trace_checkpoint("post-vblank-cs2-return", timing, 0);
}

void __wrap_M68KExec(s32 cycles)
{
   ++trace_m68k_calls;
   if (trace_post_vblank_active)
      vita_dynarec_trace_checkpoint("post-vblank-m68k-enter", (uintptr_t)cycles, 0);
   __real_M68KExec(cycles);
   if (trace_post_vblank_active)
      vita_dynarec_trace_checkpoint("post-vblank-m68k-return", (uintptr_t)cycles, 0);
}

void __wrap_Vdp2VBlankIN(void)
{
   ++trace_vblank_in_calls;
   vita_dynarec_trace_checkpoint("vblank-in-enter", 0, 0);
   __real_Vdp2VBlankIN();
   vita_dynarec_trace_checkpoint("vblank-in-return", 0, 0);
}

void __wrap_Vdp2VBlankOUT(void)
{
   ++trace_vblank_out_calls;
   vita_dynarec_trace_checkpoint("vblank-out-enter", 0, 0);
   __real_Vdp2VBlankOUT();
   vita_dynarec_trace_checkpoint("vblank-out-return", 0, 0);
   trace_post_vblank_active = 1;
}

void __wrap_SmpcINTBACKEnd(void)
{
   __real_SmpcINTBACKEnd();
}

void __wrap_CheatDoPatches(void)
{
   __real_CheatDoPatches();
}
#endif

#ifdef VITA_USE_VITAGL
void __real_glGetIntegerv(unsigned int pname, int *params);

void __wrap_glGetIntegerv(unsigned int pname, int *params)
{
   vita_dynarec_trace_checkpoint("glGetIntegerv-enter", pname, (uintptr_t)params);
   __real_glGetIntegerv(pname, params);
   vita_dynarec_trace_checkpoint("glGetIntegerv-return", pname, (uintptr_t)params);
}
#endif

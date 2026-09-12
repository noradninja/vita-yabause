#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../memory.h"
#include "../sh2core.h"
#include "../sh2_dynarec/sh2_dynarec.h"
#include "../yabause.h"
#include "vita_dynarec_vm.h"

#define DYNAREC_SMOKE_LOG "ux0:data/yabause/dynarec-runtime.log"
#define DYNAREC_EXEC_TEST_PC 0x002FFF00u
#define DYNAREC_EXEC_TEST_OPCODE 0xE02Au /* MOV #42,R0 */
#define DYNAREC_EXEC_TEST_PAD 0x0009u    /* NOP */
#define DYNAREC_EXEC_TEST_R0_BEFORE 0x13579BDFu
#define DYNAREC_MASTER_REG_COUNT 22u

extern int __real_YabauseInit(yabauseinit_struct *init);
extern int sh2_recompile_block(int addr);
extern void *get_addr_ht(u32 vaddr);
extern void sh2_dynarec_cleanup(void);
extern void vita_dynarec_test_enter(void *entry);

extern u8 *LowWram;
extern int master_reg[22];
extern int master_cc;
extern int master_pc;
extern void *CurrentSH2;

/* Read by the Vita-only generated Ari64 translation unit while it is compiling
 * the synthetic execution block.  Normal production-smoke compilation leaves
 * this zero and follows the unmodified Ari64 linker path. */
int vita_dynarec_exec_test_active;

static void smoke_log(FILE *file, const char *message)
{
   if (!file)
      return;
   fprintf(file, "%s\n", message);
   fflush(file);
}

static int run_dynarec_exec_smoke(FILE *file, uintptr_t base, uintptr_t code_limit)
{
   const u32 test_pc = DYNAREC_EXEC_TEST_PC;
   const u32 test_offset = test_pc & 0xFFFFFu;
   u16 saved_word0;
   u16 saved_word1;
   int compile_rc;
   int result = -1;
   void *entry = NULL;
   uintptr_t entry_address;

   if (!LowWram) {
      smoke_log(file, "SMOKE_EXEC_FAIL reason=low-wram-not-ready");
      return -1;
   }

   /* Keep this test isolated from the BIOS.  The synthetic SH2 instruction is
    * placed near the end of Low WRAM, executed through Ari64, then the original
    * words are restored before normal interpreter boot continues. */
   saved_word0 = T2ReadWord(LowWram, test_offset);
   saved_word1 = T2ReadWord(LowWram, test_offset + 2);
   T2WriteWord(LowWram, test_offset, DYNAREC_EXEC_TEST_OPCODE);
   T2WriteWord(LowWram, test_offset + 2, DYNAREC_EXEC_TEST_PAD);

   memset(master_reg, 0, DYNAREC_MASTER_REG_COUNT * sizeof(master_reg[0]));
   master_reg[0] = (int)DYNAREC_EXEC_TEST_R0_BEFORE;
   master_cc = 0;
   master_pc = (int)test_pc;
   CurrentSH2 = MSH2;

   fprintf(file,
           "exec-test-setup pc=%08x opcode=%04x r0_before=%08x saved=%04x,%04x\n",
           (unsigned)test_pc, (unsigned)DYNAREC_EXEC_TEST_OPCODE,
           (unsigned)master_reg[0], (unsigned)saved_word0,
           (unsigned)saved_word1);
   fflush(file);

   vita_dynarec_exec_test_active = 1;
   smoke_log(file, "stage=exec-compile-begin");
   compile_rc = sh2_recompile_block((int)test_pc);
   vita_dynarec_exec_test_active = 0;

   fprintf(file, "stage=exec-compile-end rc=%d write_depth=%u\n",
           compile_rc, vita_dynarec_vm_write_depth());
   fflush(file);

   if (compile_rc != 0) {
      smoke_log(file, "SMOKE_EXEC_FAIL reason=compile");
      goto restore_source;
   }

   if (vita_dynarec_vm_write_depth() != 0) {
      smoke_log(file, "SMOKE_EXEC_FAIL reason=unbalanced-write-transaction");
      goto restore_source;
   }

   entry = get_addr_ht(test_pc);
   entry_address = (uintptr_t)entry;
   fprintf(file,
           "stage=exec-entry entry=%08x cache_begin=%08x cache_end=%08x\n",
           (unsigned)entry_address, (unsigned)base, (unsigned)code_limit);
   fflush(file);

   if (!entry || entry_address < base || entry_address >= code_limit) {
      smoke_log(file, "SMOKE_EXEC_FAIL reason=entry-outside-vm-cache");
      goto restore_source;
   }

   /* No generated code may execute while the Vita VM write domain is open. */
   if (vita_dynarec_vm_write_depth() != 0) {
      smoke_log(file, "SMOKE_EXEC_FAIL reason=execute-while-writable");
      goto restore_source;
   }

   fprintf(file, "stage=exec-enter entry=%08x r0=%08x cc=%d\n",
           (unsigned)entry_address, (unsigned)master_reg[0], master_cc);
   fflush(file);

   /* The generated block has been forced to exactly one SH2 instruction.  Its
    * fallthrough branches to vita_dynarec_test_return instead of the normal
    * dynamic linker, so this call must return after MOV #42,R0 executes. */
   vita_dynarec_test_enter(entry);

   fprintf(file, "stage=exec-return r0=%08x cc=%d pc_shadow=%08x\n",
           (unsigned)master_reg[0], master_cc, (unsigned)master_pc);
   fflush(file);

   if ((u32)master_reg[0] != 42u) {
      smoke_log(file, "SMOKE_EXEC_FAIL reason=r0-mismatch expected=0000002a");
      goto restore_source;
   }

   smoke_log(file, "SMOKE_EXEC_PASS instruction=MOV-immediate expected_r0=0000002a");
   result = 0;

restore_source:
   vita_dynarec_exec_test_active = 0;
   T2WriteWord(LowWram, test_offset, saved_word0);
   T2WriteWord(LowWram, test_offset + 2, saved_word1);
   smoke_log(file, "stage=exec-source-restored");
   return result;
}

static void run_dynarec_compile_smoke(void)
{
   FILE *file;
   u32 pc;
   int compile_rc;
   void *entry = NULL;
   uintptr_t base;
   uintptr_t entry_address;
   uintptr_t code_limit;

   file = fopen(DYNAREC_SMOKE_LOG, "w");
   if (!file)
      return;

   smoke_log(file, "test=ari64-production-smoke revision=2 mode=compile-plus-bounded-exec generated_execution=CONTROLLED_SINGLE_INSTRUCTION");

   if (!MSH2 || !MSH2->core || !MSH2->core->GetPC) {
      smoke_log(file, "SMOKE_FAIL reason=master-sh2-not-ready");
      fclose(file);
      return;
   }

   pc = MSH2->core->GetPC(MSH2);
   fprintf(file, "interpreter_pc=%08x\n", (unsigned)pc);
   fflush(file);

   smoke_log(file, "stage=vm-init-begin");
   sh2_dynarec_init();
   if (!sh2_dynarec_target) {
      smoke_log(file, "SMOKE_FAIL reason=vm-init");
      fclose(file);
      return;
   }

   base = (uintptr_t)sh2_dynarec_target;
   code_limit = base + VITA_DYNAREC_CACHE_BYTES - VITA_DYNAREC_VENEER_BYTES;
   fprintf(file, "stage=vm-init-pass base=%08x bytes=%u write_depth=%u\n",
           (unsigned)base, (unsigned)VITA_DYNAREC_CACHE_BYTES,
           vita_dynarec_vm_write_depth());
   fflush(file);

   /* Keep the existing production compiler gate: first compile one real block
    * at the interpreter's current master-SH2 PC without executing it. */
   smoke_log(file, "stage=compile-begin");
   compile_rc = sh2_recompile_block((int)pc);
   fprintf(file, "stage=compile-end rc=%d write_depth=%u\n",
           compile_rc, vita_dynarec_vm_write_depth());
   fflush(file);

   if (compile_rc != 0) {
      smoke_log(file, "SMOKE_FAIL reason=compile");
      sh2_dynarec_cleanup();
      fclose(file);
      return;
   }

   if (vita_dynarec_vm_write_depth() != 0) {
      smoke_log(file, "SMOKE_FAIL reason=unbalanced-write-transaction");
      sh2_dynarec_cleanup();
      fclose(file);
      return;
   }

   smoke_log(file, "stage=entry-lookup-begin");
   entry = get_addr_ht(pc);
   entry_address = (uintptr_t)entry;
   fprintf(file, "stage=entry-lookup-end entry=%08x cache_begin=%08x cache_end=%08x write_depth=%u\n",
           (unsigned)entry_address, (unsigned)base, (unsigned)code_limit,
           vita_dynarec_vm_write_depth());
   fflush(file);

   if (!entry || entry_address < base || entry_address >= code_limit) {
      smoke_log(file, "SMOKE_FAIL reason=entry-outside-vm-cache");
      sh2_dynarec_cleanup();
      fclose(file);
      return;
   }

   smoke_log(file, "SMOKE_COMPILE_PASS");

   if (run_dynarec_exec_smoke(file, base, code_limit) != 0)
      smoke_log(file, "SMOKE_EXEC_RESULT FAIL interpreter-boot-will-continue-if-control-returned");
   else
      smoke_log(file, "SMOKE_EXEC_RESULT PASS");

   smoke_log(file, "stage=cleanup-begin");
   sh2_dynarec_cleanup();
   fprintf(file, "stage=cleanup-end base=%08x write_depth=%u\n",
           (unsigned)(uintptr_t)sh2_dynarec_target,
           vita_dynarec_vm_write_depth());
   smoke_log(file, "SMOKE_DONE interpreter-boot-continues");
   fclose(file);
}

int __wrap_YabauseInit(yabauseinit_struct *init)
{
   int rc = __real_YabauseInit(init);
   if (rc == 0)
      run_dynarec_compile_smoke();
   return rc;
}
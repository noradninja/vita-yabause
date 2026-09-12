#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../memory.h"
#include "../sh2core.h"
#include "../sh2_dynarec/sh2_dynarec.h"
#include "../yabause.h"
#include "vita_dynarec_vm.h"

#define DYNAREC_SMOKE_LOG "ux0:data/yabause/dynarec-runtime.log"
#define DYNAREC_MASTER_REG_COUNT 22u
#define DYNAREC_MAX_TEST_WORDS 16u
#define DYNAREC_EXEC_TEST_CC_START (-1048576)

#define DYNAREC_STRAIGHT_PC 0x002FFE00u
#define DYNAREC_BRANCH_PC   0x002FFF00u

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

/* Read by the Vita-only generated Ari64 translation unit while compiling a
 * bounded synthetic block. Normal production-smoke compilation leaves this
 * disabled and follows the unmodified Ari64 linker path. */
int vita_dynarec_exec_test_active;
unsigned int vita_dynarec_exec_test_instruction_limit;

static const u16 straight_line_program[] = {
   0xE005u, /* MOV #5,R0 */
   0x7007u, /* ADD #7,R0 */
   0x6103u  /* MOV R0,R1 */
};

/* This deliberately makes the taken/not-taken outcomes observably different.
 * If BT is taken, R1 becomes 42. If BT incorrectly falls through, R1 becomes
 * 99 and the following BRA skips the target assignment. */
static const u16 branch_program[] = {
   0xE001u, /* MOV #1,R0 */
   0x8801u, /* CMP/EQ #1,R0 */
   0x8902u, /* BT target (+2 => address +0x0c) */
   0xE163u, /* MOV #99,R1 -- not-taken path */
   0xA001u, /* BRA end (+1 => address +0x0e) */
   0x0009u, /* NOP -- BRA delay slot */
   0xE12Au, /* target: MOV #42,R1 */
   0x0009u  /* end: NOP */
};

static void smoke_log(FILE *file, const char *message)
{
   if (!file)
      return;
   fprintf(file, "%s\n", message);
   fflush(file);
}

static int run_bounded_program(FILE *file,
                               const char *name,
                               u32 test_pc,
                               const u16 *program,
                               unsigned int instruction_count,
                               u32 initial_r0,
                               u32 initial_r1,
                               u32 expected_r0,
                               u32 expected_r1,
                               uintptr_t base,
                               uintptr_t code_limit)
{
   u16 saved[DYNAREC_MAX_TEST_WORDS];
   u32 test_offset = test_pc & 0xFFFFFu;
   unsigned int i;
   int compile_rc;
   int result = -1;
   void *entry = NULL;
   uintptr_t entry_address = 0;

   if (!LowWram) {
      fprintf(file, "SMOKE_EXEC_FAIL test=%s reason=low-wram-not-ready\n", name);
      fflush(file);
      return -1;
   }

   if (!program || instruction_count == 0 ||
       instruction_count > DYNAREC_MAX_TEST_WORDS) {
      fprintf(file, "SMOKE_EXEC_FAIL test=%s reason=invalid-test-program count=%u\n",
              name, instruction_count);
      fflush(file);
      return -1;
   }

   for (i = 0; i < instruction_count; ++i) {
      saved[i] = T2ReadWord(LowWram, test_offset + i * 2u);
      T2WriteWord(LowWram, test_offset + i * 2u, program[i]);
   }

   memset(master_reg, 0, DYNAREC_MASTER_REG_COUNT * sizeof(master_reg[0]));
   master_reg[0] = (int)initial_r0;
   master_reg[1] = (int)initial_r1;
   /* Ari64 uses ARM r10 as the live cycle counter. Give the bounded test a
    * comfortably negative budget so short synthetic blocks cannot enter the
    * normal scheduler/CC stub path. */
   master_cc = DYNAREC_EXEC_TEST_CC_START;
   master_pc = (int)test_pc;
   CurrentSH2 = MSH2;

   fprintf(file,
           "exec-test-setup test=%s pc=%08x instructions=%u r0=%08x r1=%08x cc_start=%d\n",
           name, (unsigned)test_pc, instruction_count,
           (unsigned)master_reg[0], (unsigned)master_reg[1], master_cc);
   fflush(file);

   vita_dynarec_exec_test_instruction_limit = instruction_count;
   vita_dynarec_exec_test_active = 1;

   fprintf(file, "stage=exec-compile-begin test=%s\n", name);
   fflush(file);
   compile_rc = sh2_recompile_block((int)test_pc);

   fprintf(file, "stage=exec-compile-end test=%s rc=%d write_depth=%u\n",
           name, compile_rc, vita_dynarec_vm_write_depth());
   fflush(file);

   if (compile_rc != 0) {
      fprintf(file, "SMOKE_EXEC_FAIL test=%s reason=compile\n", name);
      fflush(file);
      goto restore_source;
   }

   if (vita_dynarec_vm_write_depth() != 0) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=unbalanced-write-transaction\n",
              name);
      fflush(file);
      goto restore_source;
   }

   /* Keep the bounded compile mode active through lookup in case get_addr_ht
    * has to recover/recompile the entry rather than hitting the fresh block. */
   entry = get_addr_ht(test_pc);
   entry_address = (uintptr_t)entry;
   fprintf(file,
           "stage=exec-entry test=%s entry=%08x cache_begin=%08x cache_end=%08x\n",
           name, (unsigned)entry_address, (unsigned)base,
           (unsigned)code_limit);
   fflush(file);

   vita_dynarec_exec_test_active = 0;
   vita_dynarec_exec_test_instruction_limit = 0;

   if (!entry || entry_address < base || entry_address >= code_limit) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=entry-outside-vm-cache\n",
              name);
      fflush(file);
      goto restore_source;
   }

   if (vita_dynarec_vm_write_depth() != 0) {
      fprintf(file, "SMOKE_EXEC_FAIL test=%s reason=execute-while-writable\n",
              name);
      fflush(file);
      goto restore_source;
   }

   fprintf(file,
           "stage=exec-enter test=%s entry=%08x r0=%08x r1=%08x cc=%d\n",
           name, (unsigned)entry_address, (unsigned)master_reg[0],
           (unsigned)master_reg[1], master_cc);
   fflush(file);

   /* The production dispatcher enters generated code with r10 containing the
    * live SH2 cycle counter. The bounded trampoline preserves r10 but does not
    * manufacture it, so seed the ABI register explicitly before the call. */
   {
      register int ari64_cc __asm__("r10") = master_cc;
      __asm__ volatile("" : : "r"(ari64_cc) : "memory");
      vita_dynarec_test_enter(entry);
   }

   fprintf(file,
           "stage=exec-return test=%s r0=%08x r1=%08x cc=%d pc_shadow=%08x\n",
           name, (unsigned)master_reg[0], (unsigned)master_reg[1],
           master_cc, (unsigned)master_pc);
   fflush(file);

   if (master_cc >= 0) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=cycle-budget-crossed cc=%d\n",
              name, master_cc);
      fflush(file);
      goto restore_source;
   }

   if ((u32)master_reg[0] != expected_r0) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=r0-mismatch expected=%08x actual=%08x\n",
              name, (unsigned)expected_r0, (unsigned)master_reg[0]);
      fflush(file);
      goto restore_source;
   }

   if ((u32)master_reg[1] != expected_r1) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=r1-mismatch expected=%08x actual=%08x\n",
              name, (unsigned)expected_r1, (unsigned)master_reg[1]);
      fflush(file);
      goto restore_source;
   }

   fprintf(file,
           "SMOKE_EXEC_PASS test=%s expected_r0=%08x expected_r1=%08x cc=%d\n",
           name, (unsigned)expected_r0, (unsigned)expected_r1, master_cc);
   fflush(file);
   result = 0;

restore_source:
   vita_dynarec_exec_test_active = 0;
   vita_dynarec_exec_test_instruction_limit = 0;
   for (i = 0; i < instruction_count; ++i)
      T2WriteWord(LowWram, test_offset + i * 2u, saved[i]);
   fprintf(file, "stage=exec-source-restored test=%s\n", name);
   fflush(file);
   return result;
}

static void run_dynarec_compile_smoke(void)
{
   FILE *file;
   u32 pc;
   int compile_rc;
   int straight_rc;
   int branch_rc;
   void *entry = NULL;
   uintptr_t base;
   uintptr_t entry_address;
   uintptr_t code_limit;

   file = fopen(DYNAREC_SMOKE_LOG, "w");
   if (!file)
      return;

   smoke_log(file, "test=ari64-production-smoke revision=4 mode=compile-plus-bounded-blocks generated_execution=STRAIGHT_LINE_AND_BRANCH cycle_register=INITIALIZED");

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

   /* Preserve the existing production compiler gate first: compile one real
    * block at the interpreter's current master-SH2 PC without executing it. */
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
   fprintf(file,
           "stage=entry-lookup-end entry=%08x cache_begin=%08x cache_end=%08x write_depth=%u\n",
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

   straight_rc = run_bounded_program(file,
                                     "straight-line",
                                     DYNAREC_STRAIGHT_PC,
                                     straight_line_program,
                                     sizeof(straight_line_program) /
                                        sizeof(straight_line_program[0]),
                                     0x13579BDFu,
                                     0x2468ACE0u,
                                     12u,
                                     12u,
                                     base,
                                     code_limit);

   branch_rc = run_bounded_program(file,
                                   "conditional-branch-taken",
                                   DYNAREC_BRANCH_PC,
                                   branch_program,
                                   sizeof(branch_program) /
                                      sizeof(branch_program[0]),
                                   0xAAAAAAAAu,
                                   0xBBBBBBBBu,
                                   1u,
                                   42u,
                                   base,
                                   code_limit);

   if (straight_rc == 0 && branch_rc == 0)
      smoke_log(file, "SMOKE_EXEC_RESULT PASS tests=straight-line,conditional-branch-taken");
   else
      smoke_log(file, "SMOKE_EXEC_RESULT FAIL interpreter-boot-will-continue-if-control-returned");

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

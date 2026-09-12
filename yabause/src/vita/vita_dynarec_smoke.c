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
#define DYNAREC_LINK_A_PC   0x002FFE80u
#define DYNAREC_LINK_B_PC   0x002FFEA0u
#define DYNAREC_MEMORY_PC   0x002FFEC0u
#define DYNAREC_BRANCH_PC   0x002FFF00u
#define DYNAREC_MEMORY_ADDR 0x002E0000u

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

/* Block A exits to a separately compiled block B. B is compiled first so the
 * normal Ari64 external-link path can resolve A's BRA directly to B. */
static const u16 linked_block_a_program[] = {
   0xE005u, /* MOV #5,R0 */
   0x7001u, /* ADD #1,R0 */
   0xA00Cu, /* BRA 0x002FFEA0 */
   0x0009u  /* NOP -- delay slot */
};

static const u16 linked_block_b_program[] = {
   0x700Au, /* ADD #10,R0 */
   0x6103u  /* MOV R0,R1 */
};

/* R1 contains a Low WRAM address and R0 contains the test value. */
static const u16 memory_program[] = {
   0x2102u, /* MOV.L R0,@R1 */
   0x6212u  /* MOV.L @R1,R2 */
};

static void smoke_log(FILE *file, const char *message)
{
   if (!file)
      return;
   fprintf(file, "%s\n", message);
   fflush(file);
}

static void reset_exec_test_controls(void)
{
   vita_dynarec_exec_test_active = 0;
   vita_dynarec_exec_test_instruction_limit = 0;
}

static int execute_entry(FILE *file,
                         const char *name,
                         void *entry,
                         uintptr_t base,
                         uintptr_t code_limit)
{
   uintptr_t entry_address = (uintptr_t)entry;

   if (!entry || entry_address < base || entry_address >= code_limit) {
      fprintf(file, "SMOKE_EXEC_FAIL test=%s reason=entry-outside-vm-cache\n", name);
      fflush(file);
      return -1;
   }

   if (vita_dynarec_vm_write_depth() != 0) {
      fprintf(file, "SMOKE_EXEC_FAIL test=%s reason=execute-while-writable\n", name);
      fflush(file);
      return -1;
   }

   fprintf(file,
           "stage=exec-enter test=%s entry=%08x r0=%08x r1=%08x r2=%08x cc=%d\n",
           name, (unsigned)entry_address, (unsigned)master_reg[0],
           (unsigned)master_reg[1], (unsigned)master_reg[2], master_cc);
   fflush(file);

   {
      register int ari64_cc __asm__("r10") = master_cc;
      __asm__ volatile("" : : "r"(ari64_cc) : "memory");
      vita_dynarec_test_enter(entry);
   }

   fprintf(file,
           "stage=exec-return test=%s r0=%08x r1=%08x r2=%08x cc=%d pc_shadow=%08x\n",
           name, (unsigned)master_reg[0], (unsigned)master_reg[1],
           (unsigned)master_reg[2], master_cc, (unsigned)master_pc);
   fflush(file);

   if (master_cc >= 0) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=cycle-budget-crossed cc=%d\n",
              name, master_cc);
      fflush(file);
      return -1;
   }

   return 0;
}

static int compile_bounded_block(FILE *file,
                                 const char *name,
                                 u32 test_pc,
                                 const u16 *program,
                                 unsigned int instruction_count,
                                 void **entry_out,
                                 uintptr_t base,
                                 uintptr_t code_limit)
{
   u32 test_offset = test_pc & 0xFFFFFu;
   unsigned int i;
   int compile_rc;
   void *entry;
   uintptr_t entry_address;

   if (!LowWram || !program || instruction_count == 0 ||
       instruction_count > DYNAREC_MAX_TEST_WORDS)
      return -1;

   for (i = 0; i < instruction_count; ++i)
      T2WriteWord(LowWram, test_offset + i * 2u, program[i]);

   vita_dynarec_exec_test_instruction_limit = instruction_count;
   vita_dynarec_exec_test_active = 1;

   fprintf(file,
           "stage=exec-compile-begin test=%s pc=%08x instructions=%u\n",
           name, (unsigned)test_pc, instruction_count);
   fflush(file);

   compile_rc = sh2_recompile_block((int)test_pc);
   fprintf(file, "stage=exec-compile-end test=%s rc=%d write_depth=%u\n",
           name, compile_rc, vita_dynarec_vm_write_depth());
   fflush(file);

   if (compile_rc != 0 || vita_dynarec_vm_write_depth() != 0) {
      reset_exec_test_controls();
      return -1;
   }

   /* Keep bounded mode active through lookup in case get_addr_ht has to
    * recover/recompile the freshly emitted block. */
   entry = get_addr_ht(test_pc);
   entry_address = (uintptr_t)entry;
   fprintf(file,
           "stage=exec-entry test=%s entry=%08x cache_begin=%08x cache_end=%08x write_depth=%u\n",
           name, (unsigned)entry_address, (unsigned)base,
           (unsigned)code_limit, vita_dynarec_vm_write_depth());
   fflush(file);

   reset_exec_test_controls();

   if (!entry || entry_address < base || entry_address >= code_limit)
      return -1;

   if (entry_out)
      *entry_out = entry;
   return 0;
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
   int result = -1;
   void *entry = NULL;

   if (!LowWram || !program || instruction_count == 0 ||
       instruction_count > DYNAREC_MAX_TEST_WORDS)
      return -1;

   for (i = 0; i < instruction_count; ++i)
      saved[i] = T2ReadWord(LowWram, test_offset + i * 2u);

   memset(master_reg, 0, DYNAREC_MASTER_REG_COUNT * sizeof(master_reg[0]));
   master_reg[0] = (int)initial_r0;
   master_reg[1] = (int)initial_r1;
   master_cc = DYNAREC_EXEC_TEST_CC_START;
   master_pc = (int)test_pc;
   CurrentSH2 = MSH2;

   if (compile_bounded_block(file, name, test_pc, program, instruction_count,
                             &entry, base, code_limit) != 0)
      goto restore_source;

   if (execute_entry(file, name, entry, base, code_limit) != 0)
      goto restore_source;

   if ((u32)master_reg[0] != expected_r0 ||
       (u32)master_reg[1] != expected_r1) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=register-mismatch expected_r0=%08x actual_r0=%08x expected_r1=%08x actual_r1=%08x\n",
              name, (unsigned)expected_r0, (unsigned)master_reg[0],
              (unsigned)expected_r1, (unsigned)master_reg[1]);
      fflush(file);
      goto restore_source;
   }

   fprintf(file,
           "SMOKE_EXEC_PASS test=%s expected_r0=%08x expected_r1=%08x cc=%d\n",
           name, (unsigned)expected_r0, (unsigned)expected_r1, master_cc);
   fflush(file);
   result = 0;

restore_source:
   reset_exec_test_controls();
   for (i = 0; i < instruction_count; ++i)
      T2WriteWord(LowWram, test_offset + i * 2u, saved[i]);
   fprintf(file, "stage=exec-source-restored test=%s\n", name);
   fflush(file);
   return result;
}

static int run_linked_blocks(FILE *file, uintptr_t base, uintptr_t code_limit)
{
   u16 saved_a[sizeof(linked_block_a_program) / sizeof(linked_block_a_program[0])];
   u16 saved_b[sizeof(linked_block_b_program) / sizeof(linked_block_b_program[0])];
   unsigned int i;
   int result = -1;
   void *entry_a = NULL;
   void *entry_b = NULL;

   for (i = 0; i < sizeof(saved_a) / sizeof(saved_a[0]); ++i)
      saved_a[i] = T2ReadWord(LowWram, (DYNAREC_LINK_A_PC & 0xFFFFFu) + i * 2u);
   for (i = 0; i < sizeof(saved_b) / sizeof(saved_b[0]); ++i)
      saved_b[i] = T2ReadWord(LowWram, (DYNAREC_LINK_B_PC & 0xFFFFFu) + i * 2u);

   /* Compile B first. Its normal fallthrough is bounded by the existing test
    * return hook. A ends in a real external BRA, so Ari64's branch/link path
    * remains untouched and resolves that target to the already compiled B. */
   if (compile_bounded_block(file, "linked-block-b", DYNAREC_LINK_B_PC,
                             linked_block_b_program,
                             sizeof(linked_block_b_program) / sizeof(linked_block_b_program[0]),
                             &entry_b, base, code_limit) != 0)
      goto restore;

   if (compile_bounded_block(file, "linked-block-a", DYNAREC_LINK_A_PC,
                             linked_block_a_program,
                             sizeof(linked_block_a_program) / sizeof(linked_block_a_program[0]),
                             &entry_a, base, code_limit) != 0)
      goto restore;

   fprintf(file,
           "stage=linked-blocks-ready entry_a=%08x entry_b=%08x target_pc=%08x write_depth=%u\n",
           (unsigned)(uintptr_t)entry_a, (unsigned)(uintptr_t)entry_b,
           DYNAREC_LINK_B_PC, vita_dynarec_vm_write_depth());
   fflush(file);

   memset(master_reg, 0, DYNAREC_MASTER_REG_COUNT * sizeof(master_reg[0]));
   master_cc = DYNAREC_EXEC_TEST_CC_START;
   master_pc = (int)DYNAREC_LINK_A_PC;
   CurrentSH2 = MSH2;

   if (execute_entry(file, "external-link-a-to-b", entry_a, base, code_limit) != 0)
      goto restore;

   if ((u32)master_reg[0] != 16u || (u32)master_reg[1] != 16u) {
      fprintf(file,
              "SMOKE_LINK_FAIL expected_r0=00000010 actual_r0=%08x expected_r1=00000010 actual_r1=%08x\n",
              (unsigned)master_reg[0], (unsigned)master_reg[1]);
      fflush(file);
      goto restore;
   }

   smoke_log(file, "SMOKE_LINK_PASS blocks=A->B expected_r0=00000010 expected_r1=00000010");
   result = 0;

restore:
   reset_exec_test_controls();
   for (i = 0; i < sizeof(saved_a) / sizeof(saved_a[0]); ++i)
      T2WriteWord(LowWram, (DYNAREC_LINK_A_PC & 0xFFFFFu) + i * 2u, saved_a[i]);
   for (i = 0; i < sizeof(saved_b) / sizeof(saved_b[0]); ++i)
      T2WriteWord(LowWram, (DYNAREC_LINK_B_PC & 0xFFFFFu) + i * 2u, saved_b[i]);
   smoke_log(file, "stage=linked-block-source-restored");
   return result;
}

static int run_memory_test(FILE *file, uintptr_t base, uintptr_t code_limit)
{
   u16 saved_program[sizeof(memory_program) / sizeof(memory_program[0])];
   u32 program_offset = DYNAREC_MEMORY_PC & 0xFFFFFu;
   u32 data_offset = DYNAREC_MEMORY_ADDR & 0xFFFFFu;
   u32 saved_data;
   unsigned int i;
   int result = -1;
   void *entry = NULL;

   for (i = 0; i < sizeof(saved_program) / sizeof(saved_program[0]); ++i)
      saved_program[i] = T2ReadWord(LowWram, program_offset + i * 2u);
   saved_data = T2ReadLong(LowWram, data_offset);

   memset(master_reg, 0, DYNAREC_MASTER_REG_COUNT * sizeof(master_reg[0]));
   master_reg[0] = (int)0x12345678u;
   master_reg[1] = (int)DYNAREC_MEMORY_ADDR;
   master_reg[2] = 0;
   master_cc = DYNAREC_EXEC_TEST_CC_START;
   master_pc = (int)DYNAREC_MEMORY_PC;
   CurrentSH2 = MSH2;

   if (compile_bounded_block(file, "low-wram-load-store", DYNAREC_MEMORY_PC,
                             memory_program,
                             sizeof(memory_program) / sizeof(memory_program[0]),
                             &entry, base, code_limit) != 0)
      goto restore;

   if (execute_entry(file, "low-wram-load-store", entry, base, code_limit) != 0)
      goto restore;

   fprintf(file,
           "stage=memory-check addr=%08x memory=%08x r2=%08x\n",
           DYNAREC_MEMORY_ADDR,
           (unsigned)T2ReadLong(LowWram, data_offset),
           (unsigned)master_reg[2]);
   fflush(file);

   if (T2ReadLong(LowWram, data_offset) != 0x12345678u ||
       (u32)master_reg[2] != 0x12345678u) {
      smoke_log(file, "SMOKE_MEMORY_FAIL expected=12345678");
      goto restore;
   }

   smoke_log(file, "SMOKE_MEMORY_PASS address=002E0000 value=12345678");
   result = 0;

restore:
   reset_exec_test_controls();
   for (i = 0; i < sizeof(saved_program) / sizeof(saved_program[0]); ++i)
      T2WriteWord(LowWram, program_offset + i * 2u, saved_program[i]);
   T2WriteLong(LowWram, data_offset, saved_data);
   smoke_log(file, "stage=memory-source-restored");
   return result;
}

static void run_dynarec_compile_smoke(void)
{
   FILE *file;
   u32 pc;
   int compile_rc;
   int straight_rc;
   int branch_rc;
   int link_rc;
   int memory_rc;
   void *entry = NULL;
   uintptr_t base;
   uintptr_t entry_address;
   uintptr_t code_limit;

   file = fopen(DYNAREC_SMOKE_LOG, "w");
   if (!file)
      return;

   smoke_log(file, "test=ari64-production-smoke revision=5 mode=compile-plus-bounded-runtime generated_execution=STRAIGHT_LINE_BRANCH_EXTERNAL_LINK_MEMORY cycle_register=INITIALIZED");

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

   smoke_log(file, "stage=compile-begin");
   compile_rc = sh2_recompile_block((int)pc);
   fprintf(file, "stage=compile-end rc=%d write_depth=%u\n",
           compile_rc, vita_dynarec_vm_write_depth());
   fflush(file);

   if (compile_rc != 0 || vita_dynarec_vm_write_depth() != 0) {
      smoke_log(file, "SMOKE_FAIL reason=compile-or-write-depth");
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
                                     0x13579BDFu, 0x2468ACE0u,
                                     12u, 12u, base, code_limit);

   branch_rc = run_bounded_program(file,
                                   "conditional-branch-taken",
                                   DYNAREC_BRANCH_PC,
                                   branch_program,
                                   sizeof(branch_program) /
                                      sizeof(branch_program[0]),
                                   0xAAAAAAAAu, 0xBBBBBBBBu,
                                   1u, 42u, base, code_limit);

   link_rc = run_linked_blocks(file, base, code_limit);
   memory_rc = run_memory_test(file, base, code_limit);

   if (straight_rc == 0 && branch_rc == 0 && link_rc == 0 && memory_rc == 0)
      smoke_log(file, "SMOKE_EXEC_RESULT PASS tests=straight-line,conditional-branch-taken,external-link-a-to-b,low-wram-load-store");
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

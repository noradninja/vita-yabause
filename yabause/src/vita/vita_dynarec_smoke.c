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

#define DYNAREC_INVALIDATE_A_PC   0x002FC000u
#define DYNAREC_INVALIDATE_B_PC   0x002FD000u
#define DYNAREC_RUNTIME_LINK_A_PC 0x002FFD80u
#define DYNAREC_RUNTIME_LINK_B_PC 0x002FFDA0u
#define DYNAREC_STRAIGHT_PC       0x002FFE00u
#define DYNAREC_LINK_A_PC         0x002FFE80u
#define DYNAREC_LINK_B_PC         0x002FFEA0u
#define DYNAREC_MEMORY_PC         0x002FFEC0u
#define DYNAREC_BRANCH_PC         0x002FFF00u
#define DYNAREC_MEMORY_ADDR       0x002E0000u

extern int __real_YabauseInit(yabauseinit_struct *init);
extern int sh2_recompile_block(int addr);
extern void *get_addr_ht(u32 vaddr);
extern void sh2_dynarec_cleanup(void);
extern void SH2DynarecWriteNotify(u32 start, u32 length);
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

/* Taken and fallthrough paths deliberately produce different values. */
static const u16 branch_program[] = {
   0xE001u, /* MOV #1,R0 */
   0x8801u, /* CMP/EQ #1,R0 */
   0x8902u, /* BT target */
   0xE163u, /* MOV #99,R1 */
   0xA001u, /* BRA end */
   0x0009u, /* NOP -- delay slot */
   0xE12Au, /* target: MOV #42,R1 */
   0x0009u  /* end: NOP */
};

/* Compile-time external link pair. */
static const u16 linked_block_a_program[] = {
   0xE005u, /* MOV #5,R0 */
   0x7001u, /* ADD #1,R0 */
   0xA00Cu, /* BRA ...A0 */
   0x0009u  /* NOP -- delay slot */
};

static const u16 linked_block_b_program[] = {
   0x700Au, /* ADD #10,R0 */
   0x6103u  /* MOV R0,R1 */
};

/* Runtime unresolved-link pair. */
static const u16 runtime_link_block_a_program[] = {
   0xE005u,
   0x7001u,
   0xA00Cu, /* BRA 0x002FFDA0 */
   0x0009u
};

static const u16 runtime_link_block_b_program[] = {
   0x700Au,
   0x6103u
};

/* Invalidation pair lives on separate 4 KiB Low WRAM pages.  A's BRA at
 * 0x002FC004 uses disp=0x7FC, so PC+4+disp*2 lands at 0x002FD000. */
static const u16 invalidate_block_a_program[] = {
   0xE005u, /* MOV #5,R0 */
   0x7001u, /* ADD #1,R0 */
   0xA7FCu, /* BRA 0x002FD000 */
   0x0009u  /* NOP -- delay slot */
};

static const u16 invalidate_block_b_v1_program[] = {
   0x700Au, /* ADD #10,R0 -> result 16 */
   0x6103u
};

static const u16 invalidate_block_b_v2_program[] = {
   0x7014u, /* ADD #20,R0 -> result 26 */
   0x6103u
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

static void reset_master_state(u32 pc)
{
   memset(master_reg, 0, DYNAREC_MASTER_REG_COUNT * sizeof(master_reg[0]));
   master_cc = DYNAREC_EXEC_TEST_CC_START;
   master_pc = (int)pc;
   CurrentSH2 = MSH2;
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

   if (vita_dynarec_vm_write_depth() != 0 ||
       vita_dynarec_implicit_patch_active()) {
      fprintf(file,
              "SMOKE_EXEC_FAIL test=%s reason=execute-while-writable depth=%u implicit=%d\n",
              name, vita_dynarec_vm_write_depth(),
              vita_dynarec_implicit_patch_active());
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

   reset_master_state(test_pc);
   master_reg[0] = (int)initial_r0;
   master_reg[1] = (int)initial_r1;

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

   reset_master_state(DYNAREC_LINK_A_PC);

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

static int run_runtime_link_test(FILE *file, uintptr_t base, uintptr_t code_limit)
{
   u16 saved_a[sizeof(runtime_link_block_a_program) / sizeof(runtime_link_block_a_program[0])];
   u16 saved_b[sizeof(runtime_link_block_b_program) / sizeof(runtime_link_block_b_program[0])];
   unsigned int i;
   unsigned int patches_before;
   unsigned int patches_after;
   int result = -1;
   void *entry_a = NULL;
   void *entry_b;

   for (i = 0; i < sizeof(saved_a) / sizeof(saved_a[0]); ++i)
      saved_a[i] = T2ReadWord(LowWram, (DYNAREC_RUNTIME_LINK_A_PC & 0xFFFFFu) + i * 2u);
   for (i = 0; i < sizeof(saved_b) / sizeof(saved_b[0]); ++i) {
      u32 offset = (DYNAREC_RUNTIME_LINK_B_PC & 0xFFFFFu) + i * 2u;
      saved_b[i] = T2ReadWord(LowWram, offset);
      T2WriteWord(LowWram, offset, runtime_link_block_b_program[i]);
   }

   if (compile_bounded_block(file, "runtime-link-a", DYNAREC_RUNTIME_LINK_A_PC,
                             runtime_link_block_a_program,
                             sizeof(runtime_link_block_a_program) /
                                sizeof(runtime_link_block_a_program[0]),
                             &entry_a, base, code_limit) != 0)
      goto restore;

   reset_master_state(DYNAREC_RUNTIME_LINK_A_PC);
   patches_before = vita_dynarec_runtime_patch_count();

   vita_dynarec_exec_test_instruction_limit =
      sizeof(runtime_link_block_b_program) / sizeof(runtime_link_block_b_program[0]);
   vita_dynarec_exec_test_active = 1;

   fprintf(file,
           "stage=runtime-link-enter entry_a=%08x unresolved_target=%08x patches_before=%u write_depth=%u\n",
           (unsigned)(uintptr_t)entry_a, DYNAREC_RUNTIME_LINK_B_PC,
           patches_before, vita_dynarec_vm_write_depth());
   fflush(file);

   if (execute_entry(file, "runtime-dyna-linker-a-to-b", entry_a,
                     base, code_limit) != 0)
      goto restore;

   reset_exec_test_controls();
   patches_after = vita_dynarec_runtime_patch_count();
   entry_b = get_addr_ht(DYNAREC_RUNTIME_LINK_B_PC);

   fprintf(file,
           "stage=runtime-link-return entry_b=%08x patches_after=%u r0=%08x r1=%08x write_depth=%u\n",
           (unsigned)(uintptr_t)entry_b, patches_after,
           (unsigned)master_reg[0], (unsigned)master_reg[1],
           vita_dynarec_vm_write_depth());
   fflush(file);

   if (!entry_b || (uintptr_t)entry_b < base || (uintptr_t)entry_b >= code_limit) {
      smoke_log(file, "SMOKE_RUNTIME_LINK_FAIL reason=target-not-compiled");
      goto restore;
   }
   if (patches_after <= patches_before) {
      smoke_log(file, "SMOKE_RUNTIME_LINK_FAIL reason=no-runtime-patch-observed");
      goto restore;
   }
   if (vita_dynarec_vm_write_depth() != 0) {
      smoke_log(file, "SMOKE_RUNTIME_LINK_FAIL reason=write-domain-left-open");
      goto restore;
   }
   if ((u32)master_reg[0] != 16u || (u32)master_reg[1] != 16u) {
      fprintf(file,
              "SMOKE_RUNTIME_LINK_FAIL reason=register-mismatch r0=%08x r1=%08x\n",
              (unsigned)master_reg[0], (unsigned)master_reg[1]);
      fflush(file);
      goto restore;
   }

   fprintf(file,
           "SMOKE_RUNTIME_LINK_PASS blocks=A->dyna_linker->B patches=%u expected_r0=00000010 expected_r1=00000010\n",
           patches_after - patches_before);
   fflush(file);
   result = 0;

restore:
   reset_exec_test_controls();
   for (i = 0; i < sizeof(saved_a) / sizeof(saved_a[0]); ++i)
      T2WriteWord(LowWram, (DYNAREC_RUNTIME_LINK_A_PC & 0xFFFFFu) + i * 2u, saved_a[i]);
   for (i = 0; i < sizeof(saved_b) / sizeof(saved_b[0]); ++i)
      T2WriteWord(LowWram, (DYNAREC_RUNTIME_LINK_B_PC & 0xFFFFFu) + i * 2u, saved_b[i]);
   smoke_log(file, "stage=runtime-link-source-restored");
   return result;
}

static int run_invalidation_relink_test(FILE *file,
                                        uintptr_t base,
                                        uintptr_t code_limit)
{
   u16 saved_a[sizeof(invalidate_block_a_program) / sizeof(invalidate_block_a_program[0])];
   u16 saved_b[sizeof(invalidate_block_b_v1_program) / sizeof(invalidate_block_b_v1_program[0])];
   unsigned int i;
   unsigned int publications_before;
   unsigned int publications_after;
   unsigned int patches_before;
   unsigned int patches_after;
   int result = -1;
   void *entry_a = NULL;
   void *entry_b = NULL;

   for (i = 0; i < sizeof(saved_a) / sizeof(saved_a[0]); ++i)
      saved_a[i] = T2ReadWord(LowWram, (DYNAREC_INVALIDATE_A_PC & 0xFFFFFu) + i * 2u);
   for (i = 0; i < sizeof(saved_b) / sizeof(saved_b[0]); ++i)
      saved_b[i] = T2ReadWord(LowWram, (DYNAREC_INVALIDATE_B_PC & 0xFFFFFu) + i * 2u);

   /* Compile B v1 first and then A so A starts directly linked to B. */
   if (compile_bounded_block(file, "invalidate-block-b-v1", DYNAREC_INVALIDATE_B_PC,
                             invalidate_block_b_v1_program,
                             sizeof(invalidate_block_b_v1_program) /
                                sizeof(invalidate_block_b_v1_program[0]),
                             &entry_b, base, code_limit) != 0)
      goto restore;

   if (compile_bounded_block(file, "invalidate-block-a", DYNAREC_INVALIDATE_A_PC,
                             invalidate_block_a_program,
                             sizeof(invalidate_block_a_program) /
                                sizeof(invalidate_block_a_program[0]),
                             &entry_a, base, code_limit) != 0)
      goto restore;

   reset_master_state(DYNAREC_INVALIDATE_A_PC);
   if (execute_entry(file, "invalidate-precheck-a-to-b-v1", entry_a,
                     base, code_limit) != 0)
      goto restore;

   if ((u32)master_reg[0] != 16u || (u32)master_reg[1] != 16u) {
      smoke_log(file, "SMOKE_INVALIDATE_FAIL reason=precheck-register-mismatch");
      goto restore;
   }

   /* Change B on its own Low WRAM page, then notify Ari64 exactly as a real
    * emulated write would. invalidate_page() must kill A's published direct
    * link.  That legacy set_jump_target() mutation is what exercises the new
    * implicit Vita VM write transaction. */
   for (i = 0; i < sizeof(invalidate_block_b_v2_program) /
                   sizeof(invalidate_block_b_v2_program[0]); ++i)
      T2WriteWord(LowWram,
                  (DYNAREC_INVALIDATE_B_PC & 0xFFFFFu) + i * 2u,
                  invalidate_block_b_v2_program[i]);

   publications_before = vita_dynarec_invalidation_publish_count();
   patches_before = vita_dynarec_runtime_patch_count();
   fprintf(file,
           "stage=invalidate-notify-begin pc=%08x publications_before=%u runtime_patches_before=%u write_depth=%u implicit=%d\n",
           DYNAREC_INVALIDATE_B_PC, publications_before, patches_before,
           vita_dynarec_vm_write_depth(), vita_dynarec_implicit_patch_active());
   fflush(file);

   SH2DynarecWriteNotify(DYNAREC_INVALIDATE_B_PC,
                         sizeof(invalidate_block_b_v2_program));

   publications_after = vita_dynarec_invalidation_publish_count();
   fprintf(file,
           "stage=invalidate-notify-end publications_after=%u write_depth=%u implicit=%d\n",
           publications_after, vita_dynarec_vm_write_depth(),
           vita_dynarec_implicit_patch_active());
   fflush(file);

   if (publications_after <= publications_before) {
      smoke_log(file, "SMOKE_INVALIDATE_FAIL reason=no-invalidation-publication");
      goto restore;
   }
   if (vita_dynarec_vm_write_depth() != 0 ||
       vita_dynarec_implicit_patch_active()) {
      smoke_log(file, "SMOKE_INVALIDATE_FAIL reason=write-domain-left-open");
      goto restore;
   }

   /* Re-enter A. The important correctness criteria are that invalidation was
    * published, the write domain closed, and execution observes B v2.  Ari64
    * may recover a dirty target without taking the counted dyna_linker store,
    * so runtime_patch_count is diagnostic here rather than a pass condition. */
   reset_master_state(DYNAREC_INVALIDATE_A_PC);
   vita_dynarec_exec_test_instruction_limit =
      sizeof(invalidate_block_b_v2_program) /
      sizeof(invalidate_block_b_v2_program[0]);
   vita_dynarec_exec_test_active = 1;

   if (execute_entry(file, "invalidate-relink-a-to-b-v2", entry_a,
                     base, code_limit) != 0)
      goto restore;

   reset_exec_test_controls();
   patches_after = vita_dynarec_runtime_patch_count();

   fprintf(file,
           "stage=invalidate-relink-return publications=%u runtime_patches_before=%u runtime_patches_after=%u r0=%08x r1=%08x write_depth=%u implicit=%d\n",
           publications_after - publications_before,
           patches_before, patches_after,
           (unsigned)master_reg[0], (unsigned)master_reg[1],
           vita_dynarec_vm_write_depth(), vita_dynarec_implicit_patch_active());
   fflush(file);

   if ((u32)master_reg[0] != 26u || (u32)master_reg[1] != 26u) {
      fprintf(file,
              "SMOKE_INVALIDATE_FAIL reason=stale-or-wrong-code expected=0000001a actual_r0=%08x actual_r1=%08x\n",
              (unsigned)master_reg[0], (unsigned)master_reg[1]);
      fflush(file);
      goto restore;
   }
   if (vita_dynarec_vm_write_depth() != 0 ||
       vita_dynarec_implicit_patch_active()) {
      smoke_log(file, "SMOKE_INVALIDATE_FAIL reason=relink-left-write-domain-open");
      goto restore;
   }

   fprintf(file,
           "SMOKE_INVALIDATE_PASS blocks=A->Bv1 invalidate=B recover=Bv2 publications=%u runtime_patch_delta=%u expected_r0=0000001a expected_r1=0000001a\n",
           publications_after - publications_before,
           patches_after - patches_before);
   fflush(file);
   result = 0;

restore:
   reset_exec_test_controls();
   for (i = 0; i < sizeof(saved_a) / sizeof(saved_a[0]); ++i)
      T2WriteWord(LowWram, (DYNAREC_INVALIDATE_A_PC & 0xFFFFFu) + i * 2u, saved_a[i]);
   for (i = 0; i < sizeof(saved_b) / sizeof(saved_b[0]); ++i)
      T2WriteWord(LowWram, (DYNAREC_INVALIDATE_B_PC & 0xFFFFFu) + i * 2u, saved_b[i]);
   smoke_log(file, "stage=invalidation-source-restored");
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

   reset_master_state(DYNAREC_MEMORY_PC);
   master_reg[0] = (int)0x12345678u;
   master_reg[1] = (int)DYNAREC_MEMORY_ADDR;
   master_reg[2] = 0;

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
   int runtime_link_rc;
   int invalidate_rc;
   int memory_rc;
   void *entry = NULL;
   uintptr_t base;
   uintptr_t entry_address;
   uintptr_t code_limit;

   file = fopen(DYNAREC_SMOKE_LOG, "w");
   if (!file)
      return;

   smoke_log(file, "test=ari64-production-smoke revision=8 mode=compile-plus-runtime-invalidation generated_execution=STRAIGHT_LINE_BRANCH_EXTERNAL_LINK_RUNTIME_DYNA_LINKER_INVALIDATE_RECOVER_MEMORY cycle_register=INITIALIZED");

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
   runtime_link_rc = run_runtime_link_test(file, base, code_limit);
   invalidate_rc = run_invalidation_relink_test(file, base, code_limit);
   memory_rc = run_memory_test(file, base, code_limit);

   if (straight_rc == 0 && branch_rc == 0 && link_rc == 0 &&
       runtime_link_rc == 0 && invalidate_rc == 0 && memory_rc == 0)
      smoke_log(file, "SMOKE_EXEC_RESULT PASS tests=straight-line,conditional-branch-taken,external-link-a-to-b,runtime-dyna-linker-a-to-b,invalidate-recover,low-wram-load-store");
   else
      smoke_log(file, "SMOKE_EXEC_RESULT FAIL interpreter-boot-will-continue-if-control-returned");

   smoke_log(file, "stage=cleanup-begin");
   sh2_dynarec_cleanup();
   fprintf(file,
           "stage=cleanup-end base=%08x write_depth=%u runtime_patches=%u invalidation_publications=%u implicit=%d\n",
           (unsigned)(uintptr_t)sh2_dynarec_target,
           vita_dynarec_vm_write_depth(), vita_dynarec_runtime_patch_count(),
           vita_dynarec_invalidation_publish_count(),
           vita_dynarec_implicit_patch_active());
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

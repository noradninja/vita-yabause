#include <stdio.h>
#include <stdint.h>

#include "../sh2core.h"
#include "../sh2_dynarec/sh2_dynarec.h"
#include "../yabause.h"
#include "vita_dynarec_vm.h"

#define DYNAREC_SMOKE_LOG "ux0:data/yabause/dynarec-runtime.log"

extern int __real_YabauseInit(yabauseinit_struct *init);
extern int sh2_recompile_block(int addr);
extern void *get_addr_ht(u32 vaddr);
extern void sh2_dynarec_cleanup(void);

static void smoke_log(FILE *file, const char *message)
{
   if (!file)
      return;
   fprintf(file, "%s\n", message);
   fflush(file);
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

   smoke_log(file, "test=ari64-production-smoke revision=1 mode=compile-only generated_execution=NOT_TESTED");

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

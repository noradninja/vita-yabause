#include "vita_dynarec_vm.h"

#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *sh2_dynarec_target;

/* The legacy Ari64 ARM linkage still exports a .csh2ptr slot that stores
 * through CurrentSH2.  Modern Yabause passes SH2 contexts explicitly and no
 * longer provides this compatibility global, but the assembly ABI still
 * requires the symbol to exist.  Keep it Vita-dynarec-local until the linkage
 * is modernized to remove that legacy indirection. */
void *CurrentSH2;

static SceUID dynarec_block = -1;
static unsigned int write_depth;
static unsigned int veneers;

static void dynarec_vm_fatal(const char *reason)
{
   fprintf(stderr, "dynarec VM failure: %s\n", reason);
   abort();
}

int vita_dynarec_vm_init(void)
{
   int rc;

   if (dynarec_block >= 0)
      return -1;

   dynarec_block = sceKernelAllocMemBlockForVM(
      "yabause-sh2-dynarec", VITA_DYNAREC_CACHE_BYTES);
   if (dynarec_block < 0)
      return dynarec_block;

   rc = sceKernelGetMemBlockBase(
      dynarec_block, (void **)&sh2_dynarec_target);
   if (rc < 0) {
      int release = sceKernelFreeMemBlock(dynarec_block);
      if (release >= 0)
         dynarec_block = -1;
      sh2_dynarec_target = NULL;
      return rc;
   }

   write_depth = 0;
   veneers = 0;
   return 0;
}

int vita_dynarec_vm_begin(void)
{
   int rc;

   if (dynarec_block < 0 || !sh2_dynarec_target)
      return -1;

   if (write_depth != 0) {
      ++write_depth;
      return 0;
   }

   rc = sceKernelOpenVMDomain();
   if (rc < 0)
      return rc;

   write_depth = 1;
   return 0;
}

int vita_dynarec_vm_reset(void)
{
   if (dynarec_block < 0 || write_depth == 0 || !sh2_dynarec_target)
      return -1;

   memset(sh2_dynarec_target, 0, VITA_DYNAREC_CACHE_BYTES);
   veneers = 0;
   return 0;
}

int vita_dynarec_vm_end(void)
{
   int rc;

   if (write_depth == 0)
      return -1;

   --write_depth;
   if (write_depth != 0)
      return 0;

   rc = sceKernelCloseVMDomain();
   if (rc < 0) {
      /* The close failed, so retain a logical write transaction.  This keeps
       * callers from assuming generated code was successfully published. */
      write_depth = 1;
      return rc;
   }

   rc = sceKernelSyncVMDomain(
      dynarec_block, sh2_dynarec_target, VITA_DYNAREC_CACHE_BYTES);
   if (rc < 0)
      return rc;

   return 0;
}

int vita_dynarec_vm_free(void)
{
   int rc;

   if (dynarec_block < 0)
      return 0;

   /* Collapse nested ownership during shutdown.  No generated code may be
    * executed after this point, so one physical close is sufficient. */
   if (write_depth != 0) {
      rc = sceKernelCloseVMDomain();
      if (rc < 0)
         return rc;
      write_depth = 0;
   }

   rc = sceKernelFreeMemBlock(dynarec_block);
   if (rc >= 0) {
      dynarec_block = -1;
      sh2_dynarec_target = NULL;
      veneers = 0;
   }
   return rc;
}

int vita_dynarec_vm_is_writable(void)
{
   return write_depth != 0;
}

unsigned int vita_dynarec_vm_write_depth(void)
{
   return write_depth;
}

void vita_dynarec_clear_cache(void *begin, void *end)
{
   uintptr_t lo = (uintptr_t)begin;
   uintptr_t hi = (uintptr_t)end;
   uintptr_t base = (uintptr_t)sh2_dynarec_target;

   if (write_depth == 0 || hi < lo || lo < base ||
       hi > base + VITA_DYNAREC_CACHE_BYTES)
      dynarec_vm_fatal("invalid publication range");

   /* Deliberately deferred.  The outermost vita_dynarec_vm_end() publishes
    * the whole cache, matching the validated standalone VM test. */
}

uint32_t vita_dynarec_branch_target(uint32_t source, uint32_t target)
{
   int64_t delta = (int64_t)target - source - 8;
   uint32_t *table;
   unsigned int i;

   if (write_depth == 0)
      dynarec_vm_fatal("branch patch outside write transaction");

   if (!(target & 3) && delta >= -33554432 && delta < 33554432)
      return target;

   table = (uint32_t *)(sh2_dynarec_target + VITA_DYNAREC_CACHE_BYTES -
                        VITA_DYNAREC_VENEER_BYTES);

   for (i = 0; i < veneers; ++i) {
      if (table[i * 2 + 1] == target)
         break;
   }

   if (i == veneers) {
      if ((i + 1) * 8 > VITA_DYNAREC_VENEER_BYTES)
         dynarec_vm_fatal("veneer table exhausted");

      /* LDR PC, [PC, #-4].  This does not clobber a scratch register and
       * safely handles ARM/Thumb interworking helper destinations. */
      table[i * 2] = 0xe51ff004;
      table[i * 2 + 1] = target;
      ++veneers;
   }

   target = (uint32_t)&table[i * 2];
   delta = (int64_t)target - source - 8;
   if (delta < -33554432 || delta >= 33554432 || (delta & 3))
      dynarec_vm_fatal("veneer out of range");

   return target;
}

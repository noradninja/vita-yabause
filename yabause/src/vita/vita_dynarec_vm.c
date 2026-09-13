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
static unsigned int runtime_patches;
static unsigned int invalidation_publications;
static int implicit_patch_transaction;
static int post_publish_clear_allowed;

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
   runtime_patches = 0;
   invalidation_publications = 0;
   implicit_patch_transaction = 0;
   post_publish_clear_allowed = 0;
   return 0;
}

int vita_dynarec_vm_begin(void)
{
   int rc;

   if (dynarec_block < 0 || !sh2_dynarec_target)
      return -1;

   post_publish_clear_allowed = 0;

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
   runtime_patches = 0;
   invalidation_publications = 0;
   implicit_patch_transaction = 0;
   post_publish_clear_allowed = 0;
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
      runtime_patches = 0;
      invalidation_publications = 0;
      implicit_patch_transaction = 0;
      post_publish_clear_allowed = 0;
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
   int rc;

   if (hi < lo || lo < base || hi > base + VITA_DYNAREC_CACHE_BYTES)
      dynarec_vm_fatal("invalid publication range");

   /* Legacy invalidation patches branches via set_jump_target(), then calls
    * __clear_cache from do_clear_cache().  If branch_target had to open the
    * VM domain implicitly, this is the first safe boundary at which all of
    * those writes are complete.  Publish the entire VM before execution can
    * resume. */
   if (implicit_patch_transaction) {
      implicit_patch_transaction = 0;
      rc = vita_dynarec_vm_end();
      if (rc < 0)
         dynarec_vm_fatal("invalidation patch publish failed");
      ++invalidation_publications;
      post_publish_clear_allowed = 1;
      return;
   }

   if (write_depth != 0)
      return;

   /* do_clear_cache() may report several disjoint host ranges after one
    * invalidation batch.  The first range above already published the whole
    * Vita VM, so any remaining range notifications are intentionally no-ops. */
   if (post_publish_clear_allowed)
      return;

   dynarec_vm_fatal("cache publication outside write transaction");
}

uint32_t vita_dynarec_branch_target(uint32_t source, uint32_t target)
{
   int64_t delta = (int64_t)target - source - 8;
   uint32_t *table;
   unsigned int i;
   int rc;

   /* Compilation enters the VM domain before any patching.  Runtime
    * invalidation in legacy Ari64 does not: kill_pointer() calls
    * set_jump_target() directly on published code.  Open one implicit write
    * transaction here and leave it open until Ari64 reaches do_clear_cache(),
    * where vita_dynarec_clear_cache() closes and publishes the batch. */
   if (write_depth == 0) {
      rc = vita_dynarec_vm_begin();
      if (rc < 0)
         dynarec_vm_fatal("invalidation patch begin failed");
      implicit_patch_transaction = 1;
   }

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

void vita_dynarec_patch_word(uint32_t address, uint32_t value)
{
   uintptr_t base = (uintptr_t)sh2_dynarec_target;
   int rc;

   if (!sh2_dynarec_target || address < base ||
       address + sizeof(uint32_t) > base + VITA_DYNAREC_CACHE_BYTES ||
       (address & 3u))
      dynarec_vm_fatal("invalid runtime patch address");

   rc = vita_dynarec_vm_begin();
   if (rc < 0)
      dynarec_vm_fatal("runtime patch begin failed");

   *(volatile uint32_t *)(uintptr_t)address = value;

   rc = vita_dynarec_vm_end();
   if (rc < 0)
      dynarec_vm_fatal("runtime patch publish failed");

   ++runtime_patches;
}

unsigned int vita_dynarec_runtime_patch_count(void)
{
   return runtime_patches;
}

unsigned int vita_dynarec_invalidation_publish_count(void)
{
   return invalidation_publications;
}

int vita_dynarec_implicit_patch_active(void)
{
   return implicit_patch_transaction;
}

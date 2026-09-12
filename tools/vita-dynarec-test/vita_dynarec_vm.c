#include "vita_dynarec_vm.h"
#include <psp2/kernel/sysmem.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

unsigned char *sh2_dynarec_target;
static SceUID block = -1;
static int writable;
static unsigned veneers;
static void fatal(const char *reason) {
  fprintf(stderr, "dynarec VM failure: %s\n", reason);
  abort();
}
int vita_dynarec_vm_init(void) {
  int rc;
  if (block >= 0) return -1;
  block = sceKernelAllocMemBlockForVM("ari64-test", VITA_DYNAREC_CACHE_BYTES);
  if (block < 0) return block;
  rc = sceKernelGetMemBlockBase(block, (void **)&sh2_dynarec_target);
  if (rc < 0) {
    int release = sceKernelFreeMemBlock(block);
    if (release >= 0) block = -1;
    return rc;
  }
  veneers = 0;
  return 0;
}
int vita_dynarec_vm_begin(void) {
  int rc;
  if (block < 0 || writable) return -1;
  rc = sceKernelOpenVMDomain();
  if (rc >= 0) writable = 1;
  return rc;
}
int vita_dynarec_vm_reset(void) {
  if (block < 0 || !writable || !sh2_dynarec_target) return -1;
  memset(sh2_dynarec_target, 0, VITA_DYNAREC_CACHE_BYTES);
  veneers = 0;
  return 0;
}
int vita_dynarec_vm_end(void) {
  int rc;
  if (!writable) return -1;
  rc = sceKernelCloseVMDomain();
  if (rc < 0) return rc;
  writable = 0;
  /* Conservative whole-cache publication for correctness testing. Optimize
   * dirty-page publication only after the execution/invalidation tests pass. */
  return sceKernelSyncVMDomain(block, sh2_dynarec_target, VITA_DYNAREC_CACHE_BYTES);
}
int vita_dynarec_vm_free(void) {
  int rc;
  if (block < 0) return 0;
  if (writable) {
    rc = sceKernelCloseVMDomain();
    if (rc < 0) return rc;
    writable = 0;
  }
  rc = sceKernelFreeMemBlock(block);
  if (rc >= 0) { block = -1; sh2_dynarec_target = NULL; veneers = 0; }
  return rc;
}
void vita_dynarec_clear_cache(void *begin, void *end) {
  uintptr_t lo = (uintptr_t)begin, hi = (uintptr_t)end;
  uintptr_t base = (uintptr_t)sh2_dynarec_target;
  if (!writable || hi < lo || lo < base ||
      hi > base + VITA_DYNAREC_CACHE_BYTES) fatal("invalid publication range");
  /* Publication is deferred to vm_end, before any generated execution. */
}
uint32_t vita_dynarec_branch_target(uint32_t source, uint32_t target) {
  int64_t delta = (int64_t)target - source - 8;
  uint32_t *table;
  unsigned i;
  if (!writable) fatal("branch patch outside write transaction");
  if (!(target & 3) && delta >= -33554432 && delta < 33554432) return target;
  table = (uint32_t *)(sh2_dynarec_target + VITA_DYNAREC_CACHE_BYTES - VITA_DYNAREC_VENEER_BYTES);
  for (i = 0; i < veneers; ++i) if (table[i * 2 + 1] == target) break;
  if (i == veneers) {
    if ((i + 1) * 8 > VITA_DYNAREC_VENEER_BYTES) fatal("veneer table exhausted");
    /* LDR PC, [PC, #-4]: no scratch-register clobber, ARMv7 interworking. */
    table[i * 2] = 0xe51ff004;
    table[i * 2 + 1] = target;
    ++veneers;
  }
  target = (uint32_t)&table[i * 2];
  delta = (int64_t)target - source - 8;
  if (delta < -33554432 || delta >= 33554432 || (delta & 3)) fatal("veneer out of range");
  return target;
}

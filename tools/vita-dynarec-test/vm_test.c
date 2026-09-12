/* VM/veneer prerequisite only; this executable does NOT run SH2 code. */
#include "vita_dynarec_vm.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdint.h>

static uint32_t __attribute__((noinline)) helper(uint32_t x) { return x * 3; }
static FILE *logfile;
static int failure;
static int report(const char *name, int rc) {
  if (fprintf(logfile, "%s result=%d hex=%08x\n", name, rc, (unsigned)rc) < 0 ||
      fflush(logfile) != 0) failure = 1;
  if (rc < 0) failure = 1;
  return rc;
}
int main(void) {
  int rc, iteration, lifecycle;
  sceIoMkdir("ux0:data/yabause", 0777);
  logfile = fopen("ux0:data/yabause/ari64-vm-test.log", "w");
  if (!logfile) { sceKernelExitProcess(1); return 1; }
  fprintf(logfile, "test=ari64-vm revision=1 build=%s_%s run=%llu bytes=%u sh2_execution=NOT_TESTED\n",
          __DATE__, __TIME__, (unsigned long long)sceKernelGetProcessTimeWide(), VITA_DYNAREC_CACHE_BYTES);
  fflush(logfile);
  for (lifecycle = 0; lifecycle < 3 && !failure; ++lifecycle) {
    SceKernelFreeMemorySizeInfo info = {0};
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) >= 0)
      fprintf(logfile, "lifecycle=%d free_user=%u\n", lifecycle, info.size_user);
    if (report("allocate-16MiB", vita_dynarec_vm_init()) < 0) break;
    fprintf(logfile, "base=%08x veneer_base=%08x\n", (unsigned)(uintptr_t)sh2_dynarec_target,
      (unsigned)(uintptr_t)(sh2_dynarec_target + VITA_DYNAREC_CACHE_BYTES - VITA_DYNAREC_VENEER_BYTES));
    for (iteration = 0; iteration < 100 && !failure; ++iteration) {
      uint32_t *code = (uint32_t *)sh2_dynarec_target;
      uint32_t target, value = 1 + (iteration & 1);
      if (report("begin-write", vita_dynarec_vm_begin()) < 0) break;
      code[0] = 0xe92d4010; /* push r4,lr: 8-byte aligned */
      code[1] = 0xe2800000 | value;
      target = vita_dynarec_branch_target((uint32_t)&code[2], (uint32_t)helper);
      if (iteration == 0)
        fprintf(logfile, "helper=%08x branch_target=%08x veneer=%u\n",
                (unsigned)(uintptr_t)helper, target, target != (uint32_t)helper);
      code[2] = 0xeb000000 | (((target - (uint32_t)&code[2] - 8) >> 2) & 0xffffff);
      code[3] = 0xe8bd8010;
      if (report("publish", vita_dynarec_vm_end()) < 0) break;
      rc = ((uint32_t (*)(uint32_t))code)(5) == (5 + value) * 3 ? 0 : -1;
      report("helper-and-rewrite", rc);
    }
    report("free", vita_dynarec_vm_free());
  }
  if (failure) report("cleanup", vita_dynarec_vm_free());
  report(failure ? "FAIL" : "VM_PASS_SH2_PENDING", failure ? -1 : 0);
  if (fclose(logfile) != 0) failure = 1;
  sceKernelExitProcess(failure);
  return failure;
}

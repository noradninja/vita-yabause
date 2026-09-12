/* VM/veneer prerequisite only; this executable does NOT run SH2 code. */
#include "vita_dynarec_vm.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdint.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include "debugScreen.h"

static uint32_t __attribute__((noinline)) helper0(uint32_t x) { return x * 3; }
static uint32_t __attribute__((noinline)) helper1(uint32_t x) { return x * 5; }
static uint32_t __attribute__((noinline)) helper2(uint32_t x) { return x * 7; }
static uint32_t (*const helpers[])(uint32_t) = {helper0, helper1, helper2};
static FILE *logfile;
static int failure;
static int screen_ready, cycle_number, iteration_number, completed;
static unsigned long long started;
static void progress(const char *step) {
  if (!screen_ready) return;
  psvDebugScreenPrintf("\033[1;1HAri64 VM validation - revision 3\033[K\n\n"
    "Cache reuse %d / 3     Rewrite %d / 100\033[K\n"
    "Completed: %d / 300     Elapsed: %llu seconds\033[K\n\n"
    "%s\033[K\n\n"
    "Please wait for the result screen.\033[K\n"
    "Progress advances when each operation completes.\033[K\n"
    "This tests executable memory, not SH2 emulation.\033[K\n",
    cycle_number, iteration_number, completed,
    (sceKernelGetProcessTimeWide() - started) / 1000000ULL, step);
}
static void finish_screen(const char *detail) {
  SceCtrlData pad;
  if (!screen_ready) return;
  psvDebugScreenPrintf("\033[2J\033[1;1HAri64 VM validation - revision 3\n\n%s\n\n"
    "Completed rewrites: %d / 300\n%s\n\n"
    "Log: ux0:data/yabause/ari64-vm-test.log\n\nPress X to exit.\n",
    failure ? "FAIL - test did not complete successfully" : "PASS - VM checks completed; SH2 tests still pending",
    completed, detail);
  /* Require a fresh press so a held launch button cannot dismiss the result. */
  do { sceCtrlPeekBufferPositive(0, &pad, 1); sceKernelDelayThread(50000); }
  while (pad.buttons & SCE_CTRL_CROSS);
  do { sceCtrlPeekBufferPositive(0, &pad, 1); sceKernelDelayThread(50000); }
  while (!(pad.buttons & SCE_CTRL_CROSS));
}
static int report(const char *name, int rc) {
  if (fprintf(logfile, "%s result=%d hex=%08x\n", name, rc, (unsigned)rc) < 0 ||
      fflush(logfile) != 0) failure = 1;
  if (rc < 0) failure = 1;
  if (failure && screen_ready)
    psvDebugScreenPrintf("\033[12;1HError at %s: %08x (check log)\033[K", name, (unsigned)rc);
  return rc;
}
int main(void) {
  int rc, iteration, lifecycle, resets = 0;
  started = sceKernelGetProcessTimeWide();
  screen_ready = psvDebugScreenInit() >= 0;
  progress("Opening log...");
  sceIoMkdir("ux0:data/yabause", 0777);
  logfile = fopen("ux0:data/yabause/ari64-vm-test.log", "w");
  if (!logfile) {
    failure = 1; finish_screen("Could not open the log file.");
    sceKernelExitProcess(1); return 1;
  }
  fprintf(logfile, "test=ari64-vm revision=3 build=%s_%s run=%llu bytes=%u sh2_execution=NOT_TESTED\n",
          __DATE__, __TIME__, (unsigned long long)sceKernelGetProcessTimeWide(), VITA_DYNAREC_CACHE_BYTES);
  fflush(logfile);
  progress("Allocating 16 MiB once...");
  if (report("allocate-16MiB", vita_dynarec_vm_init()) < 0) goto finish;
  fprintf(logfile, "base=%08x veneer_base=%08x\n", (unsigned)(uintptr_t)sh2_dynarec_target,
    (unsigned)(uintptr_t)(sh2_dynarec_target + VITA_DYNAREC_CACHE_BYTES - VITA_DYNAREC_VENEER_BYTES));
  for (lifecycle = 0; lifecycle < 3 && !failure; ++lifecycle) {
    cycle_number = lifecycle + 1;
    iteration_number = 0;
    /* No generated entry pointer survives the iteration scope below. */
    fprintf(logfile, "cache_reuse=%d\n", cycle_number);
    progress("Resetting code cache and helper veneers...");
    if (report("reset-begin-write", vita_dynarec_vm_begin()) < 0) break;
    if (report("cache-reset", vita_dynarec_vm_reset()) < 0) break;
    if (report("reset-publish", vita_dynarec_vm_end()) < 0) break;
    ++resets;
    for (iteration = 0; iteration < 100 && !failure; ++iteration) {
      iteration_number = iteration + 1;
      progress("Opening memory for writing...");
      uint32_t *code = (uint32_t *)sh2_dynarec_target;
      uint32_t target, actual, expected, value = 1 + lifecycle * 4 + (iteration & 1);
      uint32_t (*helper)(uint32_t) = helpers[lifecycle];
      if (report("begin-write", vita_dynarec_vm_begin()) < 0) break;
      code[0] = 0xe92d4010; /* push r4,lr: 8-byte aligned */
      code[1] = 0xe2800000 | value;
      target = vita_dynarec_branch_target((uint32_t)&code[2], (uint32_t)helper);
      if (iteration == 0)
        fprintf(logfile, "helper=%08x branch_target=%08x veneer=%u\n",
                (unsigned)(uintptr_t)helper, target, target != (uint32_t)helper);
      code[2] = 0xeb000000 | (((target - (uint32_t)&code[2] - 8) >> 2) & 0xffffff);
      code[3] = 0xe8bd8010;
      progress("Publishing code and synchronizing 16 MiB...");
      if (report("publish", vita_dynarec_vm_end()) < 0) break;
      progress("Executing native code and checking the result...");
      actual = ((uint32_t (*)(uint32_t))code)(5);
      expected = (5 + value) * (3 + lifecycle * 2);
      fprintf(logfile, "execution cycle=%d rewrite=%d expected=%u actual=%u\n",
              cycle_number, iteration_number, expected, actual);
      rc = actual == expected ? 0 : -1;
      report("helper-and-rewrite", rc);
      if (!failure) ++completed;
    }
  }
  progress("Releasing executable memory after cache reuse...");
  report("free", vita_dynarec_vm_free());
finish:
  if (resets != 3 || completed != 300) failure = 1;
  report(failure ? "FAIL" : "VM_REUSE_PASS_SH2_PENDING", failure ? -1 : 0);
  if (fclose(logfile) != 0) failure = 1;
  finish_screen(failure ? "See the last reported step in the log." : "All three cache reuse cycles passed.");
  sceKernelExitProcess(failure);
  return failure;
}

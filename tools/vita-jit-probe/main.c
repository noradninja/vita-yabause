/* Standalone feasibility test, not an SH2 backend. No emulator files touched. */
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VM_BYTES (1024u * 1024u)
#define SYNC_BYTES 4096u

typedef uint32_t (*Code)(uint32_t, uint32_t (*)(uint32_t));
static SceUID log_fd = -1;
static int failed;
static void record(const char *stage, int result) {
  char line[160];
  int n = snprintf(line, sizeof(line), "%s result=%d hex=0x%08X\n", stage, result, (unsigned int)result);
  if (log_fd < 0 || sceIoWrite(log_fd, line, n) != n) failed = 1;
  if (result < 0) failed = 1;
}
static uint32_t __attribute__((noinline)) helper(uint32_t x) { return x * 3; }

/* ARM A32: preserve r4/lr and stack alignment, branch around an error
 * sentinel, then BLX the helper pointer (supports ARM/Thumb interworking).
 * r4 is live across the C call and verified by the final addition. */
static const uint32_t program[] = {
  0xe92d4010, /* push {r4,lr} */
  0xe3a04007, /* mov r4,#7 */
  0xe2800001, /* add r0,r0,#1 (patched repeatedly) */
  0xe3500000, /* cmp r0,#0 */
  0x1a000000, /* bne: skip next instruction */
  0xe3e00000, /* mvn r0,#0 */
  0xe12fff31, /* blx r1 */
  0xe0800004, /* add r0,r0,r4 */
  0xe8bd8010  /* pop {r4,pc} */
};

int main(void) {
  SceUID uid = -1;
  void *base = NULL;
  int opened = 0, rc, i;
  char header[200];
  sceIoMkdir("ux0:data/yabause", 0777);
  log_fd = sceIoOpen("ux0:data/yabause/jit-probe.log",
                    SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (log_fd < 0) { sceKernelExitProcess(1); return 1; }
  snprintf(header, sizeof(header), "probe build=%s run=%llu revision=2 backend=none worker=none bytes=1048576",
           PROBE_BUILD, (unsigned long long)sceKernelGetProcessTimeWide());
  record(header, 0);
  /* Documented maximum is 16 MiB. Rejection tests a real API failure without
   * exhausting memory; an unexpected success is freed and fails the test. */
  uid = sceKernelAllocMemBlockForVM("probe-invalid", 17 * 1024 * 1024);
  if (uid >= 0) {
    record("invalid-allocation-unexpected-success", -1);
    record("invalid-allocation-cleanup", sceKernelFreeMemBlock(uid));
    goto done;
  }
  record("oversize-allocation-rejected", 0);
  uid = sceKernelAllocMemBlockForVM("sh2-probe", VM_BYTES);
  if (uid < 0) { record("allocate", uid); goto done; }
  record("allocate", 0);
  rc = sceKernelGetMemBlockBase(uid, &base);
  record("get-base", rc);
  if (rc < 0) goto cleanup;
  for (i = 0; i < 100; ++i) {
    uint32_t actual, increment = (i & 1) ? 2 : 1;
    rc = sceKernelOpenVMDomain();
    if (rc < 0) { record("open-domain", rc); goto cleanup; }
    opened = 1;
    if (i == 0) record("write-code", 0);
    memcpy(base, program, sizeof(program));
    ((uint32_t *)base)[2] = 0xe2800000 | increment;
    rc = sceKernelCloseVMDomain();
    if (rc < 0) { record("close-domain", rc); goto cleanup; }
    opened = 0;
    rc = sceKernelSyncVMDomain(uid, base, SYNC_BYTES);
    if (rc < 0) { record("sync", rc); goto cleanup; }
    if (i == 0) record("enter-generated-code", 0);
    actual = ((Code)base)(5, helper);
    /* Exercise the not-taken branch too: addition wraps to zero and the
     * sentinel becomes UINT32_MAX; helper multiplication then add 7 = 4. */
    if (((Code)base)(0u - increment, helper) != 4) {
      record("branch-not-taken-mismatch", -1); goto cleanup;
    }
    if (actual != (5 + increment) * 3 + 7) {
      record("execute-or-rewrite-mismatch", -1); goto cleanup;
    }
  }
  record("arithmetic-branch-helper-register-rewrite-100", 0);
cleanup:
  if (opened) record("close-domain-cleanup", sceKernelCloseVMDomain());
  record("free", sceKernelFreeMemBlock(uid));
done:
  record(failed ? "FAIL" : "PASS", failed ? -1 : 0);
  rc = sceIoClose(log_fd);
  sceKernelExitProcess(failed || rc < 0);
  return failed || rc < 0;
}

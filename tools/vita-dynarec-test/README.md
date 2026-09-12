# Ari64 Vita backend test — implementation checkpoint

This target compiles the **actual existing Ari64 SH2 compiler** as
an object library with VitaSDK. A separate VM prerequisite executable tests
the memory adapter; it deliberately does not link or execute the SH2 compiler.
The ordinary emulator target does not enable `VITA_DYNAREC_TEST`.

Configure with the VitaSDK toolchain and Ninja, then build:

```powershell
cmake -S tools/vita-dynarec-test -B build-vita/dynarec-test -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=E:/dev/VitaSDK/share/vita.toolchain.cmake
cmake --build build-vita/dynarec-test
```

Compile configuration enables ARMv6/v7, keeps assertions enabled, and disables
the Cortex-A8 workaround only for this target. Two legacy C declarations were
corrected rather than suppressing compiler diagnostics.

The test-only code-cache base is now a pointer; the future driver must allocate
the VM block before calling initialization. mmap/munmap are omitted only in
this target; the driver must own allocation, write transactions and cleanup.
The driver now owns open/write/close/sync transactions and conservatively
publishes the entire cache. Compilation and dynamic patching must always run
inside those transactions; execution is forbidden until publication succeeds.
The normal backend is unaffected. The final 4 KiB is reserved for up to 512
eight-byte interworking veneers, used for out-of-range/Thumb branch targets.
No register is clobbered by these veneers. Table exhaustion aborts explicitly.

## Hardware gate: full-size VM test

Install `build-vita/dynarec-test/ari64-vm-test.vpk` (separate title SH2ARIT01).
Launch and retrieve `ux0:data/yabause/ari64-vm-test.log`. The test records base,
helper/veneer addresses and build/run identity. Revision 3 allocates one 16 MiB
block, then clears and reuses it for three cycles of 100 rewrites, freeing it
once at the end. Reset requires an active write transaction and clears the
entire buffer (including veneers) and veneer bookkeeping. Entry pointers are
iteration-local. Each cycle changes arithmetic and helper destination; every
execution logs expected and actual values. Whole-cache publication remains.
Expected final marker: **VM_REUSE_PASS_SH2_PENDING**. Any failed API/result or
incomplete counters produces FAIL. Revision 3 hardware execution is pending.

Revision 2 hardware evidence: run 376233 passed allocation, 100 rewrites and
free, then the second allocation failed with 0x80024B0B (MEMBLOCK_OVERFLOW).
This motivates testing single-allocation reuse; it does not establish a
universal prohibition on VM reallocation. Ari64 metadata reset/invalidation
is not exercised by this hand-emitted-code test and remains a separate gate.

Revision 2 adds a VitaSDK debug-screen display: cycle, rewrite number,
completed count, elapsed time and the current operation. Counters advance on
completed operations (they are not a background heartbeat during a blocking
SDK call). The final PASS/FAIL screen stays visible until a fresh X press;
the test no longer exits automatically. The log is closed before that screen.
Builds require VitaSDK samples/common; override `VITA_DEBUGSCREEN_DIR` if your
SDK installs its debugScreen sources elsewhere. On-device visual validation
of this revision is pending.

Remaining work: hardware validation of VM driver/veneers; assembly adaptation;
bounded master/slave execution; interpreter differential harness; hardware
validation. Do not enable this macro in the emulator or infer execution support
from this object-library build.

The separate `tools/vita-jit-probe` revision 2 passed on the user's Vita
(screenshot: run 372254, 1 MiB allocation, 100 rewrites, cleanup and PASS).
That demonstrates the platform mechanism, not actual compiler correctness.

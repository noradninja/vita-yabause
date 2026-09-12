# Ari64 Vita backend test — implementation checkpoint

This target currently compiles the **actual existing Ari64 SH2 compiler** as
an object library with VitaSDK. It is not yet an executable or a passing test.
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
The VM cache-flush callback is deliberately unresolved until the driver exists.

Remaining work: VM driver and range-safe veneers; assembly adaptation;
bounded master/slave execution; interpreter differential harness; hardware
validation. Do not enable this macro in the emulator or infer execution support
from this object-library build.

The separate `tools/vita-jit-probe` revision 2 passed on the user's Vita
(screenshot: run 372254, 1 MiB allocation, 100 rewrites, cleanup and PASS).
That demonstrates the platform mechanism, not actual compiler correctness.

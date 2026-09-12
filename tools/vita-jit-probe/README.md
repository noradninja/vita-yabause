# Vita executable-code probe

Standalone optional application, title ID `SH2PROBE1`. It does not launch
Yabause, change CPU selection, or touch startup/profile logs. No plugins or
kernel patches are installed by this test.

## Build

Use the installed VitaSDK toolchain and Ninja (set the Ninja path if it is not
on PATH):

```powershell
$env:VITASDK = 'E:/dev/VitaSDK'
cmake -S tools/vita-jit-probe -B build-jit-probe -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=E:/dev/VitaSDK/share/vita.toolchain.cmake
cmake --build build-jit-probe
```

Install `build-jit-probe/vita-jit-probe.vpk` as a separate bubble. Launch once;
it exits automatically. Retrieve `ux0:data/yabause/jit-probe.log` after exit.
The file starts with build time and process-time run token, and ends in PASS
only if all checks and cleanup succeed. The token is diagnostic, not a
globally unique run identity. A missing PASS or a crash is a failed hardware
gate; preserve the last stage and the device firmware/environment details.

## Coverage and limits

Uses VitaSDK's `sceKernelAllocMemBlockForVM`, `sceKernelGetMemBlockBase`,
`sceKernelSyncVMDomain`, `sceKernelOpenVMDomain`, `sceKernelCloseVMDomain`,
and `sceKernelFreeMemBlock`. One 1 MiB VM block holds 36 bytes of A32 code.
The generated function exercises arithmetic, both conditional branch paths,
an indirect C helper call, a live r4 across that call, LR/stack restoration,
and 100 alternations of executable code at the same address. It also checks
rejection of an allocation above the SDK-documented 16 MiB maximum. This is
an API failure test, not an out-of-memory stress test. All acquired resources
are released on normal failures; a CPU exception can terminate before cleanup.

The main probe and helper currently compile as ARM; ARM-to-Thumb helper calls,
all callee-saved registers, floating-point ABI, near/far linkage veneers and
full 16 MiB allocation remain separate backend integration tests. This probe
is hand-emitted A32, not output from either SH2 compiler. Passing proves the
basic VM mechanism only, not backend feasibility or SH2 correctness.

Reference: https://docs.vitasdk.org/group__SceSysmemUser.html
Revision 1 hardware result: allocation failed with 0x80024B05 (illegal size).
Revision 2 uses a 1 MiB allocation, opens before writing, closes before syncing
4 KiB and executing, following this VitaSDK-based example:
https://gist.github.com/yifanlu/43a35324f3b76391cd15c6b96ae8b831
The SDK documents the 16 MiB maximum but not minimum/granularity; 1 MiB is
example-backed, not a claim that all legal sizes have been established.
The negative test requests 17 MiB so it no longer mixes oversize and
sub-megabyte granularity. Errors now include hexadecimal codes.

Revision 2 hardware status: **NOT RUN**. Do not infer success from a successful VPK build.

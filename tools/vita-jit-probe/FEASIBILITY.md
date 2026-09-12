# SH2 dynarec feasibility checkpoint

## Status and recommendation

Source audit and standalone probe build completed; hardware execution remains
pending. Neither backend is approved for production integration. Provisional
candidate: legacy Ari64 ARM SH2 dynarec, because it already models both Saturn
SH2s and code invalidation. Prefer adapting it over expanding the current
Play SH1 frontend, subject to the scheduler and relocation gates below.
No performance improvement is claimed by this milestone.

## Candidate comparison

| Area | Legacy ARM dynarec | Play-based SH JIT |
| --- | --- | --- |
| CPU execution | `sh2_dynarec.c:SH2DynarecExec` exits deliberately; `yabause.c` instead selects `YabauseDynarecOneFrameExec` for core ID 2 | Normal `Exec(context, cycles)` exists, but `recompile_and_exec` explicitly calls `decode(SHMT_SH1, ...)` |
| Register state | Separate master/slave register arrays and assembly data layout; current C context is not the sole authority | Uses `context->jit` plus global `current` and global block cache; getters differ from interpreter state |
| Memory/cache | Existing page invalidation and WriteNotify; audit every CPU, DMA and external write producer before trusting it | ROM-sized `MAX_SH1_BLOCKS`, direct `pc/2` indexing, NULL WriteNotify; unsuitable for Saturn address space as-is |
| Timing | Dedicated frame scheduler and assembly cycle state; cannot simply register it beside the interpreter | Carries `jit.cycles` across calls, checks interrupts before block loop; must prove event/interrupt boundaries and sleeping behavior |
| Instructions | SH2 decoder and delay-slot handling exist, but correctness must be demonstrated with current handlers | SH1 selection excludes SH2-specific instructions; multiple handlers assert, which can become silent omissions in Release |
| Host platform | Linux-gated build, sys/mman, fixed mapping at assembly target, cache-flush assumptions | MemoryFunction supports Windows/Apple/POSIX only; Vita allocation branch missing |
| ARM linkage | A32 emitter; relative branch range assertions and literal ranges constrain placement | AArch32 emitter exists; C++ runtime and helper ABI still need validation |
| Memory | 16 MiB code target plus tables/metadata; total budget not yet measured | 32768 block descriptors plus per-block generated storage; no bounded Saturn cache policy |
| Licensing | GPL-2.0-or-later notices in SH2 backend | SH frontend GPL-2.0-or-later; bundled Play code has redistribution conditions in `play/License.txt`; preserve both notices |

Evidence sources: `yabause/src/sh2_dynarec/{sh2_dynarec.c,assem_arm.h,
assem_arm.c,linkage_arm.s}`, `yabause/src/{yabause.c,sh2core.h,sh2core.c,
sh2_jit.cpp}`, `yabause/src/play/src/MemoryFunction.cpp`, and
`yabause/src/CMakeLists.txt`. Current Vita core list contains only interpreter.
No Vita dynarec correctness suite was identified in this targeted audit.
This is not an exhaustive proof that the existing backends are correct.

## Required resolutions before integration

1. Replace legacy fixed-address mmap with a relocatable VM allocation. Remove
   the static 16 MiB assembly target and pass a runtime base consistently to
   C and assembly. Audit every BASE_ADDR use, code-cache wraparound calculation,
   literal load, and direct branch. Use indirect-call veneers when targets are
   out of A32 branch range; never depend on release-disabled assertions.
2. Prove allocation at the intended cache size and report code/table overhead
   and remaining application memory. The small probe does not establish this.
3. Retain the current emulator scheduler: implement bounded per-context dynarec
   execution that returns at the scheduler's cycle boundary. Do not enable the
   legacy whole-frame bypass. First document all scheduler state the assembly
   path owns; if it cannot be separated safely, stop and reconsider candidate.
4. Establish one register authority at entry/exit. Explicitly synchronize all
   GPRs, PC, SR, GBR, VBR, MACH/MACL, PR, pending interrupts and cycle remainder.
   Test alternating master/slave execution. Do not run either CPU concurrently.
5. Enumerate writes to executable BIOS/RAM mappings, mirrored addresses, DMA,
   debugger/load-state writes and reset. Route invalidation to both contexts'
   compiled code. Check the start/length contract of each SH2WriteNotify caller;
   existing DMA sites require review, not blind reuse.
6. Unsupported instructions/conditions must exit before execution, with exact
   state and cycle handoff. No instruction fallback is allowed until that
   contract passes tests. Initialization failure selects interpreter before
   emulation and logs the reason; a detected runtime mismatch stops a test run.

## Incremental integration backlog after hardware gate

Each item is a separate reviewable change; later items depend on earlier gates.

1. **Run provenance and observability.** Add a shared session identifier, build
   commit/dirty status, requested/actual CPU backend and worker mode to startup
   and profile headers. Report profile open/write/close failures. Preserve
   default interpreter and renderer settings. Test injected I/O failures and
   confirm startup/profile IDs agree on hardware.
2. **Vita code-cache adapter.** Integrate allocation/sync/free and relocation,
   expose cache-byte accounting, test 16 MiB availability, eviction/reuse,
   ARM/Thumb calls, r4-r11/SP preservation and far helper calls. Pass repeated
   hardware tests before introducing SH2 execution.
3. **Bounded execution bridge.** Add opt-in `-Sh2Backend Interpreter|Dynarec`
   (default Interpreter), an explicit startup selection result and per-context
   execution/state transfer. Retain the normal scheduler. Gate on deterministic
   register and cycle round trips with alternating master/slave contexts.
4. **Differential instruction/block harness.** Run interpreter and compiler on
   separate copies of controlled RAM and CPU state; compare registers, writes,
   exceptions and cycle remainder. Never execute MMIO twice in a live emulator.
   Cover integer flags, shifts/rotates, multiply/divide/MAC, load/store sizes,
   branches, delay slots, calls/returns, traps, RTE, sleep, interrupts, and
   budget exits. Keep the failing seed and instruction trace. Zero mismatches
   required; enumerate unsupported cases explicitly.
5. **Invalidation and lifecycle.** Test CPU and DMA writes into executing code,
   aliases, cross-page blocks, master/slave shared RAM, reset, save/load and
   eviction. Require that the first subsequent execution sees changed bytes.
6. **BIOS validation.** Run startup, CD player, ship/starfield, memory manager
   and settings with the interpreter and candidate. Check rendering and input,
   interrupts and progress over repeated cold boots. Retain interpreter as
   default. A BIOS pass does not authorize broader compatibility claims.
7. **Performance report and next decision.** Hold atlas/worker/audio settings
   and scene sequence constant. Report cold compilation separately from warm
   execution; include frame time, MSH2/SSH2 execution, compile/cache metrics,
   VDP stages, atlas and GPU wait. Replace reliance on SH2Exec wrappers if the
   integration changes call paths. Compare detailed profiling enabled/disabled
   to estimate perturbation. Only then decide on optimization and game tests.

## Acceptance record

- Local compilation/link/SELF/VPK generation: passes with warnings as errors.
- Ordinary emulator configuration and binaries: not rebuilt or modified.
- Hardware VM lifecycle: revision 1 rejected 4 KiB allocation with 0x80024B05.
  Revision 2 uses 1 MiB and corrected write/close/sync/execute order; retest pending.
- Actual compiler-emitted code on Vita: pending.
- Full backend dependency audit, scheduler bridge and correctness: pending
  integration gates; no claim that all unknowns are resolved.

The executable-memory API is verified against installed VitaSDK headers and
https://docs.vitasdk.org/group__SceSysmemUser.html. Hardware validation is
required to determine its behavior in the user's installed environment.

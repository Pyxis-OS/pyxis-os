# Hosted program and port build flags

Accepted for 0.0.10 (chronium, 2026-10-26), implementation assigned after the
0.0.9 tag (`c02a2bfa`). The pre-decision QEMU comparison used temporary compiler
overrides. Native runs are coordinated through the orchestrator.

## Inspected boundary

The SDK's `userspace/target.mk` exports freestanding C flags to applications and
ports. C++ is already hosted. `cmake/pyxis.cmake` independently initializes C as
freestanding, and Neovim repeats an explicit freestanding C flag. Updating Make
alone would miss those consumers. Host generators retain their native tools.

Installed Clang 23.1.3 (`3d69e83`) confirms that trailing `-fhosted` restores
`__STDC_HOSTED__=1` and builtin lowering for `floorf`, `fabsf` and small copies.
With Mesa's existing `-fno-math-errno`, `sqrtf` also lowers to an intrinsic.
These are IR observations; target code generation can still use libcalls for
operations not directly supported by the selected x86-64 baseline.
This does not propose global fast-math, errno, CPU-target or floating-point
rounding changes. Header precedence remains unchanged: Pyxis's `int_fast*`
types must continue to come from libc, not Clang's freestanding `stdint.h`.

`userspace/runtime.mk` currently inherits the application C flags too. Libc,
its musl/TLSF implementation closure and pre-TLS startup need a deliberate
exception. Hosted optimization can turn an implementation of a standard
function into a call to itself. The actual `nearbyintf` is protected by its
FENV pragma/rounding flags; a simplified wrapper demonstrates the recursion
hazard. Keep those controls and the existing startup `-fno-builtin` exceptions.
Kernel and compiler-rt flags are outside this userspace policy change.

Mesa requests Meson's release build, but its inherited `c_args`/`cpp_args`
contain a later `-O2`; that wins over release `-O3`. The accepted port fix gives
Meson one deliberate optimization setting instead of contradictory flags.
The repeated nested comparison supports hosted C, but has not shown an O3
advantage. The accepted default is explicit O2, with O3 retained as a
measured comparison rather than assumed to be faster.

## Red-zone evidence

Normal resumable CPL3 interrupt/fault entry uses TSS.RSP0; syscall entry saves
user RSP and selects the kernel stack before its first push. Return paths do
not push after restoring user RSP. There is no implicit user-stack signal frame;
user faults terminate. Debugger NMI has its own IST/window handling. Fatal
same-CPL NMI/machine-check entry in the syscall stack-switch windows is a
remaining qualification caveat, not evidence that ordinary IRQs destroy a
userspace red zone.

Pyxis nevertheless documents a no-red-zone convention, Rust disables it, and
**the installed Clang driver disables it even without SDK flags**. Generated
leaf-function assembly is identical with no option and `-mno-red-zone`;
`-mred-zone` actually uses below-RSP storage. Merely removing the SDK spelling
would change nothing. Detailed source/probe evidence is retained under
`/shared/present/hosted-flags/`.

## Matched checks

Temporary compiler-prefix wrappers build the same pinned sources against one
unchanged baseline SDK/runtime: current C versus hosted C, each with actual
Mesa O2/O3. No SDK or recipe default is edited for these comparisons. C++ objects
whose flags are unchanged are reused explicitly; Go Git and the published
hosted Clang program are unchanged controls, not predicted C-mode speedups.

The [experiment record](../development/experiments/hosted-build-flags.md)
records QEMU configuration, results and limits. Install/extract checks use
never-installed
versions of identical public data; removing and reinstalling a retained version
would measure reuse. SHA-NI and portable hashing need separate matched CPU
feature configurations. No timed workload runs alongside compilation or another
timed workload. Render samples use integer scaling or 1:1 output.

The existing attended vela/Fedora SoH figures motivate this work; the Fedora
scene was letterboxed and is not an exact performance baseline. Native A/B must
use the same save bytes: delta's slot-A first-room fixture on Fedora and both
Pyxis profiles, not merely similar scene presentation. Fedora uses SoH 9.3.1
`f0599f9` and Mesa 26.2.5 softpipe; both systems use identical JSON settings:
240-line internal canvas, 4:3, 1920x1048 output, 20 FPS cap and MSAA off, with
CPU at full power. The packet records save/configuration hashes and the camera
spot. Chronium selects and loads slot A on each system; delivery and collection
are automated, with no input sequencer (chronium, 2026-10-26). Private assets remain under `/shared/oot`; no private game content enters
the repository.

## Accepted contract (chronium, 2026-10-26)

1. **Hosted boundary:** hosted C for programs, ports and reusable SDK
   libraries; `__STDC_HOSTED__` becomes 1 for those C consumers. Retain
   freestanding C for the whole libc implementation closure,
   crt0 and pre-TLS startup helpers, plus existing explicit no-builtin/strict
   rounding controls. C++ remains hosted. Synchronize Make, CMake and duplicated
   application flags without changing header order or other options.
2. **Mesa optimization:** explicitly select O2 for the release profile,
   stripping inherited optimization options from the cross-file args rather
   than relying on their order. The nested matrix shows no O3 advantage;
   compare both natively before claiming otherwise. Retain upstream math policy
   and the generic SDK's O2; do not raise every program to O3.
3. **Red zone:** retain the current SDK/compiler/Rust no-red-zone
   convention in this delivery. Explicitly enabling it needs its own ABI promise
   and runtime qualification; the normal-entry audit makes that plausible,
   but it is not implied by hosted C or by deleting a redundant option.

## Delivery

Userland SDK flag split first, ports' Mesa/Neovim adjustments second, then parent
SDK export/pins/docs; signed dependency heads must contain their mains. Rebuild
affected packages for 0.0.10; no runtime ABI change is intended. No compiler
container rebuild is needed for the accepted defaults. Repeat the selected comparisons after implementation and
prepare the coordinated vela packet, including the existing SoH private-packet
workflow, before any native staging.

- [x] Userland: hosted SDK C flags and explicit libc/startup exceptions.
- [x] Ports: Mesa optimization authority and duplicated target C flags.
- [x] Parent integration: CMake, dependency pins, build references and QEMU checks.
- [ ] Coordinated native matched comparison with the same private save/settings.

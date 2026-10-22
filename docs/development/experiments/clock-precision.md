# Explicit clock precision qualification (2026-10-22)

Task 3 of [clock reads](../../wip/clock-reads.md). The qualification DATA-package
choice is accepted (chronium, 2026-10-22). Older toolchain timings are context.

## Fresh matched nested pair

Current-main `a662e49d`, matching SDK/userland `c0b9883f`, ports `dd2882d5`,
fs `f3f22b6e`, existing compiler container `pyxis-llvm23.1.3-3d69e83`.
Ordinary source image build and interactive installation onto an 8 GiB raw disk.
Three independent clones of the pristine installed disk/firmware variables:
q35/KVM, four CPUs, 4 GiB, VirtIO block/network/RNG, OVMF, VM-local storage,
cache=writeback, `-cpu host,+invtsc`. The outer VM now exposes invariant TSC;
all guest boots select KVM clock. This replaces the old nested configuration.

`pkg --timeout 1200 install clock-clang-data@frozen-3e1a92d1` reads the private
HTTP repository. The frozen Clang tree is DATA, with no commands and never
executed: 8,778 files, 373,116,928 archive bytes, SHA-256
`170a8e2f4893f0e6c7c978bf0d5a2505f25a901514d08c824a185c0936feb0a7`.
Keep this archive byte-identical after the change; program ABI checks stay intact.

| Sample | Verify display (s) | Extract zero → first positive (s) | Extract zero → total display (s) |
| --- | ---: | ---: | ---: |
| Before 1 | 55.83 | 17.05 | 82.37 |
| Before 2 | 39.42 | 14.68 | 85.24 |
| Before 3 | 84.13 | 39.70 | 123.15 |
| After 1 | 27.39 | 7.83 | 41.14 |
| After 2 | 27.23 | 7.99 | 40.28 |
| After 3 | 27.61 | 8.10 | 41.54 |

After kernel `a09401fa` and its matching SDK/runtime use the same configuration,
geometry, journal size and private boot settings, freshly installed then cloned
three times. The DATA archive is byte-identical. Both sides use compiler
`3d69e83`; refreshed hosted tools from LLVM #15 (`0fe37d0`) remain a separate
pending qualification. The hosted source pin is independent; no compiler-container
rebuild is needed. All six commands exit 0 with complete Remote drain. Verification median
55.83 s (39.42–84.13), first positive 17.05 s (14.68–39.70), displayed extraction
85.24 s (82.37–123.15). After medians are **27.39 s (27.23–27.61)** for verify,
**7.99 s (7.83–8.10)** for first positive and **41.14 s (40.28–41.54)** for
displayed extraction: 50.93%, 53.14% and 51.73% lower respectively. Before boots
precede after boots rather than interleaving. These are shared nested-VM observations with substantial
variation, not exclusive-host measurements. Display intervals include Remote
delivery and about 200 ms redraw quantization; first positive is 2/8778 after
the large Clang executable, and displayed total precedes final sync/publication.
No debugger/perf runs alongside accepted samples; other agents share the host.
The separated ranges show a lower observed after envelope, not a cycle-level
attribution of the entire difference or a prediction for native TSC. A setup boot with the wrong
disk-template path was stopped before pkg; it is not a sample.

Raw configuration, clone commands, baseline image hash, captures and host
`vmstat` remain under `/shared/present/hpet-hot-reads/task3/`. The before fixture
is retained for matched after qualification.

## Functional qualification (implementation `a09401fa`)

Matching userland `c382ca95`, ports `80298e95`; full ordinary source image build
passes using installed compiler `3d69e83`, which remains unchanged. LLVM #15
(`0fe37d0`) changes only hosted Clang child-launch selection; its independently
pinned hosted build and runtime qualification remain pending.

Interactive fresh installation and separate 1-/4-CPU KVM clones pass coarse date,
precise sleep, parking/refusal/future expiry, TLS/errno/C++ runtime checks. Four
CPUs also pass verified CALL/SEND (64/4096 bytes, 256 messages, five rounds), Lua
precise reads, captured jobs and a concurrent pipeline. IPC is functional evidence,
not matched IPC timing. Private bundles use the existing date/sleep binaries:
READ-only coarse date succeeds; READ|SLEEP relative sleep fails with EACCES;
READ|PRECISE|SLEEP succeeds. No probe program or production self-test was added.
The capture helper stops on the expected failed sleep, so that case has command
exit 1 and its diagnostic, without a claimed final drain record.

GDB observes READ-only NOW returning DENIED/zero reply and READ-only coarse UTC
returning a normalized 16-byte reply on another CPU. The coarse reader is a
cached load without a hardware-clock call. An AP's future parking timeout has
cached time at or beyond its deadline, and the idle single-CPU scheduler waits
for interrupts. A separate four-CPU boot with invtsc/stable flag masked passes
sleep, date, parking and runtime; GDB confirms false stable flags and idle CPUs.
Forced 64-bit HPET passes date/sleep/parking. A TSC-requested nested boot retains
HPET after uncertain calibration (±241 ppm); it is a fallback check, not native
TSC qualification. No injected debugger calls or new kernel diagnostics.

All three timed after captures pass. Exact-head CI, refreshed hosted-tool
authority and the separately scheduled native DATA-package pair remain pending;
the task is not marked complete. Raw functional commands/results and debugger
captures remain beside `before-run.py` under the private record directory above.


## Hosted producer follow-up

The installed compiler/container remains `3d69e83`; hosted LLVM source is pinned
independently at `0fe37d0` (LLVM #15). Existing compiler/SDK provenance and
compiler-rt byte checks remain. Persistent Ninja trees replace fresh Make trees;
SDK content changes invalidate affected recorded object dependencies, including
fixed-timestamp bundle headers, and force fresh tool links. Attempted SDK state
is recorded before compilation so interrupted transitions can be reversed.
Compiler/tool/configuration changes and explicit `--clean` clear the trees.
Repeated exports verify/reuse an identical immutable revision; private staging
is rebuilt after interruption.

Shell/Python checks, real LLVM generator configuration in the existing container,
a seven-step LLVMDemangle build followed by Ninja's `no work to do`, source-pin
refusal, configuration-change invalidation and explicit-clean checks pass.
These are producer-control/build checks, not full hosted closure or runtime
qualification; luna's rebuilt hosted tools remain pending. Private logs are in
`task3/hosted-producer-checks/` under the raw record directory above.

Luna's full hosted build at `0f8cccb9` succeeds; its repeat reports three
`ninja: no work to do` results with no build actions and byte-identical tool
executables. The identity check fails only on copied CMake caches: reconfigure
changes help/type annotations and replaces host compiler paths with equivalent
command names. Export now records sorted configuration values with resolved tool
paths; raw caches remain in WORKDIR and actual tool hashes remain in provenance.
The three captured cache pairs normalize identically in the existing builder
container. Corrected full-producer identity and runtime checks await luna's rerun;
no rebuilt package was published by the failed attempt. Raw evidence is
`/shared/present/hpet-hot-reads/hosted-clock-tools/FAILURE.json` and
`repeat-cache.diff`.

## Native packet status

`/shared/present/hpet-hot-reads/thinkpad-clock-precision/{before,after}/`
holds the matching private native archives and source diffs. The existing
scratch-identity/RAM-home/AC/boot-ID/guarded-cleanup/PXE-restoration harness is
retained, with the new DATA identity and archive hash. Both packets pass two
QEMU miniature install/verify/cleanup runs across Remote reboots with distinct
RAM boot IDs; these are explicitly ineligible for native/performance results.
Measurement hooks keep precise reads around extraction through final staging
sync; the after bundle/helper declarations carry PRECISE. No kernel probe.

Native staging/runs remain held for endpoint confirmation and orchestrator
scheduling. Integration CI uses the existing `3d69e83` compiler image. The active
DATA fixtures are retained; merged earlier clock worktrees/test disks were removed after preserving raw
records and useful UI captures. No owned QEMU/GDB/server processes remain.

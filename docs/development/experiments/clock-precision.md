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
(`0fe37d0`) changes only hosted Clang child-launch selection; the refreshed
hosted build and runtime checks below pass.

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

All three timed after captures pass. Refreshed hosted-tool qualification passes
below; the separately scheduled native DATA-package pair remains pending, so the
task is not marked complete. Raw functional commands/results and debugger
captures remain beside `before-run.py` under the private record directory above.


## Hosted producer and runtime qualification

Pyxis `827cdd05`, userland `38e62b60`, ports `3ff62390`; installed compiler and
SDK C++ runtime sources remain `3d69e83`. Hosted source independently pins
`0fe37d0` (LLVM #15); no compiler-container rebuild. Compiler/SDK provenance and
compiler-rt byte checks remain. Persistent Ninja trees refresh target checks and
invalidate SDK object dependencies by content, including fixed-timestamp bundled
headers, then relink. Compiler/configuration changes or explicit `--clean` clear
the trees. Exported CMake configuration omits editing history and resolves tool
paths; raw caches and separate tool hashes remain available.

Luna's full build and repeated producer pass: three Ninja no-work reports, no
build actions, identical payload identity `hosted-e45c4bdc`.
The private `clang-hosted` archive is 373,193,216 bytes, SHA-256
`9854a8b2c7b632156d795b3ad59b6f9331f743b80e9217aa51082710cf51273d`.
Earlier cache-export drift is corrected; the captured cache pairs also normalize
identically. Producer source/configuration refusal, clean and no-op checks pass.

The existing image at `827cdd05` was reused, without a further rebuild for the
ABI/core/updater design. Full ordinary source image build passes. Runtime checks:
q35/KVM, four CPUs, 4 GiB, OVMF, VirtIO net/RNG, live RAM pools and a read-only
virtio-fs export of small source fixtures. The private package installs into the
RAM pool and the package-command view launches the actual bundles:

- `clang -c`, standalone `lld -flavor gnu` and single-command driver linking
  succeed; both linked programs print the expected line and exit 0.
- A syntax error reports the missing semicolon and exits 1 without an object.
  An undefined `main` exits 1; a new destination stays absent and a pre-existing
  executable keeps its SHA-256 and remains runnable.
- Headless Neovim starts bare `clangd` with four workers, receives the expected
  undeclared-identifier diagnostic, shuts the server down and exits 0.
- LLD's declared clock requires READ|PRECISE|SLEEP; the driver admits the child
  against that declaration. No direct LLD clock-call observation is claimed.

Successful commands have complete Remote drains. The existing capture helper
stops on expected error exits, so those cases have diagnostics/status without a
claimed final drain. No timing claim comes from these functional checks. Clangd
`--check` and LLVM time traces remain outside the documented profile. No new
probe, compatibility shim, ABI identity or production publication was added.

Raw producer evidence: `/shared/present/hpet-hot-reads/hosted-clock-tools-r2/`.
Runtime commands, fixtures and results: `task3/hosted-runtime/` under the raw
record directory above, including `qualified-package-results.json`.

## Native packet status

`/shared/present/hpet-hot-reads/thinkpad-clock-precision/{before,after}/`
holds the matching private native archives and source diffs. The existing
scratch-identity/RAM-home/AC/boot-ID/guarded-cleanup/PXE-restoration harness is
retained, with the new DATA identity and archive hash. Both packets pass two
QEMU miniature install/verify/cleanup runs across Remote reboots with distinct
RAM boot IDs; these are explicitly ineligible for native/performance results.
Measurement hooks keep precise reads around extraction through final staging
sync; the after bundle/helper declarations carry PRECISE. No kernel probe.

Native staging/runs await endpoint confirmation and orchestrator scheduling;
`schedule.md` beside the two packets gives exact horse commands. Integration CI
uses the existing `3d69e83` compiler image. The active DATA fixtures are retained;
merged earlier clock worktrees/test disks were removed after preserving raw
records and useful UI captures. No owned QEMU/GDB/server processes remain.

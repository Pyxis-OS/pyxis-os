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
`3d69e83`; refreshed hosted tools from LLVM #15 (`0fe37d0`) are qualified
separately below. The hosted source pin is independent; no compiler-container
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

All three timed after captures pass. Refreshed hosted-tool and native
DATA-package qualification pass below. Raw functional commands/results and debugger
captures remain beside `before-run.py` under the private record directory above.


## Hosted producer and runtime qualification

Pyxis `0f296c54`, userland `4d8f0349` (main `660f68e`), ports `3ff62390`
(main `96a0cdf`); installed compiler and
SDK C++ runtime sources remain `3d69e83`. Hosted source independently pins
`0fe37d0` (LLVM #15); no compiler-container rebuild. Compiler/SDK provenance and
compiler-rt byte checks remain. Persistent Ninja trees refresh target checks and
invalidate SDK object dependencies by content, including fixed-timestamp bundled
headers, then relink. Compiler/configuration changes or explicit `--clean` clear
the trees. Exported CMake configuration omits editing history and resolves tool
paths; raw caches and separate tool hashes remain available.

The full build and repeated producer pass: three Ninja no-work reports, no
build actions, identical payload identity `hosted-84b2439c`.
The refreshed private `clang-hosted` archive is 373,217,792 bytes, SHA-256
`7c712eb691623c5d0ee6bc5decb5b936ccda4a233ac353ff52e584824270a433`.
Earlier cache-export drift is corrected; the captured cache pairs also normalize
identically. Producer source/configuration refusal, clean and no-op checks pass.

The rebase retains main's release-ABI model, explicit enum values and
capacity-bearing DESCRIBE/FORWARDER_CREATE wrappers (#1105/#1109). The four
userland clock commits rebase unchanged. Rebuild the SDK and affected image
dependencies, then incrementally rebuild/relink all three static hosted tools
and restage their sysroots; host generators remain unchanged. The repeat build
performs no actions and keeps its payload identity. Subsequent main changes
through `f120b4e5` are documentation-only and need no artifact rebuild.

The ordinary source image build passes. Repeated one-/four-CPU checks pass date,
precise sleep, parking and TLS/errno/C++; four CPUs additionally pass READ-only
coarse date, expected precise denial without PRECISE, full precise sleep and
functional CALL/SEND. Setup boots with incomplete private configuration and one
mislaid private bundle are excluded; accepted checks use the normal packaged
live configuration and freshly built binaries. No new timing claim.

Hosted runtime checks:
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

Raw refreshed producer evidence:
`/shared/present/hpet-hot-reads/hosted-clock-tools-r3/QUALIFICATION.json`.
Runtime commands and results: `task3/rebased-runtime/` under the raw record
directory above, including `generic-results.json` and `hosted-results.json`.
The earlier qualification remains in `hosted-clock-tools-r2/` and
`task3/hosted-runtime/`. All owned QEMU/GDB/server jobs are stopped.

## Review follow-ups

Go requires READ|PRECISE|SLEEP before runtime startup. The hello manifest and
current Go grant documentation include PRECISE; `build-go.sh` pins pyxis-go
`27dbc28978ab` (pyxis-go #5). Reusing unchanged host Go tools from release-branch
`33fc8050`, rebuild only the target runtime/programs from that source: hello and
`fmt`/`os`/`time` with wall time and a boot-file read pass in the four-CPU image.
A private READ|SLEEP-only bundle prints `runtime: Pyxis clock failed (status=5)`
and exits 2 before main. The check also exposed stale early-stderr offsets after
Go's output record grew; generated `go_asm.h` offsets now preserve that diagnostic.
Raw commands/results: `task3/go-clock-qualification/` under the record root.
This changes no installed LLVM compiler or container.

The final rebase onto main `454d2504` adds upstream package/Wi-Fi work. SDK
headers and runtime sources remain identical to the qualified clock build; the
new userland commits change only `pkg`. Main's `pxa.py` now reads the native
grant table directly, so its duplicate static PRECISE entry is removed. Packing
and checking the refreshed Go bundle passes with that single authority.

The incremental-header check uses disposable Btrfs reflink copies of the actual
persistent SDK/Ninja trees at the same container paths. For the real
`LLVMDemangle` archive, recorded dependencies include SDK `ctype.h`. Rewriting
identical bytes with the fixed timestamp preserves the input inventory,
invalidates zero objects and leaves Ninja with no work. Adding a comment while
retaining that exact timestamp invalidates the recorded dependent objects; the
normal compiler command rebuilds `Demangle.cpp.obj` and the archive. A repeat
again does no work. The persistent trees and production SDK are untouched.

The real object rule uses `deps=gcc` and Clang `-MD`, including SDK system
headers. In a separate disposable copy, removing `.ninja_deps` makes Ninja's
ordinary dry run schedule recompilation with an explicit missing-dependencies
reason; an existing object is not silently trusted without its required entry.
These checks cover recorded SDK header dependencies, not arbitrary unrecorded
inputs. Raw evidence: `task3/header-check-final/` and
`task3/header-missing-deps/` under the record root. No new fallback mechanism or
test infrastructure is added.

## Native ThinkPad comparison

Before `a662e49d`, after `a09401fa`, each with its matching SDK/runtime as above.
The same private DATA archive runs on the approved disposable 32 GB SanDisk,
directly attached, built-in Ethernet, RAM home and no NVMe writes. Six fresh
boots, three before followed by three after; AC is recorded before and after
every sample. All select native TSC on 12 CPUs (RDTSCP+LFENCE). Each sample
passes extraction, verification, marker-scoped cleanup and complete Remote drain.
Both campaigns restore the PXE menu byte-identically; the ordinary-image return
is requested, not confirmed by these sample records. No kernel probe.

| Sample | Verify display (s) | Extract zero → first positive (s) | Extract + sync, precise (s) |
| --- | ---: | ---: | ---: |
| Before 1 | 7.679 | 6.972 | 64.493 |
| Before 2 | 7.679 | 12.957 | 78.787 |
| Before 3 | 7.841 | 11.465 | 79.222 |
| After 1 | 7.686 | 11.953 | 70.824 |
| After 2 | 7.908 | 10.566 | 72.643 |
| After 3 | 7.676 | 11.440 | 76.680 |

| Phase | Before median (range), s | After median (range), s | Median change |
| --- | ---: | ---: | ---: |
| Fetch, precise | 5.904 (5.893–5.916) | 5.907 (5.798–6.082) | +0.04% |
| Verify display | 7.679 (7.679–7.841) | 7.686 (7.676–7.908) | +0.09% |
| First displayed positive | 11.465 (6.972–12.957) | 11.440 (10.566–11.953) | −0.22% |
| Displayed extraction | 78.722 (64.368–79.005) | 72.591 (70.526–76.407) | −7.79% |
| Extraction + final staging sync, precise | 78.787 (64.493–79.222) | 72.643 (70.824–76.680) | −7.80% |
| Marker-scoped cleanup, precise | 23.804 (17.229–28.403) | 18.687 (18.110–21.909) | −21.49% |

Verification and first-positive medians are effectively unchanged. Extraction
and cleanup medians are lower, but their ranges overlap substantially; three
blocked samples per side do not establish an attributable native performance
change. In particular, the much lower nested after envelope does not reproduce
as a similarly large native effect. Keep all samples, including the fast first
before extraction. No cache-coldness claim is made.

Displayed intervals retain Remote delivery and approximately 200 ms redraw
quantization; first positive is 2/8778 files, after the large executable. The
precise extraction bracket includes final staging sync and is distinct from
100% display. Fetch includes host/network service. Nothing here executes the
DATA payload or measures the hosted compiler's speed.

Raw records and computed samples/medians/ranges:
`/shared/present/hpet-hot-reads/thinkpad-clock-precision/comparison.json`,
`before/results/BEFORE1-3/`, `after/results/AFTER1-3/`, and both
`native-run.log` files. Both packets also passed two miniature QEMU
install/verify/cleanup cycles across Remote reboot; those are functional checks,
not native samples. Integration uses the existing `3d69e83` compiler image.

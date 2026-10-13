# Clang and LLD hosted on Pyxis

Owner-assigned planning, 2026-10-11. This is the
[third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting):
compile, link and run a small C program inside Pyxis. Rebuilding LLVM itself
and running the [ports runner](source-builds.md) come later. Tasks 1–4 are qualified: both native target links complete and default
single-source `-c` runs on Pyxis in 1/4-CPU QEMU and passed the native owner check.
Task 5 is owner-assigned (2026-10-13); its installation, authority and capacity
defaults below are accepted. The owner authorized a separate measurement-only
kernel probe; baseline capture precedes the task 5 build/publication changes.

## Accepted owner decisions (2026-10-11)

1. **First completion:** default single-source `-c`, followed by separately
   launched LLD; inherited diagnostics, no crash-helper processes. General
   multi-job driver execution and reusable embedded compilation are later work.
2. **Concurrency:** finish native [threads gates 5–8](threads.md#delivery-gates).
   Delta owns them in sequence. LLVM worker threads stay off for the first
   compiler; real runtime synchronization and TLS are still required. No
   serial-only LLVM adaptation or successful fake locks.
3. **Memory:** copied input and owned output buffers over native FILE/private
   memory. No shared mapping, JIT or protection changes. Install tools on a pool
   volume; set image, stack, staging and working-memory budgets from native
   artifacts and qualification, preserving failure on exhaustion.

## Process boundary

**Inspected in the current fork:** default integrated cc1 and integrated assembly
run one source `-c` in-process. Multiple jobs or process statistics disable cc1
integration; the Pyxis toolchain constructs an external LLD command rather than
embedding LLD. The native fork already disables child crash helpers and recovery:
a fatal compiler/library fault terminates the process. Task 4 keeps these policies.

**Owner requirement (2026-10-12), not an assigned design:** Clang must eventually
invoke LLD itself (`clang hello.c -o hello`). Task 5 or a follow-up must provide
that through declared launch grants, the
[native command-execution core](../userland/native-jobs.md) and the
[nested execution groups direction](nested-execution-groups.md), never a wrapper.
Task 4 retains its no-launcher profile; first completion still permits separately
launching LLD. General multi-job execution and reusable embedded compilation
remain later work.

## Parallel delivery tasks

Tasks 1–3 left the then-installed fork pin (`49e2c1a`) unchanged. Gate 7 later
adopted their merged fork work in `2d234435`. Task 4 has an explicit go;
task 5 has its own owner go, with pre-implementation decisions below.

1. [x] **LLVM Support platform and header fixes** (2026-10-11): fork
   [PR 4](https://git.internal/PyxisOS/pyxis-llvm/pulls/4), Pyxis #708.
   Contracts in [LLVM Support](../development/llvm-support.md); record in
   [hosted-clang-support](../development/experiments/hosted-clang-support.md).
2. [x] **SDK C++ source closure** (2026-10-11): userland
   [#216](https://git.internal/PyxisOS/pyxis-userland/pulls/216), fork
   [PR 5](https://git.internal/PyxisOS/pyxis-llvm/pulls/5), Pyxis #714. C-only
   locale, wide FILE/UTF-8 closure and `getentropy`-backed `random_device`;
   see [C++ in userspace](../development/cxx-userspace.md) and
   [hosted-clang-cxx](../development/experiments/hosted-clang-cxx.md).
3. [x] **Remaining reached libc operations** (2026-10-11): userland
   [#217](https://git.internal/PyxisOS/pyxis-userland/pulls/217), Pyxis #718.
   Disposition of absent names and the 155/170 Support compile inventory are in
   [hosted-clang-libc](../development/experiments/hosted-clang-libc.md).
4. [x] **Thread-gated compiler/linker integration — QEMU 2026-10-12,
   native owner check 2026-10-13.** Threads
   gates 5–8 and the audited C++ adapter are merged. Build the real Clang/LLD closure with LLVM workers
   off, static TLS and real runtime primitives. **Owner check:** both target links
   complete; compile one default single-source `-c`, with diagnostics/failure exit
   and no child crash helper. No serial-only source adaptation. Fork
   [#7](https://git.internal/PyxisOS/pyxis-llvm/pulls/7), userland
   [#242](https://git.internal/PyxisOS/pyxis-userland/pulls/242), integration
   [#763](https://git.internal/PyxisOS/pyxis-os/pulls/763);
   [build, QEMU/GDB and native record](../development/experiments/hosted-clang-integration.md).
   Production adopts the merged fork and its owner-published compiler image.

   - [x] **Native owner check:** ThinkPad PXE build `8d8a1e39`, temporary bundle
     uploaded via `xfer` into `home://clang-task4`. `hello.c -c` produced a
     non-empty object; `bad.c` reported the expected-`;` diagnostic with caret
     fix-it, exited 1 and left no object. No faults or panic; shell remained usable
     ([owner evidence](https://git.internal/PyxisOS/pyxis-os/pulls/763#issuecomment-9557)).
     Native compile time and peak memory were not measured; task 5 owns those.

5. [ ] **Installed tools and first completion.** Pool-volume bundles delegate
   explicit resource/sysroot roots, scratch/output authority and launcher rights
   only where needed. Measure native image/span/stack/peak memory before settling
   budgets. **Owner check:** compile the same small C source, separately launch
   LLD, run its PXE; rejected inputs and exhausted budgets fail honestly. Record
   repeated startup/compile/link costs and native versus nested-QEMU limits.
   Opt-in producer, pool authority and immutable publication:
   [native hosted tools](../development/hosted-tools.md).
   [Task 5 baseline, artifact and separate probe record](../development/experiments/hosted-clang-task5/README.md).

### Qualified task 4 closure

Source inspection of the current
[pinned fork](https://git.internal/PyxisOS/pyxis-llvm/src/commit/4c23a208a023007024a1b4ff32e5d9cd8733cf1c).

| Area | Boundary and work |
| --- | --- |
| C++ synchronization/TLS | `ThreadPool.h` still instantiates genuine deferred futures; `Parallel.h` has mutex/condition state and LLD uses `thread_local`. Gate 8 supplies the real primitives; task 4 links the genuine closure and observes native TLS/mutex use. General detached async remains outside the profile. Pyxis LLD unlink is already synchronous in the current pin. |
| Metadata consumers | ASTReader/Writer, PrecompiledPreamble, ModuleCache, the driver module-session path and CodeGenModule retain deleted unconditional metadata accessors. Fork #7 captures the full inventory and adapts supported operations through checked optionals, preserving explicit cache refusals; no fake zero values or unchecked dereferences ([LLVM Support](../development/llvm-support.md#paths-and-inputs)). |
| Existing refusals | Module locking, PCH/modules/preambles and child crash helpers already refuse in the current fork. Preserve those policies; default integrated cc1 is already configured. |
| Native image and stack | Clang 90,335,834-byte P1F / 90,927,104-byte span; LLD 55,731,474 / 56,340,480. Temporary Clang requests 8 MiB stack; both fit existing ceilings. Installed heap/handle/native-hardware budgets remain task 5. |

### Accepted task 4 qualification defaults (2026-10-12)

The [pre-code baseline](../development/experiments/hosted-clang-integration.md)
is captured before compiler source changes. These defaults authorize task 4
qualification; installed tools still require a separate task 5 go.

1. **Resource defaults.** A temporary unpacked development Clang bundle contains
   read-only `clang_resources` and `sysroot` directories. Configure only the hosted
   build with `CLANG_RESOURCE_DIR=clang_resources://` and
   `DEFAULT_SYSROOT=sysroot://`. Qualification is ordinary
   `clang -c SOURCE -o OUTPUT`, without discovery from executable text or a launch
   wrapper. Build the real X86 Clang and standalone LLD closure; both target links
   must complete, but LLD execution and installed-tool registration stay in task 5.
2. **Declared authority.** The manifest requires `memory/manage`, `threads/manage`,
   `clock/read,sleep` and `random/read`, with inherited diagnostics and no launcher
   grant. Read-only source and writable scratch/output come from a minimal declared
   qualification-space configuration (`boot://`/`tmp://`), with output named
   explicitly. Existing manifests preserve caller-selected directory roots; this
   does not claim bundle confinement of inherited roots. No call-site grant wrapper
   or new external-directory request API. Stage the temporary bundle on an existing
   development pool/HOST path, outside the boot archive; this is not installation.
3. **Provisional qualification capacity.** Clang requests an eager guarded 8 MiB
   bundle stack. Preserve the existing 128 MiB serialized-image and 256 MiB mapped
   span ceilings. Inspect actual P1F segments/BSS and staging coexistence before
   booting; initially use a 2 GiB QEMU guest, adjusting the test configuration from
   measured artifacts without promising a heap/handle admission budget. If an image
   exceeds an existing ceiling, stop with its measured size rather than lifting
   the ceiling. Installed budgets and native capacity qualification remain task 5.

### Compiler-container boundary

Local task 4 qualification uses the existing `pyxis-llvm23.1.3-2d23443` image
and its matching C++ runtime source. It already generates the required code;
math additions alone need no new container. Fork #7 supplies the remaining
native source closure at `4c23a208a023007024a1b4ff32e5d9cd8733cf1c`.

The owner built and published `pyxis-llvm23.1.3-4c23a20` from the prepared
adoption commit `efcc309a`. Fork #7 merged at the exact qualified source SHA,
so no further image rebuild is needed. Integration adopts that pin, Containerfile
revision check and workflow image tag together under the
[single compiler/SDK source-pin contract](../development/llvm-toolchain.md#changing-the-pin).
This is source adoption, not a new codegen/TLS ABI requirement. Ordinary CI
consumes the published image without rebuilding LLVM. Userland #242 and Pyxis
#763 are merged; the adopted head passed exact-head CI. Task 5 is assigned;
its implementation follows the accepted profile and measurement gate below.

### Accepted task 5 profile (2026-10-13)

Build repeatable native Clang/LLD bundle payloads against the current SDK, publish
them on a pool volume, and qualify compile, separate link and execution of the
same small C program. Keep compiler payloads out of the boot archive. The
existing fork already supplies the source closure; no new compiler image is
currently required. If execution reaches a required fork change, publish its
exact source/adoption branch and owner build/push commands before adopting it.

**Baseline captured before implementation:** current main `0e95af3a`, current
SDK and freshly rebuilt temporary bundles pass repeated version/startup, hello
`-c`, separate LLD link and resulting PXE launch in 1/4-CPU nested-KVM QEMU.
Rejected input exits 1 without an object. Unprofiled remote-session elapsed
times and separate probe memory observations are in the
[task 5 record](../development/experiments/hosted-clang-task5/README.md);
native samples come through the owner. The owner's earlier "instant"
compile/extraction observation is qualitative, not a timed baseline.

The owner accepted all three defaults:

1. **Publication:** opt-in host-built bundle payloads on a separately prepared
   npfs `toolchain` volume, under immutable revision directories, using existing
   formatting/mounting facilities. Launch by explicit `toolchain://` bundle URI
   first. Do not add these large payloads to ordinary image/boot-archive staging,
   a package manager or a generic installer/updater in this task. Qualification
   uses a disposable pool; native storage selection/publication stays an owner
   action. This manual publication per revision is an accepted annoyance that
   the [parked package manager](later-os-directions.md#package-manager) will resolve;
   it has multiple dependencies and is not assigned.
2. **Tool profile:** two self-contained bundles, Clang and standalone LLD, each
   with the current matching SDK resources, an 8 MiB guarded stack and declared
   `memory/manage`, `threads/manage`, `clock/read,sleep`, `random/read`. Source
   and output use selected ordinary roots; app/resource roots are read-only.
   Neither tool receives a launcher. Preserve real workers-off runtime code,
   default integrated cc1 and inherited diagnostics.
3. **Capacity/qualification:** preserve the 128 MiB image/256 MiB span ceilings
   and existing honest allocation refusal; no new heap quota or profiling ABI.
   Record actual images, eager stacks, staging coexistence and process-backed
   memory from existing debugger/accounting tools, separately from elapsed time.
   Do not call sampled memory an exact peak or settle a working-memory budget
   without a native bound. If native peak is not observable with existing tools,
   stop with that limitation and a bounded measurement proposal. Exercise
   invalid inputs, denied output and constrained-memory refusal without allocator
   fault injection.

### Authorized VM backing measurement (2026-10-13)

Existing tools retain totals/snapshots, not an exact native process peak. The
owner authorized a separate, unmerged kernel probe accounting event-based
current/peak owned data frames per process, including provisional frames rolled
back on failure. Include image, eager stack, startup and private regions;
explicitly exclude page tables, kernel metadata, filesystem/cache, borrowed/
device mappings and kernel stacks. Tagged launch-capture payloads are separate
per-boot current/peak accounting, not part of a child's process charge.

Output uses `ktrace` only; there are no ordinary `klog` diagnostics. Keep the
probe patch/branch outside shipping code, label its images **probe builds** and
never stage them as normal PXE builds. Record the patch path/SHA and actual
inputs in the [task 5 experiment record](../development/experiments/hosted-clang-task5/README.md).
Use separate profiled runs for memory and untouched-main runs for elapsed
baselines. No quota, public profiling ABI, compiler change or container rebuild.
Native runs and publication go through the owner/luna.

**Queued after task 5 merges:** the owner auto-assigns driver-invoked LLD. Its
proposal must decide whether a single direct child can satisfy the REQUIRED
[nested-group direction](nested-execution-groups.md), or those groups must land
first. Today's groups are flat and the [native job core](../userland/native-jobs.md)
rejects descendant launchers; same-group launch alone does not implement the
required subgroup lifetime. Do not work around this with a broker/executor or
authority wrapper, and do not start that task during task 5.

## Resources and native limits

**Measured Linux proxy**, same fork/configuration, in the owner's Linux VM
(16 vCPUs, i9-12900K reported, 31 GiB RAM). Host libstdc++ and demand paging
make these neither Pyxis binary sizes nor native RAM admission results.

| Measurement | Result |
| --- | --- |
| clang / lld ELF bytes, before stripping | 108,438,344 / 68,931,328 |
| Same binaries, fork llvm-strip --strip-all | 90,111,728 / 57,246,064 (85.94 / 54.59 MiB) |
| 12-line C file, SDK stdint/stdio, -std=gnu23 -O2 -fintegrated-cc1 -c; /usr/bin/time -v, three runs | Peak RSS 60,860 / 61,328 / 60,560 KiB; all exit 0; elapsed 0.12 / 0.01 / 0.01 s |
| Clang resource headers / source SDK sysroot | Approximately 8.2 / 20 MiB on disk |

Native sizes remain estimates of roughly the same order; static libc++, P1F,
source adaptations and section collection will change them. The decisive loader
check is page-rounded P1F segment memory extent including BSS, not stripped
file size. Proxy file sizes are below 256 MiB, but they do not establish native
mapped spans or required physical backing. The proxy file sizes fit the now
implemented 128 MiB selected-image capture ceiling; that does not establish that
native Clang or LLD will fit or load under available physical memory.

Clang requests an 8 MiB stack and its near-exhaustion diagnostic assumes that
scale. Plain native executables retain fixed eager 1 MiB stacks after the owner
reviewed the 8 MiB default's costs. An unpacked
[application bundle](../userland/program-bundles.md#manifest-and-stack) can now supply a
bounded initial-stack parameter to the common loader, without a P1F field or
compiler-container rebuild. Clang's actual stack use remains unqualified. Threads-off
RunSafelyOnNewStack runs inline. A trivial host compile does not establish stack
safety for templates/deep includes or justify automatic stack growth.

**Inspected allocator:** TLSF pools grow from eager native zeroed private memory;
minimum 64 KiB, with slack for large requests. There is no fixed pool count or
measured total-heap limit; an individual request above 2 GiB fails. Free retains
pools until exit; realloc growth allocates/copies before freeing. Image segments,
launch staging, RAM files, copied input/output and retained pools all contribute
to native peaks. Host RSS is not a heap measurement. Large C/C++ compilation,
linker peaks, pool retention, native load/stack limits and out-of-memory behavior
remain unqualified; no performance claim or guarantee of fitting a guest follows.

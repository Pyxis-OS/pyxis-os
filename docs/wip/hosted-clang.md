# Clang and LLD hosted on Pyxis

Owner-assigned planning, 2026-10-11. This is the
[third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting):
compile, link and run a small C program inside Pyxis. Rebuilding LLVM itself
and running the [ports runner](source-builds.md) come later. Tasks 1–5 are
qualified: installed pool-volume bundles compile, separately link and run the
small C program in 1/4-CPU QEMU and on the ThinkPad (2026-10-13). Native wall
proxies and the authorized unmerged peak probe inform the capacity profile.
Driver-invoked linking is assigned to Codex delta, after the accepted minimal
nested-group prerequisite ([#788](https://git.internal/PyxisOS/pyxis-os/pulls/788),
draft [#789](https://git.internal/PyxisOS/pyxis-os/pulls/789)). This WIP records
that remaining process boundary.

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

**Owner requirement (2026-10-12), assigned follow-up (2026-10-13):** Clang must
invoke LLD itself (`clang hello.c -o hello`). The accepted follow-up uses declared
launch grants, the
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

5. [x] **Installed tools and first completion (2026-10-13).** Pool-volume bundles delegate
   explicit resource/sysroot roots, scratch/output authority and launcher rights
   only where needed. Measure native image/span/stack/peak memory before settling
   budgets. **Owner check:** compile the same small C source, separately launch
   LLD, run its PXE; rejected inputs and exhausted budgets fail honestly. Record
   repeated startup/compile/link costs and native versus nested-QEMU limits.
   Opt-in producer, pool authority and immutable publication:
   [native hosted tools](../development/hosted-tools.md).
   [Task 5 baseline, artifact and separate probe record](../development/experiments/hosted-clang-task5/README.md).

   - [x] Fresh producer, exclusive pool publication, read-only 1/4-CPU
     compile/separate-link/run, rejected input/output, clean reopen and GDB
     grant/TLS inspection. Existing ceilings preserved; separate local probe
     qualifies backing accounting and real low-memory rollback. Refreshed
     against SDK/main `6bc3a009` (216-byte startup/32-byte streams); older
     runtime payloads are superseded.
   - [x] **Native owner check:** ThinkPad PXE `9887dc48`, USB npfs toolchain
     volume, compile/separate-link/`hello 5` pass; all rejected inputs/outputs
     fail honestly, and object/PXE hashes match exactly. Ordinary wall proxies
     and the separate labelled PROBE peaks are recorded, with cleanup zero
     throughout ([owner evidence](https://git.internal/PyxisOS/pyxis-os/pulls/772#issuecomment-9779)).
     The ordinary build was restored after the probe. Working-memory planning
     allowances are 128 MiB Clang / 96 MiB LLD with separate capture backing;
     existing loader ceilings and 8 MiB stacks remain unchanged. Real exhausted
     capture/image admission was checked in QEMU, not claimed natively.

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
#763 are merged; the adopted head passed exact-head CI. Task 5 is qualified;
its implemented profile and measurement gate follow below.

### Accepted task 5 profile (2026-10-13)

Build repeatable native Clang/LLD bundle payloads against the current SDK, publish
them on a pool volume, and qualify compile, separate link and execution of the
same small C program. Keep compiler payloads out of the boot archive. The
existing fork already supplies the source closure; no new compiler image is
currently required. If execution reaches a required fork change, publish its
exact source/adoption branch and owner build/push commands before adopting it.

**Baseline captured before implementation:** then-main `0e95af3a`, matching
SDK and freshly rebuilt temporary bundles pass repeated version/startup, hello
`-c`, separate LLD link and resulting PXE launch in 1/4-CPU nested-KVM QEMU.
Rejected input exits 1 without an object. Unprofiled remote-session elapsed
times and separate probe memory observations are in the
[task 5 record](../development/experiments/hosted-clang-task5/README.md);
native samples are now in the owner qualification there. The earlier "instant"
compile/extraction observation remains qualitative, not a timed baseline.

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

## Driver-invoked LLD: accepted and assigned (2026-10-13)

Task 5 merged as #772. The owner accepted all three defaults and assigned this
follow-up on 2026-10-13. **Scoped planning-freeze exception:** implement only the
minimum native subgroup creation, ownership and subtree cleanup prerequisite,
then driver integration; broader nested-group work stays parked. The target is normal
`clang hello.c -o hello.pxe`: integrated cc1, then one native LLD job, declared
bundle authority and the [native job core](../userland/native-jobs.md).
No shell wrapper, executor or broker. The [pre-code baseline](../development/experiments/hosted-clang-driver/README.md)
is captured at fresh main `4a09711e` with the existing installed toolchain.

**Accepted prerequisite: nested groups land first.** One leaf LLD fits today's
job grant profile; it needs no descendant launcher. That only solves normal
launch/wait while Clang lives. Clang's process stop/fault does not terminate
other group members; closing an observer does not kill its process. Foreground
Ctrl+C targets Clang, while a surviving session can retain LLD. A direct leaf
therefore cannot supply the [required subtree lifetime](nested-execution-groups.md)
by itself. Killing/waiting Clang's whole session group would also stop the shell,
and Clang cannot wait for a group that still contains itself.

| Inspected gap | Evidence at the current pins |
| --- | --- |
| Process stop and group retirement affect only that process/member | `kernel/process.c:171`, `kernel/object/execution_group.c:195`; shell `launch.c:833`; native jobs explicitly have no caller-death guarantee. |
| Scoped creation/binding unavailable | `kernel/object/launcher.c:831` denies CREATE_GROUP to bound/grouped callers; `capability.c:218` and endpoint admission require exact group equality. Groups have no parent/child accounting. |
| Default linker invocation stops before launch | Fork `4c23a208` `Program.cpp:51` refuses non-null statistics; `Job.cpp:385` always supplies them. Baseline exit 1/no executable has this diagnostic, not a measured missing-launcher failure. |
| The later launch path is not the native job core | Fork `Support/Pyxis/Program.inc:155` requires explicit launch selection; `:349` makes a raw launcher CALL. Driver supplies no selection; path lookup accepts a native FILE, not a bundle. `libpyxis` already exports common bundle preparation. |

### Accepted contracts (owner, 2026-10-13)

1. **Containment and completion — accepted: native subgroups first, with a
   caller-owned job scope.** Extend the existing job record with a contained
   profile. Clang owns sole, non-forwarded CONTROL of a private LLD subgroup;
   release on Clang exit/fault requests subtree stop. Normal completion waits
   actual process retirement and subgroup cleanup; unknown admission is never
   replayed. Parent-group stop/cleanup covers every subgroup even if outside
   references retain storage. Keep PROCESS_WAIT's existing process-only meaning:
   on Clang fault, its observer can complete before LLD's deferred cleanup;
   parent-group completion is the transitive observation. A stronger process-
   to-scope completion coupling would be a separate contract, not implied here.

2. **Native creation/authority — accepted: scoped group construction from LAUNCH,
   with bounded trees.** A bound member derives inside its current group/space;
   an ordinary ungrouped local caller creates an owned root in its own space.
   This covers both local and Remote shells without a privileged helper.
   Reuse CREATE_GROUP's atomic CONTROL|WAIT + bound-LAUNCH reply, replacing its
   privileged CREATE_GROUP-right gate with LAUNCH; update in-tree callers and
   retire that redundant right. The creator stays outside its new job group;
   ordinary launches do not automatically create new groups. Accept only own-
   group/descendant launchers, never ancestor/unbound/foreign escapes. Keep
   default own-group launcher delegation where declared by launch policy, with
   explicit sandbox withholding and no synthesized kernel grant; the LLD leaf deliberately receives none. Grouped space
   creation refuses before effects in this fixed-space slice; trusted ungrouped
   space creation remains available. Accepted tunable bounds: depth 16 and
   256 allocated group objects per space, including retiring/retained storage,
   with LIMIT before creation effects. These are policy, not ABI constants.

3. **Driver/bundle profile — accepted: one self-contained Clang bundle with a
   declared read-only `linker://` root containing `lld.pxb`.** Clang requests
   optional `launcher/launch`: compile-only remains usable when withheld, while
   linking refuses honestly. Normal launch-enabled spaces supply it without a
   command wrapper. Embedded LLD keeps its declared four runtime grants, read-
   only app/resource/sysroot roots, 8 MiB stack and no launcher. Use shared SDK
   bundle preparation and the job core, selecting only held declared authority,
   explicit cwd/environment snapshots and inherited descriptors. The LLVM
   adapter becomes a thin core client; ordinary Command execution stops asking
   for unavailable process statistics. Requested statistics/other unsupported
   policies remain explicit refusals. Preserve integrated cc1, temporary-file
   cleanup, honest exit/fault/termination results and no child crash helper.
   Keep current image/span ceilings and stacks; measure overlapping Clang/LLD
   and capture backing rather than adding independent peaks as a measured total.

### Assigned delivery scope

- [x] **First unit: native subgroup foundation**, kernel/ABI plus SDK helpers only.
  Parent/child storage and cleanup references, constructor reservations, ancestry
  checks in launch/capability/COPY/IPC delivery, ancestor seal/stop versus creation
  and publication, and subtree stop/cleanup. Preserve BSP allocation preflight,
  no allocation under group/scheduler guards, safe-stop and deferred ownership.
  Qualify local-root and Remote-member creation, refusal/stop races and retained
  WAIT storage in 1/4-CPU QEMU/GDB. **After this unit the owner can:** create and
  inspect a genuinely contained native job subtree without changing Clang. The
  [qualification record](../development/experiments/hosted-clang-driver/README.md#native-subgroup-foundation-2026-10-13)
  covers builds, matched workloads and 1/4-CPU QEMU/GDB; kernel #789 awaits
  Daybreak's adversarial review before merge.
- [ ] **Core and declared bundles:** implement the contained job profile using that
  foundation and shared manifest preparation, packaging the linker root. No
  second executor, implicit authority inventory or default grouping overhaul.
  **After this unit the owner can:** run a declared leaf job with normal
  observation and caller-loss stop ownership in either shell profile.
- [ ] **Fork/integration:** wire native Command execution through the core, resolve
  the declared linker bundle and GNU LLD flavor, then build fresh target closure
  and qualify default compile/link/run plus failures and stop cleanup. **After
  this unit the owner can:** use `clang hello.c -o hello.pxe` and run its output.

Generic groups, jobs and manifests carry no x86 register/APIC/ELF-machine policy;
CPU placement stays with the existing space/scheduler contract. Executable
selection still uses the common launcher and userspace bundle/loader path, not
foreign syscalls, fork or a per-space Clang execution setting. This preserves
[the subsystem boundary](subsystems-per-space.md) and the
[aarch64 review lens](later-os-directions.md#beyond-x86-aarch64), without assigning
another personality or target architecture.

The foundation/core alone need no compiler-container rebuild. Driver fork
changes do: after approval and a qualified exact fork commit, prepare adoption
and give the owner its exact `podman build -f ci/Containerfile -t
 git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-SHA .` / `podman push` commands
before adoption. No new fork SHA/image exists or is requested for the foundation. Deliver the
kernel subgroup PR first for Daybreak's adversarial review before merge, then
the separate Clang integration PR. The recorded baseline precedes code changes.

## Resources and native limits

The [installed capacity profile](../development/hosted-tools.md#qualified-capacity)
uses the native task 5 measurements: Clang hello peak 101.85 MB, standalone LLD
70.23 MB (decimal MB, reported precision), cleanup zero throughout. It records
artifact image/span bytes, 8 MiB eager guarded stacks, separate capture backing
and provisional owned-data planning allowances with headroom. Existing loader
ceilings remain; these allowances add no heap quota and are not total system-RAM
requirements or bounds for arbitrary programs. Page tables, kernel metadata/
stacks, filesystem/cache, borrowed/device mappings and scratch files are excluded
from the probe's process metric.

Ordinary native wall proxies are 4.289 s for Clang version, 4.325 s for the small
compile and 2.966 s for LLD (medians). USB image loading dominates; effective
roughly 20 MB/s loading and no selected-image reuse are
[deferred debt](../technical-debt.md#hosted-tool-launch-loading), not task 5 work.
Nested-QEMU wall values and historical Linux RSS are not isolated comparisons
or native RAM admission requirements.

**Inspected allocator:** TLSF pools grow from eager native zeroed private memory;
minimum 64 KiB, with slack for large requests. There is no fixed pool count or
measured total-heap limit; an individual request above 2 GiB fails. Free retains
pools until exit; realloc growth allocates/copies before freeing. The 8 MiB stack
is eager backing, not measured stack high-water. Threads-off RunSafelyOnNewStack
runs inline. Large C/C++/LTO, started-tool malloc exhaustion and arbitrary stack
safety remain unqualified; actual capture/image refusal was checked in QEMU.

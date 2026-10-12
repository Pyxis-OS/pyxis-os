# Clang and LLD hosted on Pyxis

Owner-assigned planning, 2026-10-11. This is the
[third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting):
compile, link and run a small C program inside Pyxis. Rebuilding LLVM itself
and running the [ports runner](source-builds.md) come later. Tasks 1–4 are qualified: both native target links complete and default
single-source `-c` runs on Pyxis in 1/4-CPU QEMU. Installed tools, standalone LLD
execution and native-hardware budgets remain task 5, which is not assigned.

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
[native command-execution core](native-command-execution.md) and the
[nested execution groups direction](nested-execution-groups.md), never a wrapper.
Task 4 retains its no-launcher profile; first completion still permits separately
launching LLD. General multi-job execution and reusable embedded compilation
remain later work.

## Parallel delivery tasks

Tasks 1–3 left the then-installed fork pin (`49e2c1a`) unchanged. Gate 7 later
adopted their merged fork work in `2d234435`. Task 4 has an explicit go;
task 5 still needs its own separate assignment.

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
   Ordinary SDK adoption waits for the owner-built container (below).
3. [x] **Remaining reached libc operations** (2026-10-11): userland
   [#217](https://git.internal/PyxisOS/pyxis-userland/pulls/217), Pyxis #718.
   Disposition of absent names and the 155/170 Support compile inventory are in
   [hosted-clang-libc](../development/experiments/hosted-clang-libc.md).
4. [x] **Thread-gated compiler/linker integration — qualified 2026-10-12.** Threads
   gates 5–8 and the audited C++ adapter are merged. Build the real Clang/LLD closure with LLVM workers
   off, static TLS and real runtime primitives. **Owner check:** both target links
   complete; compile one default single-source `-c`, with diagnostics/failure exit
   and no child crash helper. No serial-only source adaptation. Fork
   [#7](https://git.internal/PyxisOS/pyxis-llvm/pulls/7), userland
   [#242](https://git.internal/PyxisOS/pyxis-userland/pulls/242), integration
   [#763](https://git.internal/PyxisOS/pyxis-os/pulls/763);
   [build and QEMU/GDB record](../development/experiments/hosted-clang-integration.md).
   Production fork adoption waits for the matching owner-published container.
5. [ ] **Installed tools and first completion.** Pool-volume bundles delegate
   explicit resource/sysroot roots, scratch/output authority and launcher rights
   only where needed. Measure native image/span/stack/peak memory before settling
   budgets. **Owner check:** compile the same small C source, separately launch
   LLD, run its PXE; rejected inputs and exhausted budgets fail honestly. Record
   repeated startup/compile/link costs and native versus nested-QEMU limits.

### Qualified task 4 closure

Source inspection of the current
[pinned fork](https://git.internal/PyxisOS/pyxis-llvm/src/commit/2d2344355c6825f3293e026aafe185c5632abfdb).

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

Tasks 1-3 leave the installed pin unchanged. Their Support/header and libc++
changes are fork PRs, not ports-only patches. **Before a parent PR adopts the new
fork pin**, the owner must build/publish the matching compiler container and the
PR must update `toolchain/build.sh`, `ci/Containerfile`, workflow image tag and
[toolchain documentation](../development/llvm-toolchain.md#changing-the-pin)
together. Ordinary SDK configuration or libc edits alone need no container
rebuild. Coordinate the chosen fork head with delta's gate 7 TLS change to avoid
publishing incompatible compiler/runtime pins. Gate 7 adoption pins fork #6 at
`2d2344355c6825f3293e026aafe185c5632abfdb`, including merged fork #5;
its matching owner-published image is `pyxis-llvm23.1.3-2d23443`. Gate 8 now supplies
the audited synchronization/C++ runtime through the SDK, without another container.
No rebuild is needed for this baseline. The remaining metadata source adaptations
are in [fork #7](https://git.internal/PyxisOS/pyxis-llvm/pulls/7)
against `pyxis-23.1.3`; local closure qualification can use the
current compiler first. Adopting that new fork pin into the single installed
compiler/SDK pin requires the owner to build/push its matching image before parent
CI. Fork #7 is published; an adoption branch and qualified final source head
are not ready yet. Delta will provide the
exact published SHA, adoption branch and `podman build`/`podman push` commands
when ready, rather than requesting a speculative rebuild now.

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

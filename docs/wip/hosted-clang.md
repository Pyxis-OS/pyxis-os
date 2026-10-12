# Clang and LLD hosted on Pyxis

Owner-assigned planning, 2026-10-11. This is the
[third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting):
compile, link and run a small C program inside Pyxis. Rebuilding LLVM itself
and running the [ports runner](source-builds.md) come later. No hosted compiler
has run yet: tasks 1-3 deliver Support, C++ and libc prerequisites, and the
target Clang/LLD link waits on the thread gates.

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

**Inspected in the pinned fork** (LLVM `49e2c1a1518b`): `-fintegrated-cc1 -c`
with one source and integrated assembly runs cc1 in-process.
`clang/lib/Driver/Driver.cpp:5562` disables integration for multiple jobs or
process statistics, so source-to-executable and multiple-source invocations
still launch cc1 externally. `clang/lib/Driver/ToolChains/Pyxis.cpp:96` creates
an external LLD `Command`; `-fuse-ld=lld` does not embed the linker. The first
completion keeps compilation and native LLD launch separate.

The driver launching LLD or external cc1 requires
[nested execution groups](nested-execution-groups.md), a parked required direction
for contained descendant execution. It does not change the accepted first
completion: the caller compiles one source and separately invokes LLD.

`lldMain` in `lld/include/lld/Common/Driver.h` is the later library-entry route;
embedding must respect `canRunAgain`, context cleanup and fatal-exit containment.
Integrated cc1 enables signal-based crash recovery
(`clang/tools/driver/driver.cpp:384`). The native adaptation must make faults
terminate the process, without continuing after a fatal compiler/library failure.
This is not permission to add crash helpers or start embedding work.

## Parallel delivery tasks

This line proceeds alongside delta's threads gates 5–8. Tasks 4 and 5 need
separate go-aheads. The installed fork pin (`49e2c1a`) is unchanged by tasks 1-3.

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
4. [ ] **Thread-gated compiler/linker integration.** After delta's tasks 5–8 and
   its audited C++ adapter land, build the real Clang/LLD closure with LLVM workers
   off, static TLS and real runtime primitives. **Owner check:** both target links
   complete; compile one default single-source `-c`, with diagnostics/failure exit
   and no child crash helper. No serial-only source adaptation.
5. [ ] **Installed tools and first completion.** Pool-volume bundles delegate
   explicit resource/sysroot roots, scratch/output authority and launcher rights
   only where needed. Measure native image/span/stack/peak memory before settling
   budgets. **Owner check:** compile the same small C source, separately launch
   LLD, run its PXE; rejected inputs and exhausted budgets fail honestly. Record
   repeated startup/compile/link costs and native versus nested-QEMU limits.

### Open gaps for task 4

Source inspection of the pinned fork, relative to the
[LLVM source](https://git.internal/PyxisOS/pyxis-llvm/src/commit/49e2c1a1518b3e4687b52ceb6001069c1b6d261e).

| Area | Boundary and work |
| --- | --- |
| C++ synchronization/TLS | `ThreadPool.h:97–117` instantiates `shared_future`/deferred `async` outside its threads guard; `Parallel.h:61–85` declares mutex/condition-variable state; `lld/Common/Memory.h:68` uses `thread_local`. These wait for delta's gates 5–8 and the C++ adapter audit at gate 8, even with `LLVM_ENABLE_THREADS=OFF`. The 15 Support units that still fail are listed in [hosted-clang-libc](../development/experiments/hosted-clang-libc.md#compile-and-symbol-inventory). LLD `Filesystem.cpp:78–109` also uses detached `std::thread` for unlink: give that helper an honest synchronous or joined lifetime, not unsupported detach or a serial-only compiler substitute. |
| Metadata consumers | Task 4's full build must adapt each remaining consumer of unknown metadata without replacing unknown values with zero or unchecked optional dereferences ([LLVM Support](../development/llvm-support.md#paths-and-inputs)). |
| Randomness/sleep | `LockFileManager.cpp:298` uses backoff; disable unsupported lock/module-cache features explicitly. No deterministic entropy or successful file-lock placeholder. |
| Retained-handle, memory, stack budgets | Need actual native compiler artifacts (task 5); see Resources below. |

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
its matching owner-published image is `pyxis-llvm23.1.3-2d23443`. It provides
static TLS, not the remaining gate 8 synchronization/C++ audit.

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

# Clang and LLD hosted on Pyxis

Owner-assigned planning, 2026-10-11. This is the
[third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting):
compile, link and run a small C program inside Pyxis. Rebuilding LLVM itself
and running the [ports runner](source-builds.md) come later. No hosted compiler
has run yet.

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

## Earlier measured boundary (2026-10-09)

Fresh SDK build: Pyxis `32dcff8a`, userland `22785544`, ports `772f8ff1`,
filesystem `b427df29`; clean inputs. LLVM fork
`49e2c1a1518b3e4687b52ceb6001069c1b6d261e`, matching
[the installed toolchain](../development/llvm-toolchain.md#the-fork), fetched
only through `git.internal`. Existing Clang/LLD 23.1.3 builder; no new compiler
container. `make -j16 sdk` passed, including the SDK C++ runtime.

The [unmerged probe](https://git.internal/PyxisOS/pyxis-os/src/commit/c3d91cfb/probe/hosted-clang)
records commands and compact results; raw logs and artifacts stay local.
Both builds used Release, X86 only, `clang;lld`, threads/EH/RTTI/backtraces and
optional compression/editing libraries off; no tests, examples or benchmarks.
Only clang/lld targets and required native TableGen generators were built.
The normal LLD dispatcher still includes its other object-format drivers.

| Probe | Result |
| --- | --- |
| Cross-configure with Generic/Pyxis and real SDK | Initially rejected unnecessary shared libLTO/libclang/remarks; configured after excluding them. Compile-only library checks falsely found pthread/dl/rt, which were explicitly disabled. SDK unwind exports really exist. |
| `cmake --build … --target clang lld -- -k -j16` | Failed, no target link: `llvm/ADT/bit.h:53` assumes `machine/endian.h`; bundled regex assumes size_t from sys/types.h. |
| 17 selected Support/Clang/LLD translation units, with compiler-provided endian defines and stddef.h, without failed PCH | 16 failed: C++ mutex/future/streams/wide-string facilities, rejected LLD thread_local, absent signal.h, pid_t, st_blksize, and incomplete Generic filesystem/process types. Memory.cpp parsed but supplies no Generic OS backend. No substitute declarations or successful stubs. |
| Independent real-header and symbol links | Confirmed then-current open/read/write/pread/pwrite/access/rmdir/stat/lstat/fstat/mkstemp/getenv/fileno/fseeko/ftello; `<cmath>` with std::sqrt parsed. This is availability, not LLVM-compatible semantics. |
| Matching Linux host build | Passed. Used the same fork/options, Linux target and host libstdc++; target build used SDK libc++. |

Concrete absent headers: `machine/endian.h`, `sys/time.h`, `sys/wait.h`,
`sys/resource.h`, `signal.h`, `spawn.h`, `sys/mman.h`, `sys/ioctl.h`,
`sys/statvfs.h`, `dlfcn.h`, `pwd.h`, `poll.h`, `pthread.h`.
These describe the existing Unix backend, not a requirement to supply Unix APIs.

## Planning source audit (2026-10-11)

Checked Pyxis `2fca30fd`, pinned/current userland `df210f1a`, ports `df3210b0`,
and the unchanged LLVM fork `49e2c1a1518b`. This is source/header inspection,
before task 1. Its later target build and resolved platform gaps are recorded
[separately](../development/experiments/hosted-clang-support.md). References
below are relative to that [LLVM source](https://git.internal/PyxisOS/pyxis-llvm/src/commit/49e2c1a1518b3e4687b52ceb6001069c1b6d261e).

| Area | Current boundary and work |
| --- | --- |
| Closed libc gaps | `getcwd/chdir`, proved `realpath`, copied `setenv/unsetenv`, `fdopen`, `dup/dup2`, `setvbuf`, `isatty`, `mktime`, `iconv`, `O_APPEND`, `gethostname` and stat identity/mtime exist. See [process state](../userland/process-state.md), [paths](../userland/paths.md#proved-realpath), [stdio](../userland/stdio.md) and [metadata](../interfaces/file-metadata.md). Their native limits still apply; descriptive cwd is not proof and there is no writable `environ`. |
| Support selection | `Path.cpp`, `Process.cpp`, `Program.cpp`, `Memory.cpp` and `Signals.cpp` select Unix/Windows implementations, not Pyxis. Generic leaves status/process types incomplete. Add a distinct Pyxis backend and scheme-aware path operations; do not compile Unix `.inc` files against fabricated headers. `llvm/ADT/bit.h:53` needs Pyxis endian selection; bundled regex needs an explicit `stddef.h` include. |
| Metadata | `st_dev`, `st_ino`, `st_mtim` now have independent validity. Clang `FileManager.cpp:100,263` caches directories/files by unconditional UniqueID; Unix status conversion ignores native validity and releases references too early for live-ID comparison. Preserve validity and retain native objects for the compiler's identity-cache lifetime. No invented uid/gid/link counts, access times or permission bits. Archive mtime and provider object identity may be unknown. |
| C++ streams/localization | `raw_os_ostream.cpp:25,29` needs stream write/tell; `Mustache.cpp:678` needs `ostringstream`; `clang/lib/Lex/PPMacroExpansion.cpp:1739–1757` needs `stringstream`, `locale("C")`, `put_time`, `gmtime/localtime` for `__TIMESTAMP__`. libc already has time formatting and C-only `setlocale`, but not `localeconv` or locale objects. libc++ `__locale_dir/locale_base_api.h:109–177` has no Pyxis backend; its generic classic ctype table can abort (`locale.cpp:923–928`). Implement a real C-only locale adapter and its stream/facet closure, not just flip CMake flags. |
| Wide facilities | `ConvertUTFWrapper.cpp:259–271` unconditionally uses `wstring/wstring_view`; libc++ wide `char_traits` is disabled, and current libc supplies only bounded pieces such as `wcslen`. Audit real wide conversion/classification dependencies before enabling the needed subset; no promise of full wide localization. |
| C++ synchronization/TLS | `ThreadPool.h:97–117` instantiates `shared_future`/deferred `async` outside its threads guard; `Parallel.h:61–85` declares mutex/condition-variable state; `lld/Common/Memory.h:68` uses `thread_local`. These wait for delta's gates 5–8 and the C++ adapter audit at gate 8, even with `LLVM_ENABLE_THREADS=OFF`. LLD `Filesystem.cpp:78–109` also uses detached `std::thread` for unlink: give that helper an honest synchronous or joined lifetime, not unsupported detach or a serial-only compiler substitute. |
| Randomness/sleep | `ExponentialBackoff.h:60` contains `random_device`; `LockFileManager.cpp:298` uses backoff. Qualify a real random-grant/sleep adapter if retained in the closure; disable unsupported lock/module-cache features explicitly. No deterministic entropy or successful file-lock placeholder. |
| Copied I/O and memory | `MemoryBuffer.cpp` has a copied-input route. `FileOutputBuffer.cpp:120` allocates its in-memory fallback through mapped-memory API, then writes the final name directly. Implement owned private RW backing and native FILE transfers with short-write/error handling and deliberate publication. Clang already stages through `CompilerInstance.cpp:881`/`VirtualOutputBackends.cpp:291`; preserve that contract as well as fixing LLD's fallback. Actual private memory is eager RW/NX; executable/protection/shared-map requests refuse. |
| Launch/faults | Unix Program assumes spawn/wait/signals and POSIX descriptors. Native argv/cwd/environment snapshots, explicit grants and owned completion handles replace that backend. `clang/tools/driver/driver.cpp:59,384` still assumes signals/crash recovery. Native faults terminate the process; no signal recovery, helper or continuing after a fatal compiler/library fault. |

### Remaining libc names: classify actual consumers

Absence alone does not make a function a required first-compiler addition. The
following names are still absent from current libc; re-audit the selected native
closure as it builds. Standard functions actually needed belong in libc; platform
policy belongs in LLVM Support.

| Name / consumer | Proposed native direction |
| --- | --- |
| `posix_memalign` | LLVM Support `MemAlloc.cpp` uses aligned C++ `new`; the name occurs in installed intrinsic headers/optional allocator code. Add correct libc allocation semantics only if a retained consumer needs it; not a demonstrated first-compiler blocker. |
| `fcntl` / Unix `Path.inc:1106,1337–1374`, `Jobserver.inc:27–58` | Use existing descriptor ownership/duplication and native stream properties. Unsupported advisory locks and close-on-exec assumptions must refuse or be removed from the selected backend; no fake flags. |
| `readlink`, `link/symlink` / Unix `Path.inc:244,1241,1469` | Native lookup follows no symlinks. Use explicit executable/resource roots, not `/proc/self/exe`; refuse requested symbolic/hard-link features. No need to invent Unix filesystem objects. |
| `pipe` / `raw_socket_stream.cpp:200`, `clang/lib/Interpreter/IncrementalExecutor.cpp:155` | Native pipes exist, but file-to-object compilation and inherited diagnostics need no internal pipe. Keep socket/interpreter capture out; add a real libc adapter only if a retained consumer needs it. |
| `getpid` / Unix `Process.inc:66,422`, temp/lock naming | No libc PID query exists. Program owns completion handles, not Unix PIDs; exclusive temporary creation uses randomness, not a fabricated PID. Audit remaining ID-only consumers before choosing an exposed native identity. |
| `sysconf` / Unix `Process.inc:77`, `Program.inc:550` | Page size comes from the native memory contract; argv limits need actual startup payload accounting (`clang/lib/Driver/Driver.cpp:2331`). CPU information needs query authority. Refuse unsupported statistics; worker selection remains off. |
| `chmod/fchmod`, `futimens` / Unix `Path.inc:817–859` | Pyxis has capability authority, not execute/owner mode bits or a timestamp setter. Adapt native tool output and temp creation; requested mode/time mutations refuse. libc still rejects non-0666 file creation modes. |
| `posix_fallocate` / `llvm/lib/CAS/OnDiskCommon.cpp:156` | Optional persistent CAS allocation, not ordinary Support output. Leave CAS disabled; file resize is not a promise of reserved physical storage. |
| `strerror_r` / `llvm/lib/Support/Errno.cpp` | Configure absence correctly: `Errno.cpp:59` already falls back to real `strerror`, whose current messages are immutable. Not a first-slice blocker. If later needed, implement bounded libc copying with one explicit return contract. |
| `ECHILD/ENOEXEC/EXDEV` | Still absent; `EINTR` exists as vocabulary without signal interruption. They are not proved first-path requirements: `ECHILD` appears in a Unix wait comment, `EXDEV` in Apple clone-copy fallback, no execution use of `ENOEXEC` found. Native failure translations remain distinct; add vocabulary only when reached. |

Task 1's real target build additionally reaches `tolower` in
`Support/CachePruning.cpp:112` and `FP_ILOGB0`/`FP_ILOGBNAN` in
`libc/src/__support/FPUtil/ManipulationFunctions.h:86–87` through `APFloat.cpp`.
These remain task 3 libc closure work; no shared libc/runtime edit was made.
The native backend removes Unix header consumers rather than supplying fakes.

The old probe's absent Unix headers (`signal.h`, `spawn.h`, `sys/mman.h`,
`sys/wait.h`, `pthread.h`, and others) are not a kernel API shopping list.
[Native libc portability](../userland/libc-portability.md) remains authoritative.

## Process boundary

**Inspected in the pinned fork:** `-fintegrated-cc1 -c` with one source and
integrated assembly runs cc1 in-process. `clang/lib/Driver/Driver.cpp:5562`
disables integration for multiple jobs or process statistics; source-to-executable
and multiple-source invocations therefore still launch cc1 externally.
`clang/lib/Driver/ToolChains/Pyxis.cpp:96` creates an external LLD `Command`;
`-fuse-ld=lld` does not embed the linker. The first completion keeps compilation
and native LLD launch separate.

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

Task 1 is delivered in the [native Support fork PR](https://git.internal/PyxisOS/pyxis-llvm/pulls/4),
with its [contracts](../development/llvm-support.md) and
[qualification record](../development/experiments/hosted-clang-support.md).
The installed fork pin remains unchanged. Later tasks need separate owner
go-aheads; this line proceeds alongside delta's threads gates 5–8.

1. [x] **LLVM Support platform and header fixes — delivered 2026-10-11.** Implement Pyxis Path/FILE,
   Process/Program, private Memory and fault-policy equivalents in the fork,
   with precise refusals and owned references. Configure selects Pyxis explicitly;
   native host TableGen remains native. Capture baseline I/O/launch costs before
   changing those paths. **Owner check:** inspect platform contracts and ordinary
   Support builds; QEMU copied read/write, path/identity and error checks, without
   claiming a runnable compiler before the thread gates.
2. [ ] **SDK C++ source closure.** Audit and build the stream/C-locale/time-format
   subset above, including real libc dependencies and the reached wide/random
   facilities. Keep filesystem/time-zone expansion out unless reached. Coordinate
   configuration and shared libc edits with delta. **Owner check:** ordinary SDK
   build and QEMU stream/string/time-format behavior, including `locale("C")`
   refusal of unavailable locales. Real mutex/future/TLS integration and threaded
   validation wait on gates 5–8; these are not serial emulations.
3. [ ] **Remaining reached libc operations.** Use the table to implement standard
   functions on native objects, or adapt/refuse unsupported LLVM features.
   Publish userland PRs before parent pins, keeping dependencies fast-forward.
   **Owner check:** the refreshed compile/link inventory names no unexplained
   missing function or successful fake operation; native ownership/error checks
   pass. Shared runtime synchronization waits on delta's gate 8.
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

### Compiler-container boundary

Task 1 leaves the installed pin unchanged. Its Support/header changes and any task 2 libc++
fork changes are fork PRs, not ports-only patches. A local investigation can build
that fork with the existing host compiler. **Before a parent PR adopts the new
fork pin**, the owner must build/publish the matching compiler container and the
PR must update `toolchain/build.sh`, `ci/Containerfile`, workflow image tag and
[toolchain documentation](../development/llvm-toolchain.md#changing-the-pin)
together. Ordinary SDK configuration or libc edits alone need no container
rebuild. Coordinate the chosen fork head with delta's gate 7 TLS change to avoid
publishing incompatible compiler/runtime pins. Do not change the container now.

## Accepted task 1 decisions (2026-10-11)

The owner accepted all three defaults and assigned task 1. These refine the
accepted milestone without changing its thread or container boundaries.

1. **Path/metadata lifetime — accepted:** native directory-backed scheme paths;
   retain opened file/directory references through compiler identity caches and
   require valid domain/object IDs for identity-sensitive input. Refuse providers
   without identity. Keep missing mtime explicitly unknown (including the existing
   unknown `__TIMESTAMP__` spelling), not epoch zero; disable caches requiring it.
   RAM and installed headers remain usable without inventing metadata; retained
   references increase handle/memory cost, to measure before setting budgets.
2. **Output publication — accepted:** owned buffer, exclusive random sibling temp,
   checked writes, FILE sync, same-parent replacement and directory sync; all
   grants/references released once. Before publication failure keeps the old name;
   after rename a sync failure reports uncertainty rather than claiming rollback.
   Cleanup on ordinary errors is explicit; a fatal fault may leave a temp. Refuse
   backends lacking required rename/sync authority. This protects existing artifacts and
   needs parent LOOKUP/CREATE/REMOVE plus output WRITE, not global authority.
3. **Program/fault boundary — accepted:** resolve selected tools through explicit
   delegated roots (no colon-split PATH or `/proc` discovery), snapshot cwd/env,
   preserve ordinary grant attenuation, and own native completion handles until
   observed/closed. Inherited diagnostics only; reject unsupported redirection,
   timeout/resource-limit/statistics/recovery requests before launch. Fatal faults
   terminate; successful cleanup hooks cover orderly errors only. Program uses
   explicit launch authority and owned handles, without crash helpers;
   new child resource delegation still needs an explicit caller selection.

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
[application bundle](program-bundles.md#manifest-and-stack) can now supply a
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

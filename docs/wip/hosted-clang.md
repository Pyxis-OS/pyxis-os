# Clang and LLD hosted on Pyxis

Investigation and proposals, 2026-10-09; no implementation is assigned.
This is the [third LLVM milestone](toolchains-and-runtimes.md#llvmclang-transition-and-hosting):
compile, link and run one small C program inside Pyxis. Rebuilding LLVM itself
and running the [ports runner](source-builds.md) are later results.

**Finding:** the compiler backend exists, but its host support does not.
The current SDK cannot build Clang/LLD unchanged, even with LLVM threads off.
Native image/stack capacity, C++ synchronization/TLS and streams, filesystem
identity/path handling, and launch/fault adaptation are separate prerequisites.
No hosted compiler or target LLVM executable ran in this investigation.

## Measured boundary

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
| Independent real-header and symbol links | Confirmed current open/read/write/pread/pwrite/access/rmdir/stat/lstat/fstat/mkstemp/getenv/fileno/fseeko/ftello; `<cmath>` with std::sqrt parsed. This is availability, not LLVM-compatible semantics. |
| Matching Linux host build | Passed. Used the same fork/options, Linux target and host libstdc++; target build used SDK libc++. |

Concrete absent headers: `machine/endian.h`, `sys/time.h`, `sys/wait.h`,
`sys/resource.h`, `signal.h`, `spawn.h`, `sys/mman.h`, `sys/ioctl.h`,
`sys/statvfs.h`, `dlfcn.h`, `pwd.h`, `poll.h`, `pthread.h`.
These describe the existing Unix backend, not a requirement to supply Unix APIs.

## Gaps and order

The order below separates native contracts from ordinary libc additions.
Independent tasks can proceed together once separately assigned.

| Priority | Gap and concrete evidence | Direction / shared work |
| --- | --- | --- |
| 0: admission | [Program capacity](../kernel/program-loading.md) permits a 256 MiB page-rounded image span and supplies a high fixed eager 8 MiB guarded stack. HOST/NPFS executable capture still has a separate 16 MiB serialized-file bound. | Mapped capacity is qualified with a native fixture, not Clang. Raising installed capture remains a separate peak-memory/admission task before packaging LLVM. |
| 1: build/runtime | Pyxis lacks an LLVM Support platform selection. Generic leaves file_status/getSize, EnvPathSeparator and process types incomplete. Endian selection and the regex include are independent small header fixes. Threads-off still instantiates mutex/recursive_mutex/shared_mutex, condition_variable, shared_future/async; LLD Common/Memory.h:68 uses thread_local. | Follow the [native threads/TLS and runtime plan](threads.md), or explicitly adapt a genuinely serial LLVM source closure. LLVM_ENABLE_THREADS=OFF alone is insufficient; do not export successful fake locks or POSIX threads. Fork changes eventually require a compiler pin/container update. |
| 1: C++ subset | SDK localization/wide strings/random_device are off. raw_os_ostream and Mustache instantiate streams; Clang PPMacroExpansion.cpp:1734 uses stringstream, locale("C") and put_time for __TIMESTAMP__. ExponentialBackoff uses random_device/sleep_for. | Select the actual runtime/source subset. Exceptions and RTTI already work in the SDK; LLVM itself was built with them off. Iostreams/locale are not needed for C output, but remain compile dependencies of this source closure. Shared with further C++ ports, not resolved by libc open flags. |
| 2: metadata | Unix Path.inc and independent stat probes lack ino/dev/atime/mtime/ctime, uid/gid/nlink/blksize; native stat exposes kind/size only. Clang FileManager deduplicates files/directories by UniqueID and stores modification time. | Real native identity/time or a separately specified VFS model; never constant IDs/timestamps. Alias roots and pragma-once make identity a correctness issue. Shared with [Git #568](git-on-pyxis.md), even without persistent caches. Mode/owner/execute-bit assumptions require native authority adaptation, not fabricated permissions. |
| 2: paths and libc | Link probes fail for getcwd/chdir/realpath, setenv/unsetenv, posix_memalign, fdopen/setvbuf, isatty, fcntl, readlink/link/symlink, chmod/fchmod/futimens/posix_fallocate, strerror_r; EINTR/ECHILD/ENOEXEC/EXDEV are absent. | Add proven standard functions where native operations exist; adapt or refuse remaining consumer features. Scheme roots, current grant-chain paths and working_path must stay authoritative. Unix absolute paths, colon PATH, /dev/null and /tmp do not describe Pyxis. Shared with Git and the runner. |
| 2: files and memory | FILE has positioned transfers, resize and sync, but no mapping. MemoryBuffer has a copied-input fallback. LLD FileOutputBuffer has an owned-memory fallback, but allocates it through Memory::allocateMappedMemory and writes the final name directly. | Native private RW allocation plus real FILE I/O is sufficient to investigate first; reject unsupported shared/executable mappings and permission/JIT operations. Output needs complete short-write/error handling and deliberate temp/rename publication. Do not pretend a copied buffer is shared file mapping. Shared buffer/heap/durability issues with Git. |
| 3: launch and faults | Unix Program uses posix_spawn/fork/exec/wait, dup2 redirects and signals; independent probes also lack dup/pipe/getpid/execve/sysconf/getuid/geteuid/gethostname. CrashRecoveryContext still includes signal.h with backtraces/threads off. | Native argv, grants, cwd, streams, environment forwarding and wait; no POSIX-shaped kernel. Shared with Git's eventual CLI and ports os.execute/io.popen replacement. Explicit fault termination first; symbolizer, crash reproducer, error scripts and timed cancellation are later features. |
| 4: install and qualification | Binary/resource headers, SDK, scratch and process memory require separate budgets. Default tool/resource/sysroot discovery assumes host paths. | Install on a toolchain pool volume with explicit read-only roots; tmp scratch is granted RAM storage. Qualify actual native binaries before allowing large installed launches. No boot-archive packaging. |

New libc functions are useful but not drop-in Unix behavior: access checks
grants and refuses X_OK/providers; lstat cannot inspect symlinks; open creation
accepts only 0666. LLVM temp files often request 0600 and executable output
requests execute bits. Use native private-directory authority and launch READ,
with a documented adapter; do not weaken libc's policy to satisfy those modes.
Getenv exists for startup variables. Mutable environment/environ and implicit
child inheritance do not; forward an explicit environment instead.

## Process boundary

**Inspected**, and corroborated by the host driver's `-###` output:
`-fintegrated-cc1 -c` for one source with integrated assembly runs cc1 in-process.
Driver.cpp:5562 disables integration for multiple jobs or process statistics;
source-to-executable and multiple-source compilations therefore still launch cc1.
Pyxis.cpp:96 creates an external LLD Command. `-fuse-ld=lld` does not embed LLD.

LLD has a real `lldMain`/ELF link API in `lld/Common/Driver.h`. An embedding
wrapper is possible, but unbuilt. Prefer lldMain, which cleans up context;
respect canRunAgain and preserve fatal-exit containment. Direct ELF-link
embedding must manage its context cleanup. Integrated cc1 enables signal-based crash
recovery; removing that recovery makes native faults terminate the process.
Simply disabling signals while continuing after a fatal library failure is wrong.
Ordinary file-to-object compilation does not need an internal pipe or a shell.

The smaller first completion is one single-source compile invocation, then a
separate native LLD process and execution of its PXE. A native launcher can do
this without fork/exec. General driver integration and captured-output support
for the runner remain separate work; existing Lua pyxis.run launches/waits but
does not replace the runner's shell commands, Git patching or io.popen capture.

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
mapped spans or required physical backing. They greatly exceed the unchanged
installed-image staging bound. RAM/archive exceptions
bypass only staging, not image admission; putting LLVM there is not the installation plan.

Clang requests an 8 MiB stack and its near-exhaustion diagnostic assumes that
scale. Native initial stacks are now 8 MiB, fixed and eagerly backed; threads-off
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

## Suggested first task and owner decisions

**Priority-0 mapped capacity is implemented:** [program loading](../kernel/program-loading.md),
following process-lifetime task 1 (#612), records the three defaults accepted in
[#613](https://git.internal/PyxisOS/pyxis-os/pulls/613) on 2026-10-09.
[Native qualification](../development/experiments/program-capacity/README.md)
covers disjoint mappings, boundary rejection and unpublished cleanup, plus the
material eager-stack launch and session-memory costs. It provides no compiler
port, automatic growth or public threads. Installed-image staging remains a
separate task; later Clang work is not assigned by that completion.

These are proposed defaults, not accepted implementation authority:

1. **First completion:** default single-source -c plus a separately launched
   LLD, inherited diagnostics, no crash-helper processes. A one-command/reusable
   embedded driver comes later and needs its own fault/lifetime contract.
2. **C++ concurrency route:** default finish the accepted native #567 thread/TLS
   and synchronized-runtime prerequisites, while keeping LLVM worker threads off
   for the first compiler. A serial-only source adaptation is an alternative,
   but the current probe proves stock threads-off cannot replace those tasks.
3. **Memory route:** default copied input and owned output buffers over native
   FILE/private-memory operations; defer shared mapping/JIT/protection changes.
   Installed tools live on a pool volume. Set large-image/stack and staging
   budgets from native artifacts and measured admission, preserving hard failure
   on exhaustion; never bypass bounds with boot/RAM packaging.

Docs only. No dependency pins, kernel/libc code, toolchain container, tests or
QEMU changes. Probe branches stay unmerged; stop for owner review.

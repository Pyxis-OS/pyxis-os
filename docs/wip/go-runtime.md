# Go on Pyxis

Status: tasks 1 and 2 merged; task 3 is proposed below, without implementation.
The sparse private-VM foundation, native `GOOS=pyxis` runtime/direct P1F linker,
opt-in hello and explicit parallel Ps are implemented. Native startup/parallel
qualification and the file/time/network tasks remain open.
See the [Go build guide](../development/go-toolchain.md) and
[startup record](../development/experiments/go-native-hello/README.md) and
[parallel record](../development/experiments/go-parallel/README.md).

**Standing direction (chronium, 2026-10-11; reconfirmed 2026-10-21):** use the
standard Go gc compiler, assembler, linker and runtime, a new `GOOS=pyxis`, static
amd64/v1 and `CGO_ENABLED=0`. Use an owner-maintained pinned Go fork and host
cross-toolchain, separate from LLVM. The private reservation/commit direction
below is accepted, but startup must be fast: reserving unused virtual space must
not allocate data frames, zero them or populate proportional page tables.
TinyGo, a replacement runtime and Linux syscall emulation are outside this plan.

## Evidence and source pin

Inspection uses Pyxis `221e8f8383d5`, userland `3b3ec3ad5906`, and **Go 1.27.3**,
[released 2026-10-19](https://go.dev/doc/devel/release), the latest stable release
at investigation time. The exact GitHub tag archive was fetched successfully
through the existing cache:

- Upstream: `https://github.com/golang/go/archive/refs/tags/go1.27.3.tar.gz`.
- Cache: `https://repo.internal/repository/raw-github/golang/go/archive/refs/tags/go1.27.3.tar.gz`.
- SHA-256: `79c0fe60909817414323a8973e51e531de087bdafa06db8101f45d24155f6ac3`.
- Licence: Go's BSD-3-Clause `LICENSE`; retain it and upstream notices when
  packaging the fork. This archive is not the differently hashed go.dev source
  archive. No new mirror or LLVM-container build is needed for this investigation.

Source links below name that release. Host probes used a toolchain built from
this archive with `/usr/lib/golang` Go 1.26.8 as bootstrap, `GOMAXPROCS=8`,
`GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off CGO_ENABLED=0 GOAMD64=v1`.
No external modules were fetched. Linux results establish source/layout and a
reference cost, not a working Pyxis runtime or a native startup prediction.

## Current native boundary

| Area | Native boundary | Go task 1 behavior |
| --- | --- | --- |
| Threads | [CREATE/EXIT and completion](../interfaces/threads.md), executable trampoline with RDI argument, guarded system stack and optional distinct FS context; completion follows native retirement. Current policy allows 64 live/retiring tasks per process, including the initial task. | Native Ms/sysmon and completion-gated observer collection are implemented. No stack/TLS or M reclamation follows a Go-side exit marker alone. GOMAXPROCS counts Ps, not native threads. |
| Parking | [32/64-bit address WAIT/WAKE](../../include/abi/thread.h), compare/enrol atomically, natural alignment, absolute monotonic deadlines, explicit infinite wait and mapping-incarnation cleanup. | Runtime 32-bit lock/note words use native WAIT/WAKE with predicate loops, atomics and checked relative-to-absolute deadlines. No futex/requeue/PI kernel ABI was added. |
| Startup and TLS | [startup_info](../../include/abi/startup.h) in RDI, capability environment, raw FS=0, caller-only initial SET_FS and CREATE-supplied FS. Rebinding is refused. | Pre-g native entry binds a separate guarded FS context; the linker and assembler use g at FS−8. No libc startup or ordinary m.tls/global context is registered. |
| Stacks | [Loader](../kernel/program-loading.md): initial eager RW/NX 1 MiB stack, bundle-selectable 1–8 MiB, unmapped lower guard, no automatic growth. | Initial g0 uses the upstream conservative 64 KiB window below entry RSP. Other M/g0 stacks are separately guarded/registered; copied/growing goroutine stacks stay Go-owned heap memory. |
| Private VM | [MEMORY](../../include/abi/memory.h) has eager/guarded allocation and aligned sparse RESERVE, COMMIT, DECOMMIT and whole identity release. [VM mutations](../kernel/memory.md#execution) remain BSP-owned with borrower drainage and sibling quiescence. | The native adapter uses reservation bookkeeping and actual commit/decommit transitions. Sparse operations accept only RESERVE identities. |
| Images | [P1F](../../include/pxe/p1f.h) has fixed addresses, page-aligned non-overlapping R/RX/RW segments, zero-filled tails, W^X and executable entry. Loader span is 256 MiB including gaps/BSS; serialized executable capture is independently capped at 128 MiB. | The direct writer retains loadable Go metadata, compact payload and zero-filled BSS; no conversion or loader symbol table. Runtime reservations remain separate from image span. |
| Grants | [Bundles](../userland/program-bundles.md) can explicitly request memory/manage, threads/manage, clock/read+sleep and random/read. Raw ordinary application launches do not implicitly acquire threads/manage. | Hello declares all four grants and uses supplied console/pipe diagnostics. Missing authority is an admission error; native entropy failure terminates, without predictable substitution. |
| CPU and faults | [Architecture state](../../arch/x86_64/user.c) preserves x87/SSE; OSXSAVE/AVX and user FSGSBASE remain disabled. A hardware fault stops the process and siblings. | amd64/v1 and OS-aware feature gating avoid AVX and WRFSBASE assumptions. Hardware faults stay process-fatal; no asynchronous signal handler or fault-to-sigpanic conversion. |

[Go startup](https://github.com/golang/go/blob/go1.27.3/src/runtime/asm_amd64.s#L332)
runs args, osinit and schedinit before creating runtime.main. That
[main starts sysmon](https://github.com/golang/go/blob/go1.27.3/src/runtime/proc.go#L177)
before package initialization, even with GOMAXPROCS=1. Normal GC starts sweeping
and scavenging goroutines through
[gcenable](https://github.com/golang/go/blob/go1.27.3/src/runtime/mgc.go#L207).
Hello therefore needs real threads, parking, memory, time and entropy, rather
than a one-thread runtime with GC disabled.

Native calls use the shared capability environment and native objects. Early
assembly must decode startup and retain the needed handles without Go allocation
or an initialized g. Binding FS, M creation and completion ownership must be
settled before Go can use runtime locks. Random reads obey the existing 256-byte
request and five-second deadline limits; refuse unavailable entropy. CPU online
count is not a caller-affinity entitlement: start with one P, then qualify the
chosen GOMAXPROCS against actual thread admission and placement.

## Fast private memory

The release's [page allocator](https://github.com/golang/go/blob/go1.27.3/src/runtime/mpagealloc_64bit.go#L74)
reserves five summary arrays: **0.125 + 1 + 8 + 64 + 512 = 585.125 MiB**.
Its scavenge index reserves another **512 MiB**: 48 address bits, 4 MiB chunks,
eight-byte entries. This **1,097.125 MiB is address space, not resident RAM**.
The amd64 heap reserves 64 MiB-aligned arenas and initially maps heap in 4 MiB
chunks; it does not pre-reserve a 512 GiB heap at startup. These sizes describe
this Go release, not new kernel limits.

A second trap is the
[arena index](https://github.com/golang/go/blob/go1.27.3/src/runtime/malloc.go#L851):
the non-Windows amd64 layout allocates a 32 MiB L2 table through sysAllocOS and
explicitly relies on demand paging. Cheap sysReserve alone does not fix that.
The Pyxis target reuses Go's existing two-level index, with six L1 bits:
a 512-byte first level and 512 KiB eager L2 allocations as needed, retaining
64 MiB arenas. This is a target layout choice to measure, not a request for a
fault-driven pager or a reduced heap-address limit.

### Accepted private-VM reservation/commit contract

Keep the already accepted private-VM contract (owner, 2026-10-11), under the
existing MEMORY authority and process VM registry. Leave ordinary eager
ALLOCATE/RELEASE intact; guarded eager contexts share the reservation registry
and pin rules. The implemented ABI names describe these accepted behaviors:

Scope clarification (orchestrator, 2026-10-21): COMMIT/DECOMMIT accept only
identities created by sparse RESERVE. Existing guarded stack/TLS allocations
stay eager and unchanged: pinned guarded identities return BUSY, unpinned ones
BAD_REQUEST. Sparse identities cannot be used as native guarded contexts.

| Boundary | Accepted behavior |
| --- | --- |
| Ownership | Caller-process-owned generational reservation identity, base and rounded span. Transferred MEMORY handles still manage only the caller. Closing a handle releases nothing; drained process teardown reclaims everything. |
| Reserve | Nonzero page-rounded size and power-of-two page-or-greater alignment, including 64 MiB. Choose a disjoint lower-half range excluding image, startup, stacks/guards and reservations. Metadata/address space only; no backing, per-page metadata or page-table population proportional to unused span. No fixed placement/replacement or partial unreserve. |
| Commit | Contained page-aligned subrange, eager private zeroed RW/NX backing for missing pages, preserving existing committed bytes. Mixed committed/unbacked ranges work. No executable/shared/file backing or demand faults. |
| Decommit | Discard contained committed pages, retire their mapping incarnations/address waits, retain reservation; recommit produces zeros. Already unbacked pages are unchanged. Live/retiring stack/FS pins refuse decommit. |
| Release | Whole identity only, freeing committed extents and address range. Stale identities refuse after address reuse. Live/retiring contexts remain pinned until native completion. |
| Failure | Invalid size/alignment/identity/range: BAD_REQUEST; authority: DENIED; invalid reply: BAD_BUFFER before effects; physical/virtual/metadata exhaustion: NO_MEMORY; conflicting pins/mutation or bounded quiescence failure: BUSY before effects. Failure preserves existing mappings, bytes and identities. |

Prepare fallible bookkeeping and provisional backing before publication; failed
commit rolls back only new backing. Prepare decommit/release bookkeeping before
irreversible changes. Operate on committed extents, not every page of empty
reservations; account separately for retained page-table infrastructure. Keep
explicit metadata/work budgets and bounded commit chunks. Capture inputs under
activity leases, park outside the private root, close admission and drain actual
borrowers, then mutate with the BSP token and acknowledged CR3 departure. Do not
block the BSP executor or retain spin/queue guards while waiting. Validate and
hold the RESERVE reply through final publication; unmapping operations use
captured input with no post-unmap user copy. These extend the
[current VM protocol](../kernel/memory.md#execution), not its ownership model.

The adapter maps aligned native reservations directly, skipping Go's optional
fixed-address hint probes and prefix/suffix trimming. Keep allocation-free early
ownership records. Prepared ranges can stay unbacked until sysUsed; commit missing
pages there, decommit on sysUnused, and release whole identities on sysFree or
sysUnreserve. Preserve actual zeroing/state accounting; do not report physical
scavenging while retaining backing. Qualify exhaustion and bounded precommit
BUSY handling without turning arbitrary committed I/O into retries.

Interior DECOMMIT can return NO_MEMORY while preparing split metadata, before
changing existing backing. On this scavenging failure, `sysUnusedOS` retains the
affected committed pages and clears them, preserving Prepared zeroing semantics
without claiming physical release. Other backing failures remain explicit fatal
errors. Retained backing can consume RAM until reuse or whole-identity release.

## Executable metadata and output

**No loader-visible symbol table is needed for the first Go runtime.** Go's
[moduledata](https://github.com/golang/go/blob/go1.27.3/src/runtime/symtab.go#L400)
contains pointers to PC/name tables, types, GC data and initialization tasks.
The linker always constructs
[pclntab and moduledata](https://github.com/golang/go/blob/go1.27.3/src/cmd/link/internal/ld/main.go#L429);
[.gopclntab](https://github.com/golang/go/blob/go1.27.3/src/cmd/link/internal/ld/data.go#L2120)
and associated tables are ordinary loadable data. Preserve these bytes and
linked addresses for runtime traceback, explicit panic and GC stack maps.
Stripping external ELF symbols/DWARF with `-s -w` preserves them, as the host
probe confirms. The [P2F optional-symbol direction](../technical-debt.md#backtraces-and-symbols)
remains useful for kernel fault reporting/debugging generally; Go does not need
it merely to find its own functions.

**Accepted (chronium, 2026-10-21):** Go's linker writes static fixed-address P1F
executables directly, as LLD and TCC do. There is no host ELF-to-P1F converter.
Emit the P1F header, segment descriptors and loadable bytes, preserving linked
addresses, entry, permissions, zero-filled BSS and Go's internal metadata. Enforce
all P1F span/overflow/alignment checks; refuse dynamic dependencies and unresolved
runtime relocations. Native Go owns its separately registered TLS context, with
no ELF PT_TLS dependency. An ELF debugging side output is optional only if the
linker can emit it without extra machinery; it is not a prerequisite.

## Startup evidence and budget

Host-only probe: Linux amd64 KVM VM, i9-12900K CPUID, 12 vCPUs, 24 GiB RAM,
2026-10-21. Program: `package main; func main() { println("hello") }`.
Build with the pinned `go build`, `CGO_ENABLED=0 GOAMD64=v1`; compare normal ELF
and `-ldflags='-s -w'`. Both are Linux binaries. `readelf -W -l/-S` shows three
RX/R/RW load segments, no INTERP/DYNAMIC/TLS and retained .gopclntab in stripped
output. Normal ELF is 1,904,052 bytes; stripped ELF 1,228,960; its calculated
P1F serialization is 1,221,057 bytes with 1,445,888-byte rounded image span.
These are inspected Linux layouts, not emitted Pyxis executables. A stripped
fmt.Println variant is 1,523,872 bytes and has the same three-segment shape; it
was not timed.

Three warm-cache batches, each ten discarded warmups then 101 measured
launches per GOMAXPROCS setting, `GODEBUG=asyncpreemptoff=1`. Python
`perf_counter_ns` brackets Popen through reading `hello` from stderr and then
waits for successful exit; no debugger/profiler in timed runs. Parent scheduling
and Linux process creation are included. Ranges below are across batch statistics:

| Linux probe | Launch to first output p50 / p95 | Observed first-output min–max | Launch through exit p50 / p95 |
| --- | --- | --- | --- |
| GOMAXPROCS=1 | 1.46–1.64 / 2.50–2.78 ms | 1.10–3.59 ms | 1.60–1.80 / 2.68–2.89 ms |
| GOMAXPROCS=4 | 1.44–1.60 / 2.51–2.73 ms | 1.15–3.73 ms | 1.60–1.77 / 2.64–3.15 ms |

One separate GDB stop at main.main with GOMAXPROCS=1 observed three native
threads, 1,197.7 MiB of user mappings and **492 KiB RSS**. These are debugger
observations, not Pyxis memory promises or timing samples. Commands, raw samples
and snapshot stay at `/shared/present/go-investigation-20261021/`; no benchmark
framework is added to the tree.

**Estimated engineering budgets, proposed for task 1:** warm launch to first
hello p95 **at most 50 ms native** and **250 ms nested QEMU/KVM** (four vCPUs,
2 GiB guest, GOAMD64=v1, GOMAXPROCS=1). These are engineering estimates for acceptance
budgeting, not measurements or runtime guarantees; no seconds-long startup is
acceptable.
Native qualification uses AC power. Record the active clock source; nested
results taken before kvmclock lands must identify that limitation. TCG is a
separate correctness-only run, not compared with KVM. Revisit a missed budget with chronium before expanding
the port, rather than accepting eager large mappings as an unavoidable cost.

Measure with the existing native launcher/job and clock interfaces, starting
immediately before LAUNCH through first hello observed on a supplied pipe, then
process/group retirement. LAUNCH captures the executable internally, so the
budget includes those source reads and loader work. Any capture-only attribution
needs an existing trace/debugger observation, reported separately from unprofiled
runs, not subtracted from the user-visible startup figure. Use the same artifact
and launch path before/after VM/runtime changes; report cold first
launch separately from at least 30 warm samples, p50/p95/range, image/stack and
peak committed/page-table/metadata bytes. Include runtime bootstrap, schedinit,
sysmon and every package init; Go's runtimeInitTime is sampled too late to cover
all startup. No normal log traffic in the timed envelope. Record the source type
and cache state; do not hide slow native source I/O or pad the launch budget with
a host Remote transport round trip. The original investigation measured only Linux. Task 1 now has the separate
[Pyxis startup record](../development/experiments/go-native-hello/README.md);
its nested results do not establish native startup.

## Accepted decisions (chronium, 2026-10-21)

1. **Executable route:** Go's linker writes P1F directly. No host ELF-to-P1F
   converter. Preserve Go's loadable runtime metadata; P2F symbols are not a
   prerequisite. Keep an ELF only if the linker can emit it as a debugging side
   output without extra machinery.
2. **Initial scheduling/fault profile:** cooperative-only Go preemption, normal
   multi-M runtime/GC, native kernel thread preemption, Go stack-check safe points
   and process-fatal hardware faults. Explicit Go panic/recover works;
   fault-derived recover does not. A call-free loop may delay another goroutine
   or stop-the-world GC indefinitely. Qualify workloads with safe points and
   document this limit; do not claim complete upstream scheduling/fault semantics.
   Native resumable faults and asynchronous safe-point delivery remain outside
   this initial profile and are not implied by the
   [control/events direction](control-events-faults.md).
3. **Runtime boundary:** a small Go/assembly native capability-call layer, no
   libc, owning startup, TLS, memory, parking, clock, entropy, supplied output and
   exit. Keep pre-g native entry allocation-free, and low-level OS hooks
   allocation-free/nosplit wherever their calling contracts require it; normal
   runtime bootstrap still allocates. Bracket blocking calls with the proper Go
   scheduler/P handoff. Use the accepted private-VM family and the bounded
   arena-index layout above.

Chronium created [pyxis-go](https://git.internal/PyxisOS/pyxis-go), based on the
`go1.27.3` tag at `2aae1000ea1d` on `release-branch.go1.27`, and authorized task 1
on 2026-10-21. Work uses a separate `pyxis` branch, never the release branch.
The first runtime profile above is qualified in nested QEMU; later interfaces and native startup still need their own evidence.

## Tasks

Tasks 1 and 2 are merged. Task 3 was assigned (chronium, 2026-10-22), with
decisions required before implementation. Task 4 still needs its own go. Completed
task-1 work:

- [x] Kernel/ABI sparse private memory, with QEMU/GDB qualification; see the
  [foundation record](../development/experiments/go-private-vm/README.md).
- [x] Native Go fork target, entry/TLS/calls and direct P1F writer; measured hello.
- [x] Parent recipe/pins and opt-in image integration.

Task 2 keeps the one-P default and `runtime.NumCPU() == 1`; explicit
`GOMAXPROCS` is not capped to that value. Four/eight Ps and dynamic changes use
the existing native Ms, address parking and VM serialization without new kernel
or Go runtime mechanisms. See the [parallel record](../development/experiments/go-parallel/README.md).

- [x] Channels, mutexes, copied stacks, live heap graphs and GC with parallel Ps.
- [x] Timed/infinite native parking and another goroutine's progress during a
  blocked native wait, including GC with one P.
- [x] Completion-gated native M retirement and whole-process/group termination.
- [x] Memory/native-thread exhaustion, explicit diagnostics and subsequent launch.

| Task | Scope and qualification | What chronium can do afterwards |
| --- | --- | --- |
| 1. Cross-compiled native hello, with fast startup — complete | Implement the accepted sparse private-VM foundation first under existing BSP/quiescence ownership, then a pinned Go fork's new target, native entry/TLS/calls, normal one-P runtime/GC and the linker's direct P1F writer. Register GOOS/build tags and target/linker/entry handling explicitly; do not opt into Unix tags. Build on horse with GOOS=pyxis CGO_ENABLED=0 GOAMD64=v1, run hello in QEMU, measure startup/backing and full cleanup. Qualify sparse reservation cost, recommit zeroing and real exhaustion. | Launch a real Go hello, see its exit and a measured startup report, then schedule native startup qualification. |
| 2. Parallel goroutines — complete | GOMAXPROCS>1 on four or more vCPUs; channels, mutexes, timed/infinite address parking, copied stacks, GC, native M retirement and whole-process stop. Include a blocked native call while another goroutine progresses; report memory/admission exhaustion and cooperative-only limits. | Run concurrent allocating Go programs with honest lifecycle and progress evidence. |
| 3. Files and time | Native os.File/capability-root adapters, supplied streams, partial I/O/closure/errors, fmt, monotonic timers and wall time with explicit availability. Reuse the capability environment, command search and native jobs where an actual launch consumer needs them; no global Unix descriptor namespace. | Run small file/format/timer programs with unchanged application Go and granted roots. |
| 4. Networking | Native sockets/readiness and cancellation/deadlines for TCP, UDP and DNS, feeding Go netpoll without epoll emulation. Verify no authority appears from host path strings or environment text. | Run a network client against explicit supplied networking authority. |

Go fork commits/mirrors belong in the chronium-created repository; publish any
kernel/ABI/SDK prerequisite before the Go recipe and parent image pins, following
[repository boundaries](../development/sdk-and-repositories.md). Source downloads
stay pinned through the cache. No LLVM rebuild is intrinsic to the no-cgo target.
Later only: hosted compiler/assembler/linker and module fetch/build need the
native file/process/network layers; Tailscale needs its concrete native tunnel
and device-authority contract after networking. Neither is a task in this slice.
Race detector, cgo, profiling and resumable hardware faults remain outside it.

## Task 3: files, time and native readiness — proposed

Deliver unchanged Go programs using `os`, `io`, `fmt` and the full `time` import
on the existing capability and clock interfaces. No libc, Unix syscall
translation, global descriptor namespace, process launcher, network stack or
new kernel ABI. Finish only the target-specific `syscall` types, errors and
operations these packages actually require; unsupported operations fail plainly.
Preserve the startup budget; importing these packages must not introduce eager
poller threads or native resources into diagnostic hello.

The existing baseline has native timers/parking, but a full `time` import fails
on missing target `syscall` definitions. `fmt` reaches the unfinished `os` adapter.
Task 2's private timer/note probes are not substitutes for those public packages.
Capture the corresponding functional baseline before implementation and repeat
hello startup with the same clock/configuration if its initialization path changes.

### Decision 1: native file ownership and paths — pending

**Recommended default:** an `os.File` owns one native capability and its adapter
state. `NewFile` adopts a held native handle on success; `Fd` borrows that native
handle, not an integer from a Unix descriptor table. Failed adoption leaves the
caller owning its handle. The caller must not adopt the same handle twice. Standard streams are
owned copies of the supplied `stdin`, `stdout` and `stderr`, preserving their
actual interfaces and rights. Runtime diagnostics retain separate grants; closing
an ordinary standard stream does not revoke the runtime's diagnostic copy.
Share startup FILE-stream cursor state so ordinary output and later diagnostics
do not overwrite one another. Other independent opens have independent cursors.

Use the existing [native path contract](../userland/paths.md): exact `name://`
lookup through the caller's capability environment, supplied package roots where
applicable, and relative paths through the retained startup cwd chain. No ambient
`/` root, URL reinterpretation or re-resolution of descriptive cwd text. Go cwd
changes retain real directory handles, are process-wide and synchronize with
relative operations. Text environment operations use the native caller table;
text is never interpreted as a new grant.

The first slice covers native DIRECTORY/FILE roots, regular-file open/create,
read/write and positioned I/O, seek, truncate, sync, available metadata and
native directory operations where granted, plus supplied pipe/console/terminal streams and
`os.Pipe` when pipe creation is granted. Exported provider/forward-only STREAM
adapters and process execution are outside this task; encountering an unsupported
interface returns an error before consuming it. Do not add host-style device
paths, permission or signal emulation. Missing metadata remains visibly absent
in native `FileInfo.Sys()` validity fields, rather than invented identities or
timestamps.

Keep Go's concurrent `File` guarantees, partial counts, `io.EOF` only after
buffered data drains, real peer-closure/input-loss errors and `errors.Is` support
for the applicable standard error classes. Preserve the native status in errors.
Advance cursors only for confirmed bytes; do not replay an uncertain mutation.
Refuse `O_APPEND` in this slice: native FILE offsets do not provide an atomic
append operation across independent holders. Do not disguise SIZE+WRITE as one.
Closing wakes Go waiters and retires in-flight references before closing/reusing
the native handle. Regular FILE calls without readiness release the P with the
proper syscall handoff; `SetDeadline` there returns `os.ErrNoDeadline`.

### Decision 2: bounded native netpoll and wake authority — pending

**Recommended default:** include the real Go netpoller now, backed by
[`wait_many`](../../include/abi/wait.h), for pipe and supplied console/terminal
I/O; later sockets use this same path. TRY/poll transfers precede registration;
WOULD_BLOCK arms only the needed direction and parks the goroutine. Successful
readiness disarms that direction; retry the transfer because readiness reserves
nothing. Never watch every writable handle continuously. Stream waits do not
consume one native M per waiting goroutine. Locked `println`/panic diagnostics
remain the explicitly accepted blocking runtime path, outside ordinary
`os.File`/`fmt` I/O.

Use one blocking poller and at most **31 registered application handles**, with
one additional interest for a private wake pipe: the native ABI currently permits
32 interests. Both read/write modes share one interest per handle. Distinct pipe
ends consume distinct slots. Beyond this bound, registration returns a resource-
limit error; do not fall back to blocking an M or rotate blocking waits over
subsets. Regular FILEs do not occupy readiness slots. Many goroutines on an
admitted stream still park in Go, not one native task each.

Request the existing **`pipe/create`** bundle grant when readiness or `os.Pipe`
is needed. Lazily create one byte-only wake pipe, retained privately until process
exit; no new launcher-supplied capability, service or ambient authority. Wake
registration changes, closes and earlier timer deadlines through coalesced TRY
writes; drain the wake reader before rebuilding the wait set. Missing creation
authority is an explicit error, not an I/O fallback. Long/infinite Go waits
reissue native waits within the 30-second ABI bound, preserving the Go deadline.

Snapshot registrations under a short guard and release it before waiting.
Validate registration generations and Go poll-descriptor generations on return.
Unregister/evict/wake on close and retire any poller snapshot before native CLOSE
or descriptor reuse, including the interval before native wait admission. Retain
native operation buffers until completion; no guard spans a native readiness wait.

The alternative is a separately designed larger fan-in mechanism or sharded
pollers. The recommendation keeps that out of this first file/time slice and
makes its capacity visible, rather than silently growing kernel stack usage.

### Decision 3: wall time and timezone availability — pending

**Recommended default:** timers, sleeps and deadlines use native monotonic time.
Wall time comes only from `CLOCK_WALL_NOW`, preserving its availability and
approximate boot-seed accuracy; never synthesize a date from uptime. Provide a
Pyxis-specific `syscall.ClockWallNow() (Timespec, error)` availability read.
`time.Now()` cannot return an error, so retain the current runtime's explicit
fatal diagnostic on unavailable/invalid wall time. Programs needing to tolerate
absence can check availability. `Sleep` and callback timers (`AfterFunc`) need
only monotonic time; channel timers/tickers produce `Time` values through `Now`
and therefore still need wall time. Timer scheduling itself remains monotonic. Do not change the clock service or boot policy.

Default `time.Local` to UTC. Explicit `ZONEINFO` uses already granted native
file/directory paths; `time/tzdata` and `LoadLocationFromTZData` remain opt-in
embedded/caller-provided data. `LoadLocation` returns an error for unavailable
zone data. No ambient `/etc/localtime`, host GOROOT path, automatic timezone
download or new timezone grant. This defines representation, not a claim that
an approximate UTC seed is an accurately synchronized clock.

### Delivery and qualification after acceptance

1. Native low-level Go file/path/error ownership and syscall definitions, then
   standard `os`/`fmt` and full `time` imports; use the existing fork and ABI.
2. Native netpoll registration/wake/close/deadline integration for supported
   streams, preserving nonblocking transfer and completion lifetimes.
3. QEMU on one/four or more vCPUs: RAM/boot files, attenuated/missing roots,
   partial I/O, empty versus closed streams, close during pending reads/writes,
   many parked goroutines without matching M growth, capacity refusal, timers
   and earlier-deadline wakeup, wall-time availability and actual process cleanup.
   Use ordinary standalone programs and interactive debugger inspection; no new
   test framework or fault injection. Label nested clocks and measurements.
4. Fork PR first, then parent recipe pin/bundle grants/docs. Preserve default
   images and report checks against the exact submitted revisions.

**Before task 4:** propose native thread-limit sizing from actual task-3 M use
(including poller/sysmon and blocking disk calls) and guarded stack/TLS/metadata
cost. The current 64 live/retiring-task cap remains unchanged in task 3. Revisit
netpoll fan-in alongside it before networking; raising the thread limit alone is
not a substitute for parking socket goroutines in Go.

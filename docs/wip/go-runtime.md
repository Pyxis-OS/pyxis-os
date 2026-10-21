# Go on Pyxis

Status: source investigation and accepted decisions, 2026-10-21. No
`GOOS=pyxis` target exists and no Go executable has run on Pyxis. Implementation
waits for chronium's explicit go and the mirrored Go fork to exist.

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

| Area | Present on main | Go adaptation or missing piece |
| --- | --- | --- |
| Threads | [CREATE/EXIT and completion](../interfaces/threads.md), executable trampoline with RDI argument, guarded system stack and optional distinct FS context; completion follows native retirement. Current policy allows 64 live/retiring tasks per process, including the initial task. | Real M creation, sysmon, observer collection and bounded exhaustion handling. Do not reclaim stacks/TLS on a Go-side exit marker before native completion. GOMAXPROCS counts Ps, not native threads. |
| Parking | [32/64-bit address WAIT/WAKE](../../include/abi/thread.h), compare/enrol atomically, natural alignment, absolute monotonic deadlines, explicit infinite wait and mapping-incarnation cleanup. | Map runtime 32-bit lock/note words directly; retain predicate loops, atomics and relative-to-absolute overflow checks. No futex/requeue/PI ABI is needed. |
| Startup and TLS | [startup_info](../../include/abi/startup.h) in RDI, capability environment, raw FS=0, caller-only initial SET_FS and CREATE-supplied FS. Rebinding is refused. | Native entry owns Go TLS before any TLS-using Go code. Allocate a guarded context and bind Go's g slot with the matching linker offset; do not run libc startup and overwrite its FS. Go's ordinary m.tls/global address alone is not an admitted guarded context. |
| Stacks | [Loader](../kernel/program-loading.md): initial eager RW/NX 1 MiB stack, bundle-selectable 1–8 MiB, unmapped lower guard, no automatic growth. | Derive conservative initial g0 bounds from entry RSP within the guaranteed minimum backing (upstream starts with a 64 KiB window); startup_info supplies no stack size. Allocate/register other M/g0 stacks; copied/growing goroutine stacks remain Go-owned ordinary memory, not native thread registrations. |
| Private VM | [MEMORY](../../include/abi/memory.h) has eager zeroed RW/NX allocation, exact whole release and guarded eager identities. [VM mutations](../kernel/memory.md#execution) remain BSP-owned with borrower drainage and sibling quiescence. | Aligned reserve-only, subrange commit/decommit and whole reservation release are still missing. Existing internal reservation helpers are not a public sparse-memory contract. |
| Images | [P1F](../../include/pxe/p1f.h) has fixed addresses, page-aligned non-overlapping R/RX/RW segments, zero-filled tails, W^X and executable entry. Loader span is 256 MiB including gaps/BSS; serialized executable capture is independently capped at 128 MiB. | Static Go loadable data fits this shape. Runtime reservations are separate from image span. A Linux binary in P1F still has the wrong entry, TLS and calls. |
| Grants | [Bundles](../userland/program-bundles.md) can explicitly request memory/manage, threads/manage, clock/read+sleep and random/read. Raw ordinary application launches do not implicitly acquire threads/manage. | First hello is a bundle with these grants and supplied diagnostic output. Missing authority is an admission error, not a predictable entropy or clock substitute. |
| CPU and faults | [Architecture state](../../arch/x86_64/user.c) preserves x87/SSE; OSXSAVE/AVX and user FSGSBASE remain disabled. A hardware fault stops the process and siblings. | Keep GOAMD64=v1 and Go's OS-aware CPU-feature checks; never infer AVX usability from CPUID alone or use WRFSBASE. No fault-to-sigpanic or asynchronous signal handler exists. |

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
The proposed Pyxis target reuses Go's existing two-level index, with six L1 bits:
a 512-byte first level and 512 KiB eager L2 allocations as needed, retaining
64 MiB arenas. This is a target layout choice to measure, not a request for a
fault-driven pager or a reduced heap-address limit.

### Accepted private-VM reservation/commit contract

Keep the already accepted private-VM contract (owner, 2026-10-11), under the
existing MEMORY authority and process VM registry. Leave ordinary eager
ALLOCATE/RELEASE intact; guarded eager contexts share the reservation registry
and pin rules. Operation names here describe behavior, not new placeholder APIs:

| Boundary | Accepted behavior, not implemented |
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
a host Remote transport round trip. Pyxis timing remains unmeasured until a real
native target runs; this docs-only investigation performs no QEMU Go boot.

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

Chronium will mirror `golang/go` and create the fork, based on the `go1.27.3` tag
on `release-branch.go1.27`, as with LLVM. No implementation starts before that
fork exists and chronium gives an explicit go. These are accepted contracts,
not implemented behavior.

## Proposed task split

These are bounded deliveries, not assignments or permission to start code.

| Task | Scope and qualification | What chronium can do afterwards |
| --- | --- | --- |
| 1. Cross-compiled native hello, with fast startup | Implement the accepted sparse private-VM foundation first under existing BSP/quiescence ownership, then a pinned Go fork's new target, native entry/TLS/calls, normal one-P runtime/GC and the linker's direct P1F writer. Register GOOS/build tags and target/linker/entry handling explicitly; do not opt into Unix tags. Build on horse with GOOS=pyxis CGO_ENABLED=0 GOAMD64=v1, run hello in QEMU, measure startup/backing and full cleanup. Qualify sparse reservation cost, recommit zeroing and real exhaustion. | Launch a real Go hello, see its exit and a measured startup report, then schedule native startup qualification. |
| 2. Parallel goroutines | GOMAXPROCS>1 on four or more vCPUs; channels, mutexes, timed/infinite address parking, copied stacks, GC, native M retirement and whole-process stop. Include a blocked native call while another goroutine progresses; report memory/admission exhaustion and cooperative-only limits. | Run concurrent allocating Go programs with honest lifecycle and progress evidence. |
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

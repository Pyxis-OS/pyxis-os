# Go on Pyxis

Status: tasks 1–3 are merged. The accepted readiness-set and native-thread
prerequisites are merged (2026-10-22).
The sparse private-VM foundation, native `GOOS=pyxis` runtime/direct P1F linker,
opt-in hello and explicit parallel Ps are implemented. Native startup/parallel
qualification and networking remain open.
See the [Go build guide](../development/go-toolchain.md) and
[startup record](../development/experiments/go-native-hello/README.md) and
[parallel record](../development/experiments/go-parallel/README.md) and
[files/time record](../development/experiments/go-files-time/README.md) and
[readiness/capacity record](../development/experiments/go-readiness-set/README.md).

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
| Threads | [CREATE/EXIT and completion](../interfaces/threads.md), executable trampoline with RDI argument, guarded system stack and optional distinct FS context; completion follows native retirement. Current policy allows 256 live/retiring tasks per process, including the initial task. | Native Ms/sysmon and completion-gated observer collection are implemented. No stack/TLS or M reclamation follows a Go-side exit marker alone. GOMAXPROCS counts Ps, not native threads. |
| Parking | [32/64-bit address WAIT/WAKE](../../include/abi/thread.h), compare/enrol atomically, natural alignment, absolute monotonic deadlines, explicit infinite wait and mapping-incarnation cleanup. | Runtime 32-bit lock/note words use native WAIT/WAKE with predicate loops, atomics and checked relative-to-absolute deadlines. No futex/requeue/PI kernel ABI was added. |
| Startup and TLS | [startup_info](../../include/abi/startup.h) in RDI, capability environment, raw FS=0, caller-only initial SET_FS and CREATE-supplied FS. Rebinding is refused. | Pre-g native entry binds a separate guarded FS context; the linker and assembler use g at FS−8. No libc startup or ordinary m.tls/global context is registered. |
| Stacks | [Loader](../kernel/program-loading.md): initial eager RW/NX 1 MiB stack, bundle-selectable 1–8 MiB, unmapped lower guard, no automatic growth. | Initial g0 uses the upstream conservative 64 KiB window below entry RSP. Other M/g0 stacks are separately guarded/registered; copied/growing goroutine stacks stay Go-owned heap memory. |
| Private VM | [MEMORY](../../include/abi/memory.h) has eager/guarded allocation and aligned sparse RESERVE, COMMIT, DECOMMIT and whole identity release. [VM mutations](../kernel/memory.md#execution) remain BSP-owned with borrower drainage and sibling quiescence. | The native adapter uses reservation bookkeeping and actual commit/decommit transitions. Sparse operations accept only RESERVE identities. |
| Images | [P1F](../../include/pxe/p1f.h) has fixed addresses, page-aligned non-overlapping R/RX/RW segments, zero-filled tails, W^X and executable entry. Loader span is 256 MiB including gaps/BSS; serialized executable capture is independently capped at 128 MiB. | The direct writer retains loadable Go metadata, compact payload and zero-filled BSS; no conversion or loader symbol table. Runtime reservations remain separate from image span. |
| Grants | [Bundles](../userland/program-bundles.md) can explicitly request memory/manage, threads/manage, clock/read+precise+sleep and random/read. Raw ordinary application launches do not implicitly acquire threads/manage. | Hello declares all four grants and uses supplied console/pipe diagnostics. Missing authority is an admission error; native entropy failure terminates, without predictable substitution. |
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

Tasks 1–3 are merged. Networking and the adjacent standard-library/consumer
task have the scopes below; implementation still needs its own go after the
networking prerequisites are settled. Completed task-1 work:

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
| 3. Files and time — complete | Native os.File/capability-root adapters, supplied streams, partial I/O/closure/errors, fmt, monotonic timers and wall time with explicit availability. Reuse the capability environment, command search and native jobs where an actual launch consumer needs them; no global Unix descriptor namespace. | Run small file/format/timer programs with unchanged application Go and granted roots. |
| 4. Networking | Native sockets/readiness and cancellation/deadlines for TCP, UDP and DNS, feeding Go netpoll without epoll emulation. Verify no authority appears from host path strings or environment text. | Run a network client against explicit supplied networking authority. |
| 5. Standard-library adapters and first networked consumer | Native RANDOM for sysrand; filepath helpers; crash-output ownership in runtime/debug; native command lookup for os/exec; X.509 roots; explicit os/signal and os/user policy. Then qualify Caddy with optional mmap databases omitted and narrow dependency platform files. | Build and run the first real networked target against granted networking and storage authority, with unsupported features stated. |


### Standard-library adapters and Caddy — accepted scope (chronium, 2026-10-22)

The unchanged-source [age](https://github.com/FiloSottile/age/tree/v1.3.2) and
[Caddy](https://github.com/caddyserver/caddy/tree/v2.11.7) compile probe exposed
platform gaps beyond task 3. Task 5 addresses `crypto/internal/sysrand` through
the existing native RANDOM grant, Pyxis `path/filepath` helpers, private
crash-output lifetime in `runtime/debug`, `os/exec` command-search `lookPath`,
`crypto/x509` certificate roots, and explicit `os/signal` and `os/user` policy.
This scope does not invent Unix signals, user identities or ambient roots.
Command lookup alone does not provide subprocess execution.

**Caddy is the first networked target after task 4.** Use `nobbolt` and avoid
Badger (`nobadger`); retaining their mmap/locking backends is outside this task.
Add narrow Pyxis platform files for logrus, readline, tscert and grpc where
needed, preserving explicit unsupported results instead of successful stubs.
Caddy has no supplied HTTP/3 compile-out tag: its generic QUIC path still needs
real native UDP and deadlines. Its config watcher polls files and does not
require a new file-notification interface. Full probe evidence is retained at
`/shared/present/go-probe/README.md`; a failed compile or source finding is not
runtime qualification. Capture the actual Caddy module set, feature limits and
supplied authorities when implementing this task.

### Standard-library adapter delivery — decided

The readiness prerequisites are merged. This delivery adds the standard-library
platform adapters from the accepted scope above; it does not claim Go sockets,
subprocess execution, Caddy service lifecycle or a working age CLI. Task 4 still
needs its own transport-capacity, UDP/readiness and resolver decisions before
network implementation. The following decisions are **accepted (chronium,
2026-10-22)**. The native prerequisite is merged; adapter delivery status and the
accepted command-lookup limit are recorded below.

**Decision 1 — public TLS roots, accepted:** load the existing pinned
`boot://share/ca-certificates/cacert.pem` through the caller's held read-only boot
root, lazily on first system-root use. Use Go's own certificate parser/verifier;
there is no platform verifier, network root download or embedded second bundle.
Missing, empty or malformed configured public roots fail system-root loading,
without an automatic insecure fallback. The entire bundle is refused if one
certificate is malformed, so future CA updates must be validated before use or
default TLS trust can become unavailable. Do not search host-style certificate paths or
interpret `SSL_CERT_FILE`/`SSL_CERT_DIR` in this first delivery. Applications can
still explicitly supply a `CertPool` through the ordinary Go APIs, including
upstream `SetFallbackRoots`; that is application-supplied trust, not a grant to
read another root. A program that registers SetFallbackRoots (including by
importing x509roots/fallback) can use that pool after a failed system-root load;
GODEBUG=x509usefallbackroots=1 forces a registered pool even with valid system
roots. Pyxis embeds no fallback pool. Parsed roots keep upstream's process-local
lifetime.
This reuses the [public trust source](../userland/https.md#trust-configuration-and-updates),
not the HTTPS provider's distinct runtime or custom augmentation policy.

**Decision 2 — users and interruption, accepted with changes:** `os/user`
current/lookup/group operations return explicit unsupported errors. Environment
text such as USER/HOME is not proof of identity; do not invent numeric users or
groups. `signal.Notify` must not panic: `os.Interrupt` must represent a real
native interrupt so programs such as Caddy can shut down gracefully. Other
signal values are accepted but never delivered. `Stop`/`Reset` remain safe
cleanup. `Ignore(os.Interrupt)` keeps a receiver and discards notifications;
`Ignored(os.Interrupt)` reports true until Reset. Other values report false.
Imports and build metadata do not start a signal goroutine. The accepted native
interrupt contract below is separate from forced stop, which Go cannot catch.

**Decision 3 — command lookup, revised and accepted (chronium, 2026-10-23):**
`exec.LookPath` is explicitly unsupported for now. It returns an error matching
both `exec.ErrNotFound` and `errors.ErrUnsupported` for bare names and explicit
paths, without searching PATH or held roots. `exec.Command` may construct its ordinary value, but native
process launch remains unsupported. The common resolver includes bundle/grant/
ABI admission in a userspace C library; duplicating that policy in the no-cgo
fork is deferred until shared-policy reuse is settled. No broker is introduced.

#### Native cooperative interrupt — accepted (chronium, 2026-10-22)

The merged [native receiver](../interfaces/processes.md#cooperative-process-interruption)
is separate from process TERMINATE. It permits application cleanup without
weakening forced process/group stop or granting terminal-input authority.

The process-local opt-in supplies a waitable cooperative notification. A holder of existing process TERMINATE authority may request the
weaker cooperative interrupt. Go's first `os.Interrupt` subscription installs
its own receiver and parks the delivery goroutine; closing the last receiver
joins detachment. No launcher/terminal authority is acquired by registration,
no injected callback runs on an arbitrary M, and no Unix signal ABI is added.
Repeated pending interrupts coalesce. Delivery acknowledges a notification,
not process completion; the supervisor still observes actual retirement.

For direct foreground stages, the shell first requests cooperative interrupt;
without an opted-in receiver it immediately uses today's TERMINATE. After an
accepted cooperative request, a second Ctrl+C forces TERMINATE rather than
leaving an unresponsive handler in control. A Ctrl+C-cancelled foreground job
ends the command list even when its process exits gracefully; native completion
kind and exit status remain unchanged. There is no automatic grace timer
in this first delivery. Native TERMINATE, owned-script/group stop and remote
session disconnect remain forceful and uncatchable. This preserves existing
cleanup authority and makes the graceful/forced distinction explicit rather
than silently weakening stop. Terminal interrupt acknowledgment/rearming must
preserve presses across the transition. Extend other launch supervisors only
when they deliberately request cooperative interruption.

This adds a kernel/ABI and shell prerequisite before the fork adapter. Implement
and qualify those native contracts first, then map Go's `os.Interrupt` onto
them. This is native process behavior, not a Go-only exception. TERMINATE/group
stop keep their existing forceful meaning; there is no cooperative replacement
of the safe-stop path.

Native prerequisite delivery:

- [x] Kernel receiver ownership, coalescing, TAKE and both readiness paths;
  terminal ACK preserves a second recognized press.
- [x] C helpers and direct-shell first-cooperative/second-force integration,
  with local TTY, mux and Remote QEMU checks. See the
  [native record](../development/experiments/cooperative-interrupt/README.md).
- [x] Go subscription/delivery/Stop/Reset/Ignore implementation in
  [fork PR #7](https://git.internal/PyxisOS/pyxis-go/pulls/7), qualified in QEMU.
  No native signal callback or permanently blocked M is introduced; merge pending.

`Ignore(os.Interrupt)` is **accepted (chronium, 2026-10-22)**: keep a receiver
and discard notifications, with `Ignored(os.Interrupt)` true until Reset. The
second parent Ctrl+C still forces termination. Other signal values remain
accepted/undelivered and report false. Notify's existing nil-channel argument
panic is unchanged, distinct from supported registration on Pyxis.

The remaining adapters use existing contracts without changing authority:

- `crypto/internal/sysrand` fills from the native RANDOM grant with the existing
  bounded reads and deadline/error behavior; no predictable or host-file source.
  Each native chunk and its pre-admission retries get a five-second window,
  without a whole-fill deadline that penalizes large requests.
- `path/filepath` uses the existing `name://`/held-cwd rules and semicolon list
  separator, without adding Unix tags or treating URI text as authority.
- `runtime/debug.SetCrashOutput` must own a private native copy and usable
  diagnostic output state, independent of closing the supplied `os.File`.
  Replacement/disable must join the runtime's actual output borrow before
  release. Preserve output offsets/append behavior; do not merely make
  DupCloseOnExec compile while the runtime still rejects that handle. Unsupported
  stream types or insufficient rights fail before replacing a working target.

#### Current adapter delivery

[Fork PR #7](https://git.internal/PyxisOS/pyxis-go/pulls/7) implements RANDOM,
filepath helpers, unsupported user lookups, private crash output, cooperative
signal delivery and lazy public roots. The [record](../development/experiments/go-library-adapters/README.md)
distinguishes native QEMU checks from the host root-parser check. Full target
X.509 compilation and runtime qualification still depend on the unfinished `net`
package; a host parser check is not target TLS qualification.

The command-lookup limit is **accepted (chronium, 2026-10-23)**: the package
builds, but `exec.LookPath` and subprocess launch explicitly return unsupported.
The [common resolver](../../userspace/lib/command_selection.c) includes
[bundle admission](../../userspace/lib/bundle.c), strict JSON validation and
package ABI admission. Reimplementing only ordered lookup would omit this
policy; separately duplicating it or adding a broker is outside this delivery.

Delivery: fork PR first, then a parent recipe pin and adapter/qualification
record. Rebase on merged typed-bootstrap work before changing runtime startup.
Use ordinary QEMU programs to check random/error propagation, native paths,
configured/missing trust when networking is available, explicit unsupported
command lookup and other unsupported facilities, and crash output after caller
close/replacement/disable.
Repeat the existing hello startup check with matched revisions and label nested
clock conditions. No new native run, port patches or test framework is implied.
Caddy and the unchanged CLI compile probes are repeated after the required
networking implementation; a
partial adapter graph is not reported as a working consumer.

Go fork commits/mirrors belong in the chronium-created repository; publish any
kernel/ABI/SDK prerequisite before the Go recipe and parent image pins, following
[repository boundaries](../development/sdk-and-repositories.md). Source downloads
stay pinned through the cache. No LLVM rebuild is intrinsic to the no-cgo target.
Later only: hosted compiler/assembler/linker and module fetch/build need the
native file/process/network layers; Tailscale needs its concrete native tunnel
and device-authority contract after networking. Neither is a task in this slice.
Race detector, cgo, profiling and resumable hardware faults remain outside it.

## Task 3: files, time and native readiness

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

### Decision 1: native file ownership and paths — accepted (chronium, 2026-10-22)

**Accepted contract:** an `os.File` owns one native capability and its adapter
state. `NewFile` adopts a held native handle on success; `Fd` borrows that native
handle, not an integer from a Unix descriptor table. Failed adoption leaves the
caller owning its handle. The caller must not adopt the same handle twice. Standard streams are
owned copies of the supplied `stdin`, `stdout` and `stderr`, preserving their
actual interfaces and rights. Runtime diagnostics retain separate grants; closing
an ordinary standard stream does not revoke the runtime's diagnostic copy.
Share startup FILE-stream cursor state so ordinary output and later diagnostics
do not overwrite one another. Other independent opens have independent cursors.
**Supplied append policy accepted (chronium, 2026-10-22):** honor the existing
exact `stdout.append=1`/`stderr.append=1` TEXT companions from the stream snapshot.
Read SIZE before each flagged write and write at that end, including diagnostics;
this is non-atomic consumer append, as in libc. Malformed policy or unavailable
SIZE fails that stream. It does not enable arbitrary O_APPEND opens.
Unsupported or missing standard streams leave nonnil os.File wrappers whose I/O
returns the retained error; they do not panic at initialization. Duplicate handle
adoption is refused. Missing pipe/create reports a permission error.

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

### Decision 2: bounded native netpoll and wake authority — accepted (chronium, 2026-10-22)

**Accepted contract:** include the real Go netpoller now, backed by
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

### Decision 3: wall time and timezone availability — accepted with change (chronium, 2026-10-22)

**Accepted contract:** timers, sleeps and deadlines use native monotonic time.
Wall time comes only from `CLOCK_WALL_NOW`, preserving its availability and
approximate boot-seed accuracy; never synthesize a date from uptime. Provide a
Pyxis-specific `syscall.ClockWallNow() (Timespec, error)` availability read.
`time.Now()` cannot return an error, so retain the current runtime's explicit
fatal diagnostic on unavailable/invalid wall time. Programs needing to tolerate
absence can check availability. `Sleep` and callback timers (`AfterFunc`) need
only monotonic time; channel timers/tickers produce `Time` values through `Now`
and therefore still need wall time. Timer scheduling itself remains monotonic. Do not change the clock service or boot policy.

`time.Local` uses the session-configured `TZ`, read through the native process
environment, and loads `boot://share/zoneinfo/<name>` through the already supplied
`boot` directory grant. Reuse the [configured-zone contract](../userland/timezones.md)
and the archive's [pinned IANA data](../development/ports.md#timezone-data), not
an embedded Go database, host zoneinfo paths or a new timezone service. Admit
IANA names with the existing component rules; missing/invalid named data is an
explicit error, never a silent UTC fallback. Absent/empty `TZ` retains Pyxis's
existing UTC behavior for startup that supplies no zone. `LoadLocation` uses the
same archive data; caller-supplied `LoadLocationFromTZData` remains an ordinary
explicit byte parser. Native UTC clock values remain independent of local-zone
conversion and do not claim synchronized accuracy.

### Delivery and qualification

The implementation is submitted in [pyxis-go #3](https://git.internal/PyxisOS/pyxis-go/pulls/3),
then the parent recipe pin. The [files/time record](../development/experiments/go-files-time/README.md)
contains one/four-vCPU QEMU results, existing native-parent lifecycle checks,
separate debugger inspection and matched hello startup. No kernel/ABI or
userspace change was required; no native hardware qualification is claimed.

- [x] Native files/paths and full os/io/fmt/time imports.
- [x] Pipe/console netpoll, lazy wake authority, close and deadline integration.
- [x] QEMU files, partial I/O, closure, capacity, timers and process cleanup.
- [x] Existing matched startup probe and native-task debugger observation.

Delivery scope:

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

**Required before task 4 (chronium, 2026-10-22):** deliver larger readiness
fan-in beyond 31 handles and resolve native thread-limit sizing from actual
task-3 M use (poller/sysmon and blocking disk calls), guarded stack/TLS and
metadata cost. These are prerequisites, not optional notes. Task 3 retained the
previous 64-task cap; the following prerequisite raises it to 256. Raising the
cap alone cannot substitute for parking socket goroutines in Go.

- [x] Larger readiness fan-in implemented and qualified on one/four-vCPU QEMU.
- [x] Accepted 256-task sizing implemented and qualified, including actual
  completion, replacement populations and whole-process/group cleanup.
- [ ] Native qualification remains open; networking task 4 is a separate task.

## Before task 4: kernel readiness sets and native-thread sizing

**Decided (chronium, 2026-10-22), implemented:** readiness fan-in grows through
an actual kernel readiness-set object: register interests once, return only ready
entries, and size registrations by a memory budget with clean refusal. Keep the
transient wait_many bound separate; Go netpoll will use the set.

**Accepted (chronium, 2026-10-22), implemented:** 256 live/retiring native
tasks per process, with Go's initial M limit also 256, as
specified [below](#accepted-native-thread-sizing). The readiness-set contracts
below are **decided (chronium, 2026-10-22)**. Implementation of these prerequisites
is authorized; the creation rule is changed to ungranted local creation.

Task 3's [record](../development/experiments/go-files-time/README.md) observed
three native tasks with 200 goroutines on one pipe. This establishes shared
parking, not capacity for many distinct connections or blocking FILE calls.
The dynamic set adapter replaces the 31-registration table; the accepted native
limit is now 256 live/retiring tasks. See the
[implemented interface](../interfaces/readiness-sets.md) and
[qualification record](../development/experiments/go-readiness-set/README.md).

### Decision 1: ungranted creation, owner-local rights and captured authority — decided

Creation is **ungranted**: any process can create this local kernel object within
its readiness-metadata budget, because it can only watch handles the caller
already holds. No creation/factory grant, manifest entry or startup delegation is
needed. Creation gives its owner CONTROL and WAIT on the returned handle; these
rights do not supply input, network or file authority:

| Right | Authority |
| --- | --- |
| CONTROL | Register, update/rearm/disarm, unregister, signal a coalesced wake, and inspect budget/registration metadata. |
| WAIT | Wait for and collect ready records; observe the set through `wait_many`. |

A set belongs to its creating process. Copies can attenuate rights within that
process, but it cannot be delegated/inherited as a usable set by another process;
all operations check owner identity. Closing the last CONTROL grant logically
shuts down the set and wakes collectors with closure. WAIT-only copies do not
keep subscriptions active. Owner exit closes every owned set even if handles
remain elsewhere. Cleanup joins source workers and is attributed to the owner's
execution group; group completion must include that drain. No persistent record
borrows a creator task, user buffer or private-VM pointer after a CALL returns.

Registration captures an exact currently held handle, the same event/rights/
transport/space-owner checks as [wait_many](../../kernel/user/wait.c), an allowed
condition mask, and an opaque caller tag. The active mask can be zero; rearm may
select only validated conditions. Expanding the allowed mask requires fresh
validation through a held source grant. Return a set-local non-reused identity;
modifications advance a generation. Replies carry identity, generation, tag,
ready bits and any terminal native status, never a new target capability.
Exhausted identity/generation space refuses rather than wrapping. Different
copies may have separate registrations and charges.

Capture object **storage**, attenuated observation authority and an independently
owned per-registration forwarder lineage, not a copied logical grant and not the
registering task's accumulated lineage. The
[storage/grant distinction](../../include/kernel/object/object.h) permits final
pipe grants to close normally while observation storage remains alive. Source
handle closure does not itself unregister; it also must not postpone EOF,
peer closure or natural endpoint shutdown. Retarget does not move an existing
registration to a new target. CUT invalidates future observation through that
lineage, queues one terminal DENIED record and detaches its source hooks; never
mark the registration permanently STARTED to exempt it from revocation. Recheck
revocation at collection; records already returned remain stale observations,
not permission to perform I/O. Preserve restrictions and do not broaden source
support to arbitrary exported protocols.

Publish source hooks before the initial level check. Registration reserves all
state and validates its reply buffer before publication; failed admission leaves
no partial registration. Unregister first excludes new notifications, removes
queued records, joins worker borrows, then releases lineage/storage and returns.
After its acknowledgement no kernel producer can use that registration; an
already returned record can still exist in userspace and must fail its generation
check. Final set close can drain asynchronously, but cannot free storage early.
CUT, loss of required ownership or whole-source invalidation queues one terminal
registration error, detaches hooks and releases source storage after drain; its
charged registration remains until explicit removal. Deliver its terminal error
once, without repeated terminal notifications. Ordinary EOF and half-close are
level conditions, not registration invalidation: preserve buffered readable bytes
and any still-open direction until explicit removal. An observer storage reference never keeps that direction
logically open.

Limit the first delivery to the sources Go needs now and for task 4: native
pipes, console/terminal input and output, and existing TCP streams/listeners.
Reuse each source's current authority/event rules; observation supplies no I/O
or acquire authority. Other existing sources remain usable through wait_many
and can gain source hooks in later slices without a new set contract. UDP
readiness is task 4 work, not invented here. Arbitrary exported protocols and
readiness sets themselves are refused as registration targets, preventing
subscription cycles.

### Decision 2: level readiness, targeted notifications and bounded batches — accepted

Use **level-triggered** semantics, not edge-only delivery. Watching an already
ready source must report it. Recheck current state before collecting a candidate;
readiness consumes/reserves nothing and TRY I/O can still return WOULD_BLOCK.
Return only ready registrations, up to the caller's result capacity within
bounded work steps and the per-call reply window. Keep remaining candidates queued
and rotate level-ready entries fairly; include a registration at most once in a
batch. A small batch window is not a registration-count limit. If a work window
examines only stale candidates while others remain queued, yield and continue the
same collection in resumable worker steps; do not park the work or report timeout
before finishing that pending pass. New notifications do not restart its cursor
or extend the absolute deadline. A bad output buffer does not discard pending
notification state. Every rearm/update performs a fresh level check, even if no
source mutation occurred while it was disarmed.

Keep one preallocated ready/dirty node per registration, coalescing repeated
changes instead of allocating notification records. Source-local watcher links
and a dirty-source queue replace global full-set scans on the persistent path.
An interrupt/producer marks its source dirty with bounded work; workers fan out
and evaluate affected registrations in resumable batches, yielding between them.
Do not walk all connections with IF=0, allocate from callbacks, or use a full
queue as a reason to lose readiness. Allocation/index growth and bulk close also
run in bounded BSP-owned steps, preserving existing allocation/VM ownership.

TCP evaluation and subscription attach/detach run in the network worker;
non-network source locks and owners remain authoritative. Notify on changes to
buffers, peer closure, errors, listener state **and direction-admission release**,
not just packet arrival. [Existing TCP readiness](../../kernel/net/lwip/readiness.c)
already depends on those conditions. Publish hooks and remembered dirty/wake
state under the source's ordering so neither initial registration nor
wake-before-park can miss an event. Keeping today's global
[readiness rescans](../../kernel/user/readiness.c) behind a persistent object
would not satisfy this design.

One WAIT collector per set; a competing collector returns BUSY without consuming
anything. CONTROL updates may run while it waits. A zero deadline polls; finite
waits use absolute monotonic deadlines within today's 30-second bound, with Go
reissuing longer waits against its unchanged deadline. A sticky, coalesced
CONTROL wake makes WAIT return to recompute runtime timers even without source
records; it is remembered across park preparation and requires no wake pipe.
Stop/close wakes the collector and drains borrowed state. No user output mapping
lease spans the sleep.

`wait_many` stays the **32-interest transient primitive**, with unchanged source
behavior, ABI layout and stack/request sizes. It additionally accepts READABLE
on an owned readiness set with WAIT authority, plus its closure indication.
That observes queued ready/terminal records or an explicit wake, consuming
nothing; the set's WAIT operation collects records. It can be mixed with existing
waitable sources in a small loop. The set does not recursively invoke wait_many.

Go lazily creates one set at first readiness use, registers handles once, and
updates masks around TRY I/O: arm only blocked directions and disarm delivered
ones before the next wait. Keep poll-descriptor and registration-generation
checks and acknowledged unregister before descriptor reuse. Replace the fixed
31-slot table with dynamically grown, owned runtime metadata; no second fixed
connection cap or one-M-per-source fallback. Use the set's wake for netpollBreak;
pipe/create remains needed for os.Pipe, not for readiness itself. Timer-only and
diagnostic hello retain their lazy no-set/no-poller-resource path.

### Decision 3: visible per-process memory budget, shared by all sets — accepted

Use a **16 MiB per-process kernel readiness-storage budget**, charged on demand
across all its sets; copies share the account and do not charge the same set
storage twice. This is a tunable resource policy, not an ABI
count or physical-memory reservation. Creating many sets or copying their handles
cannot multiply the allowance. Expose charged bytes and the ceiling through
CONTROL inspection; report the actual entry costs and
resulting capacities during qualification rather than promising a count now.

Charge set headers, registration records, indices, retained-lineage metadata,
source-hook allocations, queue nodes and any peak growth/replacement storage
before publication. Pre-existing target backing follows its own resource limits;
the 16 MiB figure is not a bound on all memory reachable through observed
objects. Invalidated/revoked registrations detach and release observation storage
after worker drain; ordinary half-close keeps the surviving directions intact.
Outstanding borrows keep their charge until real reclamation, not merely a
REMOVE request. All metadata growth must be linear/geometric, not
repeated exact-size copying. Notification/WAIT use reserved nodes and bounded
scratch, with no registration-sized stack or per-task request arrays.

Budget exhaustion returns LIMIT; allocator/global-capacity exhaustion returns
NO_MEMORY, both before effects and without damaging existing registrations.
No eviction, silent unregistration, dropped notifications or fallback to blocking
an M. Go reports registration/resource errors and unwinds its unadmitted local
metadata. Its metadata can also fail allocation; neither layer claims unlimited
capacity. Global kernel-memory constraints and network-object limits remain
visible and must be included in the Caddy readiness report.

### Accepted native-thread sizing

Raise the kernel's per-process task-storage bound from 64 to **256**, counting
the initial thread and storage awaiting actual reclamation. Match Go's native
context/observer table to 256 noninitial entries and its Pyxis initial
`maxmcount` to 256. The initial M is separately bound, so the native total
permits at most 255 siblings. Keep
`debug.SetMaxThreads`' existing Go semantics; it cannot grant more native
capacity. The libc wrapper's independent 64-context bound is unchanged.
No new thread grant, worker affinity, scheduler policy, VM ownership or allocator
lock is introduced. Existing holders of native thread-create authority can
consume the larger per-process allowance; it is not Go-only authority.

This capacity policy is qualified in nested QEMU; it is not a claim that
Caddy needs 256 Ms. Readiness-backed pipes and later sockets still park
goroutines. Regular FILE operations without readiness release the P but may
occupy native Ms.
Do not add a blocking-FILE admission queue or change Go's scheduler to hide
capacity exhaustion.

For each additional Go M, current fixed backing is **64 KiB g0 + 4 KiB TLS +
16 KiB kernel stack = 84 KiB**, plus two unbacked guard pages. At the accepted
limit, 255 additional Ms require **21,934,080 bytes** of that backing, plus the
initial thread's existing stack/TLS/kernel backing. Kernel metadata adds at
least the current task (1,344 bytes), profile (544), service area (6,544), and
completion object (72) per sibling, plus allocation/capability records,
page tables, Go M/G metadata and allocator slack. One in-flight CREATE can also
have provisional backing before admission. These are source/layout costs, not
a physical-memory reservation or a complete maximum-RAM promise. Allocation and
the kernel's shared 4,096-entry VM-range metadata can refuse earlier.

The kernel's [task-storage counter](../../kernel/process.c) remains authoritative.
Go's `mcount` excludes some exiting Ms before native retirement; matching the
nominal limits cannot guarantee Go detects exhaustion first. Preserve native
completion-gated stack/TLS release and observer retirement. An actual context,
backing failure or genuine CREATE exhaustion remains an explicit process-fatal
runtime diagnostic. A definitive CREATE LIMIT with no observer waits only for a
known retiring peer's actual completion before retrying. Each retry follows real
reclamation; it never waits for itself or a live M. Never retry an uncertain
CREATE, recycle an exiting context, or pretend that capacity is available.
Lowering Go's limit is not a substitute for observing native retirement.

### Authorized delivery and qualification

1. **Kernel/interface first:** implement ungranted set creation, the process
   account and owned cleanup, target-specific notifications, and accepted
   256-thread admission. Update SDK/references and readiness-scaling debt; keep
   wait_many's existing bound. No Go/fork pin may precede its kernel dependency.
2. **Baseline then capacity/lifetime checks:** use ordinary native callers and
   Go programs on one/four-vCPU QEMU. Compare current task-3 counts/costs with the
   set path at hundreds and thousands of independent registrations, sparse and
   all-ready changes, duplicate observations, small output batches, always-ready
   fairness and a source with many watchers. Include sufficient pipe/stream
   backing in the guest; do not confuse source-memory exhaustion with set budget.
   Record charged memory, peak stack/request use, work per notification, BSP
   latency and other-process progress, not just a registration total.
3. **Refusals and cleanup:** validate wrong/missing rights, foreign-owner sets,
   unsupported sources, restricted/forwarded grants, CUT/retarget, masks,
   exhausted memory budget and unchanged prior registrations. Exercise initial
   readiness, concurrent rearm/unregister/close, stale generations, earlier wake,
   deadlines and owner exit/group stop. Verify no logical endpoint is held open,
   no notification is lost, and actual worker detachment precedes reclamation.
4. **Fork:** move netpoll to the dynamic set and wake path, preserve TRY/park/
   close rules, and apply accepted native-context/M sizing. Repeat task-3 I/O,
   GC/copied stacks and timers, plus thousands of waiting Gs on many sources;
   native M growth must follow runnable/blocking work, not stream count.
5. **Thread capacity:** compare native live/retiring storage with Go M counts
   under concurrent real FILE calls, more than 64 tasks, at-limit refusal and
   replacement churn. Verify provisional-resource unwind and completion-gated
   reuse, then whole-process/group drain and a successful following launch.
   Record guarded backing, metadata pressure and global exhaustion separately.
6. **Parent pin last:** repeat matched hello startup, label nested clocks, and
   keep debugger observations separate from timing. Readiness-set qualification
   and thread sizing are required before networking implementation; Caddy remains
   the first networked target after task 4 and the accepted adapter work.

**After this prerequisite:** chronium can run Go programs with Caddy-scale
readiness registrations constrained by visible memory, without per-wait full
connection arrays or one native thread per idle stream. Implementation and nested
qualification are recorded above. Go sockets and
the existing TCP connection-pool bound remain networking task 4 work; no native
or Caddy qualification is claimed here.

## cgo for Go programs that wrap C libraries — candidate (chronium, 2026-10-23)

Direction stated by chronium; a candidate for a later milestone, not authorized
work and not scheduled. It ranks below the
[command-resolution service](later-os-directions.md#command-resolution-service)
(chronium, 2026-10-23). Go on Pyxis is pure Go today: `CGO_ENABLED=0`, with no C
library inside a Go process. A cgo milestone would let a Go program wrap a C
library. Questions it must settle before any task:

- **External linking** through LLD against the static SDK, replacing the Go
  linker's direct P1F writer for these programs.
- **One process, two runtimes.** The Go runtime and libc coexist: thread and TLS
  handoff between Go's registered TLS context and libc's, callbacks from C into
  Go, and startup reconciliation of the two initializers. The Go runtime's current
  boundary (its own threads, memory and TLS, with no libc descriptor table) is the
  starting point, not something this note changes.

Anything the same C code would need from libc, such as command selection, must
not require this milestone; see the
[command-resolution service](later-os-directions.md#command-resolution-service).

## Task 4 networking — accepted (chronium, 2026-10-23)

The dynamic readiness set and 256 live/retiring native tasks are implemented.
[Adapter PR #1137](https://git.internal/PyxisOS/pyxis-os/pulls/1137) supplies the
adjacent library work. The three decisions below are accepted; implementation
and qualification remain separate from this record.

### Native prerequisite status

[TCP](../devices/tcp.md) stream TRY and listener TRY_ACCEPT retain their existing
readiness contract. CONNECT and UDP SEND/RECEIVE reuse
[native completions](../interfaces/native-completion.md). Kernel prerequisites
replace the singleton completion gate with a 16 MiB metadata account, add owned
receiver readiness-set observation, and replace small global TCP/UDP record
counts with the shared 64 MiB storage allowance. Receiver delivery slots,
per-object concurrency and packet queue bounds remain separate.

Four-vCPU nested QEMU qualification has exercised 64 simultaneous TCP streams,
64 pending UDP receives with cancellation and receipt acknowledgement, receiver
READABLE/CLOSED and joined removal, completion-account refusal with 925 retained
receipts, and transport NO_MEMORY with 1011 held one-byte TCP streams. The
remaining 1010 streams survived the refusal; closing them permitted a fresh
connection. These populations are observed results, not ABI limits. Go sockets,
TLS and Caddy remain unimplemented/unqualified here. The ordinary-client
completion constructor decision below remains open.

### Decision 1 — concurrent completion ownership and waits

**Accepted:** replace the singleton completion gate with a separate
16 MiB per-process completion-metadata allowance. Charge operation records,
copied input/output, captured-reference/lineage metadata, backend records and
retained receipt claims before admission. Keep the charge until acknowledgement
or actual uncollected-work drain, including after process exit. Capturing a
handle does not charge its entire pre-existing object graph again. Apply this
shared completion contract consistently to network and filesystem operations;
no new authority to invoke either is supplied by the allowance.

Keep owner-bound receiver creation under the existing explicit service creation
grant. No receiver or completion machinery starts on import. Preserve monotonic
correlation IDs, the receiver's 16 delivery slots, receipt backpressure, returned
grants, cancellation election and real-outcome reporting. Go may own several
receivers as needed; their use cannot bypass the process allowance. Add receiver
READABLE/CLOSED observation to the existing readiness set and park completion
collection through Go netpoll. Do not add another async operation family or a
native thread for each waiting socket.

TCP streams/accept continue using TRY plus readiness. CONNECT and UDP use native
completions. Keep native maximum operation windows: CONNECT 30 s, UDP SEND 5 s,
UDP RECEIVE 30 s, shortened by the caller's earlier context/deadline. Setup and
send can therefore time out at those bounds even with a longer or absent Go
deadline. An infinite UDP read repeats only a confirmed empty timeout against
its unchanged public deadline; no CONNECT/SEND replay after timeout, cancellation
or an uncertain outcome. Deadline changes and Close cancel pending operations,
then join collection/acknowledgement before reuse. A successful connection racing
cancellation must have its returned grant collected and closed. Preserve actual
UDP progress/outcomes; cancellation is not rollback.

### Decision 2 — transport capacity and binding authority

**Accepted:** replace the small global transport-object and pending
network-operation count limits with dynamically owned records under an initial
64 MiB shared native TCP/UDP storage allowance. Charge actual transport/operation
metadata and retained buffers, including lwIP allocations, before accepting work.
An operation can consume both this global allowance and its process completion
allowance; these are intersecting limits on the same storage, not duplicate
allocations. Refuse cleanly on exhausted budget or heap; never evict a held
connection, ignore TIME_WAIT or weaken teardown ownership to admit another.
These allowance values are initial resource settings, not wire-format constants.

Retain the per-object rules: one operation per direction, four pending TCP
handshakes/accepts per listener, existing UDP receive queues, 1472-byte datagrams,
packet/NIC queue backpressure and native address-epoch invalidation. Dynamic
worker service, timeout/cancel and teardown passes must remain bounded and
resumable; no process-sized IF=0 scans or allocation in hardware interrupt
handlers. A larger object budget is not proof of Caddy throughput or arbitrary burst handling.

Preserve held factory scopes and rights. CONNECT never grants LISTEN. The first
server profile binds a concrete configured IPv4 address and nonzero TCP port
under explicit LISTEN authority; wildcard, loopback/ephemeral TCP listening,
reuse, IPv6, raw sockets and multicast remain explicitly unsupported. Do not
translate `:443` into broader authority. Socket options without a real native
operation or a provably identical inherited setting return unsupported.

### Decision 3 — DNS and the first resolver profile

**Accepted:** use Go's userspace DNS wire machinery with the existing
startup [`DNS_SERVER` plus held `dns_udp` pair](../userland/dns.md#resolver-configuration).
It is a configuration/scope snapshot, not authority from environment text.
Numeric addresses bypass DNS. Missing/malformed configuration or missing authority
fails explicitly; do not read resolv.conf, search suffixes or choose public DNS.
The first transport is IPv4; unsupported address families fail honestly.

Default DNS remains on that UDP scope. A truncated response reports unsupported
TCP fallback rather than silently substituting a potentially wider general TCP
factory. An application-supplied Resolver.Dial callback can supply its own held
transport; it is not an implicit expansion of the default resolver. Do not add
resolver caching or an embedded host configuration source in this task.

### Authorized delivery and qualification

1. Capture functional baselines on current main: native TCP client/server,
   UDP exchange, hostname resolution and Go hello/readiness behavior. Record
   revisions and matched four-vCPU nested KVM/VirtIO configuration. No timing
   claim is established by the decision record.
2. Implement the native completion/receiver and transport admission changes,
   with SDK/libpyxis consumers updated together. Qualify concurrent operations,
   capacity refusal, cancellation/close, receipt retention, owner exit and group
   drainage before the fork pin can depend on them.
3. Add native Go TCP, then UDP/default DNS, with readiness-backed waits,
   partial I/O, EOF/half-close, deadline changes and concurrent goroutine progress.
   Preserve actual native errors and no authority from host paths/environment.
4. Qualify HTTP/TLS client and explicit-address server, the held boot trust bundle
   and missing/malformed trust, DNS and grants. Repeat hello startup and inspect
   native task growth separately from connection count. Caddy with `nobbolt`
   follows as the first real networked consumer; no fake socket options or
   claimed HTTP/3 removal tag. Record actual unsupported features.

Native kernel/ABI and any userland dependency publish before the fork; the parent
recipe/pins follow their published heads. No native/PXE run is authorized here.
After this task, chronium can run a Go network client and an explicitly granted
IPv4 server through native capabilities without one native thread per idle
connection, subject to the reported budgets and protocol limits.

### Implementation checklist

Native prerequisite evidence and limits: [QEMU record](../development/experiments/go-networking/README.md).

- [x] Baseline native exchange, DNS and Go hello on four-vCPU nested KVM.
- [x] Native completion metadata accounting and receiver readiness-set hooks.
- [x] Dynamic TCP/UDP storage, bounded adapter queues and clean budget refusal.
- [x] Native concurrent operation, cancellation, receipt, owner-exit and
  post-drain replacement checks in QEMU.
- [ ] Decide ordinary client completion-constructor provisioning below.
- [ ] Implement and qualify Go TCP, UDP/default DNS and HTTP/TLS through netpoll.
- [ ] Qualify the first real networked consumer and report unsupported features.

### Additional decision — ordinary client completion authority (pending)

Runtime qualification found that a normal bundle declaring `service/create`
is refused by the shell: the grant source is offered only to provider launches
([source](../../userspace/shell/launch.c)). The shell already holds that private
constructor for providers/session successors; it is deliberately withheld from
ordinary children. Go's accepted CONNECT/UDP completion path needs it to create
owned receivers. Network factory authority alone cannot do so.

**Recommended default:** trusted launchers may satisfy an explicit bundle
`service/create` declaration from their already held constructor, including for
ordinary clients. Keep it a private runtime-role binding and never forward it
implicitly to ordinary children. Missing source authority still refuses; holding
TCP/UDP does not manufacture it. Apply the same declared-grant rule in the common
native launch adapters, with no command wrapper or broker. Under current grant-all
this is the existing general endpoint/IPC constructor, not a new completion-only
right; a declaring program can create endpoints/exports and consume their kernel
metadata. The grant is opt-in through the manifest, not added to every Go bundle.

Alternative: keep the provider-only delegation rule and design a separate narrow
completion-receiver constructor. That would change the just-accepted creation
contract and needs a separate authority/lifetime decision. No implementation of
either alternative is authorized by the three earlier networking decisions.
Native kernel qualification can use the existing trusted session handoff;
that is a private test context, not the proposed user-facing way to run Go.

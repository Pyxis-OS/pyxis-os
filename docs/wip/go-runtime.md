# Native Go runtime investigation

Status: docs-only source refresh, 2026-10-11. No Pyxis Go target/runtime is
implemented, and no Go program has run in the guest. The 2026-09-30 Linux
compile/conversion probes below were not repeated. The owner accepted the three
decisions below on 2026-10-11. No Go or VM task is assigned; implementation awaits
a separate owner go after hosted Clang.

**Owner ordering, 2026-10-11:** Go follows hosted Clang. Clang's decisions are
accepted; beta is planning its threads-independent line, while delta owns
[threads gates 5–8](threads.md#delivery-gates). This investigation identifies
Go's actual dependencies within those gates; it assigns none of their work.
The target remains the standard Go compiler/runtime, static amd64/v1, initially
without cgo. TinyGo, WebAssembly hosting and a replacement runtime are outside
this investigation.

## Baselines and evidence

Source inspection (2026-10-11) used Pyxis `2fca30fd`, userland `df210f1a`, ports
`df3210b0`, filesystem `b427df29`, host LLVM fork `49e2c1a1518b` and Go
`go1.26.7` (`e3336a22ad3f0a90bd252c95d8b5544e02674205`, official
[source archive](https://go.dev/dl/go1.26.7.src.tar.gz), SHA-256
`0ed24eac755105085b89fe9cabc2742b91a0ad7b94b59d3ad364918ebc8956ad`).

Go supplies its own compiler, assembler and linker for the cgo-disabled slice;
LLVM language-TLS delivery or a compiler-container rebuild is not intrinsically
required by it. The [host LLVM reference](../development/llvm-toolchain.md)
records the current P1F writer; `elf2pxe` was removed on 2026-10-07.

The 2026-09-30 Linux probes used the Fedora `golang-1.26.7` package
(`CGO_ENABLED=0`, linux/amd64, GOAMD64=v1) and the since-removed `elf2pxe`:
`GOOS=pyxis` was rejected as an unsupported target pair; builtin `println` and
`fmt.Println` produced 1.87 MB and 2.46 MB static ELF files (31 and 62
dependency-list entries) with three page-aligned RX/R/RW LOAD segments and no
INTERP/DYNAMIC/TLS headers, which the converter accepted (1.19 MB and 1.58 MB
P1F). Under gdb with GOMAXPROCS=1 and asyncpreemptoff=1 the Linux hello showed
three threads at `main.main` (one host observation, not a promised count).
Conversion proved container compatibility only: Linux startup, FS setup and
syscalls still made the images unusable on Pyxis. No guest runtime,
resident-memory measurement or syscall trace was obtained. The probes were not
repeated and need a checkout of `625fd2a` or earlier to reproduce.

## Current image, stack and bundle boundary

[Program loading](../kernel/program-loading.md) admits a **256 MiB page-rounded
image span**, including segment BSS, padding and gaps. This is independent of the
**128 MiB per-selected-executable serialized capture ceiling** shared by HOST,
NPFS and RAM. Neither ceiling guarantees physical capacity. Runtime VM
reservations would be separate from both image budgets.

The loader places the initial stack below `0x7ffffffff000`, with a reserved
unmapped lower guard and eager RW/NX backing. Plain programs keep 1 MiB.
[Unpacked bundles](../userland/program-bundles.md#manifest-and-stack) may request page-aligned
1–8 MiB initial stacks, rejected rather than clamped. There is no automatic stack
growth or P1F stack-size field. A Go initial g0 stack must respect that backing;
Go's other M/g0 stacks and movable goroutine stacks need their own allocation
and lifetime contract.

An unpacked `bin://NAME.pxb` resolves by bare NAME after NAME.pxe and supplies the
program's own read-only app:// and resource roots. Runtime memory, clock and
entropy authority must be requested explicitly, with the actual received set read
from startup. Native thread authority is not a recognized bundle grant today.
ZIP/.pxa remains unimplemented and cannot be a prerequisite for the first Go result.

### P1F output options: no route selected

| Route | Advantages | Costs and constraints |
| --- | --- | --- |
| Go linker writes P1F, following LLD's approach | One native output step; Go owns segment layout, entry and its TLS convention; no intermediate conversion executable. | Adds a maintained writer to the Go fork, matching P1F admission and rejecting unsupported layouts. Preserve an ELF/debug-symbol output for inspection; coordinate any actual format change with the existing writers. |
| Restore an ELF-to-P1F converter as a host tool | Reuses Go's ELF layout and permits independent inspection; useful to another static ELF producer too. | A new maintained component/build step with the same segment/entry validation, BSS/rounding checks and unsupported-feature refusals. TLS/entry/syscall adaptation still belongs in Go; conversion cannot make a Linux runtime native. |

Either route must obey [P1F](../../include/pxe/p1f.h) and common loader checks.
The current LLD rejects PT_TLS; Go's own FS storage need not be an ELF PT_TLS
segment. An eventual host build must use owner mirrors/caches and pinned sources.

## Go prerequisites within the threads gates

[amd64 startup](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/asm_amd64.s)
and [runtime initialization](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/proc.go)
initialize g0, allocation, locks and scheduling before main; runtime.main starts
sysmon on an OS thread. GOMAXPROCS limits P execution resources, not the M thread
count. Disabling asynchronous preemption does not remove sysmon or the GC.

Threads tasks 1–4 and gate 5 (native CREATE/EXIT/completion, #709; joinable
siblings only) are merged. Task 4 supplies [VM transactions/activity leases](../kernel/memory.md#execution);
address parking (gate 6), static TLS (gate 7) and sparse backing operations remain
open. The following dependency split is required by a normal cgo-disabled runtime:

| Gate | Parts Go needs | C/C++ parts Go need not await |
| --- | --- | --- |
| **5: CREATE/EXIT/completion** | Atomic sibling admission against process/group stop; runtime-selected trampoline, arguments, disjoint registered stacks/context and initial FS; per-task CPU/FP state and placement; task-local retirement, observable native completion and reclamation. Qualify real sibling copy/unmap/close races before entry is enabled. | Pthread/C11 interfaces, libc stack defaults and user destructor conventions. |
| **6: address WAIT/WAKE** | Compare-and-enroll without lost wakes, predicate loops and atomic ordering; finite monotonic deadlines and an explicit indefinite-park contract; timeout/wake/stop/unmap cleanup and mapping-incarnation keys. Settle comparison width/alignment against Go's existing 32-bit futex words or adapt the Go storage, without unaligned/over-wide loads. | Pthread condition clocks, cross-process futexes, requeue and priority inheritance. |
| **7: static TLS/errno** | The native FS-base bootstrap/lifetime subset shared with gate 5: establish initial FS before TLS-using Go code, supply sibling FS before entry and preserve it through calls/migration. Go owns its thread record and offset convention. | LLVM/LLD local-exec C TLS templates, libc errno/TCB layout, runtime keys and C/C++ TLS destructors. Go need not await all of gate 7. |
| **8: thread-safe libc** | Not a cgo-disabled prerequisite when the port uses Go runtime locks/allocation and native capability-call adapters. | Libc malloc, descriptors/FILE caches, pthreads/C11 threads and the C++ synchronization adapter. Adding cgo or a C-library dependency changes that boundary. |

**Go FS ownership.** The pinned Linux amd64
[FS setup](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/sys_linux_amd64.s)
uses `&m.tls + 8` with current g at FS-8; the
[Go linker](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/cmd/link/internal/ld/sym.go)
selects the corresponding offset. This describes the inspected Linux backend,
not a prescribed Pyxis layout. Go needs a native initial-thread FS setup operation
and CREATE's initial FS; both are supplied by threads task 5. Do not adopt
libc's TCB or silently enable user FSGSBASE. Kernel GS remains CPU-local.

**Stacks and joinable-only lifetime.** Go owns M/g0 system stacks separately
from growing/copying [goroutine stacks](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/stack.go).
Register native M/g0 stack/FS contexts, not each goroutine stack; G-stack
movement remains Go-owned. Prepare disjoint system-stack backing/guards rather
than reuse libc's allocator or assume its convenience sizing. The native
caller-owned context route is not restricted to libc's proposed
1 MiB/1–8 MiB convenience range. Gate 5's backing pin lasts until native
completion. Linux's pre-exit `freeWait` marker must not authorize release/reuse
before that completion. A Go collector can retain and consume joinable observers
without requiring detach, then free M/g0/FS storage; bound outstanding observers
and handle real native thread/record exhaustion, including the tunable 64-entry
policy. Process exit/fault/Ctrl+C stops all siblings; forced teardown promises no
Go or C destructor execution.

**Parking adapters.** Go's
[notes and locks](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/lock_futex.go)
need wake-before-park ordering, relative-duration to absolute-deadline conversion,
overflow handling and spurious-wake predicate checks. These paths may run without
a usable G or with runtime locks held: no Go heap allocation or scheduler callback
may be required to make the native call. Current capability
[wait_many](../../include/abi/wait.h) has a finite 30-second deadline bound and no
address compare/enrollment; CLOCK_SLEEP is not interruptible by an address wake.
Neither substitutes for gate 6. The gate must explicitly settle indefinite parks;
this doc does not assign a new parking ABI.

## Remaining runtime work

A native assembly entry consumes Pyxis startup_info in RDI, rather than Linux's
initial argc/argv stack, and retains only granted resources. Early no-allocation
adapters must preserve Go's ABI, nosplit and stack/runtime-lock constraints.
Required initial services are memory, monotonic clock, entropy, supplied stderr
and explicit process exit. Native calls that block an M must cooperate with Go's
P handoff so other goroutines can progress. Kernel scheduling alone does not
provide that runtime integration.

The first profile still needs an explicit fault/preemption decision. Current
user hardware faults terminate the process. Go's
[Unix fault path](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/signal_unix.go)
can turn eligible faults into recoverable sigpanic; that is not implemented by
Pyxis termination. Likewise kernel task preemption is not a Go safe point.
[Cooperative Go preemption](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/preempt.go)
retains GC/scheduling but a call-free loop may indefinitely delay another
goroutine or stop-the-world completion. Native resumable fault delivery and
safe asynchronous interruption would need separate general contracts, not
POSIX-shaped signals added for Go.

fmt.Println later needs a native os.File/standard-stream adapter, partial-write
and closure/error behavior. Go's Linux numeric descriptor/poll tables and
Pyxis libc's descriptor table are not present in a cgo-disabled executable.
Blocking output alone does not require epoll; use native readiness when needed.
Files, sockets/netpoll, os/exec, module fetching, hosted Go, race detection,
profiling and cgo remain later concrete consumers. Unsupported operations must
fail honestly; no successful stubs or disabled GC/scheduler to obtain hello.

## Accepted private-VM reservation/commit contract

**Owner-accepted, 2026-10-11; not implemented or assigned. No existing threads
gate owns this contract.**
Task 5's guarded-context work does not imply general sparse runtime backing.
Coordinate ownership with it, without creating competing reservation authorities.
This extension should serve native allocators/runtimes generally, not expose a
Go-shaped mmap or Linux compatibility mechanism.

**Calculated Go requirement:** the pinned
[64-bit page allocator](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/mpagealloc_64bit.go)
reserves five sparse summary arrays: `0.125 + 1 + 8 + 64 + 512 = 585.125 MiB`;
its scavenge index reserves another 512 MiB. The total is **1,097.125 MiB**,
approximately 1.1 GiB of virtual addresses, not resident RAM. With 48 heap-address
bits, 4 MiB allocation chunks and eight-byte entries, this is independent of
GOMAXPROCS. Ordinary amd64 heap arenas are **64 MiB, 64 MiB aligned**. The
[arena map](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/malloc.go)
also assumes demand-paged metadata; these totals are not the entire runtime
footprint. Qualify that additional metadata before claiming an eager native
backend fits. Eagerly allocating every reservation would already fail a normal
small guest before useful execution.

Current [MEMORY](../../include/abi/memory.h) supplies eager zeroed RW/NX ALLOCATE
and exact whole-region RELEASE, plus task 5's guarded eager allocation and
whole identity release in the same private-memory registry. The accepted family
below will subsume that eager convenience operation under the same authority,
identity and context-pin model; it is not assigned. Internal
[vm_reserve/vm_back](../../include/kernel/mm/vm.h) are not public contracts;
vm_back can back one suffix of a bare whole reservation, including the eager
guarded subset. Sparse subranges, their owned frames/metadata and atomic rollback need deliberate implementation.

The accepted contract leaves existing ALLOCATE/RELEASE intact and adds an
explicit reservation family; names below describe behavior, not assigned opcodes:

| Operation / boundary | Accepted contract (not implemented) |
| --- | --- |
| Authority and identity | MEMORY MANAGE applies only to the calling process, even after handle transfer. RESERVE returns a process-owned opaque generational identity plus base/rounded size. The identity is not transferable mapping authority; stale/released identities reject even after address reuse. Closing a MEMORY handle releases nothing; final drained process teardown reclaims all reservations/backing. |
| RESERVE | Nonzero bytes rounded to 4 KiB; power-of-two alignment at least 4 KiB, including 64 MiB. BSP chooses a disjoint lower-half user range, excluding image, startup, stacks/guards and other reservations. No fixed-address replacement or placement hint in the first slice. It owns metadata/address space only: no data frames or proportional page tables; access faults until COMMIT. Virtual/metadata exhaustion is a real failure, not a promise of future physical capacity. |
| COMMIT | Nonzero page-aligned offset/length wholly inside that reservation. Eager private zeroed RW/NX backing for every currently unbacked page; already committed pages and their contents remain unchanged. Mixed committed/unbacked ranges are permitted. No demand-fault growth, shared/file mapping, executable permission or huge-page guarantee. |
| DECOMMIT | Page-aligned contained subrange; discard contents and free data frames, keeping the address reservation. Already unbacked pages are unchanged. Recommit supplies zeros. Retire affected mapping incarnations and address-wait registrations before address reuse/reopening admission. Registered live/retiring thread stack/FS-context backing cannot be decommitted: BUSY until native completion. |
| RELEASE | Whole reservation identity only, freeing any backing and its reserved range. No partial trim/release; live context pins cause BUSY. Later allocation may reuse the address with a different identity. Captured request data may lie in the released range, but no post-unmap user access is allowed; returning code/stack must remain valid. |
| Failure and budgets | Invalid size/alignment/identity/bounds: BAD_REQUEST; missing authority: DENIED; failed reply validation: BAD_BUFFER before effects; backing, virtual-range or metadata exhaustion: NO_MEMORY; conflicting mutation/context pins or bounded quiescence failure: BUSY before effects. Set explicit metadata/commit-work budgets during implementation qualification. No new limit silently restricts a valid sparse reservation to the loader's 256 MiB image span. |

**Failure atomicity:** RESERVE publishes one complete identity/range or none.
COMMIT prepares all required ownership metadata and data backing; allocation
failure unmaps/frees its provisional prefix, preserving prior pages and contents.
DECOMMIT/RELEASE validate and prepare every fallible bookkeeping change before
irreversible removal; failure leaves mappings, contents and identities unchanged.
Do not leak provisional frames or records. Private page-table infrastructure
retains its ordinary VM lifetime; account for it separately from data backing.
Sparse operations must use owned committed extents rather than scan every page
of a large unbacked span. Large commits still need bounded work/qualification
rather than an unbounded BSP pause disguised as cheap reservation.

**Task 4 integration:** capture request inputs under short activity leases, then
park the caller outside its private root. All ownership and VM mutation stays on
BSP under the existing process transaction shared with MEMORY/DISPLAY. Close
admission, drain roots/actual borrowers, acknowledge departure after CR3 reload,
and mutate only with the BSP token. Pending drainage never blocks the BSP
executor or holds allocator/queue/spin guards. For RESERVE, revalidate reply
storage before effects and retain exclusive REPLY ownership through final copy
and cleanup. Status-only decommit/release uses kernel-captured input and no
post-unmap user copy. Sleepers hold no mapping lease; their registration cleanup
must compose with gate 6. Final retirement waits for activity/request drainage.
Existing precommit BUSY is not Go's timer deadline or a permission to retry
arbitrary committed I/O.

**Go mapping of the native contract:**
[sysReserve/sysReserveAligned](https://github.com/golang/go/blob/e3336a22ad3f0a90bd252c95d8b5544e02674205/src/runtime/mem.go)
use aligned RESERVE; the native alignment avoids the generic Go backend's
prefix/suffix trimming. Ignore optional address hints rather than add fixed
mapping replacement. sysMap's Prepared state may remain unbacked until sysUsed;
sysUsed COMMITs missing pages while preserving already Ready contents.
sysUnused DECOMMITs, forfeiting contents; subsequent sysUsed obtains zeros,
with the adapter's zeroing/accounting rule stated explicitly. Whole-region
sysFree/sysUnreserve RELEASE the original identity. The adapter needs
allocation-free early ownership bookkeeping and synchronized page/state
accounting; retaining eager backing while reporting scavenged memory would be
false. Physical exhaustion must reach real runtime OOM/fatal handling, and
precommit BUSY needs a bounded retry/failure policy outside forbidden contexts.

## Proposed bounded progression

No Go or VM task is assigned. Implementation starts only after hosted Clang and
a separate owner go. Gate delivery below remains delta's work; every new
native-VM or Go task needs its own assignment.

| Task | Scope and boundary | What the owner can do after it |
| --- | --- | --- |
| 1: private-VM foundation | Small first implementation once separately assigned, still one user task: aligned reservation, commit/decommit and whole release under task 4, with no Go target or new thread API. Qualify sparse metadata-sized ranges, 64 MiB arena alignment, recommit zeroing, rollback/retirement and backing/pause costs with a native consumer. | Inspect a large sparse reservation and real backing reclamation without installing Go. |
| 2: target/output closure | Owner-pinned Go fork, amd64/v1 static target registration, native entry/assembly boundary and a bounded comparison of P1F writer versus restored converter. Select the output route only from that review; no Linux-converted image is a guest-runtime result. | Inspect native artifact layout and approve one maintained output route. |
| 3: minimum normal runtime | After gates 5/6 and native FS setup: M/g0/completion collection, Go-owned FS, parking, VM/time/entropy, diagnostics and process exit. Preserve GC and normal scheduler; implement the agreed fault/preemption limits and actual startup grants. | Run builtin println with an observable exit and full reclamation. |
| 4: progress qualification | Allocation/GC and moved stacks, channels, timers, M creation/retirement, blocking output while another goroutine progresses, real capacity/OOM and whole-process fault/stop cleanup. Measure launch/peak backing and parking/VM pause costs; distinguish nested QEMU from native results. | Judge the declared runtime profile beyond a printed literal. |
| 5: supplied streams and fmt | Native os.File stream adoption with partial writes, errors and closure; fmt.Println unchanged at the application level. File/network/hosted-toolchain consumers remain separately scoped. | Run a small formatting program through delegated streams. |

## Accepted owner decisions (2026-10-11)

1. **First runtime profile — accepted.** Normal multi-M scheduler/GC, no cgo,
   cooperative Go preemption initially, and process-fatal hardware faults.
   Explicit Go panics retain normal runtime behavior, but fault-derived recover
   and call-free-loop/stop-the-world progress are not promised. General resumable
   faults and safe asynchronous runtime preemption follow separately.
2. **Repository/toolchain ownership — accepted.** An owner-maintained pinned Go
   fork and host cross-toolchain, separate from the LLVM fork/container. The owner
   creates its repository and mirror/cache before build work. **P1F writer versus
   restored host converter remains open, with no preferred route here**; task 2
   presents the comparison before implementation selection. A restored converter
   would be a Pyxis host tool, not a hidden Go/CI dependency in this refresh.
3. **Private-VM contract — accepted.** The process-owned aligned reservation family
   above: generational identities, eager zeroed RW/NX commit preserving existing
   pages, explicit decommit, whole release, and BSP/activity-lease atomicity.
   No demand paging, fixed-address replacement, partial release or Go-shaped mmap.

No code, dependency pins, new compiler container, Go build/CI dependency, tests
or probe execution are part of this refresh. These accepted decisions authorize
no implementation: Go and VM tasks await a separate owner go after hosted Clang.

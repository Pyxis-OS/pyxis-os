# Technical debt and implementation tradeoffs

Record concrete limitations of implemented choices here: what we chose, its
cost, and when to reconsider it. This is a working record, not a roadmap or a
commitment to replace every simple implementation. Remove or update entries
when the underlying tradeoff changes.

## Contiguous RAM-file backing

RAM files currently own one kernel heap buffer. Growth reserves geometric spare
capacity when possible, falling back to the required size if that allocation
fails. Replacing a buffer temporarily holds both the old and new allocations
and copies the live contents. Large files therefore amplify peak memory usage
and copy cost; one allocation is also subject to TLSF's block-size limit.

Shrinking to a nonzero size retains capacity for reuse. Truncated bytes cannot
be observed after regrowth, but a small file may keep a much larger allocation.
Resize to zero or final object destruction returns the buffer to the heap;
the heap's existing pools remain mapped.

Reconsider this when larger files or memory pressure make those costs material.
Chunked backing and a policy for releasing excess capacity are possible changes,
not requirements for the current milestone. RAM writes retain full completion
or unchanged-on-failure behavior; the general [file contract](interfaces/processes.md#implemented-file-calls)
also permits short writes.

## Retained userspace heap pools

Libc's TLSF allocator reuses freed blocks but retains every backing pool until
process exit. Pools are at least 64 KiB; there is no fixed pool-count registry.
A short-lived peak therefore leaves memory mapped for the rest of that process.
Shrinking `realloc` also keeps the original block capacity; growth may briefly
hold both blocks and copy contents to preserve 16-byte alignment.

Kernel process destruction reclaims all private backing, including live malloc
allocations. Reconsider empty-pool release and in-place aligned growth when
long-lived applications make retained capacity or copying material. The current
allocator and errno assume one thread per process; add synchronization and
thread-local errno when introducing userspace threads.

## Allocation measurement follow-ups

The [allocation benchmark and caller-scoped memory profile](development/allocation-profiling.md)
separate warm userspace heap throughput, heap expansion and direct private-page
requests. Profiling splits parking/publication, BSP queue time, service and
resumption. Report the accelerator, CPU count, live set and host/nested-VM context;
the instrumentation itself reads HPET and perturbs timings.

Standalone kernel `kmalloc`/`kfree` throughput and deeper PMM/VM timing remain
unmeasured. Pool growth counters describe backing acquired during a measurement
window, not total retained memory or a fragmentation metric. Private-memory
requests now [notify the BSP after publication](kernel/smp.md), removing their dependency
on a later timer wakeup. The common executor now admits all migrated services,
including HOST forwarding; measure queue and worker costs before changing
allocation policy.

## I/O baseline attribution and coverage

The [I/O/IPC baselines](development/io-ipc-baselines.md) measure elapsed workload boundaries
in nested KVM. The [RAM attribution](development/io-reliability-attribution.md#ram-file-profiling)
separates RAM replacement costs and identified queue time as dominant in the
earlier growing writes/copies. The common executor now resolves their missing
notification; see the FILE service-delay entry below.
HOST profiling now separates guest queues, worker service and transport, but
strongly perturbs the nested workload; see the profiling entry below. Transport
still combines device/daemon/backing service and guest/host scheduling. Host
write/sync baselines used tmpfs and do not establish physical-disk durability cost.

Revisit host attribution resolution before changing batching or transfer limits.
Private-memory, RAM replacement and HOST collections remain independent. Short native
and SEND intervals are close to clock overhead; finer comparisons need a separate
longer-batch or scoped-instrumentation contract. Owner-host/physical-hardware
results, capability attachment cost, cross-space contention, mixed-workload
fairness and per-process CPU accounting remain unmeasured. The final combined
IPC/HTTP/RAM/HOST matrix and additional resolution decisions were deferred when
the reliability milestone closed; no final matched matrix is claimed. Gather
relevant coverage before making deployment-capacity or fine-grained performance
claims; keep each environment and completion boundary distinct.

## RAM FILE BSP service delay

Resolved by the [BSP request migration](kernel/bsp-service-requests.md#profiling-and-scheduling-costs):
RAM replacements now notify the common executor promptly. Matched four-CPU
nested-KVM controls reduced unprofiled growing-write median from 59.041 to
1.957 ms and growing-copy median from 58.852 to 2.540 ms. Prepared controls and
replacement/copy counts stayed comparable. The
[implementation PR](https://git.internal/PyxisOS/pyxis-os/pulls/237) retains all
off/on controls and separates queue from service observations.

The [earlier attribution](development/io-reliability-attribution.md#ram-file-profiling) remains
historical evidence. These results do not remove individual non-preemptible
allocation/copy costs or establish owner-host performance. Revisit scheduling
limits when mixed-workload measurements justify a different service policy;
this task does not change buffer growth or allocator concurrency.

## Host FILE profiling perturbation

The [HOST attribution matrix](development/io-reliability-attribution.md#host-profiling-and-attribution-limits)
records separate initial BSP queue, worker queue/service, transport and resumption
intervals. In the agreed five-sample nested-KVM groups, profiled medians were
13.7–16.1 times their controls. Initial queue wait took 73.8–78.8% of profiled
transfer time; these percentages cannot partition normal unprofiled transfer time
or establish host filesystem cost. Timestamp overhead changes interleaving as
well as adding elapsed time, so subtracting a constant is inappropriate.

The [task-4 controlled experiment](development/experiments/host-profile-slowdown/README.md)
reproduces 14.9× full-profile slowdown on prepared HOST writes. Explicit initial
BSP notification reduces it to 2.0×; counts-only runs stay near their off controls.
Initial queue mean drops from 5.331 to 0.137 ms/request. This establishes a large
notification-dependent amplification under the measured workload. Queue-sweep
timing followed by timer wake is consistent with code and near-8 ms maxima, but
individual wake causes were not traced. The residual includes direct timestamp
work and unresolved scheduling/transport observation effects; no constant
correction or normal-workload phase partition is justified.

Initial HOST publication now uses the common executor's synchronized idle
notification, preserving request ownership and early wake semantics. The
[correction validation](development/io-reliability-attribution.md#host-publication-notification)
repeats the affected off/on controls; full profiling still perturbs execution.
The milestone closed after this correction. Additional resolution/coverage and
the final combined IPC/HTTP/RAM/HOST matrix were deferred as independent work,
not completed. Future comparisons must retain unprofiled elapsed controls and
settle any additional measurement coverage separately. The report's counts-only
patches are experimental, with unavailable phase timings explicitly omitted; no permanent
collection mode or clock change has been accepted. Full-profile attribution
remains limited to instrumented behavior after the notification correction.
Host-side component timing and durable storage remain separately scoped work;
current fixtures are tmpfs with sync off.

## Fixed userspace stacks

Each process eagerly backs a 1 MiB user stack, including programs that use much
less. This gives native parsers and callbacks room without port-specific
recursion limits, at a cost of 960 KiB more backing per process than the former
64 KiB budget. It is still finite; port stack requirements need review.

A reserved, unmapped page below the stack catches ordinary downward overruns,
but a large adjustment can skip it. Compiler stack probing and automatic stack
growth are not implemented. Reconsider eager backing when process counts or
memory pressure justify it. Demand-backed stacks must respect BSP ownership of
allocation and page-table mutation; they are not just a fault-handler shortcut.

## BSP-only allocation and VM mutation

Kernel allocation and page-table mutation remain owned by the BSP. Tasks submit
specific requests and wait for BSP service. The
[common executor](../kernel/service/request.c) separates subsystem operations
from scheduling through a closed service catalog and one FIFO. Each user task
owns one reusable request allocation and a separate caller-only profiling
allocation; kernel workers own neither. Subsystems own request capture, service
helpers and profile controls. This keeps allocator and VM ownership explicit,
but long non-preemptible service operations still delay other requests and BSP
work.

The handoff ordering is part of correctness, not incidental queue plumbing.
Private-memory requests are published only after the requester has left its
task stack and private address space; resumption reloads CR3 before returning to
the task stack. A wake arriving before a task finishes parking records a
notification without making the still-running context runnable elsewhere.
Changes to service placement or synchronization must preserve these guarantees
or explicitly replace them with an equally defined ownership and translation
invalidation contract.

Reconsider BSP-only service when its latency becomes material or before allowing
concurrent use and mutation of one private address space. The
[implemented BSP request contract](kernel/bsp-service-requests.md) has separated operation
ownership, submission/completion and subsystem service from scheduling while
retaining BSP-only allocation and the inactive-root handoff. Allowing allocation
on other CPUs remains a separate decision; an allocator spinlock alone does not
resolve these ownership constraints. Eager task-lifetime storage and long
non-preemptible operations remain explicit costs.

## Synchronous launch preparation

Each in-flight launch reserves a full 64 KiB metadata capture buffer plus a
small header from the kernel heap, even for short argument lists. BSP performs
child preparation with interrupts disabled, as for existing VM/heap services.
The executable file's operation ownership serializes reads, writes and resizes
through image validation/loading, avoiding another whole-image copy. Large
images therefore delay both BSP work and callers using that file.

Revisit staging size and preparation scheduling when larger applications or
concurrent launches make these costs material. A snapshot or immutable backing
could shorten file ownership, at a memory/complexity cost. No such mechanism or
asynchronous launch protocol is introduced now.

## Initial terminal editor

Libterm redraws the full visible line on each edit or cursor move and reads one
input byte per call. This keeps cursor/scroll behavior explicit and avoids
holding keystrokes needed by a future foreground child, at a syscall/rendering
cost. Revisit changed-span rendering or input buffering with an explicit handoff
when interactive workloads make that cost material.

Editing assumes exclusive output use. The single-CPU fallback shares Caelum's
TTY with scheduler/kernel logs, which can move the cursor during a read and
visibly disrupt its display. Input bytes and the returned line remain separate
from those writes. A real terminal ownership policy or separate log view is
needed before treating that fallback as a normal interactive environment.

The current editor accepts only one-cell ASCII and keeps the prompt/line/cursor
on screen. History, Unicode widths and larger-line viewports are not implemented. These boundaries are recorded in [the terminal contract](userland/terminal.md).

## Process termination and Ctrl-C

Process handles remain non-owning WAIT observers; closing one does not stop
execution. [Execution-group CONTROL](interfaces/execution-groups.md) now permits
whole-group termination, including blocked-operation unwind. Local foreground
commands and pipelines do not yet have separate interruption groups, so Ctrl-C
cannot interrupt their execution.

Revisit interactive cancellation with explicit foreground targeting and authority.
The group stop paths provide safe ownership return, including IPC caller cancellation,
but terminal Ctrl-C routing and the shell's command/pipeline policy remain to be
implemented. Native cancellation need not require general POSIX signals.

## Console input completion

Framebuffer console input still provides no EOF operation. EOF-driven consumers
such as cksum and tee need finite file/pipe input or an
[independent terminal session](userland/terminal-sessions.md), whose attachment
can explicitly end input. Libc accepts that session's zero-byte read as EOF.
Ctrl-D remains an application-interpreted byte and does not close either backend.

Revisit console input completion and its interaction with line editing when
interactive EOF-driven tools are explicitly in scope. The cksum port preserves
upstream behavior; neither cksum nor restricted tee adds terminal controls or
signal handling.

## Initial independent terminal limits

[Terminal sessions](userland/terminal-sessions.md) have fixed dimensions, 4 KiB
input and 64 KiB output queues, and one attachment. Creation has no per-space
quota; a trusted creator can allocate multiple bounded sessions until allocation
fails. Output backpressure has no deadline. A controller that stops draining can
block application writers; hangup wakes terminal calls but does not stop CPU-bound
code or operations in other subsystems. Execution groups provide separate supervision;
the [remote server](userland/remote-terminal.md) bounds admission at four and
abandons closing output after five seconds. Cleanup can still wait indefinitely
for published HOST work, retaining its admission slot. Four idle or blocked
sessions can exhaust the server; there is no idle timeout, authentication,
encryption, restart or reconnection. Host-loopback forwarding limits the QEMU
host entry point, but every process able to reach it receives the configured
shell privileges. Sessions share granted filesystem roots, space and CPU; there
are no separate principals, private files or per-session execution quotas.
Address changes invalidate the listener and are not automatically rebound.
The interactive host renderer uses one `?` cell for
non-ASCII bytes; machine mode preserves the original data. A full client input
queue delays reading Ctrl+] behind a paste; once read, its close acknowledgment
is bounded at five seconds. Host SIGINT/SIGTERM forces disconnect even under
backpressure. Revisit admission
policy, authentication and presentation breadth with a concrete non-development
deployment or text consumer. Resize and reconnect remain separate work.

## Libc compatibility gaps

The completed [descriptor portability slice](userland/libc-portability.md) supplies
open/read/write/close for cksum and restricted tee. Public O_RDWR, seeking,
fdopen/fileno and duplication remain absent even though fopen supports update
modes and stdio seeking internally. Consumers requiring those interfaces need
a separately agreed extension. Revisit them against a pinned consumer's actual
needs; duplication must settle shared open-state/cursor ownership before adding
new descriptor aliases. Descriptor inheritance and cross-process shared offsets
are not supplied by the existing dedicated startup-stream grants.

Signals are absent, so tee rejects -i and broken pipes report EPIPE without
SIGPIPE. Revisit signal disposition, delivery and lifetime when a selected
consumer needs them; a successful no-op handler would misrepresent support.
Public O_APPEND is also absent, so tee rejects -a; the
[stdio append limitation](#non-atomic-stdio-append) needs a native atomic operation
before an atomic append contract can be offered.

Polling/nonblocking descriptor I/O, fork/exec-style process semantics and buffered
stdio are outside this slice. Current streams are unbuffered and supply no
buffering controls, pushback, scanning or wide I/O. Fixed-width inttypes output
macros do not imply scanning or other integer-type families. Revisit these gaps
only for a concrete consumer, defining native blocking/lifetime behavior or the
library semantics it actually requires. No successful placeholder APIs exist
for the missing operations.

## Public open creation mode

O_CREAT accepts only mode 0666 as a request for native creation policy. It does
not install Unix permissions, change ownership or create authority. Other modes
fail with ENOTSUP before lookup/mutation, including restrictive requests such as
0600 and opens of existing files. Virtio-fs retains its current 0644 creation
request under the host-service identity and host restrictions still apply.

This is the accepted compatibility policy for the first writable public opens,
used by tee. Revisit it when a file permission system, users and ownership are
introduced; define mode enforcement and umask behavior together rather than
silently discarding requests callers expect to restrict access.

## Non-atomic stdio append

Append streams query the current file size before each native write. Another
writer can change the file between those calls, so concurrent appenders can
overwrite one another. Seeking does not disable the append policy, but it cannot
make the pair atomic. Keep this limitation until concurrent appending needs a
native operation that chooses the end and writes under one file operation.

Formatted stream output currently stages the full result using snprintf, with
heap allocation and a second formatting pass when the small stack buffer is
insufficient. This avoids a second formatter or a generic output callback layer,
but large formatted output requires temporary memory. Revisit bounded streaming
when real consumers make that cost material. All FILE streams are unbuffered;
there are no pending writes to flush yet.

## Unexpected native close failures

Libc invalidates a descriptor and its FILE association before one native CLOSE
attempt. Today's native success/BAD_HANDLE outcomes leave no owned capability
entry. If a future native failure or malformed reply makes release uncertain,
libc reports the error and discards its metadata without retrying. Any residual
native capability survives until kernel process teardown and can delay pipe EOF
or EPIPE until then. Normal exit does not retry previously invalidated entries;
open rollback applies the same policy while retaining the original open errno.

This limit is accepted for unexpected failures, not ordinary deferred release.
Revisit it if CLOSE gains additional outcomes or asynchronous release semantics;
define whether ownership remains before introducing retries or pending-close
storage. The [close contract](userland/libc-portability.md#close-failure-and-cleanup)
records the current status and errno rules.

## Directory APIs in libpyxis

The first ls and mkdir use native libpyxis enumeration and creation helpers,
with libc for output and other C support. This keeps the initial utilities small,
but programs expecting libc directory APIs will still need native adaptations.

TODO: revisit libc directory enumeration and creation APIs when extending libc
or porting consumers that need them, including whether to expose dirent-style
iteration and a mkdir wrapper. Define how those APIs map to capability roots,
directory context, rights and errno before implementing them. The native ABI
remains capability-based; this does not commit the OS to POSIX semantics or add
those wrappers to the first-shell milestone.

## File identity across capability paths

The filesystem protocol has no operation for determining whether two opened
file handles refer to the same underlying file. Path strings cannot provide
that identity: different capability roots and directory paths can reach the
same object, and a descriptive path is not authority or a canonical name.

This blocks reliable `#pragma once` handling in native TCC. The port rejects
that directive explicitly for now; ordinary include guards remain usable.
Revisit an identity operation when adding this facility. Define its comparison
scope, lifetime and behavior across mounts and file replacement before exposing
it; do not substitute normalized path strings or add `realpath` just for TCC.
See [the TCC contract](userland/tcc.md#remaining-limits).

## Wall-clock time and clock-source performance

[Monotonic time and deadline sleep](kernel/timekeeping.md) now use the shared HPET
counter. Console timeouts no longer count delivered BSP interrupts. APIC timer
interrupts still bound wakeup latency; nanosecond units do not promise precise
wakeup, and time spent with the VM paused need not count.

[UTC wall time](kernel/wall-clock.md) uses a whole-second Limine RTC seed plus elapsed
monotonic time. Firmware accuracy, subsecond alignment and boot handoff delay
are not known; there is no drift correction or resynchronization. Time while the
VM is paused need not advance. A missing seed is an explicit error, but a
plausible incorrect RTC value cannot be detected. Future adjustments must not
change monotonic deadlines. [Zoneinfo-backed local time](userland/timezones.md) is handled
in userspace. TCC uses UTC calendar macros and monotonic `-bench`;
Kilo uses monotonic time for status-message expiry. HTTPS certificate validity
also depends on this UTC value: an available but incorrect RTC date can cause
incorrect acceptance or rejection. Revisit authenticated time synchronization
before treating TLS date checks as independent of firmware/hypervisor time.

HPET MMIO reads can be expensive, especially under virtualization. The
[HOST forwarding investigation](kernel/bsp-service-requests.md#profiling-and-scheduling-costs)
removed unnecessary reads for empty scheduler deadline lists and untimed HOST
idle waits, restoring the measured unprofiled transfer times to baseline. Active
deadlines and profiling still pay the clock cost. Consider a
validated TSC source later, including frequency discovery and cross-CPU
consistency, without changing the clock protocol. The current source requires
a 64-bit, memory-mapped HPET; there is no source registry or fallback. VirtIO
RTC remains deferred until PCI/VirtIO infrastructure exists.

## Doom configuration and save-format limits

[Doom save/load](userland/doom.md#saves) now uses checked temporary writes and atomic
replacement through the [RAM filesystem](interfaces/filesystem-mutations.md). Saves remain
volatile across reboot. The upstream parser assumes trusted saves matching the
loaded game data; full malformed-file validation and separation by PWAD are not
implemented. Interrupted saves can leave temporary files for manual removal.

Configuration persistence is already disabled in the pinned generic engine.
Re-enabling it needs an explicit writable configuration location and review of
its parser/formatting requirements. Floating printf is now available for the
upstream timedemo report; exercising timedemo remains separate from normal
gameplay and demo playback. Wall-clock time is not a prerequisite.

## Virtio-fs runtime resource retention

The first [virtio-fs transport](devices/virtio-fs.md) reserves queue storage and device
mappings before AP startup. A runtime failure masks interrupts, disables bus
mastering and attempts reset, but retains the claim, 40 KiB of ring/payload
allocations, CPU-side queue bookkeeping and their mappings until reboot. Even a
confirmed reset does not make it safe to change shared kernel mappings without
a TLB invalidation and
reader-lifetime contract. No reconnect or repeated allocation occurs.

Revisit reclamation alongside shared-mapping invalidation and a defined device
teardown/reconnect lifecycle. Never free an outstanding DMA buffer solely because
a request timed out. Idle daemon disconnection is not necessarily observable
until the next request or device event; there is no heartbeat.

## Shared split-queue scaling and validation

The [shared queue helper](devices/virtio-queues.md) supports multiple direct chains and
allocates storage for the selected queue size. Request-ID uniqueness checks and
completion validation scan queue-sized bookkeeping arrays. Current filesystem
and entropy queues are small and remain serialized; no throughput improvement
or scaling result is established. Revisit these scans if a measured block or
network workload makes their cost material.

Runtime validation covers the migrated serialized consumers and the block
driver's eight outstanding writes. Two concurrent block reads also completed
out of order with correct ticket association and full readback. Allocation and
malformed-completion/reset failure paths have code inspection only; no fault
injection was authorized for these tasks. Revisit those failure paths before
expanding the storage reliability claims.

## Virtio-blk failure and validation limits

The [block driver](devices/block-storage.md) latches write failure after an ordinary
write or flush error. Further writes and flushes fail until reboot, while reads
can continue on a healthy transport. This prevents a later flush from hiding an
earlier persistence failure, but makes transient backend write errors require a
reboot before writes can resume. Revisit with a concrete filesystem consumer and
an explicit error-acknowledgment/recovery contract; never silently retry writes
that may already have modified storage.

Runtime device failure retains the PCI claim, queue bookkeeping, DMA allocations
and mappings even after confirmed reset. The default profile reserves 512 KiB
of payload storage plus control buffers and rings. This bounds retained memory
but provides no reconnect or reclamation. Revisit alongside shared-mapping TLB
invalidation, DMA ownership and a defined device teardown lifecycle. Timeout alone
cannot release storage still accessible to the device.

Physical hardware and power-loss persistence have no coverage in this milestone.
Normal QEMU restart/readback cannot establish either. Error, reset-failure and
malformed-completion behavior require separate validation if fault injection is
later authorized. Revisit durability evidence before promising filesystem
recovery or support for production data.

## GPT snapshot and profile limits

[GPT discovery](devices/gpt.md) publishes one immutable boot-time snapshot. There is no
raw-block write gate, metadata generation tracking or rescan, so trusted kernel
clients must preserve GPT metadata and avoid external mutation for the entire
boot. Snapshot health does not track later device failure. Revisit with the first
partition I/O consumer and any format/repair workflow, defining authority,
metadata exclusion and replacement lifetimes before allowing live changes.

The supported profile is GPT 1.0 on 512-byte or 4 KiB blocks, at most 256 entries
and 64 KiB per array. Unsupported revisions, larger layouts, reserved attributes
and legacy/hybrid MBRs expose no map. One valid copy supplies a read-only degraded
map only when the other is absent or invalid; I/O errors, timeouts and unsupported
metadata prevent fallback. These conservative bounds can exclude otherwise usable
media. Revisit only for a concrete consumer with explicit resource limits and
recovery policy; no automatic repair is available.

## Virtio-net runtime resource retention

The [network transport](devices/networking.md#virtio-net-transport) uses two nine-page
contiguous allocations for rings and packet buffers (72 KiB total). Runtime
failure attempts reset and disables delivery/DMA, but retains the PCI claim,
allocations and mappings until reboot, for the same shared-mapping lifetime
reason as virtio-fs. No reconnect or repeated allocation occurs. Revisit both
drivers' reclamation with a real teardown and SMP invalidation contract.

## Host filesystem request storage and enumeration

The [native virtio-fs backend](devices/virtio-fs.md#native-directory-and-file-objects)
uses the largest record in each user task's reusable 4,928-byte request allocation,
including a 4 KiB read/write buffer. A separate 816-byte persistent profile
allocation is also eager. Kernel workers allocate neither area. This avoids
allocating on APs or exposing private stacks to the worker, but every user task
pays both costs even if it never accesses HOST or enables profiling. Eager
provisioning is accepted to keep submission allocation-free and guarantee cleanup
capacity. Revisit lazy provisioning if user-task counts or memory pressure make
the cost material, with explicit allocation-failure, BSP handoff and guaranteed
cleanup ownership; do not add another fixed request registry. The common BSP
executor forwards these records without waiting for blocking host I/O.

The native enumeration ABI returns one name per call. The backend requests a
fresh 4 KiB READDIR batch and discards unused entries, so a large listing can
transfer the same trailing names repeatedly. There is no attribute/data cache
or directory snapshot. Revisit batching with a concrete consumer and explicit
host-change semantics. Host executable loading captures at most 16 MiB per
launch into owned memory; it does not provide a coherent snapshot if a host
process edits the file in place during capture. Callers must avoid in-place
changes while loading, and before/after size checks cannot prove snapshot
consistency. Revisit the per-capture limit only with a bounded staging and
concurrency design that preserves this lifetime contract.


## Initial TCP listener limits

Native [TCP listeners](devices/tcp.md#listening-and-admission) bind only the exact
configured NIC IPv4 address, with no wildcard, loopback listener, ephemeral bind
or reuse. Four listeners and their pending/accepted connections share 32 global
transport records; TIME_WAIT can block later admission even below the per-listener
backlog limit. These development bounds provide no per-space quota or protection
against exhausting the global budget. Revisit them with concrete concurrent-server
demand and an explicit authority/accounting policy, not by evicting live records.

The echo consumer serves four clients with bounded output and fair service, but
has no idle-client or output-drain deadline. Four stalled clients can occupy all
active slots indefinitely. Readiness also supports terminal attachments and
execution-group and process completion. The [remote server](userland/remote-terminal.md)
adds its own session supervision and closing-output deadline; the echo consumer
retains its simpler semantics. Revisit echo-client expiration only if a concrete
consumer needs it.

## UDP ICMP errors and ephemeral selection

The first [UDP implementation](devices/networking.md#udp-datagrams-and-deadlines) silently
drops traffic for unbound ports and does not deliver received ICMP errors to
applications. A remote absent listener can therefore look like packet loss until
a receive deadline expires. Add bounded, rate-limited ICMP error generation and
safe matching of quoted packets before claiming full UDP host conformance;
keep completed/retired calls immune to late errors.

Generic ephemeral binding currently scans 49152–65535 from a rotating cursor;
this allocator is not a defense against off-path reply guessing. The
[DNS client shared by dig and ping](userland/dns.md) explicitly chooses random ports
using [host-backed randomness](devices/randomness.md). Revisit the generic allocator's
policy for other consumers. Network authority and resource
bounds also remain system-wide rather than isolated by space.

## Shell redirection side effects and file aliases

File redirection opens all targets before truncating outputs, but creation,
truncation and child launch are separate operations. A failed open can leave
newly created files; a failed resize or later launch can leave truncated outputs.
There is no rollback. Redirecting an output onto an input file destroys its
contents before the child consumes it, even through different path aliases.
No object-identity/same-file check is provided.

stdout and stderr retain independent per-descriptor offsets when both refer to
the same file. Their writes can overwrite each other; this does not implement
descriptor duplication or merged output. These limitations are accepted for the first
redirection scope. Revisit if alias-safe copying or shared-position output becomes
an explicit requirement; batch launch does not promise filesystem rollback.
See [shell redirection](userland/shell.md#file-redirection-and-stdin).

## Pipe scheduling and resource limits

Native pipes have fixed 64 KiB storage and 4 KiB per-call transfer limits. Copied
readers compete for bytes and copied writers may interleave transfers, without
a guaranteed atomic write size or strict fairness. Creation uses normal kernel
allocation limits; there is no separate per-process pipe-memory quota. A holder
of unused endpoint copies can delay EOF or EPIPE indefinitely. There are no
nonblocking operations, deadlines, wait sets or direct cancellation operations.
Group termination detaches blocked readers/writers safely. Revisit these
limits when a concrete multi-producer or multiplexed consumer needs them.
[Shell streams](userland/shell-streams.md) documents the implemented launch ownership.

## Batch launch after publication

Batch launch protects preparation: all one through eight children are prepared
before any can execute, and failure starts none. Ordinary ungrouped batches do
not cancel a running child when a sibling faults, an observer closes or the
launcher exits. Group supervision separately stops members on final CONTROL loss. A child
waiting on terminal input or doing unrelated work may therefore keep running
indefinitely after a peer finishes. Filesystem creations/truncations before
launch remain visible after preparation failure. Revisit scoped cancellation or
larger batches only for a concrete lifecycle requirement. Foreground pipelines
wait for all children; an unrelated or terminal-blocked stage can therefore keep
the shell waiting even when the last stage has finished. Last-stage success does
not hide earlier diagnostics, but it permits scripts to continue; no pipefail
policy is provided. See [shell pipelines](userland/shell.md#foreground-pipelines).

## Exact line limits in head

Head's line mode reads one byte per backend call so it never consumes past the
requested newline. With unbuffered stdio this increases syscall overhead for long
lines. Byte mode retains bounded bulk reads. Revisit buffering or a native
bounded-delimiter read only when a concrete consumer needs both throughput and
exact stream consumption; do not silently discard read-ahead. Multi-file output
headers and additional head options are outside the current consumer scope.
See [head usage](userland/shell.md#bounded-input-with-head).

## Endpoint cancellation and capacity

[Endpoints](interfaces/endpoints.md) have no external cancellation operation, wait sets or
wait-for-capacity facility. Admission to a full endpoint fails immediately.
A provider can retain all sixteen delivery slots by leaving receipts unfinished;
deadlines release callers but do not reclaim delivered work. Calls without a
deadline can wait indefinitely, including self-calls or cycles between blocked
single-task processes. Control notifications remain deliverable at full capacity,
but the provider must receive and finish the retained work.

Revisit with asynchronous service scheduling and explicit cancellation/wait APIs.
Preserve delivery/outcome reporting and receipt ownership; cancellation must not
silently revoke attachments already delivered. Group termination now cancels its
callers and detaches receivers while preserving outside provider receipts.

## Endpoint throughput limited by deferred receipt reclamation

Resolved for completed work: final receipt release now performs logical cleanup
synchronously under the endpoint lock. A delivery record becomes reusable once
receipt ownership and CALL outcome collection, if any, have both ended. The
embedded receipt never enters the retirement queue; a separate endpoint backing
object preserves BSP destruction ownership. See the
[endpoint contract](interfaces/endpoints.md) and
[reliability milestone](development/io-reliability-attribution.md) for implementation and
validation details.

Historical observations before the fix: the sixteen delivery records retained
completed CALLs and finished SENDs until the BSP destroyed their retired receipt
objects. The [I/O and IPC baseline](development/io-ipc-baselines.md) observed this on CPU 1 in
four-CPU nested KVM: a zero-byte 256-call warmup completed 21 round trips before
QUEUE_FULL; SEND admitted and acknowledged two groups of eight, then rejected
message 17. These are observed failure points, not deterministic capacity
thresholds: BSP scheduling/reclamation changed the number completed.

Matched HTTP runs also observed ordinary 1 MiB snapshot reads fail with EAGAIN
after 61320 and 122640 confirmed bytes, at caller request sizes 4088 and 65536. Each
OPEN fetched the full body successfully; the retained FILE reads then failed.
The 32 KiB matched fixtures completed. These failures were consistent with the
same receipt capacity limit and affected real exported-file consumers, not only
synthetic IPC batches. Larger caller buffers did not bypass the 4088-byte FILE
transfer limit.

The original baseline used fresh endpoints and eight-message `ipcbench` samples,
with creation and teardown outside timing, to obtain successful measurements.
This workaround did not establish sustainable throughput; even shutdown control
calls could encounter the same limit. The original measurements remain
historical evidence, separate from the reliability milestone's reruns.

The live-work limit remains sixteen delivery records. Queued messages,
unfinished provider receipts and CALL outcomes awaiting collection still consume
capacity, and genuine exhaustion still reports QUEUE_FULL. A deadline releases
the caller, not a delivered receipt or the provider's attachment handles;
calls without deadlines can still wait indefinitely. Revisit these remaining
limits with the [cancellation and capacity work](#endpoint-cancellation-and-capacity),
without retrying or pacing away admission failures.

## Service startup failure before publication

The namespace publication command waits on the provider's registration endpoint.
Providers can explicitly report setup failure before registration; the launcher
acknowledges it, and optional startup continues after a well-formed failure. If a
launched provider exits or faults without reporting, the parent cannot currently
wait for either IPC or process exit, so startup can remain blocked.
A provider CALL deadline bounds its own registration wait, but does not bound
the parent's RECEIVE. Revisit with endpoint/process wait sets or a bounded receive
facility; do not infer provider readiness from launch success or add automatic
restart. See [process termination and Ctrl-C](#process-termination-and-ctrl-c)
for forced shutdown.

The manually invoked pipe/IPC benchmarks have the same startup/reporting limit:
a companion that faults before its readiness or result message can leave the
coordinator in RECEIVE. Their CALL deadlines do not bound pipe or process waits.
Revisit benchmark-wide timeouts with the same bounded-receive/wait-set work;
retain explicit partial progress and never describe a CALL deadline as forced
process termination. See [I/O and IPC baselines](development/io-ipc-baselines.md).

## Provider calls through synchronous file helpers

The shared FILE helpers and ordinary path/libc opens currently submit calls
without a deadline. Native provider OPEN exposes an explicit caller deadline.
A live provider that stops replying can therefore block ordinary file readers
and shell input redirection indefinitely.
Provider exit or withdrawal releases affected waits, but neither is automatic.
The immutable text service does no blocking work inside a request. The HTTP fetch
library bounds one fetch to thirty seconds (or an earlier caller deadline), but
this does not bound queueing or invocation through the shared file/open helpers.
Revisit caller-controlled bounded file/open waits alongside cancellation/wait
sets; do not introduce hidden retries or an arbitrary global timeout. Existing endpoint APIs already support explicit deadlines.

## HTTP framing compatibility

The [initial HTTP library](userland/http-fetch.md) deliberately rejects duplicate or list
Content-Length, folded fields, non-CRLF headers and unsupported transfer/content
codings. The pinned chunk decoder also rejects sufficiently excessive framing
overhead. Some otherwise valid origins can therefore fail before the configured
body limit. Close-delimited responses cannot prove whether an orderly EOF was
intended to end the content. Revisit these restrictions when expanding HTTP client
compatibility; do not silently accept ambiguous framing or publish partial bodies.

## HTTPS trust and platform limits

The [HTTPS provider](userland/https.md) uses a pinned Mozilla-derived PEM export, which
omits Mozilla's additional trust-store constraints. It verifies chains, names
and dates, but configures no revocation source or online revocation policy.
A certificate can therefore remain accepted despite revocation or omitted
constraints while its other verification checks pass. Trust updates are
manual image/ports updates followed by provider restart, so a deployed instance
does not automatically receive later removals. Revisit richer trust constraints,
revocation and update policy for a deployment that requires them; preserve
explicit authority and bounded failure rather than silently downloading policy.

HTTPS accepts DNS names only. Numeric-address URIs fail as unsupported because
IP-only SAN verification and SNI handling have not been implemented. Revisit when
a concrete IP-address consumer needs HTTPS; do not route it through DNS/CN name
matching. Scheme authority and optional custom roots do not confine destinations.
Revisit destination policy separately when a consumer requires isolation.

Entropy comes from the [VirtIO random capability](devices/randomness.md) and trusts the
hypervisor's bytes. There is no implemented physical-hardware entropy path or
fallback. Missing entropy leaves HTTPS unpublished and also disables new kernel
TCP connections for that boot. Inventory and implement a supported hardware
source before claiming native-machine HTTPS; a presumed CPU feature is not an
entropy source. UTC remains subject to the
[wall-clock limits](#wall-clock-time-and-clock-source-performance) above.

TLS buffers, chain depth and the 2 MiB counted allocation cap deliberately reject
oversized handshakes. Successful controlled connections establish
fit for the measured roots and peers, not all valid certificate chains or suites.
Deadline checks between operations do not preempt CPU-bound cryptography. Revisit
these limits only for a demonstrated endpoint or scheduling requirement, with
measured demand and bounded ownership; do not weaken verification or silently
raise budgets.

## HTTP provider responsiveness

Each [HTTP/HTTPS provider](userland/http-fetch.md) task performs synchronous fetches. While it
fetches, existing snapshot reads and lifecycle processing wait behind it, and
retired bodies can continue occupying the storage budget. Each fetch has a
30-second budget or earlier caller deadline; ordinary file helpers still submit
unlimited IPC waits, and queued work can compound that delay. Cancellation cannot
interrupt a blocking network operation instantly. Revisit with asynchronous
service work and wait sets; no worker-process or thread framework is included.

## Native mount design limits

The [native read-only mount contract](wip/native-readonly-filesystem.md) is in
design review; these are agreed integration limits, not implemented behavior.
The OS READ grant continues to bundle file bytes and length, so native acquisition
and descendant lookup must hold both core read and metadata rights. A persistent
read-only grant lacking metadata cannot become an OS READ grant. Revisit when
there is a concrete consumer for separately delegable OS file metadata; do not
weaken core policy to accommodate the existing ABI.

Trusted init scripts share one configured bootstrap principal in this milestone.
They can delegate different subsets, but this is not independent authentication
or admission for each session. Ordinary applications receive no principal-based
reacquisition service or mount authority. Revisit with the identity broker/session
admission work, keeping identity separate from held capabilities.

The agreed starting proposals of 32 native requests and 8 MiB of live core payload
need representative-image measurements during adapter implementation. A valid
image may exceed the budget and return LIMIT; the cap does not cover all kernel
allocation overhead or bound CPU/I/O work. Record measured peaks and revise the
profile explicitly before claiming supported workload coverage.

## Filesystem host prototype limits

The host-image tools record prototype reserve defaults, but no writable
transaction/recovery cost bound proves those budgets sufficient. Sparse host
image sizing also does not reserve host disk space. Before writable work, settle
bounded admission costs, allocator self-hosting, orphan retention and recovery
bookkeeping under the agreed
[commit rules](../fs/docs/format.md#future-publication-and-reclamation-envelope).
The [implemented filesystem contracts](devices/filesystem-readonly.md) do not promise
crash recovery or production-data safety.

The bulk planner reserves fixed volume/depth workspaces and conservative tree
bounds. Small explicit memory caps can reject tiny images; the default cap does
not guarantee the maximum record profile fits. There is no temporary-file spill
strategy. Planning fails before image creation. The checker retains bounded
records for both committed states, including transient old/new arrays during
growth, and resource exhaustion leaves the result explicitly incomplete.
Revisit workspace sizing when real imports need larger manifests or lower caps.

Readonly object access scans consulted ancestor directories to validate unique
naming. Separate calls repeat ancestry, grant and allocation-proof work; file
reads can repeat proof closure. Memory is capped, but I/O work has no small-count
or performance guarantee. Successful source-tree and binary extraction establishes
correctness for the recorded inputs, not scaling bounds. Measure costs before
larger workloads justify caches or change the lifetime contract.

[Host validation](../fs/docs/host-tools.md#validation) covers populated directory
paging, nested policy decisions, inline file extents, empty and multi-level
images, 256 volumes and explicit GPT selection with both sector sizes. All
runtime-checked committed pairs have identical generation-1 roots. Sparse host
files are materialized into data, so sparse and multiple-extent reader paths
retain source-review coverage. Grant-free volumes, degraded GPT, concurrent
source changes, malformed/unknown metadata and actual I/O/flush failures also
have source-review coverage only.

Cross-generation retention/reuse, retired charges and differing committed roots
have no runtime evidence. Revisit those paths using agreed tooling before writable
transactions rely on retention or recovery behavior. Structural checking verifies
allocation claims without reading payloads; file-data checksums remain deferred.
Extraction and host comparison establish content equality only for the exercised
inputs. The offline checker cannot prove runtime readers or outstanding I/O have
released storage, so it cannot authorize reclamation by itself.

## Execution-group shutdown

[Execution groups](interfaces/execution-groups.md) now support termination and cleanup
completion. Published BSP/HOST loans still finish before their caller retires, so a
stalled host operation can delay completion indefinitely. Killing members cannot
roll back completed external effects or recall capabilities delegated outside the
group. Completion excludes legitimately external owners and independent protocol
maintenance after native ownership ends.

Revisit bounded HOST cancellation when transport ownership can be revoked safely;
do not turn a timeout into permission to free lent process state. Group/member
allocation has no quota beyond available storage. The remote server bounds
concurrent sessions at four; that is not a descendant or per-session memory quota.
Foreground interruption remains separate work.

## System-information observation limits

[System information](interfaces/system-information.md) caches one guest-visible
BSP CPU brand and the online logical count at boot. It cannot describe a
heterogeneous machine, hotplug or process CPU allowance; revisit that snapshot
when CPU lifecycle or scheduling contracts change. Allocator counters exclude
permanent reservations and expose system-wide usage to every READ holder. Keep
the explicit **Memory (allocator)** label; a future installed-memory query needs
its own authoritative source and meaning.

The embedded source commit identifies the running kernel's base checkout, not
whether its inputs were modified. Revisit dirty-input provenance when release
or support workflows need that distinction, after agreeing which inputs count.
No dirty suffix or clean-tree attestation is currently implemented.

## Fastfetch first-port boundary

The [implemented port](userland/fastfetch.md) uses explicit
native-URI JSON/JSONC configs, one-shot text/JSON output and nine selected modules.
Automatic config discovery, config/cache writes, dynamic refresh, image logos,
Lua execution and executable/network helpers are excluded. Existing upstream
configs may encounter unsupported diagnostics or upstream fallback behavior.
The port does not impose a new strict option validator. Revisit individual
features only when a concrete native use case and authority contract exist.
The normal image packages this port. Narrow terminals retain upstream layout,
so logo/data lines may wrap; use `--logo none` or shorter formats. Revisit layout
only with a concrete display requirement, without changing upstream formatting
as part of routine platform integration.

Uptime exposes duration since HPET initialization. JSON bootTime is null;
calendar boot-time/age placeholders are unset and render empty, so configurations
that need those observations require editing. Revisit this only if Pyxis gains an authoritative boot epoch
and agrees its meaning across wall-clock changes; do not infer one by subtracting
monotonic duration from the current wall clock.

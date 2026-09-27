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
or unchanged-on-failure behavior; the general [file contract](processes.md#implemented-file-calls)
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

The [allocation benchmark and caller-scoped memory profile](allocation-profiling.md)
separate warm userspace heap throughput, heap expansion and direct private-page
requests. Profiling splits parking/publication, BSP queue time, service and
resumption. Report the accelerator, CPU count, live set and host/nested-VM context;
the instrumentation itself reads HPET and perturbs timings.

Standalone kernel `kmalloc`/`kfree` throughput and deeper PMM/VM timing remain
unmeasured. Pool growth counters describe backing acquired during a measurement
window, not total retained memory or a fragmentation metric. Private-memory
requests now [notify the BSP after publication](smp.md), removing their dependency
on a later timer wakeup. Other BSP request queues retain their existing service
paths; measure them separately before extending notification or changing
allocation policy.

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
specific requests and wait for BSP service. This keeps allocator and VM ownership
explicit, but moves subsystem coordination into [the scheduler](../kernel/task.c):
capability-table growth, directory-entry allocation, file-buffer replacement,
private memory, mapped graphics and launch preparation each carry request state in the task and
have their own queue and BSP service path. More consumers mean more scheduler
coupling, and long service operations delay other requests and BSP work.

The handoff ordering is part of correctness, not incidental queue plumbing.
Private-memory requests are published only after the requester has left its
task stack and private address space; resumption reloads CR3 before returning to
the task stack. A wake arriving before a task finishes parking records a
notification without making the still-running context runnable elsewhere.
Changes to service placement or synchronization must preserve these guarantees
or explicitly replace them with an equally defined ownership and translation
invalidation contract.

Reconsider this split when adding a subsystem repeatedly expands task state and
scheduler service paths, when BSP service latency becomes material, or before
allowing concurrent use and mutation of one private address space. Moving
subsystem work out of the scheduler and allowing allocation on other CPUs are
separate decisions. A generic request framework or allocator spinlock alone
does not resolve the ownership constraints; no replacement is chosen yet.

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
on screen. History, Unicode widths and larger-line viewports are not implemented. These boundaries are recorded in [the terminal contract](terminal.md).

## Console input completion

The console input protocol waits for bytes and provides no EOF operation.
Consequently EOF-driven consumers such as cksum cannot finish normally with
terminal-only stdin. For now they require finite file input or a pipe whose
last writer closes. A zero console transfer is still an error, not fabricated
EOF, and Ctrl-D is not a libc substitute for a terminal protocol decision.

Revisit console input completion and its interaction with line editing when
interactive EOF-driven tools are explicitly in scope. The cksum port preserves
upstream behavior and does not add terminal controls or signal handling.

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
storage. The [close contract](wip/libc-portability.md#close-failure-and-cleanup)
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
See [the TCC contract](tcc.md#remaining-limits).

## Wall-clock time and clock-source performance

[Monotonic time and deadline sleep](timekeeping.md) now use the shared HPET
counter. Console timeouts no longer count delivered BSP interrupts. APIC timer
interrupts still bound wakeup latency; nanosecond units do not promise precise
wakeup, and time spent with the VM paused need not count.

[UTC wall time](wall-clock.md) uses a whole-second Limine RTC seed plus elapsed
monotonic time. Firmware accuracy, subsecond alignment and boot handoff delay
are not known; there is no drift correction or resynchronization. Time while the
VM is paused need not advance. A missing seed is an explicit error, but a
plausible incorrect RTC value cannot be detected. Future adjustments must not
change monotonic deadlines. [Zoneinfo-backed local time](timezones.md) is handled
in userspace. TCC uses UTC calendar macros and monotonic `-bench`;
Kilo uses monotonic time for status-message expiry.

HPET MMIO reads can be expensive, especially under virtualization. Consider a
validated TSC source later, including frequency discovery and cross-CPU
consistency, without changing the clock protocol. The current source requires
a 64-bit, memory-mapped HPET; there is no source registry or fallback. VirtIO
RTC remains deferred until PCI/VirtIO infrastructure exists.

## Doom configuration and save-format limits

[Doom save/load](doom.md#saves) now uses checked temporary writes and atomic
replacement through the [RAM filesystem](filesystem-mutations.md). Saves remain
volatile across reboot. The upstream parser assumes trusted saves matching the
loaded game data; full malformed-file validation and separation by PWAD are not
implemented. Interrupted saves can leave temporary files for manual removal.

Configuration persistence is already disabled in the pinned generic engine.
Re-enabling it needs an explicit writable configuration location and review of
its parser/formatting requirements. Floating printf is now available for the
upstream timedemo report; exercising timedemo remains separate from normal
gameplay and demo playback. Wall-clock time is not a prerequisite.

## Virtio-fs runtime resource retention

The first [virtio-fs transport](virtio-fs.md) reserves queue storage and device
mappings before AP startup. A runtime failure masks interrupts, disables bus
mastering and attempts reset, but retains the claim, two 20 KiB queue/buffer
allocations and their mappings until reboot. Even a confirmed reset does not
make it safe to change shared kernel mappings without a TLB invalidation and
reader-lifetime contract. No reconnect or repeated allocation occurs.

Revisit reclamation alongside shared-mapping invalidation and a defined device
teardown/reconnect lifecycle. Never free an outstanding DMA buffer solely because
a request timed out. Idle daemon disconnection is not necessarily observable
until the next request or device event; there is no heartbeat.

## Virtio-net runtime resource retention

The [network transport](networking.md#virtio-net-transport) uses two nine-page
contiguous allocations for rings and packet buffers (72 KiB total). Runtime
failure attempts reset and disables delivery/DMA, but retains the PCI claim,
allocations and mappings until reboot, for the same shared-mapping lifetime
reason as virtio-fs. No reconnect or repeated allocation occurs. Revisit both
drivers' reclamation with a real teardown and SMP invalidation contract.

## Host filesystem request storage and enumeration

The [native virtio-fs backend](virtio-fs.md#native-directory-and-file-objects)
keeps one bounded request record in each task, including a 4 KiB read/write buffer.
This avoids allocating on APs or exposing private stacks to the worker, but
charges that storage to every task even if it never accesses the host. Revisit
lazy staging if task counts make the cost material; do not add another fixed
request registry. The BSP scheduler only forwards these records, never performs
blocking host I/O.

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


## UDP ICMP errors and ephemeral selection

The first [UDP implementation](networking.md#udp-datagrams-and-deadlines) silently
drops traffic for unbound ports and does not deliver received ICMP errors to
applications. A remote absent listener can therefore look like packet loss until
a receive deadline expires. Add bounded, rate-limited ICMP error generation and
safe matching of quoted packets before claiming full UDP host conformance;
keep completed/retired calls immune to late errors.

Generic ephemeral binding currently scans 49152–65535 from a rotating cursor;
this allocator is not a defense against off-path reply guessing. The
[DNS client shared by dig and ping](dns.md) explicitly chooses random ports
using [host-backed randomness](randomness.md). Revisit the generic allocator's
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
See [shell redirection](shell.md#file-redirection-and-stdin).

## Pipe scheduling and resource limits

Native pipes have fixed 64 KiB storage and 4 KiB per-call transfer limits. Copied
readers compete for bytes and copied writers may interleave transfers, without
a guaranteed atomic write size or strict fairness. Creation uses normal kernel
allocation limits; there is no separate per-process pipe-memory quota. A holder
of unused endpoint copies can delay EOF or EPIPE indefinitely. There are no
nonblocking operations, deadlines, wait sets or cancellation. Revisit these
limits when a concrete multi-producer or multiplexed consumer needs them.
[Shell streams](shell-streams.md) documents the implemented launch ownership.

## Batch launch after publication

Batch launch protects preparation: all one through eight children are prepared
before any can execute, and failure starts none. It does not cancel a running
child when a sibling faults, an observer closes or the launcher exits. A child
waiting on terminal input or doing unrelated work may therefore keep running
indefinitely after a peer finishes. Filesystem creations/truncations before
launch remain visible after preparation failure. Revisit scoped cancellation or
larger batches only for a concrete lifecycle requirement. Foreground pipelines
wait for all children; an unrelated or terminal-blocked stage can therefore keep
the shell waiting even when the last stage has finished. Last-stage success does
not hide earlier diagnostics, but it permits scripts to continue; no pipefail
policy is provided. See [shell pipelines](shell.md#foreground-pipelines).

## Exact line limits in head

Head's line mode reads one byte per backend call so it never consumes past the
requested newline. With unbuffered stdio this increases syscall overhead for long
lines. Byte mode retains bounded bulk reads. Revisit buffering or a native
bounded-delimiter read only when a concrete consumer needs both throughput and
exact stream consumption; do not silently discard read-ahead. Multi-file output
headers and additional head options are outside the current consumer scope.
See [head usage](shell.md#bounded-input-with-head).

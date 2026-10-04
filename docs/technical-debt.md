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
retaining BSP-only allocation and the inactive-root handoff. The agreed
[runtime SMP milestone](wip/scheduling-and-threads.md), after native writer completion,
will introduce independent spaces, single-task migration and local private-memory
operations with allocator synchronization and explicit mapping lifetime rules. It
is not implemented yet; an allocator spinlock alone does not resolve these ownership
constraints. Selected serial services and deferred destruction remain BSP-owned
initially. Worker relocation and shared kernel mapping reuse need their own
handoff/invalidation contracts. Eager task-lifetime storage and long non-preemptible
operations remain explicit costs; measure them in matched before/after workloads.

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

## Early console and post-handoff panics

The [early boot console](kernel/early-console.md) shows boot progress and
panics only until the display presenter's first frame. Panics after that remain
serial-only, as before. Showing them would mean taking the screen back from a
presenter that may still be running on the BSP, so a machine without serial
shows no panic text once userspace has started. A serial port that stops
accepting output is latched off for the rest of boot and not retried.
Early ordinary log bytes are retained in a fixed, prefix-preserving 32 KiB buffer
and replayed into the Caelum TTY once. This does not provide scrollback: later
output can still displace the beginning, and a full buffer drops later bytes
with a notice. Revisit capacity or a separate log-view capability only when
native bring-up needs more retained history.
Revisit with real-hardware bring-up, if post-boot panics need to be visible
without serial.

## PS/2 scan-set query compatibility

The ThinkPad ACKs set-2 selection and its query but supplies no set-ID byte.
[Keyboard setup](devices/keyboard.md) therefore accepts an absent ID after a
short monotonic wait, keeps wrong observed IDs and controller errors fatal,
and drains queued output before enabling scanning. The fallback relies on the
ACKed selection producing untranslated set 2; native character/modifier/extended
key qualification remains required. A drain cannot identify arbitrary firmware
replies delayed until after scanning starts, although the expected late `02`
has no key mapping. Revisit this policy if native input disproves the selection
or another controller supplies delayed contradictory output. A translated set-1
decoder is a separate compatibility decision, not part of this fallback.

## Process termination and Ctrl-C

Process handles are non-owning observers; closing one does not stop execution.
Launch grants their holder WAIT and TERMINATE, and TERMINATE stops just that
process through the per-task safe stop. [Execution-group CONTROL](interfaces/execution-groups.md) permits
whole-group termination, including blocked-operation unwind.

The [shell terminates its foreground job on Ctrl+C](userland/shell.md#interrupting-foreground-commands),
stage by stage, with immediate termination and no cooperative interrupt.
Accepted limits of this first slice:
- **Descendants.** Only the shell's direct children are terminated. Processes a
  stage launched itself keep running. Today ordinary commands receive no
  launcher, so none exist.
- **Background jobs** cannot be interrupted, and there is no job control.
- **Passthrough holders** cannot be interrupted while passthrough is held.
  Locally that leaves no recovery short of ending the session.
- **A nested interactive shell** cannot arm, so Ctrl+C in the outer shell ends
  the whole inner shell.
- **Remote typeahead.** After more than 4 KiB of typeahead that the command does
  not read, the remote server stops reading frames until its pending injection
  drains. A later Ctrl+C never reaches the kernel; Ctrl+] remains the fallback.
- **Startup scripts.** A startup script holds the right, so Ctrl+C can terminate
  its foreground command. The script then stops before starting its session,
  which leaves that space with no shell until reboot. No current startup script
  runs a foreground command. Revisit if one gains one, for example by not
  arming scripts or by continuing past an interrupted command.

[Foreground interruption](userland/foreground-interruption.md) records the
design and validation. Revisit with cooperative interrupts or job control, or if pasting into
hung remote commands matters. The remote case would need an out-of-band
interrupt from the server. Native cancellation need not require general POSIX
signals.

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
is bounded at five seconds. A full guest input queue likewise holds back a
later Ctrl+C (see [process termination and Ctrl-C](#process-termination-and-ctrl-c)). Host SIGINT/SIGTERM forces disconnect even under
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
when real consumers make that cost material. All FILE output is unbuffered;
there are no pending writes to flush yet.

## Console line input

Consoles are never read ahead: their input is shared with the parent shell, and
ISO C treats them as interactive. Line input from console-backed stdin through
`fgetc`, `fgets` or `getline` therefore remains one native read per byte,
including a large paste into such a program. File and pipe input is fetched in
BUFSIZ blocks by [input read-ahead](userland/stdio.md#input-read-ahead).
Revisit only with a terminal input design that can return unread console bytes
to their next owner; do not work around it in individual ports.

## Input read-ahead limits

Buffered file bytes are a private copy. If another descriptor or process
writes the same file, a stream can return stale bytes until `fseek`, `rewind`
or input `fflush` refetches them. `setvbuf`, `setbuf` and `ungetc` remain
absent, so programs cannot size or disable the buffer or push back input.
Revisit together with output buffering in a later stdio completeness task. See
[input read-ahead](userland/stdio.md#input-read-ahead).

## Duplicated port output lists

`scripts/ports.mk` repeats staged output paths already declared in each recipe's
`metadata.lua`, including the sbase executables and notices. Adding an output
requires coordinated edits; drift can leave Make unaware of a missing staged
file. Revisit the build integration to derive output dependencies from one
authoritative list without growing a new build framework. Until then, review
both lists when updating a recipe's outputs.

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
deadlines and profiling still pay the clock cost. The current source requires
a memory-mapped HPET; there is no source registry or fallback. On 2026-10-03
the owner chose
[software-extended HPET first](wip/thinkpad-kvm-tsc.md#accepted-direction-and-implementation-handoff).
The implementation preserves direct 64-bit reads and extends 32-bit counters
with a shared CAS accumulator. Each advancing extension read publishes to one
cache line and may retry under contention. BSP maintenance is configurable in
timer deliveries, default 120 (nominally one second), with explicit early-boot
sampling. The [matched host-KVM observations](wip/thinkpad-kvm-tsc.md#local-implementation-results)
show lower clock-call cost for forced low-32-bit extension, with shared-state
cost included, but do not establish native performance. The direct profiled
allocation median was about 2.6% higher, mostly in BSP queue time; its cause
was not isolated. The owner has reached native userspace on all 12 ThinkPad
CPUs, as [recorded from a screen photo](wip/thinkpad-kvm-tsc.md#native-bring-up-continuation).
The owner recorded the 32-bit/software-extended path log; the native multi-wrap
clock check remains pending. Missing keyboard input is a separate bring-up
blocker; the diagnostic follow-up identified an absent scan-set query ID.

The accepted support requirement is strictly less than one advancing-counter
wrap between incorporated samples, including individual boot operations, long
interrupt-disabled execution, firmware stalls and debugger/VM pauses. A violating
gap requires reboot: the low word cannot identify or reconstruct missing wraps.
Nominal interval validation cannot enforce the actual gap bound, and suspend,
resume and migration remain unqualified. Revisit this limitation with an
independent source or an explicitly scoped stronger progress guarantee. The
accepted future direction is TSC with extended-HPET fallback, with frequency
discovery and cross-CPU qualification, preserving the clock protocol. Revisit performance after native
bring-up when the TSC stage is assigned; it is not part of the first HPET task.
The VirtIO RTC driver remains deferred.

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
and mappings even after confirmed reset. Each prepared device reserves up to
512 KiB of payload storage plus control buffers and rings. Retained memory scales
with the inventory; there is no reconnect or reclamation. Revisit alongside
shared-mapping TLB invalidation, DMA ownership and a defined device teardown lifecycle. Timeout alone
cannot release storage still accessible to the device.

Physical hardware and power-loss persistence have no coverage in this milestone.
Normal QEMU restart/readback cannot establish either. Error, reset-failure and
malformed-completion behavior require separate validation if fault injection is
later authorized. Revisit durability evidence before promising filesystem
recovery or support for production data.

## GPT snapshot and profile limits

[GPT discovery](devices/gpt.md) publishes one snapshot per device. Exclusive
installer raw claims exclude mounts and refresh the snapshot on release, but
there is no hotplug, external-mutation detection or automatic repair. Borrowed
snapshot views last only until the next scheduling point. Health does not track
later transport failure or changes during raw writes. Revisit generation
tracking and broader replacement lifetimes when a concrete consumer needs them;
external host writers remain unsupported.

The supported profile is GPT 1.0 on 512-byte or 4 KiB blocks, at most 256 entries
and 64 KiB per array. Unsupported revisions, larger layouts, reserved attributes
and legacy/hybrid MBRs expose no map. One valid copy supplies a read-only degraded
map only when the other is absent or invalid; I/O errors, timeouts and unsupported
metadata prevent fallback. These conservative bounds can exclude otherwise usable
media. Revisit only for a concrete consumer with explicit resource limits and
recovery policy; no automatic repair is available.

## Installer authority and retained pools

The [installer disk service](devices/installer-authority.md) supplies explicit raw
claims and immutable boot sources, but `installer.pxe` is not yet packaged. It
does not implement target consent, formatting or installation. The trusted
installer must establish consent; the kernel does not interpret `SAFE_TO_WIPE`.
Revisit those remaining operations in the assigned installer task.

Any retained npfs pool blocks an exclusive raw-write claim on its device, even
when read-only and after all handles close. Opening a volume to inspect a marker
therefore cannot be followed by raw formatting of that device in the same boot.
This is the owner-accepted current direction. There is no pool teardown or
installer bypass. Task 4.3 will inspect each volume's root marker through raw
reads and the format library, overlaying a committed journal in memory without
writing before consent. Rejected consent leaves the disk untouched; accepted
targets will be wiped rather than receive a persisted replay. Pool retirement
is a prerequisite of the later live-install flow, which can inspect through
normal mounts before installing in the same boot. It needs an explicit
pool-retirement and ownership contract. Read-only raw handles acquire no claim
and promise no snapshot against raw writes.

Physical-media, power-loss and uncertain-failure evidence remains separate from
ordinary emulated operation; revisit reliability claims only with corresponding
validation.

## USB image updates and firmware qualification

The [raw USB image builder](development/usb-image.md) creates fresh images and
replaces the sample pool and its identities on every rebuild. There is no
preservation of installed data, rollback or atomic physical update protocol.
The manual copy procedure relocates backup GPT on larger media but does not
expand the pool. Revisit image preparation and update ownership before the
persistent-installation phase stores user data.

Emulated USB boot has reached the shell, but an intermittent
[pre-kernel Limine file-open failure](development/qemu.md#usb-firmware-file-open-failure-before-kernel-entry)
remains unexplained. Successful unchanged-image retries do not qualify firmware
boot reliability or physical-controller behavior. Revisit with firmware/USB I/O
diagnosis and the separately assigned hardware stage; native reads/writes and
physical media have no validation claim from Phase A.

## RTL8111 initial-state support

The [RTL8111 preparation path](devices/rtl8111-hardware.md#controller-preparation)
rejects D3hot wake without `NoSoftRst`: that transition can discard assigned BARs,
and the temporary identity probe saves only Command/PMCSR. Such a controller
remains unavailable while boot continues. Revisit PCI configuration restoration
if owner-run native qualification encounters this state.

## RTL8111 runtime limits

The [RTL8111 I/O path](devices/rtl8111-hardware.md#ethernet-io) supports XID `541`
only. Each prepared controller retains two contiguous 68 KiB ring allocations
and one page for hardware tally snapshots; unselected hardware stays inactive.
Runtime failure attempts reset and disables
DMA/delivery but retains claims, buffers and shared mappings until reboot. The
first binding has no fallback or controller switching. Revisit reclamation with
a concrete teardown and SMP invalidation contract.

[Qualification](development/rtl8111-qualification.md) covers sustained VFIO
traffic and an owner-run native cold/PXE boot with the dock attached, without
imported firmware. Native unplug, device-owned TX at carrier loss, every PHY
speed and gigabit line rate remain unqualified. The native wired result and VFIO
Wi-Fi results have different environments and cannot isolate a throughput
bottleneck. Revisit those limits with a concrete reproduction or a separately
assigned measurement task. No jumbo-frame reassembly, offloads, firmware
interpreter or automatic restart is implemented; a measured firmware requirement
would need a focused import with provenance and redistribution terms.

Hardware tallies are accessible only through the internal GDB capture helper.
Revisit that diagnostic interface when a network status command is assigned;
there is no public statistics ABI or periodic tally polling today.

## Virtio-net runtime resource retention

The external interface's first unique configuration binding lasts until reboot.
Address clearing preserves it; controller switching and fallback after failure
are unsupported. Unselected prepared controllers retain their boot resources
with DMA/delivery off. Revisit runtime switching with a concrete teardown,
packet draining and SMP invalidation contract rather than adding implicit fallback.

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
using [hardware-backed randomness](devices/randomness.md). Revisit the generic allocator's
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
requested newline. Stdio read-ahead cannot serve this exact path, so long lines
still cost one backend call per byte. Byte mode retains bounded bulk reads. Revisit buffering or a native
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

Entropy comes from the [hardware-backed random capability](devices/randomness.md):
VirtIO when present, otherwise checked CPU RDSEED/RDRAND. The selected hardware
is trusted directly, with no kernel generator or source mixing. Missing or failed
entropy leaves HTTPS unpublished and disables new kernel TCP connections for that
boot. The owner must confirm this startup path on native hardware; successful
QEMU CPU reads are guest evidence. UTC remains subject to the
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

[Native mounts](devices/native-readonly-filesystem.md) select one configured GPT
disk identity, explicit partition and volume. Capability grants govern access;
the filesystem has no principal or persistent permissions. Observation exposes
identity and shared-pool capacity, but not usage, quotas or charged bytes.
Concurrent external image modification, hotplug and unmount are unsupported.
Mounted pool state survives final handle closure until reboot, so dirty contents
and errors outlive the process's cleanup charge. Revisit teardown with a concrete
need and explicit synchronization, shared-mapping and dirty-data ownership.

The worker admits 32 jobs with a cooperative 30-second deadline. Adapter storage
has a 1 MiB/1,024-wrapper limit; pool metadata, the free-inode list and caches are
separate. Each pool can retain 4 MiB of cached file payload plus entry metadata,
and a writable pool reserves up to 520 KiB for 128 journal images and encoding
buffers. These are implementation bounds, not format limits or aggregate memory
admission. The retained allocation bitmap needs one bit per pool block, rounded
to 4 KiB: 32 KiB for a 1 GiB pool, about 8 MiB for 256 GiB. Mount reads and
validates it after replay; both mount modes retain it and pressure cannot evict it.
Allocation and mapping checks use this memory plus current journal overlays.
Mount scans the selected volume's inode file and builds its free list;
reclaimed slots retain their inode allocation as a list node until reuse.
Large inode files can therefore exhaust memory or take too long to mount. Revisit
compact free-slot storage or an explicit pool budget if representative workloads
reach those limits; preserve NO_MEMORY/LIMIT versus corrupt-image reporting.

Memory pressure wakes the filesystem worker after allocator work. It can flush
dirty data and return whole clean cache chunks to VM; failed writeback preserves
dirty chunks. The allocating call is not transparently retried. Kernel heap
backing remains mapped under the existing heap policy. Revisit reclaim granularity
and admission only with measured pressure workloads and BSP ownership intact.

All native reads and writes traverse the BSP worker, including cache hits.
Metadata lookup remains linear and is not generally cached. The
data, journal payload and checkpoint paths still wait for single-block transfers.
Revisit contiguous transfer batching with a concrete latency budget and measured
consumer workload. The
[task-3 measurements](development/experiments/native-filesystem-task3/README.md)
distinguish this scheduling/I/O cost from RAM file calls. Revisit only when an
actual consumer needs lower latency. Executable capture permits one image of up
to 16 MiB per caller outside wrapper/cache limits; concurrent captures have no
aggregate staging budget and can fail allocation below that per-image limit.
The userspace root selection remains bounded to 16 entries within existing
64 KiB startup/capture storage, independently of format volume/name limits.

Uncertain backing failure and allocation pressure retain source-review coverage.
Committed-journal recovery has been exercised at runtime; see the
[adapter qualification](devices/filesystem-native-adapter.md#task-3-validation).
That does not qualify arbitrary interrupted cleanup or storage failure. The
[populated-pool review](development/experiments/native-filesystem-task3/populated-pool-review.md)
reproduced the old allocation timeout and validated the retained-bitmap correction.
No fault injection or physical-media validation
is claimed by the native writer's ordinary QEMU workflow.

## Native filesystem design limits

The [native format decisions](wip/native-filesystem-format.md#decision-status)
now have [implemented codecs and host tools](../fs/docs/npfs-host-tools.md).
Caelum owns the mounted inode/cache/writer state. Accepted limits include 64 volume slots, roughly 513 GiB per-file block-pointer capacity, and linear directory lookup.
Revisit only when a concrete workload exceeds those bounds or lookup becomes costly;
reserved bytes and feature flags provide extension points. Volumes can exhaust the
shared pool; quotas and starvation policy remain deferred until a concrete need.

One transaction commits/checkpoints at a time. Cleanup delays space reuse, and
large shrinking truncates stall further writes/resizes of the affected inode.
The accepted sync completion point is durable COMMITTED; checkpointing continues
in the background before the next commit. The writer's free-inode list avoids
per-create scans but adds mount-time work and memory usage.
The [task-3 record](development/experiments/native-filesystem-task3/README.md)
measures latency and virtual-device bytes from the initial writer.
128 MiB is the initial 256 GB target setting, not a measured optimum or universal
minimum. Journal capacity is selected per pool and has no v1 resize operation.

Creation/modification times use signed 64-bit Unix nanoseconds, clamped on write.
Dates outside that range lose precision at the endpoints, and wall-clock values
can repeat or move backwards; timestamps are not unique change counters. A missing
clock leaves the affected timestamp explicitly unknown without failing mutation.
Revisit only if a consumer needs wider dates or a stronger change-detection contract.

Unknown required features refuse opening; unknown read-only-compatible features
refuse writes, including recovery writes; unknown compatible features are ignored.
Conflicting valid headers require repair. These are accepted compatibility rules.
The owner accepted refusal of committed journals for read-only opening and no
home-metadata checksums in v1. Writable fsck replays the journal; checksums cannot
detect every later metadata corruption once it is cleared. Revisit metadata
checksums when integrity needs justify a feature-gated layout change. Committed
replay has been exercised at runtime; see the
[adapter qualification](devices/filesystem-native-adapter.md#task-3-validation).
Interrupted cleanup and arbitrary failure points remain source-reviewed.
Broader qualification waits until explicitly assigned. Host tools require unchanged
standalone regular images and cooperating locks, stage replay payloads in memory,
and do not repair arbitrary damage or reclaim cleanup lists. Large images/volumes
can exhaust host checker memory. Physical-media wear remains unmeasured; native operation latency and QEMU target
bytes are measured in a nested VM, and do not qualify SSD endurance.

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
native-URI JSON/JSONC configs, one-shot text/JSON output and ten selected modules.
Automatic config discovery, config/cache writes, dynamic refresh, image logos,
Lua execution and executable/network helpers are excluded. Existing upstream
configs may encounter unsupported diagnostics or upstream fallback behavior.
The port does not impose a new strict option validator. Revisit individual
features only when a concrete native use case and authority contract exist.
The normal image packages this port. Narrow terminals retain upstream layout,
so logo/data lines may wrap; use `--logo none` or shorter formats. Revisit layout
only with a concrete display requirement, without changing upstream formatting
as part of routine platform integration.

Disk reports only explicitly selected native roots with observation authority.
Shared-pool capacity is separately labeled; usage, volume totals, quotas,
guarantees and percentages remain unavailable until the filesystem observation
contract establishes their meaning and evidence. Repeated bindings/volumes can
share a pool ID and capacity, so consumers must not sum those rows. Revisit richer
Disk values with a concrete verified core accounting interface, not inferred
used/free arithmetic. Folder/glob filters remain unsupported on Pyxis; their
Unix path grammar is not a capability-binding selector. Add selection only with
a concrete caller need and an explicit native binding contract.

Uptime exposes duration since HPET initialization. JSON bootTime is null;
calendar boot-time/age placeholders are unset and render empty, so configurations
that need those observations require editing. Revisit this only if Pyxis gains an authoritative boot epoch
and agrees its meaning across wall-clock changes; do not infer one by subtracting
monotonic duration from the current wall clock.

## xHCI hardware profile and runtime retention

Native xHCI initialization is [disabled by default](devices/usb-xhci.md) while
ThinkPad qualification is paused. Firmware USB boot remains available, but
Caelum USB enumeration is unavailable. Revisit the default after the physical hardware profile has been qualified.

The [initial controller](devices/usb-xhci.md) is qualified only against QEMU's
PCI xHCI profile, now with multiple independently discovered controllers. It requires firmware memory decoding enabled for a
page-aligned BAR0 prefix, interpreted extended capabilities within that 4 KiB
prefix, 64-bit DMA, 4 KiB pages and MSI-X. Other profiles, SuperSpeed hubs,
power management and insertion after the startup snapshot are unsupported.
QEMU advertises zero scratchpads and 32-byte contexts. An
[owner-reported ThinkPad run](targets/t14-gen1-amd/usb-bringup.md) observed
nonzero scratchpads and 64-byte contexts; nondefault PSI mappings and BIOS
ownership handoff remain unmeasured paths. This first native snapshot does not
establish broad controller qualification. Enumeration publishes root devices and bounded USB 2 hub descendants;
SuperSpeed hubs, LUN/media support and USB block access remain pending.

USB 2 root-port reset has no explicit connect-debounce interval. The startup snapshot
waits 20 ms only after the driver powers a port; it has no separate link-settling
wait when power was already on or the controller lacks port power control. It can
miss a physical USB 3 link still initializing after controller reset, leaving that
device unreserved until reboot. Revisit debounce and bounded startup settling with
physical firmware/device evidence before claiming hardware qualification. A vendor
reset-delay quirk also needs evidence from the selected controller.

Legacy handoff timeout leaves the OS-owned request asserted. Firmware may release
BIOS ownership asynchronously after preparation has failed; Pyxis does not retry
or reclaim the controller during that boot. Settle the timeout rollback policy
with firmware ownership evidence before changing the semaphore behavior. Revisit
topology and the bootstrap profile with the ThinkPad before expanding support.
BAR sizing saves/restores the assignment, but there is no explicit comparison
with the original bootstrap physical base. Revisit that consistency check when
extending PCI mapping/profile validation, keeping one authority for mapping identity.

Each controller worker admits its own commands serially and checks notifications/health at
a ten-millisecond interval even when idle, scheduling up to 100 polling
opportunities per second. The shared MSI-X vector notifies every active controller per delivery, so unrelated
workers may wake. Actual CPU wakeups and laptop power cost are unmeasured.
Rings and polling/deadline budgets are initial choices, not machine/image
requirements. Revisit event-driven waiting and health-poll costs when descriptor
transfers and actual USB storage reads provide a workload; controller startup is
not a storage benchmark.
Runtime stop retains claims, mappings, slot/command records and DMA backing until
reboot, even after confirmed halt. This follows current shared-VM ownership and
prevents reuse while device ownership is uncertain. Runtime reclamation belongs
with the VM/device lifetime work, not a local allocator-lock workaround.

## USB descriptor bounds and per-port preparation

[Enumeration](devices/usb-enumeration.md) inspects every advertised configuration
using an initial 4 KiB descriptor/control budget. A larger configuration makes
inventory incomplete. The initial arena retains up to 512 validated interface
records per controller; overflow is partial. Unknown/vendor classes are valid
unbound observations. Revisit these bounds with concrete descriptor/topology requirements. Storage
selection across controllers must be settled separately before class/media work.

All advertised ports receive input/output contexts and an EP0 ring/control buffer
before AP startup. With the current 4 KiB buffer and 4 KiB allocations, this adds
four pages/range records per port even when empty. The unused two bulk-ring pages
per port, BOT matcher and endpoint setup were removed from the inventory slice.
This fits QEMU's eight-port profile but consumes the shared VM range budget and
can fail preparation on larger controllers. Revisit boot inventory/resource
preparation with physical port-count evidence; runtime allocation/reclamation
requires the VM ownership work rather than allocator locks. Reintroduce class
transfers with a concrete consumer and an explicit pre-AP resource policy, rather
than restoring unused reservations for future work.

Hub discovery uses a pre-AP descendant pool, initially 32 per controller,
capped by advertised Slot capacity after reserving possible roots. One owned DMA
arena avoids multiplying VM range records but retains all reserved backing even
when no hub is attached; the current 32-entry/4 KiB profile adds 512 KiB per
controller. Pool allocation failure can fail that controller's preparation.
Revisit the budget and root reservation policy with actual topology/resource
requirements, without runtime mapping or allocation outside the VM contract.
The shared startup deadline can expire on large trees; exhausted branches are
partial. USB 3 hub traversal and low-speed hardware paths remain unqualified.
The first owner-reported ThinkPad snapshot exercised full-speed descendants
behind high-speed hubs; recovery and broader TT qualification remain pending.
QEMU's built-in hub exercises full-speed descendants only. SuperSpeedPlus root
recognition uses discovered protocol metadata, but QEMU's current devices do not
exercise that link profile; native address/descriptor qualification is pending.
USB 3 hub traversal is source/spec-reviewed, with QEMU USB 2 regression coverage;
no USB 3 hub execution coverage is claimed. The ThinkPad recheck is deferred
while the owner works on its NIC. Revisit with the next available native run.
Only standard symmetric Gen1/Gen2 one/two-lane downstream links are attached.
Absent/ambiguous controller profiles remain partial; revisit with actual profile
evidence rather than picking a speed ID. Categorical inventory
omits directional rates and lane counts; SSP isochronous byte budgets remain
uninterpreted until actual non-control endpoint scheduling needs them.
Hub descendants are not monitored after publication; idle downstream removal
retains their slots/backing until reboot. Revisit this with separately scoped
hotplug/lifetime work. Root removal still retires the retained subtree, and
active request errors quarantine the controller.

The first implementation bounds each device to one active control request. Early
errors, deadlines or removal during active work stop the whole controller and
retain unresolved DMA until reboot. There is no endpoint-local recovery yet.
The inventory client configures supported hubs but leaves other classes unbound;
short packets, active abandonment, early errors, ring wrap and nonzero alternate
selection follow reviewed source/spec rules but have no synthetic validation.
Revisit with an actual class-transfer workload in BOT/SCSI work, keeping hardware
ownership explicit. Physical USB qualification remains separate.

USB 3 inspection omits SET_SEL and SET_ISOCH_DELAY, which the specification
requires during full enumeration. EP0 routing/descriptor inspection does not
consume their power-exit or isochronous scheduling values, but complete inventory
is not full USB 3 conformance. Revisit with actual path-latency accounting before
adding power management or non-control scheduling; do not send successful zero
placeholders. USB 3 boot traversal retains the existing conservative USB 2
stability/recovery delays and adds no explicit warm-reset recovery retry.

## CPU entropy without a kernel generator

The native entropy path trusts RDSEED/RDRAND directly, with bounded instruction
retries and checks for zero, all-ones and repeated words. VirtIO remains preferred
when present; neither source provides independence from the hardware/hypervisor.
The CPU boot self-test and runtime checks reject obvious failures, not arbitrary
bias, malicious hardware or firmware defects. Availability depends on the
instruction supply; carry-clear exhaustion fails the current read. A health
failure disables its instruction until reboot and discards/refills the whole
request from any healthy survivor. An ambiguous cross-instruction repeat disables
both. The source is unavailable once no healthy instruction remains. See
[randomness](devices/randomness.md).

The accepted follow-up is a kernel ChaCha20 generator seeded from these sources.
Revisit source mixing, reseeding and generator ownership in that task; do not add
predictable fallback bytes or treat the current checks as entropy certification.

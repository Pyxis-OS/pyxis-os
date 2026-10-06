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
requests. Since SMP task 7a, profiling splits service from the whole syscall;
earlier records also split publication, BSP queue time and resumption. Report the accelerator, CPU count, live set and host/nested-VM context;
the instrumentation itself reads HPET and perturbs timings.

Standalone kernel `kmalloc`/`kfree` throughput and deeper PMM/VM timing remain
unmeasured. Pool growth counters describe backing acquired during a measurement
window, not total retained memory or a fragmentation metric. Private memory
now runs in the caller's syscall ([memory](kernel/memory.md#execution)). The
common executor admits the remaining migrated services,
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

Kernel allocation and page-table mutation remain owned by the BSP, except private
memory operations, which run in the caller's syscall since SMP task 7a. Tasks
submit other specific requests and wait for BSP service. The
[common executor](../kernel/service/request.c) separates subsystem operations
from scheduling through a closed service catalog and one FIFO. Each user task
owns one reusable request allocation and a separate caller-only profiling
allocation; kernel workers own neither. Subsystems own request capture, service
helpers and profile controls. This keeps allocator and VM ownership explicit,
but long non-preemptible service operations still delay other requests and BSP
work.

The handoff ordering is part of correctness, not incidental queue plumbing.
Display requests are published only after the requester has left its task stack
and private address space; resumption reloads CR3 before returning to
the task stack. A wake arriving before a task finishes parking records a
notification without making the still-running context runnable elsewhere.
Changes to service placement or synchronization must preserve these guarantees
or explicitly replace them with an equally defined ownership and translation
invalidation contract.

Reconsider BSP-only service when its latency becomes material or before allowing
concurrent use and mutation of one private address space. The
[implemented BSP request contract](kernel/bsp-service-requests.md) has separated operation
ownership, submission/completion and subsystem service from scheduling while
retaining BSP-only allocation and the inactive-root handoff. The
[runtime SMP milestone](kernel/smp.md), completed on 2026-10-06, introduced
independent spaces, single-task migration and local private-memory operations,
with allocator synchronization and explicit mapping lifetime rules. The heap,
physical allocator and scratch mappings are safe on any CPU, and private memory
operations use them locally; other allocating services still run on the BSP.
Serial services and deferred destruction remain BSP-owned. Worker relocation and
shared kernel mapping reuse need their own handoff/invalidation contracts
([follow-ups](wip/scheduling-and-threads.md#serial-services-off-the-bsp)). Eager
task-lifetime storage and long non-preemptible operations remain explicit costs;
measure them in matched before/after workloads.

## PMM first-fit search under its lock

`pmm_alloc()` searches first fit under the PMM lock. Since the SMP task-7 PMM
follow-up, it skips fully allocated 64-bit bitmap words and starts at a hint
below which every frame is unavailable. That ended the serialization measured in
7a: lock wait fell to about 150 cycles per call with two page clients
([record](development/experiments/smp-task7-pmm/README.md#pmm-lock)).

Search time can still grow with fragmentation: free frames scattered through
mostly allocated words, or a long run requested among short free stretches. The
lock is still taken once per frame by `vm_back()` and heap growth, which allocate
one frame at a time. Revisit if a workload shows PMM lock waiting again; batching
frames per call would be the next step.

## Scratch-slot false sharing

Each CPU's two scratch slots are adjacent PTEs, so the slots of CPUs 0–3, 4–7
and so on share one 64-byte cache line of the scratch page table. The
`scratch_busy` flags of every CPU also share lines. Each page that private memory
or heap growth maps uses the slots about a dozen times, for zeroing and for each
page-table level read. CPUs doing that at the same time bounce those lines.

In the nested VM, two concurrent page clients each took about 1.9 times as long
as one alone. Giving each CPU's PTEs and flags their own line, in a throwaway
build, cut that to about 1.4 times
([record](development/experiments/smp-task7-pmm/README.md#remaining-concurrency-cost-scratch-slot-false-sharing)).

There are two options:

- **Spread the slots, one line per CPU.** Covering all 256 xAPIC IDs needs an
  8 MiB scratch window instead of 2 MiB, which moves the APIC, I/O APIC and
  HPET mappings.
- **Walk the active address space through the recursive mapping.** This removes
  most slot use, and single-CPU cost, entirely.

Natively the cost is small. On the ThinkPad, two page clients each took
1.15–1.25 times one alone, and eight up to 1.5 times ([task-8
record](development/experiments/smp-task8/README.md#native-thinkpad-check-owner-run)).
The owner prefers the recursive walk (2026-10-06); with the native numbers, it
is a later optimization rather than an SMP prerequisite. Frame zeroing would
still use a slot. It could instead go through the frame's final mapping before
anyone can see it, which would change the "zeroed before mapping" rule, or keep
one padded zeroing slot per CPU. Copy-on-write zero pages were considered and
set aside. They only move the zeroing to the first write, need allocating page
faults, and defer NO_MEMORY from ALLOCATE to an ordinary store.

## Never-reused kernel heap arena

Kernel heap pools come from a 256 GiB arena whose addresses are never reused, so
publishing a pool needs no TLB shootdown. Pools are never removed. The arena
therefore bounds every pool ever added plus every retired page for the whole
boot. Running out of it makes all later heap growth fail, even after memory is
freed. RAM FILE backing is heap storage that doubles as a file grows, so it is
the likeliest consumer.

Growth refuses a pool that does not fit in the free frames before mapping
anything, so a request larger than free memory retires nothing. A growth still
retires the pages it mapped when another CPU takes frames while it maps. That
needs concurrent allocation, which starts in SMP task 7. The check reads a
snapshot of the free count, so a growth can still briefly take most free frames
when the pool only just fits.

Growth also zeroes and maps its whole pool with interrupts disabled while holding
the growth lock. A large RAM-file doubling occupies its CPU for that time, and
other CPUs that need growth wait. The BSP already behaved this way before SMP
task 6.

Revisit in SMP task 7, when frames are allocated on several CPUs, and whenever
`heap_stats` shows retired bytes or arena use approaching its size, or growth
latency becomes material. Moving RAM-file backing out of the heap is the first
option.

## BSP userspace and kernel workers

Since SMP task 7b, user tasks run on the BSP alongside the kernel workers:
presentation, the BSP request executor, network, native filesystem and USB.

- **Placement:** ties go to the APs first, and the BSP pulls only while none of
  its workers is runnable.
- **Preemption:** a woken worker preempts a BSP user task at the next interrupt.
- **No priorities:** user tasks and workers otherwise share the BSP's queue on
  equal terms.
- **Interrupt-masked syscalls:** user syscalls run with interrupts masked, so a
  long BSP user syscall delays workers and device interrupts until it returns.
  For example, a large private allocation must zero its pages first.

With all four CPUs loaded in the nested VM, ttcp and RAM-file writes stayed
within their spread ([7b record](development/experiments/smp-task7b/README.md)).
Display smoothness and input latency under BSP load were not measured.

Revisit if the native check in SMP task 8 or interactive use shows worker or
presentation latency under load. Options include excluding the BSP again by
policy, worker priority, or preemptible long syscalls.

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

Editing assumes exclusive output use of the space's terminal.

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
open/read/write/close for cksum and restricted tee, and `lseek` for files.
Public O_RDWR, fdopen/fileno and duplication remain absent even though fopen
supports update modes internally. Consumers requiring those interfaces need
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
buffering controls or wide I/O. Pushback is one byte per FILE, and scanning
covers narrow conversions only; fixed-width inttypes input (SCN) macros are absent. Revisit these gaps
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

## Narrow libc file metadata

Libc offers `mkdir`, `opendir`/`readdir`/`closedir` and `stat`/`fstat` for
ports such as [Links](userland/links.md). Native objects report only a kind and,
for files, a size, so `struct stat` has only `st_mode` file-type bits and
`st_size`. There are no permission, owner, link-count, identity or time fields,
and code that reads one fails to compile instead of seeing invented values.

- **Sizing opens the file.** `stat` opens a file with READ, or WRITE if READ is
  denied, so a file with neither right cannot be sized. On a provider URI it
  performs the request, so a port that calls `stat` before `fopen` fetches
  twice. Links sends provider URIs straight to `fopen`.
- **Symlinks.** Lookup never follows a symlink, so `stat` of a symlink entry
  fails; `readdir` reports it as `DT_LNK`. `lstat` and `readlink` are absent.
- **Listings.** `readdir` returns no `.` or `..` entries. A detected
  concurrent change ends the listing with EAGAIN rather than restarting it.
- **Native utilities.** The first ls and mkdir still use libpyxis helpers.

Revisit when native objects gain timestamps or other metadata, when a port
needs `lstat`, `readlink` or `access`, or when a port calls `stat` on provider
URIs. Review of the Links port proposed failing there with ENODEV, as
`opendir` does, without issuing the request. Add fields only for values
the native layer reports.

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

## Quake port limits

The [Quake port](userland/quake.md) renders at quakegeneric's fixed 320x240.
A resolution switcher is wanted: it needs a video driver with a mode list behind
Quake's Video Modes menu, reallocation of the frame, z-buffer and surface cache,
and a check of the renderer's size limits (upstream reverted 640x480 as
unstable). Sound, networking, CD audio and joysticks are absent; adding sound
needs a native audio device first.

Saves are Quake's trusted text format, written in place without a temporary
file, and are lost on reboot with the rest of `home://`. Shareware and retail
data share `home://quake/id1`, so their configuration and saves mix.
QuakeC strings outside the hunk use a 512-entry engine-string table; overflowing
it stops the game with an error. Revisit these when persistent storage or a
second data set makes them matter.

## vi port limits

[BusyBox vi](userland/vi.md) displays ASCII only and searches literally, because
Pyxis has no Unicode-capable renderer or `regex.h`. Saves keep upstream's
in-place write followed by `ftruncate`, so a short write or crash can leave a
truncated or mixed file. Revisit with atomic replacement or a durable-save
policy alongside the [native filesystem](wip/native-filesystem.md) work.
`:!` and shell filters need a native launch adapter, and the read-only marker
probes WRITE authority because truthful file metadata does not exist yet. The
recipe's libbb adapter covers only vi's helpers; BusyBox less will extend it.
Input EOF exits and loses unsaved edits, as upstream does; Kilo handles that
case explicitly.

## less pager limits

The [BusyBox pager](userland/less.md) retains read display lines for backward
paging, with the selected line-count limit and process-memory bound. It measures
screen dimensions once, displays ASCII, and searches literal case-sensitive text
without highlights. There are no raw escapes, regex, shell commands or live
refresh. A content read during refill/search blocks, so a stalled producer can
delay keys; cached navigation performs no extra read. Revisit native readiness
through a proven libc extension when an actual live-stream consumer needs it,
and screen resizing when terminal size-change notification is designed.

## tar archive limits

The [BusyBox ustar subset](userland/tar.md) captures the whole input archive or
all creation file contents in process memory before writing. Large archives can
fail allocation before mutation; recursive creation also consumes stack by tree
depth. Creation names are limited to 99 bytes plus directory slash. Compression,
GNU/PAX extensions, links, `-C` and stdin/stdout archives are absent. Revisit these
limits with a concrete larger documentation/archive consumer.

Validation prevents unsafe archives from writing any members, and a read-only
root fails on its first required mutation. Extraction and output writes are not
transactional: later I/O/authority failures can leave earlier entries or partial
files. Directory enumeration is live. Revisit streaming or archive-wide rollback
only with an explicit snapshot/transaction design; ordinary close is not sync.

## Links port limits

[Links](userland/links.md) loads every page synchronously, so a slow network
fetch freezes the interface until the HTTP provider's own 30-second budget
ends. In review under nested KVM, a server that accepted the connection and
never answered left a blank screen for 32 s before "Operation timed out". A Ctrl+C pressed during the wait
is held and quits Links only after the open returns. Revisit with a native way
to wait on a provider open alongside console input.

- **No saved configuration.** Options, bookmarks and history are not saved.
  Revisit once `home://` persists and libc has exclusive creation.
- **No downloads.** Downloads to disk fail, because they need exclusive
  creation too.
- **Fixed screen size.** It is read once, without resize notification.
- **Sockets compiled in.** Links' socket and DNS code is compiled but
  unreachable. The port's socket functions fail, so `ftp://` and `finger://`
  report "Host not found".
- **Remote pages can link to local roots.** A page fetched over HTTP(S) can
  link to `host://`, `home://` or `system://`, and following the link opens the
  local object. Without scripting, a page cannot read or send what it opens, so
  this matches a local link the user chooses to follow. Desktop browsers refuse
  such navigation. Revisit before Links gains POST, cookies or providers that
  act on requests.

The HTTP-side limits are recorded under [HTTP redirects](#http-redirects) and
[response metadata through fopen](#response-metadata-through-fopen).

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
claims and immutable boot sources. The [native installer](userland/installer.md)
establishes target consent; the kernel does not interpret `SAFE_TO_WIPE`.

Any retained npfs pool blocks an exclusive raw-write claim on its device, even
when read-only and after all handles close. Opening a volume to inspect a marker
therefore cannot be followed by raw formatting of that device in the same boot.
This is the owner-accepted current direction. There is no pool teardown or
installer bypass. The installer inspects each volume's root marker through raw
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

## Installer inspection and recovery limits

Consent validates GPT, npfs headers and the complete committed journal, then
checks allocation/mappings encountered on root-marker paths. It does not prove
whole-filesystem ownership or inspect unrelated files. A large committed log
requires reading every record before eligibility is known; inspection retains
descriptors rather than the whole log. Revisit with measured large-log workloads
or a concrete need for whole-filesystem qualification.

[System-update inspection](userland/system-updates.md) validates allocation
bitmaps, live catalog records and the system inode file/root/cleanup chain
against writable-mount admission. It does not run a whole-pool ownership check
or read ordinary file contents. Its bounded
FAT32 reader follows required paths on the fixed 512 MiB ESP, with at most
64 KiB of configuration and 64 bytes of revision text. It checks traversed FAT
copies/chains, not every unrelated file. Healthy GPT and a compatible empty-journal
pool permit rebuilding missing/damaged ESP contents; readable foreign/invalid
disk bindings and raw I/O/allocation failures still refuse. The optional revision
record cannot override configuration binding checks. Revisit these bounds when
supporting a new installed layout or general FAT service; whole-pool checking remains fsck's
role. A selected committed journal is refused without loading or replaying it.

Installation writes fresh metadata and boot files; it does not securely erase
free space. Update replaces the whole ESP, discarding unrelated ESP files, with
no fallback entry. An interrupted ESP replacement can be rebuilt by booting live
media again and choosing Update while the GPT and pool remain eligible; see the
[QEMU recovery record](development/experiments/system-updates-task2/README.md).
Revisit the absence of fallback/atomic replacement with a separately agreed
in-system update design. An interrupted installation can leave a partial disk,
without rollback or automatic repair. Ordinary QEMU success/refusal cases and
host structural checks do not qualify power loss, uncertain I/O, physical USB
media, NVMe or physical firmware.
Revisit those limits with the assigned end-to-end hardware task and separately
authorized recovery validation.

The owner deferred native ThinkPad installation on 2026-10-04 while completing
[task-5 QEMU
qualification](development/experiments/native-filesystem-task5/README.md).
VirtIO and per-device qualified USB now support writable native mounts. USB
write/cache synchronization is implemented for C.1, and C.3 enables the trusted
installer's bounded raw authority for retained USB candidates. On 2026-10-05,
the first native [USB installation](devices/usb-installation.md) wrote one
expendable stick from PXE live media. Writable mounting, persistence across a
synced power-off and one Update round trip passed natively; see the
[owner-reported record](targets/t14-gen1-amd/usb-bringup.md#2026-10-05-first-native-installation-c4).
Power loss during writes, uncertain I/O and other devices, ports or the dock
path remain unqualified. The internal NVMe remains unsupported.

## System layout renames

The [system layout](wip/system-layout.md) renames leave two gaps until later tasks.

- `home://` is unbound until task 4 adds the persistent home volume. Doom saves,
  Quake's write directory, the hello demo and the `home://` examples in the
  shell, port and tool guides fail until then. They keep the name so task 4
  needs no second rename; use `tmp://` or a mounted volume meanwhile.
- Update recognizes only the current installed form, boot init's normal and
  rescue entries. An installation from before task 2, such as 0.0.2, is
  reported as having damaged or missing boot files and an unknown revision, and
  is rebuilt. The pool is unaffected. Revisit with task 3's two-stage Update.

## Boot init and space creation

[Boot init](userland/init.md#boot-configuration) and the
[space factory](userland/init.md#space-creation) have these limits:

- Space creation panics on memory exhaustion, as boot-time creation always
  did. Only boot init can create spaces, early in boot. Make creation fallible
  before the new-space flow lets users create spaces.
- A failed launch returns only a status. The caller cannot tell a request
  rejected before creation from a space that was created and left unstarted;
  boot init only reports it. Revisit with the space manager.
- Spaces are never destroyed.
- There is no limit on how many spaces a configuration creates, though each
  costs about 8 MiB at 1080p. The owner chose not to add one (2026-10-06):
  installed hardware has ample memory, and the rescue entry recovers an
  override that exhausts it.
- Space inits must be `boot://` archive entries, and they hold no mount
  authority, so `sync --disk` is unavailable to them; `sync PATH...` works.
- Every new space's first process receives that space's console, keyboard,
  pointer, display and space grants, so the installer now holds input,
  display and title authority it does not use.
- Configuration errors are Lua messages and call statuses are numbers.
- Spaces created one after another usually start their inits on the same AP,
  because earlier inits are blocked and do not count toward load; boot used to
  place all inits before scheduling, on CPUs 1, 2 and 3. Balancing moves
  runnable tasks later. Revisit if interactive latency suffers.

## Rescue set programs

Installed systems run ordinary programs from `bin://`, but the boot archive's
rescue set still carries `textfs` and `httpfs`. The init scripts start those
providers before any shell, and a launch failure stops a script, so without them
a system whose `bin` volume is missing would get no shell, not even from the
rescue entry. Most programs should eventually load from the installed system
rather than the rescue archive. Revisit once inits can start providers from
`bin://` with a fallback, or tolerate a missing provider.

Related limits of the [program stage](userland/system-updates.md#program-stage):

- Only executables move. `share/`, `sdk/` and configuration stay in `boot://`,
  because programs and ports name those paths; moving data needs path changes.
- Builds without a Git revision share `bin/unknown`, so two such builds cannot
  keep separate program directories.
- The installer holds the boot archive twice in memory while filtering it,
  about 90 MiB for today's archive.
- Spaces receive `bin://` read-only; only the installer writes it.

## Archive-only network configuration

Network profiles live only in `boot://config/network.lua`. On an installed
system, changing them needs an Update, while spaces can change through the
pool override ([system layout](wip/system-layout.md)). Revisit after boot init:
move network configuration onto the pool, following the same override pattern.

## USB image updates and firmware qualification

The [raw USB image builder](development/usb-image.md) creates fresh images and
replaces the sample pool and its identities on every rebuild. There is no
preservation of installed data, rollback or atomic physical update protocol.
The manual copy procedure relocates backup GPT on larger media but does not
expand the pool. Installed systems use the [native installer](userland/installer.md)
and its pool-preserving Update instead; the raw image remains a development
artifact. Revisit image preservation only if raw images become a delivery format.

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

## Initial net0 selection limits

[Automatic selection](devices/net0-selection.md) requires complete discovery and
reported carrier. VirtIO without STATUS needs an explicit selector; an incomplete
inventory leaves the setup owner waiting while the shell remains available.
Selection binds once until reboot, with no controller fallback or lease
revalidation on link-up. A cable moved to another port therefore requires reboot
or an explicit future switching design. Revisit with drain/teardown ownership and
DHCP link-up policy, rather than adding a second binding or lease authority.

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
including a 4 KiB read/write buffer. A separate 720-byte persistent profile
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

## DHCP maintainer and client limits

[DHCP](devices/dhcp.md) renewal, rebind, expiry and background discovery live in
the trusted setup session. Detected failures attempt to clear IPv4 before
stopping. An unexpected maintainer fault or indefinite scheduling stall leaves
no independent kernel lease-expiry backstop; reboot is required. Revisit this
limit with explicit supervision or kernel deadline ownership, rather than two
uncoordinated lease authorities.

DHCP does not probe address conflicts or persist leases across reboots. Newly
launched programs receive current chosen DNS; existing programs retain their
startup DNS_SERVER. Revisit conflict detection for networks with competing
static addresses. Unassigned input accepts broadcast replies, so a server that
ignores the BOOTP broadcast flag can prevent acquisition. Clearing or replacing
IPv4 invalidates concrete endpoints and listeners; DHCP reacquisition does not
restart services holding those listeners.

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

## HTTP redirects

The [HTTP and HTTPS providers](userland/http-fetch.md) never follow redirects. A 3xx
response is a rejected final status, so opening a moved page fails even when
the server names its new location. Browsing through `fopen`, as the
[Links port](userland/links.md) does, meets this on ordinary sites.

Deferred by the owner on 2026-10-04: redirects are wanted, but not yet. When
they are implemented, settle:

- **Hops:** a bounded hop count, with loop detection.
- **Schemes:** HTTPS never redirects to plain HTTP. Whether HTTP may upgrade to
  HTTPS is part of the same decision.
- **Location:** a relative `Location` resolves against the URL of the request
  that received it, which is the current hop's URL after earlier redirects.
- **Methods:** methods are GET only today, so 303 versus 307/308 method rules
  can wait for non-GET requests.
- **Request data across origins:** credentials and other request headers are
  not carried to a different origin. See the
  [scheme provider notes](wip/userspace-scheme-providers.md).
- **The final URL:** the consumer must learn where it ended up. A browser
  resolves relative links against the final URL, not the one it asked for.
  Delivering it to programs that read through `fopen` belongs to
  [response metadata through fopen](#response-metadata-through-fopen).

## Response metadata through fopen

A program reading a provider URI through libc `fopen` receives only bytes. It
gets no media type, HTTP status or, once redirects exist, final URL. The native
OPEN reply already carries an optional media type, and the providers retain
the final HTTP status, but neither reaches the program. The
[Links port](userland/links.md) therefore detects HTML by sniffing or file
extension, and shows a rejected status only as an open error.

Revisit with a way to expose response metadata to programs that fits Pyxis,
alongside [discoverable resource representations](wip/userspace-scheme-providers.md#discoverable-resource-representations).
Following redirects is a separate deferral, recorded in
[HTTP redirects](#http-redirects).

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

## Configured mount discovery latency

Deferred by the owner on 2026-10-04 from the non-blocking review of
[PR #389](https://git.internal/PyxisOS/pyxis-os/pulls/389), revision `0d82e57`.
With `CONFIG_XHCI=y`, configured GUID mounts wait for sealed boot discovery
across all controllers and terminal GPT scans, even when the selected disk is
VirtIO. Slow USB discovery can delay a startup mount such as `system://`, or
exhaust its deadline. Enumeration and mount requests have existing 30-second
budgets; waiting does not provide an additional mount budget.

The native startup cost has not been measured; the first native installation
did not record it. On a later native USB mount boot, record discovery completion, GPT scan completion and mount readiness,
including the controller/topology and target backend. Revisit any latency policy
with those measurements while preserving duplicate-GUID detection and explicit
partial-discovery results.

## Configured GUID and boot-device identity

Deferred by the owner on 2026-10-04 from the same
[PR #389 review](https://git.internal/PyxisOS/pyxis-os/pulls/389), revision `0d82e57`.
The accepted observed-uniqueness policy intentionally permits the sole observed
matching GUID under partial discovery. If an intended internal system disk is
not observed because it is not ready or lacks a driver, removable media carrying
that GUID can instead supply `system://`. If both matching disks are observed,
the duplicate prevents mounting and can block startup. A GUID does not
authenticate a disk or its contents.

Revisit selection before supporting installed systems on NVMe or another
internal-disk backend. Preferring the boot device's identity is a review proposal,
not an accepted or implemented replacement policy; its discovery, authority and
lifetime contract still need discussion. Current read-only USB boot selection
remains unchanged.

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
separate. Each pool can retain 4 MiB of cached file payload plus entry metadata.
A separate metadata read cache can retain 512 KiB plus physical-home keys.
A writable pool reserves up to 520 KiB for 128 journal images and encoding
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
Directory lookup remains linear; directory, inode-file and indirect pages now
have a best-effort [128-page clean cache](devices/npfs-metadata-cache.md).
Its physical-home lookup is linear too, and pressure can discard every page.
[Matched measurements](development/experiments/npfs-metadata-cache/README.md)
show lower warm-open time without a payload/sync improvement; revisit indexing
or cache size with a representative larger working set.

Contiguous file data and journal payload now use bounded runs; checkpoint groups
adjacent homes already adjacent in scratch. An optional 128 KiB/pool gathering
buffer is best effort and pressure-reclaimable. Device limits can split those
runs; fragmented writes still wait on separate requests. The
[matched batching record](development/experiments/npfs-io-runs/README.md) measures
sync and request-count changes on VirtIO; revisit broader request scheduling or
reordering with a measured consumer workload. The
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
The [disk-full and retained-open deletion follow-up](development/experiments/npfs-runtime-qualification/README.md)
also exercises delayed ENOSPC, background retention, one-time error reporting and
recovery after freeing space, complete reads after unlink, final-close reclamation
and pending detached cleanup after an unclean writable restart. The last case
stops before reclamation while a handle is still open; it does not qualify
arbitrary interruption during a cleanup batch or storage failure. The
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
Create and file rename no longer flush unrelated cached files. Replacement
rename flushes the moved file before discarding the old name; shrink flushes its
target. Both can still fail on that file's delayed allocation. Rename without
replacement makes the directory edit durable without synchronizing moved-file
contents. Cache exhaustion and explicit/background sync still flush the pool.
The [namespace record](development/experiments/npfs-namespace-writeback/README.md)
checks this separation and retained-open dirty replacement. Revisit target-only
shrink writeback if a concrete workload needs to truncate despite its own disk-full
writeback error, with an explicit partial-block and pending-growth contract.
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
Kernel read-only mounts and image-only inspection/checking refuse committed
journals; the Linux FUSE adapter separately provides a RAM replay view. The owner
accepted no home-metadata checksums in v1. Writable fsck replays the journal; checksums cannot
detect every later metadata corruption once it is cleared. Revisit metadata
checksums when integrity needs justify a feature-gated layout change. Committed
replay has been exercised at runtime; see the
[adapter qualification](devices/filesystem-native-adapter.md#task-3-validation).
Pending detached cleanup across an unclean writable restart has runtime coverage
in the [follow-up record](development/experiments/npfs-runtime-qualification/README.md).
Arbitrary mid-batch cleanup interruption and failure points remain source-reviewed;
broader qualification waits until explicitly assigned. Formatting, checking and
inspection require unchanged standalone regular images and cooperating locks,
stage replay payloads in memory,
and do not repair arbitrary damage or reclaim cleanup lists. Large images/volumes
can exhaust host checker memory. Physical-media wear remains unmeasured; native operation latency and QEMU target
bytes are measured in a nested VM, and do not qualify SSD endurance.

The [read-only Linux mount](development/npfs-linux-mount.md) also accepts npfs
partition devices.
Its source must stay unchanged for the entire mount: image locks only coordinate
cooperating tools, and devices have no external writer exclusion. Linux can cache
that immutable view. Directory handles retain sorted metadata snapshots and can
exhaust host memory for very large open directories; global ownership checking
remains fsck's job. Revisit snapshot/caching policy with measured large-directory
workloads or a separately designed shared-writer protocol. Committed journals
are fully validated and replayed into RAM without source writes. The full payload
and sorted home-block index live until unmount; validation also temporarily holds
a pool-sized target bitset. Large pools/journals can exhaust host memory and fail
the mount. This recovery view shares fsck's payload validation, not its writable
feature admission or sequence increment. The
[task-2 record](development/experiments/npfs-fuse-task2/README.md) qualifies a
real interrupted write and unchanged source bytes; it does not measure worst-case
replay memory or startup cost. Revisit those costs with a concrete recovery
workload. Native timestamp xattrs
preserve unknown status and creation time that ordinary Linux attributes cannot
represent; Linux access/change time and allocated-block accounting are not native
npfs metadata.

GPT selection, automatic mounting and host writes remain separate follow-ups.
A host writer needs an explicitly chosen sharing boundary with Caelum's writer;
revisit when host writing is assigned. Per-user visibility awaits the
[users milestone](wip/users-and-authority.md); currently every volume is visible
to the mounting user.

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

Native xHCI initialization is [enabled by default](devices/usb-xhci.md), as the
owner requested on 2026-10-05. Broader controller and recovery qualification
remain pending; this default change does not expand the supported hardware
profile. `CONFIG_XHCI=n` remains available for images that must skip native
controller preparation and workers.

The [initial controller](devices/usb-xhci.md) has agent-run QEMU coverage and
limited owner-reported ThinkPad evidence, with independently discovered
controllers. It requires firmware memory decoding enabled for a
page-aligned BAR0 prefix, interpreted extended capabilities within that 4 KiB
prefix, 64-bit DMA, 4 KiB pages and MSI-X. Other profiles,
power management and insertion after the startup snapshot are unsupported.
QEMU advertises zero scratchpads and 32-byte contexts. An
[owner-reported ThinkPad run](targets/t14-gen1-amd/usb-bringup.md) observed
nonzero scratchpads and 64-byte contexts; nondefault PSI mappings and BIOS
ownership handoff remain unmeasured paths. This first native snapshot does not
establish broad controller qualification. The later owner-reported run also
traversed the dock's USB 3 hub and completed root/descendant storage probes.
Recovery remains unexecuted. The first native installation then mounted one
qualified stick writable from a built-in port; that single profile does not
qualify other controllers, ports or devices.

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
probing, kernel block registration and configured mount authority now use
accepted per-device support across controllers. Qualified disks support explicit
writable mounts; broader physical qualification remains separate work.

All advertised ports receive input/output contexts and an EP0 ring/control buffer
before AP startup. With the current 4 KiB buffer and 4 KiB allocations, this adds
four pages/range records per port even when empty. The unused two bulk-ring pages
per port, BOT matcher and endpoint setup were removed from the inventory slice.
This fits QEMU's eight-port profile but consumes the shared VM range budget and
can fail preparation on larger controllers. Revisit boot inventory/resource
preparation with physical port-count evidence; runtime allocation/reclamation
requires the VM ownership work rather than allocator locks. The first BOT consumer
now reserves a separate four-device bulk pool per controller in one 528 KiB DMA
arena, plus a 64 KiB non-DMA read scratch buffer. Exhaustion is an explicit
per-device unsupported result. Unused storage backing remains until reboot.
Revisit these bounds with concrete multi-device workload/resource evidence.

Hub discovery uses a pre-AP descendant pool, initially 32 per controller,
capped by advertised Slot capacity after reserving possible roots. One owned DMA
arena avoids multiplying VM range records but retains all reserved backing even
when no hub is attached; the current 32-entry/4 KiB profile adds 512 KiB per
controller. Pool allocation failure can fail that controller's preparation.
Revisit the budget and root reservation policy with actual topology/resource
requirements, without runtime mapping or allocation outside the VM contract.
The shared startup deadline can expire on large trees; exhausted branches are
partial. Low-speed hardware paths remain unqualified. The first owner-reported
ThinkPad snapshot exercised full-speed descendants behind high-speed hubs;
recovery and broader TT qualification remain pending. QEMU's built-in hub
exercises full-speed descendants only. SuperSpeedPlus root recognition uses
discovered protocol metadata; QEMU does not exercise that profile. The subsequent
[owner-reported native run](targets/t14-gen1-amd/usb-bringup.md#2026-10-04-read-only-storage-and-usb-3-hub-follow-up)
identified the dock's SuperSpeedPlus USB 3 hub and completed reads from its
SuperSpeed storage descendant. Additional link/firmware profiles and recovery
remain unqualified; revisit them with further native evidence.
Only standard symmetric Gen1/Gen2 one/two-lane downstream links are attached.
Absent/ambiguous controller profiles remain partial; revisit with actual profile
evidence rather than picking a speed ID. Categorical inventory
omits directional rates and lane counts; SSP isochronous byte budgets remain
uninterpreted until actual non-control endpoint scheduling needs them.
Hub descendants are not monitored after publication; idle downstream removal
retains their slots/backing until reboot. Revisit this with separately scoped
hotplug/lifetime work. Root removal still retires the retained subtree, and
active request errors quarantine the controller.

Each device admits one active control request; each admitted BOT device also
serializes private bulk exchanges. Owned stalls have bounded endpoint recovery,
including TT cleanup and safe dequeue retirement. Other early errors, deadlines
or removal during active work stop the whole controller and retain unresolved
DMA until reboot. BOT probes and registered kernel block reads execute 512/4096-byte media reads
and large-LBA SCSI commands in QEMU, including hub descendants and multiple
controllers. GPT waits for terminal USB discovery and scans retained candidates
without treating partial discovery as a global I/O failure.
Stall/TT/reset recovery, active abandonment, ring wrap and nonzero alternate
selection remain source/spec-reviewed without forced-error validation. Revisit
with natural device evidence; physical USB qualification remains separate.

The [BOT/SCSI probe](devices/usb-storage.md) accepts one non-composite BOT
interface, no streams, and one LUN. Multiple LUNs, other interface shapes and
observed READ CAPACITY (16) protection-enabled geometry remain unsupported.
Terminal candidates now register kernel block devices and support GPT
discovery. Configured native GUID authority supports USB mounts after sealed
discovery and terminal GPT scans, including explicitly writable mounts on
qualified media. Observed uniqueness is accepted under partial discovery;
unseen disks may conceal another matching GUID. Duplicate observed matches
fail, and selected-disk errors never fall back. Installer raw authority now
accepts retained USB candidates after observed discovery is sealed. It permits
partial USB coverage while refusing lost registry records or incomplete VirtIO
bookkeeping. Unseen disks may conceal additional eligible targets; the existing
sole-eligible selection and typed consent apply only to observed disks. Normal
boots grant no raw service, and qualified write claims still exclude mounted or
claimed devices and latched write failure. The native C.4 installation listed
its stick while unsupported EHCI kept coverage partial. Revisit inventory coverage
and target selection with further native topology evidence rather than inferring
a complete machine inventory from a successful installer list. Two captured I/O slots per
supported disk and snapshot capacity are
reserved before AP startup; GPT USB scans share one scratch buffer. Revisit the
pre-AP reservation cost with measured topology/resource requirements and later
native qualification. Revisit those limits in their focused
integration/qualification tasks. Runtime READ rejection with valid sense now
fails only its ticket; healthy transport remains READY. Failed sense, real
transport failure, timeout and unsafe host states remain terminal. These
rejection/failure paths have source review only; revisit with natural media
errors rather than forced-error validation. NOT READY media retain sense and
fail immediately, including NOT READY / 04h/01h (becoming ready); bounded UNIT
ATTENTION retries do not implement a spin-up policy. Revisit a bounded wait only
if natural device evidence requires it, within the existing media deadline.

USB 3 inspection omits SET_SEL and SET_ISOCH_DELAY, which the specification
requires during full enumeration. EP0 routing/descriptor inspection does not
consume their power-exit or isochronous scheduling values, but complete inventory
is not full USB 3 conformance. Revisit with actual path-latency accounting before
adding power management or non-control scheduling; do not send successful zero
placeholders. USB 3 boot traversal retains the existing conservative USB 2
stability/recovery delays and adds no explicit warm-reset recovery retry.

## USB writable-media qualification limits

C.1 requires known WP-clear protection and a successful real blocking
SYNCHRONIZE CACHE (10) before enabling writes and flushes. MODE SENSE (6) captures
only its four-byte header; fallback to the eight-byte MODE SENSE (10) header is
limited to current ILLEGAL REQUEST / invalid-opcode or invalid-field rejection.
Unknown protection, unusable optional headers and clean qualification rejection
leave healthy media readable but not writable. No MODE SELECT or write-cache
mode change is attempted. Revisit compatibility only with natural device
responses that need a concrete bounded extension; do not infer writable or
flush support from vendor IDs or successful reads.

Two hardware compatibility watchpoints from merged
[PR #395](https://git.internal/PyxisOS/pyxis-os/pulls/395) remain deferred.
The first natively qualified stick, the C.4 install target, triggered neither.
A device that cleanly rejects SYNCHRONIZE CACHE stays read-only, including a
device whose firmware might not use a volatile write cache. Querying its caching
mode page and reported Write Cache Enable (WCE) state is a possible extension
guided by device evidence, not an accepted alternative qualification or proof of
physical durability. Revisit only
after observing an affected expendable device and settling the write/flush policy.

An optional MODE SENSE or qualification synchronization exchange that breaks
transport currently fails the whole media probe, even when earlier reads
succeeded. Returning to read-only service after a successful reset is a proposal;
it requires an explicit recovery policy and verified healthy transfer ownership
and reads. Prior read success alone does not establish those conditions. Revisit
with natural physical-device evidence rather than weakening failure handling now.

The five-second exchange deadline and shared boot-media deadline also bound
synchronization. A slow genuine flush can retire the device even when the medium
is capable of persisting data. Revisit those bounds with measured physical
flush latency. QEMU command completion and restart checks do not qualify device
firmware, physical cache behavior or power loss. One physical stick has
qualified natively; other devices remain unqualified.

A failed runtime write/flush or abandoned published mutation permanently latches
write failure for this boot. Healthy transport can still admit reads, but the
filesystem may separately retain its own writeback error. No mutation replay or
later successful flush clears either backend uncertainty. Revisit any recovery
only with an explicit error-acknowledgment and ownership contract. Mutation
failure/abandonment, MODE SENSE fallback, unsupported flush and malformed
qualification responses have source review, without forced-error validation;
revisit with natural device evidence.

## USB controller and transport coverage

xHCI is the only USB host-controller driver. EHCI, OHCI and UHCI controllers,
such as the ThinkPad's Realtek DASH EHCI, remain unsupported inventory records.
`lsusb` then reports partial coverage, and disks behind those controllers are
invisible to configured mounts and the installer. [USB storage](devices/usb-storage.md)
uses Bulk-Only Transport only. A device offering UAS as an alternate is used
through BOT; a UAS-only device is unsupported, and any BOT throughput cost is
unmeasured. Classes other than hubs and storage, including HID, remain unbound.
Revisit when a target device or workflow needs another controller type, UAS or
a USB input class; add each through the existing [layer
boundaries](devices/usb-installation.md#layers-and-ownership).

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

## PS/2 mouse synchronization and routing

The [PS/2 mouse](devices/mouse.md) realigns packets only by the first byte's
always-set bit. A byte lost inside the device can yield wrong motion or buttons
for a few packets before a misaligned first byte is rejected. Its IRQ 12 route
must share the keyboard's I/O APIC; firmware that places it elsewhere leaves the
mouse unavailable. Reconsider these when native packets show drift that a short
inter-byte timeout would catch, or a target routes IRQ 12 to another I/O APIC.

Only that PS/2 stream is supported. On the ThinkPad the touchpad stays in its
firmware relative mode, with no scrolling or multi-finger input, and TrackPoint
motion arrives mixed into the same stream. USB HID mice need configured
interrupt endpoints, which xHCI does not set up yet, plus a HID boot-protocol
driver; they fit best after USB storage's endpoint work. Pointer sessions are
relative only: there is no on-screen cursor or absolute positioning. Doom has no
mouse support yet, although pointer sessions would allow it. Revisit Synaptics
absolute mode when gestures or scrolling are wanted, and USB mice after bulk
endpoints exist.

# Technical debt and implementation tradeoffs

Record concrete limitations of implemented choices here: what we chose, its
cost, and when to reconsider it. This is a working record, not a roadmap or a
commitment to replace every simple implementation. Remove or update entries
when the underlying tradeoff changes.

## Remote transfer memory and staging limits

[Explicit remote transfers](userland/remote-terminal.md#explicit-file-transfer)
stream with constant memory and no fixed size limit (owner decision,
2026-10-08). Unverified bytes reach the disk, but only under the private staging
name, and are published after the size and SHA-256 match. Senders read the source
twice because the digest is announced before data. The framing keeps one 2 KiB
chunk in flight. On the ThinkPad over wired LAN, 15 MiB took 6.5 s up and
7.5 s down on 2026-10-09, about 2–2.3 MiB/s, owner timings after the
[transfer throughput](wip/remote-file-transfer.md#native-re-timing-2026-10-09)
and [network throughput](development/network-throughput.md) work; nested QEMU
is slower in both directions. Uploading 692 MB natively takes about 4½ minutes,
which the owner accepts. Keeping several chunks in flight is an owner decision,
since it changes the framing and the guest's 4 KiB typeahead allowance. Guest names are
limited to 200 UTF-8 bytes and host query/resolved paths to 1024 bytes.

The mandatory negotiated SHA-256 extension intentionally excludes stock kitty
peers. Reconsider interoperability only if a peer can supply the same verification
and publication guarantees. Transfers are single regular files without resume,
compression or deltas.

Exclusive `.NAME.xfer-partial-ID` siblings can survive abrupt process/session
death, and with streaming they can hold a partial file of any size, which
matters on a USB stick. Handled cancellation/errors attempt cleanup and report
failures, but stale files are never automatically deleted or overwritten:
manual review owns their removal. Rename commits the complete target; late cancellation cannot roll it
back. Linux host atomic publication is validated; the macOS exclusive-rename path
still needs an owner run. Revisit staging recovery if interruptions make manual
cleanup burdensome, with explicit ownership rules rather than age-based deletion.

## Regex character classes and back-references

The [libc regex interface](userland/libc-portability.md#regular-expressions-and-utf-8-conversion)
decodes UTF-8 but classifies and folds only ASCII. Non-ASCII values have no
character class and fold to themselves, so Unicode class searches and
case-insensitive non-ASCII searches are incomplete. Revisit Unicode tables or
locale policy when a concrete consumer requires them.

Owner decision, 2026-10-07: preserve the pinned musl 1.2.5 TRE matcher behavior.
BRE back-references remain bytewise under `REG_ICASE`, and the backtracking
path assumes single-byte lookahead and does not fully restore variable-width
decoder state. UTF-8 back-reference matches and offsets are therefore unreliable.
Invalid subject UTF-8 returns `REG_NOMATCH` when encountered, with no whole-string
validation guarantee. Collating symbols and equivalence classes remain unsupported.
Revisit the pinned engine or a focused matcher correction when a consumer needs
these behaviors; vi/less retain the documented limits.

## Remote drop prompt tracking

[Host file drops](userland/remote-terminal.md#dropping-a-host-file) require a
known empty root-shell prompt. OSC 133;B and command completion do not acknowledge
consumption of host input: a completion/prompt can precede queued commands.
The client therefore latches uncertainty when it forwards input while a command
is pending. Drops remain normal pastes until reconnection, including after an
interactive editor. Canceling an edited shell line can also suppress detection
until a later completed command. This trades missed upload offers for avoiding
injected commands in running programs, without changing the terminal wire or
kernel authority. Revisit when a concrete native input-acknowledgment contract
can establish prompt/input ordering; rendered prompt text is insufficient.

Detection supports printable ASCII host paths only, matching the shell editor's
input contract, and refuses final symlinks to match `xfer`'s source-opening policy.
Quoted commands too large for the editor fall back to text. Revisit path coverage
with native Unicode command editing or a supported argument-delivery interface,
not by interpreting a host shell or silently truncating names.

## SMT placement and later balancing

[Placement](kernel/smp.md#placement-and-migration) prefers idle siblings only
between equally loaded logical CPUs. The existing preemption push and idle pull
still balance logical load; they do not guarantee one compute task per core.
Read-only GDB on an eight-CPU/four-core QEMU run showed four tasks initially
occupying four cores,
then a push moved one onto an occupied sibling while a different core became
idle. The [measurement record](development/experiments/core-placement/README.md)
records that observation and the owner-run native improvement: heap ×4 wall fell
from 2.352 s to 1.389 s, with all four clients near the solo-client time.

Revisit topology-aware balancing if owner-run workloads show that these later
moves erase the benefit. This task leaves the push threshold and pulling policy
unchanged. CPUID-unavailable CPUs, older AMD compute-unit encodings and AMD
non-power-of-two thread counts remain isolated; extend detection when a concrete
supported target requires it.

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

Streaming [remote transfers](userland/remote-terminal.md#explicit-file-transfer)
make such files ordinary: on 2026-10-08 a 1.1 GiB upload into `tmp://`, written
in 64 KiB pieces, left the allocator reporting 4.32 GiB in use, against 112 MiB
before, because the replaced buffers' heap pools stay mapped. Large transfers
belong on a pool-backed directory such as an installed `home://`.

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

After topology-aware placement (#451), the native page batches looked slower per
client than the task-8 record: two clients took 0.148–0.149 s each against
0.105–0.115 s, and eight up to 0.344 s against 0.137 s; one client was unchanged
([record](development/experiments/core-placement/README.md)). That comparison
spans sessions and older main, so it is unmeasured. One possibility is that
spreading clients across cores and the two CCXs makes this sharing costlier. When
revisiting, start with a same-sitting native A/B against main.

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
presentation, the BSP request executor, network, native filesystem, USB and ACPI.

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

## ACPI interpreter host limits

The [ACPI host interface](kernel/acpi.md) makes these choices for the first
milestone task:

- **PCI configuration writes are refused.** AML that needs them fails the
  access and logs the function, offset and value. Revisit if the ThinkPad or a
  later ACPI task logs refusals; a write path would need its own ownership rule
  for functions that drivers own.
- **The firmware mapping window is never reused.** Its 64 MiB of address space
  bounds every distinct mapping for the whole boot, and an operation region
  is mapped whole. QEMU uses 14 pages and the ThinkPad 2,127 (8.3 MiB).
  Revisit if a machine fills the window or maps very large regions.
- **AML is trusted with hardware.** It may use any I/O port, including the
  legacy PCI configuration ports, and its device-memory mappings can alias
  registers that the kernel owns, such as the HPET and APICs. Revisit if a
  firmware access interferes with a driver.
- **The SCI must share the keyboard's I/O APIC.** It is installed only on the
  controller mapped for PS/2 routing, and not when no PS/2 route exists. ACPI
  events are then unavailable.
- **Waiting for deferred work runs it inline.** Installing the embedded
  controller's GPE handler does not wait, and nothing removes handlers. Revisit
  when the power-button task or notifications add handlers that wait.

## Power-off and restart limits

[Power-off and restart](kernel/acpi.md#power-off-and-restart) follow the first
version of "clean" agreed for the ACPI milestone (user tasks held, pools flushed):

- **No orderly stop of programs or services.** User tasks are held where they
  are; nothing is asked to exit or save. Revisit with service supervision.
- **Failed pools are skipped.** A pool whose writeback already failed takes no
  writes, so power-off proceeds without it; its unsynced changes are lost, as
  they would be with the power button. The failure was logged when it happened.
  Refusing instead would make a clean power-off impossible until reboot.
- **One flush failure keeps the system up.** There is no forced power-off
  command; holding the power button remains the way out.
- **Raw disk handles are not flushed.** The installer flushes its own writes;
  another raw writer would need to.
- **Untested failure paths.** The flush-failure, firmware-failure and reset
  fallback paths were checked by code inspection only. Revisit if a machine
  reaches them.
- **Held tasks ignore stop requests until release.** A group stopped during a
  failed power operation stops when its tasks resume.
- **Control-method power buttons are ignored.** Machines that report presses as
  `Notify(PNP0C0C, 0x80)` instead of the fixed event get no clean power-off
  from the button. The T14 and QEMU use the fixed event. Revisit on such a
  machine, with the notifications work.
- **A button press interrupts an Update.** A press powers off at once
  without asking, even while the installer rewrites a stick's ESP through raw
  disk writes, which are not flushed, so the stick could be left unbootable.
  Typing `poweroff` had the same effect, but a button is easier to press by
  accident. Revisit with installer work: it could hold off power operations
  while it writes.
- **A failed S5 entry freezes the BSP for 10 s.** uACPI waits that long with
  interrupts disabled before reporting that the machine did not power off.
  Revisit if a machine reaches that path.

## Embedded controller and battery limits

The [embedded controller and battery](kernel/acpi.md#embedded-controller-and-battery)
reader is the smallest that serves the space-bar widget:

- **EC transactions busy-wait on the BSP.** Each byte waits up to 500 ms by
  polling rather than sleeping. The worker stays preemptible, but a slow EC
  turns its time slices into polling while other BSP tasks wait their turn.
  ThinkPad polls took 1.8–8.3 ms after a first poll of 13.4 ms. Revisit if
  presentation stutters every five seconds; the EC's GPE could wake the worker
  instead.
- **The ACPI global lock is not taken.** uACPI's global lock is not recursive,
  and AML may already hold it around a field access when the EC handler runs.
  A `_GLK` request is logged. The T14 has none. Revisit on a machine whose EC
  asks for it.
- **One controller, one GPE number.** Only the ECDT or the first `PNP0C09`
  device is used, and a `_GPE` package naming a GPE block device is refused.
- **Two batteries, one adapter.** More are ignored. Batteries are summed as
  reported, so two batteries using different power units would give a wrong
  percentage.
- **Polled, not notified.** Charge changes appear within five seconds, and a
  battery's full capacity is only reread when it reappears. Battery and AC
  notifications from `_Qxx` methods only reach the trace log. Revisit with
  [ACPI notifications](wip/later-os-directions.md#power-and-acpi); that work
  also rereads full capacity and cycle count on `Notify(0x81)`, as Linux does.

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
on screen. Unicode widths and larger-line viewports are not implemented. These boundaries are recorded in [the terminal contract](userland/terminal.md).

[Shell history](userland/shell.md#commands-and-quoting) lives in memory, at most
100 lines per shell process, so it is lost when the shell exits or the machine
reboots, and a new remote session or mux pane starts empty. The accepted
follow-up (owner, 2026-10-09) saves it per space in `home://`. One space can
run several shells, such as mux panes and remote sessions, and spaces can share
a home, so that task must first decide how concurrent shells write the saved
history. Ctrl+R search is deferred too.

## Presenter-drawn block cursor

The presenter draws the [block cursor](userland/terminal.md) by recoloring the
cursor cell's pixels: the top-left pixel stands for the cell's background, which
takes the scheme's `cursor` color, and every other pixel takes `cursor_text`.
The TTY keeps no cell grid, so the glyph cannot be redrawn with its own
foreground and background. A cell whose top-left pixel belongs to the glyph,
such as a block or box-drawing character, inverts the wrong way, and a font
with more than two colors per cell would break the rule. Revisit by keeping a
TTY cell grid (character, foreground, background, style) and letting the TTY
render the cursor cell with the scheme's colors.

## Early console and post-handoff panics

The [early console and display panic path](kernel/early-console.md#panic-ownership)
provides direct boot-framebuffer panic output before and after presenter handoff.
Taking over another CPU's writer uses a bounded poll count without requiring a
clock, GS, locks or scheduler progress. A preempted or delayed BSP can exceed that
budget even though its next copy would notice the panic gate; then reporting is
serial-only and normal screen writes remain stopped. Framebuffer faults during
reset or drawing also revoke screen output. Revisit stronger CPU-stop/takeover
coordination with measured native failures, rather than assuming panic can wait
for scheduler progress. VirtIO panic output deliberately remains serial-only in
VMs; no emergency reset/queue or dedicated frame allocation is planned.

A serial port that stops accepting output remains latched off for the boot.
Revisit retry policy when reliable late recovery is needed; an absent/stuck port
must not block early boot or panic output.

## Space-layer qualification

The [space-layer milestone](userland/space-layers.md) closed on 2026-10-08 with
nested QEMU qualification. The owner ran the deferred native Quake check on
2026-10-08 on the ThinkPad (PXE boot of main `4332801`, 1920x1080 internal display, on AC): Super+Down/Up, continued game time and live terminal output,
fresh held input, switching away and back, queued-text clearing and hidden-layer
Ctrl+C behaved as specified. `timedemo demo1` ran at 684.5 and 684.7 fps.

Acquisition without PRESENT, explicit repeated PRESENT while hidden, independently
surviving capture on DISPLAY_RELEASE, shortcut releases after Super and device/
queue-loss propagation were source-inspected, without separate runtime coverage.
Revisit those checks when changing the corresponding session, input or teardown
paths, or when a concrete failure appears. Milestone closure does not convert
source inspection into measured coverage.

## Native system pointer qualification

The owner accepted native PS/2 deferral on 2026-10-08 while the ThinkPad was
occupied by the [Bluetooth investigation](development/bluetooth-investigation.md).
Matched QEMU checks on boot, Bochs and VirtIO displays suffice to close the
[system pointer](interfaces/pointer.md); this native entry remains open.

Owner-reported boot 1, 2026-10-09: ThinkPad, PXE main `114f2ac`, PS/2 touchpad
and TrackPoint, 1920x1080 boot framebuffer.

- Checked and working: ordinary motion and buttons, tab clicks, and **text
  selection**. The owner did not specify local terminal, multiplexer or both.
  A `screenshot` taken from the remote terminal while the local I-beam was
  showing contained the cursor.
- Not checked: multiplexer wheel; Quake lock/Super+Esc/click-to-relock; space and
  layer changes with the cursor shown; program cursor image/hotspot/show/hide
  and bounded warp; cursor cost samples.
- The default arrow looked wrong natively. The task 5 redraw addresses its
  shape; its updated native appearance remains to be judged by the owner.

Owner-reported boot 2, 2026-10-09: ThinkPad, PXE main `183f793`, PS/2,
1920x1080 boot framebuffer.

- Checked and working: Super+Esc unlocks the cursor in Quake; switching spaces
  with the cursor shown; text selection in the local terminal.
- Multiplexer selection and wheel were not checked. The image had no space
  with `multiplexer = true`; launching `mux` by hand printed its documented
  "missing terminal, clock, session creation or launcher authority" diagnostic.
  This is expected authority refusal, not a failure.
- Still not checked: click-to-relock after Super+Esc; layer changes
  (Super+Up/Down) with the cursor; program cursor image/hotspot/show/hide and
  bounded warp; cursor cost samples.

The consequence is that the remaining behavior and native cursor cost remain
unqualified. Boot 2 establishes local-terminal selection, while multiplexer
selection and wheel still lack a native check. Nested-VM results do not
establish native input latency or display performance.

- [ ] Finish the unchecked native items in a later owner ThinkPad batch and
  judge the redrawn default cursor. Record revisions, boot/display/device
  configuration, behavior and cursor cost samples. Update this entry with
  the reported results; neither partial native coverage nor QEMU milestone
  closure marks it complete.

## System pointer selection and input limits

Visible-cell selection has no export, clipboard publication or paste operation.
Stored cells are 8-bit glyph indices, so a later owned text snapshot also needs
an explicit encoding before it can be advertised as `text/plain`. Clipboard
stores, gestures, capability transfer and conversion remain in the separate
[clipboard proposal](wip/clipboard.md); revisit this boundary when that milestone
is assigned, using both kernel-local and mux-owned selections.

Mux drag autoscroll and selection across off-view history are absent; users must
first browse the desired history into view. Kernel terminals retain visible
cells without scrollback. Revisit these interaction limits with a concrete
terminal-history extension. PS/2 remains the sole implemented pointer source,
with raw counts and relative-mode Synaptics behavior. Bluetooth aggregation and
conditional source loss, USB HID, acceleration, absolute-mode scrolling, remote
pointer transport and multiple-display/window composition remain separate
tracks. Revisit input routing through the
[pointer source boundary](interfaces/pointer.md#input-source-coordination) when
another trusted source is integrated; independent masks must not release a
surviving source's held buttons. The current PS/2 reset hook alone does not
implement the accepted conditional multi-source rules.

## VirtIO cursor frontend limits

The owner accepted [GTK on X11, relative PS/2 and unscaled 1:1 committed guest
geometry](development/qemu.md#hardware-pointer-frontend) for hardware-pointer
qualification on 2026-10-08. QEMU installs the cursor through its host GUI.
Source inspection found a no-op native Wayland position warp, scaled/centered
GTK placement differing from the input transform, different SDL channel packing
and no VNC position callback. Other frontends and modes remain unqualified.

The consequence is that successful guest completion and cursor-inclusive
capture do not establish correct cursor placement or colors on those host
frontends. GTK fit-mode output may briefly scale during resizing; qualification
requires the committed guest dimensions to match actual GTK content at 1:1.
QEMU 10.2.2's zero-length used completion confirms command-buffer consumption,
not independently acknowledged cursor application or visible scanout timing.
The kernel carries no frontend-specific GUI switches. Boot and Bochs keep
software composition. See [hardware qualification](development/system-pointer-qualification.md#task-4-hardware-qualification).

Revisit on the next emulator/frontend upgrade or when broader frontend support
is requested, with matched shape, hotspot, position, clipping, alpha and resize
checks before extending qualification.

## Unselected graphical applications

The owner chose to keep Quake, Doom, Mandelbrot and `mousetest` running without
input focus. An unattended game in an unselected space or a hidden graphics layer
can use a CPU indefinitely and continues writing its mapped pixels even though
the presenter copies only the chosen surface of the selected space. Programs
may still pause themselves explicitly; completing
a Mandelbrot render returns it to its ordinary input/resize wait.

Revisit resource budgets or application-specific idle behavior when concurrent
graphical workloads make this cost a practical problem. Selecting a space does
not establish a scheduler budget or suspend other spaces.

## VirtIO GPU resize limits and runtime retention

The [2D display driver](interfaces/graphics.md#live-destination-geometry)
handles selected-output size changes and uses full-frame transfer/flush.
Boot and Bochs geometry remains fixed. Acquired application mappings retain
their original layout; presentation clips them and fills exposed margins.
Geometry wakeups and explicit mapping replacement support libterm, Kilo,
Mandelbrot and Doom. Other applications retain their acquired geometry until
they query or explicitly adapt. Kilo keeps its two-column, three-row minimum;
support for smaller terminals is deferred. Adaptive libterm reads require clock
READ authority; without it line helpers retain ordinary input behavior and
their initial dimensions. Revisit these limits with concrete additional
consumers. There is no vblank guarantee, 3D or
recovery after driver failure. Graphics acquisition, presentation and size
queries then return unavailable, while release remains usable; the last screen
may stay stale or blank. A selected VirtIO GPU's failure does not try a separate
VGA firmware framebuffer even in a
hand-built mixed-device VM. Revisit failover only with an explicit multi-device
policy. Revisit full-frame idle cost with measurements at the new geometries.

Successful resize reclaims old GPU backing only after confirmed fenced detach
and resource unreference. Runtime failure retains uncertain backing,
queue/control storage and PCI mappings until reboot, even after confirmed reset.
An uncertain bootstrap shutdown retains temporary storage too; a
PCI claim that ever enabled DMA stays retained even after successful bootstrap
reset. Revisit failure recovery only with a device teardown/reconnect ownership
contract. VirtIO panic reporting remains serial-only without GPU operations.

Old AP-visible TTY buffers use
[quiescent acknowledged TLB retirement](kernel/smp.md#memory-and-output-boundaries).
A missing acknowledgement retains one mapped old TTY/navigation/cursor batch
until reboot and disables further resizing; the newly committed display keeps
working. The implementation uses one one-second deadline, not an ABI latency
guarantee. Revisit retention only with a defined recovery protocol; an eventual
acknowledgement alone does not free that batch. Preparing and copying every
space's pixels also costs a whole old/new pair during resize. Every CPU's
console and kernel-log writes wait on the global output lock during the copy;
four-TTY nested-KVM measurements ranged from 0.45 to 4.39 ms. Revisit with
measured copy durations and concrete output-latency needs.

## Screen capture memory and consistency limits

[Screen capture](interfaces/screen-capture.md) admits one pending/in-flight
request, but completed immutable FILEs have ordinary reference lifetimes and no
separate quota. Each retained snapshot costs `4 * width * height` bytes, and
callers can exhaust available memory by retaining several. Revisit admission and
retained-image policy with concrete pressure workloads and an explicit authority
and lifetime contract; the one in-flight slot does not bound retained storage.

Capture backing starts uninitialized and relies on the presenter's full repaint
to fill every visible pixel before publication. Revisit this invariant before
adding damage tracking: a pending capture must force full composition to avoid
exposing unwritten or stale allocation bytes. See the authoritative
[frame contract](interfaces/screen-capture.md#frame-boundary-and-lifetime).

The captured bytes freeze one presenter composition and preserve tearing from
concurrent single-buffer application or TTY writes. No atomic application frame,
vblank or physical scanout timing is promised. A stuck presenter/scheduler or
panic cannot complete a capture. Revisit stronger consistency or bounded recovery
only with a separate presenter/backing ownership contract. The
[qualification report](development/screenshot-qualification.md) records QEMU
boot-framebuffer, Bochs and VirtIO PNG/monitor comparisons, shown-layer and
resize coverage, retained snapshot checks and BUSY admission. Failure cleanup
remains source-reviewed without injected failures.

## Bochs boot-mode scope and aperture retention

The [Bochs driver](kernel/display.md#bochs)
supports QEMU's modern register interface with an already enabled firmware DISPI
mode. It cannot restore legacy VGA state from DISPI registers, so disabled modes,
GETCAPS state and older register interfaces keep firmware output without mode
writes. The supplied boot framebuffer must start at BAR0; nonzero placement
refuses mode setting rather than rebasing a published direct mapping. Revisit
these limits only with a concrete device/firmware profile that needs them.

A prepared WC aperture and PCI register mapping/claim remain until reboot,
including after mode refusal. The WC aperture borrows boot leaves and extends
them before AP startup; it is outside generic PCI/VM release ownership. Retention
prevents aliasing or invalidating panic/fallback targets. Any reclamation needs
an explicit shared-mapping lifetime contract. A failed mode with unverified
firmware restoration stops boot with a serial panic, as agreed for task 4.

## Reverse remote terminal discovery

[Reverse connections](userland/remote-terminal.md#reverse-connections) remain
unauthenticated and unencrypted. A matching non-secret name selects a host,
which then receives the configured shell's authority. The owner accepted this
for a trusted development LAN; revisit when Pyxis gains authentication or is
used on untrusted networks.

Discovery owns UDP port 2324 on net0 exclusively while waiting; a conflicting
binding stops that daemon with a diagnostic. One reverse session runs at a time,
and the host tool accepts one session per invocation. Beacon cadence adds up
to approximately one second after discovery opens, plus network, scheduling
and connection/cleanup delays. Reverse discovery and log following passed the
owner's ThinkPad PXE check; macOS listener behavior remains unqualified. See
[remote-debugging qualification](development/remote-debugging.md#qualification).
Revisit these limits if a concrete multi-host
or unattended development workflow needs more.

## Kernel log retention and LAN visibility

The [kernel log](interfaces/kernel-log.md) retains 256 KiB in static storage,
evicting whole oldest lines. Oversized lines are discarded through their
newline. It is volatile, and following polls every 100 ms. Revisit capacity,
event-driven following or durable capture with measured native driver workloads.
Reads locate both cursors by walking retained length headers under the ring
lock with interrupts disabled; logging on other CPUs waits for that walk.
Revisit a cached line/index position if measured following workloads show
material writer latency, especially with many short retained lines.

Every configured space receives read-only log authority, including remote
shells. Kernel addresses in log text are therefore readable through the
unauthenticated remote terminal on the trusted development LAN. The owner
accepted this exposure for bring-up. Revisit the default grants and disclosure
policy when Pyxis gains authentication or runs on an untrusted network.

The packaged [capture policy](userland/init.md#boot-configuration) opts live
Development, installed `pyxis` and Remote into whole-screen CAPTURE. Any program
in a granted space can observe whatever any space currently shows; anyone
reaching the unauthenticated remote terminal on the development LAN can read the
whole local screen, including all spaces as they are shown. The owner accepted
this authority breadth for bring-up. Revisit it with user isolation,
authentication and capture delegation policy when Pyxis gains users or runs on
an untrusted network. See [screen capture](interfaces/screen-capture.md).

Panic ring capture is best effort: a fatal interruption of a ring lock owner
skips retention without waiting. Userspace readers also require a functioning
scheduler. Opt-in kernel UDP capture bypasses those locks, but remains best
effort: early panics without an active selected NIC, interrupted activation or
reset, failed bounded CPU handoff, carrier loss and stalled DMA can lose fatal
text. Failed completion retains buffers permanently until reboot. There is no
retransmission or persistence; a receiver started late loses earlier datagrams.
Enabled logging broadcasts kernel addresses across the trusted LAN and reduces
ordinary TX capacity to 15/16 VirtIO descriptors or 30/32 RTL8111 descriptors.
The ThinkPad's panic and disabled checks passed in
[PR #476](https://git.internal/PyxisOS/pyxis-os/pulls/476). Normal replay waits
for an IPv4 address, and a plain enabled boot then delivered the whole boot log
natively (main `fcf142e`, 2026-10-07;
[PR #480](https://git.internal/PyxisOS/pyxis-os/pulls/480)). RTL's ambiguous-slot
duplication and stalled-NIC abandonment remain code-inspected rather than
hardware-qualified. Revisit those paths, polling budgets, capacity and disclosure
with concrete recovery/authentication requirements; checked NIC completion
alone cannot guarantee host delivery.

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
  stage launched itself keep running. Opted-in spaces now delegate LAUNCH to
  ordinary foreground commands, so `pyxis.run` children can outlive interrupted
  Lua. Revisit with an explicit descendant lifetime design; remote execution
  groups already provide their separate group-wide lifetime policy.
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

## Readiness scaling

`wait_many` has a general 32-interest bound, with per-call copies and reference
retention, about 40 bytes of kernel-stack arrays per interest and interests in
the task's existing reserved request area. The readiness worker rescans the
interests on notification. Large connection sets can make these costs material.

A persistent wait-set object is a proposed later direction: register interests
once, allocate/fail at registration, and return ready entries without a new
per-call input array, in the style of epoll/kqueue. A haven web server with many
connections is one possible consumer. Authority, lifetime, capacity and failure
rules remain open; this is neither agreed nor scheduled, and no API is added.

## Initial terminal multiplexer limits

The [multiplexer](userland/multiplexer.md) has one window and up to eight panes.
Its native terminal subset has no alternate screen, Unicode widths or detach;
full-screen applications reuse the shell's screen. History retains 1,024
scrolled-off rows, with no reflow or erased-screen archive. Rows cropped by
resize are not inserted into history, so growing the view cannot recover them;
retaining those rows is a proposed follow-up, not part of this slice. Presentation crops
at the terminal-session maximum; history storage retains the largest width seen.
Maximum steady text storage is about 2.5 MiB per pane; resize can temporarily
double this for one pane. There is no per-group CPU/memory quota.

A full pane input queue can hold a prefix behind already staged ordinary input;
bounded, lossless storage cannot bypass an arbitrary pending paste. Confirmed
closure waits for group cleanup and output EOF, and published HOST work can
delay cleanup indefinitely. Output producer grants explicitly delegated outside
a pane group can delay EOF even after its cleanup completes. Mux exit/fault
requests termination through final
controlling-grant closure without waiting for all cleanup. Local keyboard and
graphics grants remain shared space facilities, so graphical launches share
the existing one-session ownership rather than acquiring pane-local devices.

Live pane resize depends on the consumer. Shell and Kilo observe RESIZED;
Links currently reads geometry once in its native adapter. Existing vi/less
behavior is preserved. Revisit these consumers with a focused port change;
revisit windows, ratio adjustment, input recovery and quotas when those
interactions are selected, rather than expanding the first slice.

## Initial independent terminal limits

[Terminal sessions](userland/terminal-sessions.md) have explicit resize, 4 KiB
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
deployment or text consumer. Remote resize negotiation and reconnect remain
separate work.

## Libc compatibility gaps

The completed [descriptor portability slice](userland/libc-portability.md) supplies
open/read/write/close for cksum and restricted tee, and `lseek` for files.
Public O_RDWR, fdopen and duplication remain absent even though fopen
supports update modes internally. Consumers requiring those interfaces need
a separately agreed extension. `fileno` was agreed on 2026-10-08 for the
[SDL2 port](development/sdl2.md); it exposes the stream's existing descriptor and adds
no new aliasing. Revisit them against a pinned consumer's actual
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
or input `fflush` refetches them. `setvbuf` and `setbuf` remain absent, so
programs cannot size or disable the buffer. One-byte `ungetc` is implemented.
Revisit together with output buffering in a later stdio completeness task. See
[input read-ahead](userland/stdio.md#input-read-ahead).

## Duplicated port output lists

`scripts/ports.mk` repeats staged output paths already declared in each recipe's
`metadata.lua`, including the sbase executables and notices. Adding an output
requires coordinated edits; drift can leave Make unaware of a missing staged
file. Revisit the build integration to derive output dependencies from one
authoritative list without growing a new build framework. Until then, review
both lists when updating a recipe's outputs.

## zlib core profile and qualification

The [zlib development library](development/ports.md#zlib-development-library)
retains unmodified public headers but omits `gz*` file helpers, as accepted for
the screenshot milestone on 2026-10-08. Those declarations therefore have no
linkable definitions in this profile. Revisit the file helpers with a concrete
consumer that needs them, auditing its actual libc and file-authority needs.
In-memory gzip framing remains available through the core stream APIs.

Archive builds, symbol inspection and staging checks do not establish runtime
compression correctness on Pyxis. The [screenshot consumer](userland/screenshot.md)
now supplies observed deflate-to-PNG output that decoded on the host. Other
compression/decompression profiles remain unqualified.

## libpng profile and runtime qualification

The [libpng development library](development/ports.md#libpng-development-library)
keeps conventional read/write APIs and generates its matching public
configuration, while omitting the simplified API and architecture acceleration.
Those omissions are accepted for the screenshot milestone. Revisit them with a
concrete consumer or measured cost that needs the omitted API or acceleration.

Archive/configuration/symbol inspection and image staging provide build
evidence only. The [screenshot consumer](userland/screenshot.md) now qualifies
the conventional RGB8 non-interlaced write path through host-decoded captures.
Target PNG decoding and other write profiles remain unqualified.

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

## Program location

A process cannot find where its own executable came from. `argv[0]` is whatever
the launcher passes, and the shell passes the command word as typed, so a
program started as `devilutionx` sees only that name. Even a full URI would be
descriptive: a path is not authority, and the process holds no lookup right to
the directory its executable came from. The [startup record](interfaces/processes.md#startup-record)
carries no program location, and libpyxis has no equivalent of `/proc/self/exe`
or `GetModuleFileName`. The [SDL2 port](development/ports.md#sdl2-development-library)
therefore reports `SDL_GetBasePath` as unsupported.

Ports that keep files beside their executable need a fixed location instead.
DevilutionX's recipe hard-codes its assets to `boot://share/devilutionx/`, so
running it as a self-contained bundle from another directory needs its own
fallback patch. Every such port carries a similar per-port path patch, and a
program cannot simply be copied with its files into another directory and run.

Revisit when another port needs files beside its executable, or when
self-contained bundles become a supported way to add programs. Options to weigh
then, none decided: a read-only directory grant for the program's own directory
at launch, given like the other startup resources; or a descriptive location in
the startup record, which confers no access and has `argv[0]`'s weaknesses. Do
not infer the location from `argv[0]` or add path normalization for one port.

## Sleep wake granularity

Per-CPU one-shot LAPIC timers now target local deadlines while retaining nominal
120 Hz preemption and HPET timekeeping. The owner accepted this scope on
2026-10-08. The [matched qualification](development/experiments/sleep-wake-granularity/timer.md)
records SDL mean-frame medians of 25.565 ms before, 24.243 ms with expiry IPIs
alone and 17.415 ms with local deadlines; Quake capped-loop medians were
49.223, 59.595 and 70.291 FPS. These are nested-KVM observations, not native
or maximum-latency guarantees. See [timekeeping](kernel/timekeeping.md).

Natively, the owner observed on 2026-10-08 on the ThinkPad (PXE boot of main `4332801`, 1920x1080 internal display, on AC) that capped Quake
play was much smoother than before. Its tear line stayed in about the top third
of the screen, consistent with a 72 FPS game on an unsynchronized 60 Hz
presenter, and `timedemo demo1` was unchanged at about 684 fps. That is an owner
observation, not a measured native sleep or cap latency. Precise native
latency, LAPIC power-state behaviour and long-running 32-bit HPET extension
remain unmeasured; revisit them with a consumer that needs tighter bounds.

Nanosecond units remain a representation, not a precision promise. Interrupt-
disabled intervals, runnable load, firmware/host stalls and large due batches
still delay execution. Sorted-list insertion/cancellation remains linear, with
no separate timer quota. Revisit stronger bounds or another data structure only
with a measured consumer need; tickless scheduling is outside this task.

## Wall-clock time and clock-source performance

[Monotonic time and deadline sleep](kernel/timekeeping.md) now use the shared HPET
counter. Console timeouts no longer count delivered BSP interrupts. Local timer
dispatch and scheduling still delay execution; nanosecond units do not promise precise
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
[software-extended HPET first](kernel/timekeeping.md#software-extension-sampling-and-support-limit),
with TSC as the [later direction](wip/later-os-directions.md#clock-source). The running HDA
worker also uses a 5 ms watchdog (about 200 timed wake opportunities per second),
with timer re-arms and HPET reads; it is absent while playback is parked.
Its measured wake/deadline amplification is in the
[audio profile](development/experiments/audio-task2/profiling.md). Cheaper
clock sources remain separate kernel work.
The implementation preserves direct 64-bit reads and extends 32-bit counters
with a shared CAS accumulator. Each advancing extension read publishes to one
cache line and may retry under contention. BSP maintenance is configurable in
timer deliveries, default 120 (nominally one second), with explicit early-boot
sampling. The [matched host-KVM observations](https://git.internal/PyxisOS/pyxis-os/src/commit/93851aebce74c71ceea93774c4d97e01bc2a60e7/docs/wip/thinkpad-kvm-tsc.md#local-implementation-results)
show lower clock-call cost for forced low-32-bit extension, with shared-state
cost included, but do not establish native performance. The direct profiled
allocation median was about 2.6% higher, mostly in BSP queue time; its cause
was not isolated. On the ThinkPad the owner recorded the 32-bit
software-extended path log and, on 2026-10-07, a native session of more than
15 minutes with the clock holding; see the
[target notes](targets/t14-gen1-amd/notes.md#native-status).

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
Per-packet network work and audio refill both pay several clock reads per
event; see [TCP throughput limits](#tcp-throughput-limits).
The VirtIO RTC driver remains deferred.

## Doom configuration and save-format limits

[Doom save/load](userland/doom.md#saves) now uses checked temporary writes and atomic
replacement through the [filesystem mutations](interfaces/filesystem-mutations.md). Saves
persist on installed systems and last until reboot on live boots. The upstream parser assumes trusted saves matching the
loaded game data; full malformed-file validation and separation by PWAD are not
implemented. Interrupted saves can leave temporary files for manual removal.

Configuration persistence is already disabled in the pinned generic engine.
Re-enabling it needs an explicit writable configuration location and review of
its parser/formatting requirements. Floating printf is now available for the
upstream timedemo report; exercising timedemo remains separate from normal
gameplay and demo playback. Wall-clock time is not a prerequisite.

## Clang code generation and predefines

Pyxis builds with Clang since the [LLVM toolchain milestone](development/llvm-toolchain.md).
Matched nested-KVM measurements on 2026-10-07 (four CPUs) left two measured
regressions against GCC 16:
- **Heap allocation:** `allocbench heap` takes about 13.1 ns/op, against
  12.0 with GCC (about 9%). The TLSF fix for Clang's narrowed flag stores
  removed most of the original 33% gap; the remainder is uninvestigated. Two
  early samples after the fix, 20.4 and 21.7 ns/op, did not recur.
- **Quake:** `timedemo demo1` runs about 3% slower (median 1560 against
  1604 fps).

Clang also emits more byte-sized read-modify-writes than GCC (164 against 75 in
the kernel); no other measured path showed a cost. Reconsider when an
allocation-heavy program, such as Lua, TCC or the JVM experiment, measures the
heap gap, or when the LLVM pin moves.

Clang predefines `__INT_FAST8_TYPE__` and `__INT_FAST16_TYPE__` as `signed
char` and `short`, while libc's `stdint.h` defines `int`. Code using the
compiler macros instead of the header gets a different type. Reconsider if a
port relies on those macros; the fork's target information could then match
the header.

## C++ runtime subset

The [C++ in userspace milestone](development/cxx-userspace.md) shipped a
deliberate subset, accepted by the owner on 2026-10-08:

- **No threads or thread-local storage.** `<thread>`, `<mutex>`, `thread_local`
  and non-lock-free atomics are absent, and local statics use single-threaded
  guards. Programs that start threads cannot be ported yet.
- **No localization or wide characters.** `<iostream>`, `<locale>`, `<regex>`
  and wide strings are absent; so are the fmt headers that need them
  (`chrono.h`, `ostream.h`, `std.h`, `xchar.h`, `printf.h`). libc now has a
  "C"-only `setlocale` (no `localeconv`) and `wcslen`, but ports that print
  through `std::cout` still need the other wide-character functions: 58 of
  the 59 names in `<cwchar>`.
- **Most of `<cmath>` is missing.** libc's math subset leaves 161 of the 186
  names `<cmath>` imports undefined, so their first use fails to compile.
- **Exceptions cannot cross C frames.** C code has no unwind tables, so an
  exception thrown through, for example, a `qsort` comparator terminates the
  program.
- **Uncaught exceptions name mangled types,** such as `St11logic_error`, because
  the terminate handler leaves out the 184 KB demangler.
- **No `<filesystem>`, `random_device` or time zones.**
- **fmt's license is not staged for the boot payload yet.** It lives only in
  the fmt development files; a port that ships an fmt program must install it.

Revisit threads and TLS with the Clang hosting milestone. Revisit
localization, wide characters and `<cmath>` when a selected port, such as
DevilutionX, needs them. Each addition belongs in libc or the runtime
configuration, never in a port-local stub.

## HD Audio scheduling and startup tuning

The [HDA engine](devices/hda.md#progress-and-refill-limits) uses the accepted
80 ms hardware ring and fails closed until reboot when its conservative refill
guards cannot establish safe progress. Immediate, unprimed start produced a
58.667 ms initial silence gap. Revisit ring depth,
startup latency and service margins during native qualification and separately
assigned audio-consumer work. The [task 2 report](development/experiments/audio-task2/README.md)
records the evidence and limits; native and milestone closure checks remain open.

## HD Audio sustained eight-session playback

Accepted by the owner **2026-10-09**: deliver the session/mixer/refill work in
[#557](https://git.internal/PyxisOS/pyxis-os/pulls/557) with this recorded limit.
Sustained eight-session playback fails closed in **nested QEMU**: the longer
uninstrumented run produced **112.227 s of output from first RUN** before the
codec WALCLK commit guard rejected progress. The exact eight-only failure time
was not timestamped. Matched four-CPU BSP host-thread CPU was **99.55–99.61%**
before notification gating/one-clock-per-readiness-scan fixes and
**93.93–96.40%** afterward (of one host CPU). One-session BSP cost fell
**54.41–54.47% → 40.47–44.73%**. Linux-accounted eight-session guest BSP time was
**42.42–42.48% → 41.73–43.40%**. Eight-source whole-QEMU CPU increased
**125.11–126.51% → 136.26–144.20%** as retry/wait traffic increased. The
[profiling report](development/experiments/audio-task2/profiling.md) records
revisions, configuration, ranges and shared-host/probe limits; these are not
native performance figures. Bounded request batching remains deferred.

The most likely trigger is a transient nested-host scheduling/VM-exit delay
inside one IF=0 commit window, which includes mixing and MMIO/HPET observations,
turned into permanent unavailability by fail-closed. The mixer itself costs
about **5–6 µs** per period; this failure does not establish inadequate mixing
throughput. Maximum recorded HPET commit time was **922,780 ns**, so the rejecting
condition was the later WALCLK check reaching 1 ms. The exact codec-clock delta
and a coincident host descheduling event were not captured; the attribution is
an inference, not a proven host trace.

`QEMU_CODEC_BURST_BYTES` (**8192**) and the **1 ms HPET commit/WALCLK bounds**
are derived from QEMU's timer-driven codec, not native HDA guarantees. Task 5
must re-derive or replace these values for AMD `1022:15e3` using controller/FIFO,
DMA progress and native timing evidence, rather than inherit them as universal
limits. The current values are unchanged in #557.

One guard trip continues to disable all audio until reboot; STATUS/RELEASE and
cleanup remain serviceable, with DMA retained. Native evidence decides whether
a separately reviewed controller-reset recovery path is needed; no recovery
path is accepted or implemented yet. **HDA milestone closure requires native
eight-session playback**, not QEMU closure. This supersedes the earlier
QEMU-closure/native-later alternative. Revisit batching, native bounds and
recovery with task 5's owner-run ThinkPad speaker/headphone batch.

## SDL2 port limits

The [SDL2 port](development/sdl2.md) covers video, keyboard, pointer, timing
and preference paths. Missing pieces:

- **Audio:** the [native PCM grant](interfaces/audio.md) and
  [QEMU HDA engine](devices/hda.md) are available, but SDL2 has no audio backend
  yet. Revisit with a separately assigned playback consumer task.
- **Threads:** without them, `SDL_INIT_TIMER` callback timers and
  `SDL_CreateThread` fail. Revisit with userspace threads.
- **Waiting:** `SDL_WaitEvent` keeps upstream's polling loop with a 1 ms delay.
  Deadline sleeps now make that about 1 ms rather than the old 8.33 ms tick,
  so an idle waiting program wakes about 1000 times a second instead of about
  120, increasing its CPU wake cost. This is the expected polling rate, not a
  measured `SDL_WaitEvent` run; see the
  [timer limits](development/experiments/sleep-wake-granularity/timer.md#limits).
  Revisit a blocking wait on the input and display handles when a consumer
  waits for events.
- **Windows:** one fullscreen window; multiple windows remain outside the
  current display contract. System pointer positions, program images,
  show/hide, bounded warp and relative lock now use the
  [native pointer contract](interfaces/pointer.md).
- **Text:** US layout only, from the shared kernel table.
- **Not covered by validation:** key repeat, because QEMU's injected PS/2 input
  has no typematic repeat.

## DevilutionX port limits

[DevilutionX](userland/devilutionx.md) is personal-use only, because its
non-commercial licence and libmpq's GPL cannot both be met by someone who
distributes it. It is therefore an opt-in build that ordinary images, CI and
bundles never contain.

It has no sound, multiplayer, game controllers or translations; the build
host has no gettext. Saves are in `home://devilution/`, which is RAM on live
boots.

Retail data cannot be staged in images: `DIABDAT.MPQ` is about 500 MB, which
would stay in RAM and does not fit the ESP. On installed systems it has to
arrive through [remote transfers](#remote-transfer-memory-and-staging-limits),
which today means splitting it into 15 MiB pieces. Revisit with streaming
transfers.

## SDL2 and DevilutionX native qualification

The owner checked the shareware build natively on 2026-10-08 on the ThinkPad (PXE boot of main `4332801`, 1920x1080 internal display, on AC). DevilutionX
started and played with keyboard, touchpad and TrackPoint; key repeat worked in
name entry. With the default "Limit FPS" setting it ran at 59–65 FPS, mostly
60–62, filling the 1920x1040 content area. The results are in the
[DevilutionX reference](userland/devilutionx.md#measurements).

A [standalone bundle](userland/devilutionx.md#standalone-bundle) with retail
data on the installed stick remains unchecked natively. The owner deferred it:
the shareware result is enough for now, and moving 692 MB waits for streaming
transfers. At the native upload rate measured the same day, about 2.5 MiB/s,
that is roughly 4½ minutes. Revisit when the owner wants to play the retail data.

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

[BusyBox vi](userland/vi.md) displays ASCII only. Its BRE search/substitution
uses libc's [regex limits](#regex-character-classes-and-back-references).
Owner decision, 2026-10-07: adapt GNU regex calls using bounded copies; matching
stops at an embedded NUL within each copied slice. Revisit bounded/binary regex
interfaces when a concrete consumer needs them. Saves keep upstream's
in-place write followed by `ftruncate`, so a short write or crash can leave a
truncated or mixed file. Revisit with atomic replacement or a durable-save
policy alongside the [native filesystem](devices/filesystem-native-adapter.md).
`:!` and shell filters need a native launch adapter, and the read-only marker
probes WRITE authority because truthful file metadata does not exist yet. The
recipe's libbb adapter covers the selected vi/less helpers only.
Input EOF exits and loses unsaved edits, as upstream does; Kilo handles that
case explicitly.

## less pager limits

The [BusyBox pager](userland/less.md) retains read display lines for backward
paging, with the selected line-count limit and process-memory bound. It measures
screen dimensions once and displays ASCII. BRE search/highlighting inherits
libc's [regex limits](#regex-character-classes-and-back-references).
There are no raw escapes, shell commands or live
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

The display's quiescent TLB-flush helper does not establish this transport's
reader or DMA lifetime. Revisit reclamation with that ownership contract and a
defined device teardown/reconnect lifecycle. Never free an outstanding DMA
buffer solely because a request timed out. Idle daemon disconnection is not necessarily observable
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

## Updates from before boot init

Update recognizes only the current installed form of the
[system layout](userland/system-layout.md), boot init's normal and rescue entries.
An installation from before boot init, such as 0.0.2, is reported as having
damaged or missing boot files and an unknown revision, and is rebuilt. The pool
is unaffected. Because the previous revision is unknown, that Update removes no
program directories. Revisit if another older form needs a direct Update.

## RAM volumes

Boot init makes each configured `ram` volume, such as the live `home://`, as a
subdirectory of one private RAM directory the kernel hands it as the `ram`
resource. No ABI creates a detached RAM directory, so only boot init can make
RAM volumes, all share one RAM filesystem, and nothing limits their size apart
from memory. Add an ABI that creates RAM volumes when a second user, such as a
per-session scratch volume or a size limit, needs one.

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

## Interim program revision directories

Each Update copies every moved program into a new `bin/REVISION` directory, even
when a program is unchanged, and the pool keeps two complete revisions. The
directory is selected by the running kernel's revision, so programs cannot be
updated without a new kernel and ESP. The [system layout](userland/system-layout.md#programs)
accepted this as interim. Revisit when a final program update scheme is
designed.

## Archive-only network configuration

Network profiles live only in `boot://config/network.lua`. On an installed
system, changing them needs an Update, while spaces can change through the
pool override ([system layout](userland/system-layout.md)). Boot init now exists, so
this is the next follow-up: move network configuration onto the pool, following
the same override pattern.

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

## TCP throughput limits

After the [network throughput](development/network-throughput.md) work, native
send reaches 70.5 MiB/s with 8 KiB writes and receive 85 MiB/s into a discard
sink, measured on the ThinkPad on 2026-10-09.

- **Send is bound by per-segment and per-call cost.** At most about 24–26 KiB
  of the 64 KiB window is in flight, and 2 KiB writes reach 44 MiB/s against
  70. Each native call moves at most 4 KiB through the single BSP network
  worker. Revisit with worker batching or a larger call extent, measured
  against the same runs.
- **A second loss in one window waits for the retransmission timeout.** One
  native 2 KiB send in six stalled about 1.09 s: fast retransmit repaired the
  first missing segment, and lwIP resent the second only on its timeout. Watch
  for repeats before changing loss recovery.
- **Receive into a RAM file is consumer-bound.** Into `tmp://` it reaches
  45–57 MiB/s while Pyxis's advertised window falls close to zero; the program
  writing the file is the limit, not TCP.
- **Clock reads per packet.** In QEMU the network worker's wake and sleep
  cycle reads the HPET about 27 times per data segment, nearly all in timer
  handling. The audio work in #557 found the same amplification. Their native
  cost is unmeasured; cheaper timekeeping is separate kernel work under
  [clock-source performance](#wall-clock-time-and-clock-source-performance).
- **Not implemented:** path-MTU discovery, so routed peers get 536-byte
  segments; window scaling, so windows stop at 65,535; SACK.

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
the kernel ChaCha20 generator seeds from VirtIO when present, otherwise checked
CPU RDSEED/RDRAND. Hardware trust remains; there is no independent source mixing.
Initial/required seed failure stops random reads and TLS operations needing new
material until a later full attempt succeeds. Provider startup failure can leave
HTTPS unpublished; initial TCP identity failure separately disables new kernel
TCP connections for that boot. The owner must confirm the generator/startup path
on native hardware; successful QEMU CPU reads are guest evidence. UTC remains subject to the
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

The [native format design rules](devices/filesystem-readonly.md#design-rules)
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
native-URI JSON/JSONC configs, one-shot text/JSON output and eleven selected modules.
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

Battery has no temperature or manufacture date, which ACPI does not report.
Power Adapter is not built: ACPI's `_PSR` gives only online or offline, and the
module prints watts, so it would show an invented value; AC presence appears in
Battery's status instead. Linux shows no power adapter on the T14 either.
Revisit with a source of adapter wattage, such as USB-C power delivery.

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

### Bluetooth cold firmware upload and running-version policy

For the [Bluetooth investigation](devices/ax200-bluetooth.md), the owner accepted using
already operational AX200 firmware and deferred cold bootloader upload on
2026-10-08. Warm boot or passthrough may retain another OS's chosen build; the
probe verifies that build stays unchanged, without comparing it to a Pyxis pin.
The consequence is that investigation results depend on prior host initialization:
a cold bootloader device cannot become ready through this probe.

Revisit before claiming native Bluetooth startup or designing the persistent
stack. Intel secure upload needs bulk OUT and an owned event strategy for bulk
IN as well as interrupt IN. The existing synchronous bulk timeout/quarantine is
unsuitable for polling an idle bootloader event channel. Firmware boot also needs
its real vendor notification, rather than a fabricated Command Complete. Any
reset that re-enumerates USB must account for the existing controller quarantine
and retained DMA policy.

The owner must mirror exact `.sfi`/`.ddc` files, alias targets, provenance and
license before a committed build uses them. Capture cold version/boot parameters,
qualify secure download and boot, apply DDC after boot, and re-read the version.
Comparing another OS's running build with a future Pyxis pin needs an explicit
policy decision; the investigation's warm acceptance does not settle it.

### USB interrupt-IN initial profile and failure retention

The implemented private [interrupt-IN path](devices/usb-interrupt-in.md) follows
the owner's narrower initial profile for
[Bluetooth task 3a](devices/ax200-bluetooth.md#accepted-interrupt-in-decisions): boot-present,
root-connected full-speed endpoints, with other profiles explicitly unsupported.
This leaves behind-hub periodic endpoints and other speeds unavailable to the
initial shared receive path, including HID consumers on those paths. Revisit
admission and periodic/TT handling when a selected device needs another profile,
with its descriptors and hardware evidence. Qualification currently covers
AX200 passthrough behind emulated xHCI, not native periodic transfers.

For the internal AX200 investigation, active removal may quarantine the whole
controller and stop unrelated storage, retaining backing until reboot. A STALL
is accepted as terminal interrupt-stream failure, with DMA backing and ring
identity retained until reboot and no automatic recovery. Thus a stalled stream
cannot resume during that boot, and a persistent receive makes controller-wide
active-removal handling the usual case. Revisit these accepted limits with
separately scoped endpoint/device retirement and periodic recovery before
expanding hotplug or recovery guarantees for HID consumers; confirmed halt alone
does not change the current retention contract.

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

## Random generator trust and availability

The [BSP-owned ChaCha20 generator](devices/random-generator.md) now seeds and
reseeds from the selected VirtIO or CPU source. The owner accepted the global
construction, demand-driven 40-byte seed/reseed policy and unchanged grant on
2026-10-08. The [matched qualification](development/experiments/random-generator/generator.md)
records the RFC vector, erasure observations, both natural reseed triggers,
TLS/cancellation and nested-KVM latency/throughput; native generator performance
and CPU seed supply under heavy load remain unmeasured.

Hardware/hypervisor trust remains. CPU boot/runtime checks detect specific
obvious failures, not arbitrary bias or malicious hardware; mixing one selected
source creates no independence or entropy certification. A required reseed that
cannot complete stops random reads and TLS operations needing new material until
a later full attempt succeeds. Permanent source failure disables them until
reboot. This accepted availability trade-off includes carry-clear exhaustion
under load; no stale-seed or predictable fallback is provided.

Request service still pays BSP scheduling and shares eight slots, including
unconsumed completions. State compromise exposes buffered/future output until a
successful independent reseed; completed slots and caller memory may retain
delivered bytes. Whole-VM snapshots/clones can duplicate initialized state.
Revisit independent source mixing, snapshot recovery, persistent seed or per-CPU
state only with a concrete threat model or measured consumer need. Revisit native
seed/performance qualification when owner hardware is available; keep trust
claims separate from the observed health and RFC checks.

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

## Lua build runtime limits

[Lua](userland/lua.md) supplies io/os, pure-Lua modules and native build helpers.
`file:setvbuf`, `io.popen`, `os.execute`, `os.clock`, `os.setlocale`, debug, full
math and dynamic modules remain absent. `os.time` accepts wall time only;
calendar-table conversion needs an explicit `mktime` policy for ambiguous and
nonexistent local input. Revisit each missing interface for a concrete consumer.

`os.tmpname` reserves a real exclusive empty file; callers must remove it.
`io.tmpfile` creates and immediately unlinks a real file, but abrupt death
between those operations, or a failed unlink, can leave a recognizable named
file. No stale-name cleanup runs. Revisit atomic anonymous creation only if a
concrete lifecycle need warrants a native operation; reserved names are not
ISO C `tmpnam`.

`pyxis.run` inherits live C streams, omitting closed ones. Cursors, append mode,
pushback and read-ahead belong to the parent runtime, not its delegated native
handle. Child file streams begin at zero, and unread buffered pipe bytes remain
in Lua. Lua `io.input`/`io.output` rebinding is local. Revisit shared stream state
only with a native ownership design, rather than silently forwarding private
buffer contents or copying stale startup bindings.

C-locale `strftime` supports standard conversions and E/O forms, but no width
or flag extensions. `%z` loses historical offset seconds by its standard minute
precision; `tm_gmtoff` retains them. A `tm_zone` designation is borrowed until
successful timezone-cache replacement or process exit. Locale selection and
reverse calendar conversion remain deferred.

## Sorted ls memory and live file details

Native [ls](userland/ls.md) collects all names for one directory before sorting.
Memory grows with the entry count and total name bytes; exhaustion reports
failure without a truncated listing. Terminal colors may add one file lookup
and at most two content bytes per non-program file; long format also queries
sizes. These later observations do not form a snapshot with enumeration and
can fail after names were collected. Unknown sizes remain explicit; script
classification falls back to regular-file color when its prefix is unreadable.

Terminal names use one printable ASCII cell per byte, replacing control and
non-ASCII bytes with `?`; plain file/pipe output preserves the original bytes.
Revisit memory or lookup costs when real directory workloads exhaust memory or
show unacceptable listing latency, and Unicode presentation when the terminal
has an agreed character-width contract. Owner-run ThinkPad and disk-backed
listing qualification remain unperformed; current evidence is nested QEMU with
archive, RAM and HOST directories.

## Native cp staging and recovery limits

[cp](userland/cp.md) uses exclusive sibling temporary files and held-directory
rename/removal. Accepted 2026-10-07: other writers must leave the temporary
file/name untouched until completion; native mutation APIs do not bind a name
to the held file identity. Source data remains live, copying its initial size;
concurrent overwrites can mix contents, and same-file aliases replace the object.
Staging requires destination CREATE/WRITE_FILES/REMOVE rather than permission to
write an existing file alone. It has no direct-truncation fallback.

Recursive directory copying remains deferred; cp currently accepts files only.
Revisit it with a bounded directory-tree copying contract when ordinary use
needs it.

Interruption can leave a named temporary file. Unconfirmed creation/publication
is reported without retry or name removal; failed cleanup can leave partial
storage. No stale-file sweeper or crash-durability guarantee is provided, and
operator cleanup must establish which names currently exist before removing
anything. Revisit reservation/publication primitives if cp must tolerate another
writer changing its temporary file/name, and cleanup policy when persistent
operational use needs recovery from interrupted copies. Provider sources,
native disk copies, durability and owner-run ThinkPad usage remain unqualified;
current measured evidence covers archive/RAM/HOST copying in nested QEMU.

## Bluetooth HCI connection handle reuse boundary

Accepted 2026-10-08 for [runtime HCI task 2](devices/bluetooth-hci.md): fail closed
if the controller reuses a previously disconnected connection handle. Independent
event and ACL endpoint ordering cannot establish which link delayed bytes belong
to; a new generation alone is insufficient. Known retired-link ACL is discarded
without hiding the disconnect event or independently disabling storage.

This restricts repeated connections during one controller lifetime. Establish
and measure a safe retirement/reuse boundary in the connection/reconnect tasks
before durable bonded reconnect can qualify. It does not relax native closure.

## Bluetooth runtime re-grant after radio work

The current [HCI adapter](devices/bluetooth-hci.md#progress-and-failure) sets a
sticky dirty flag on non-read-only command publication, connection admission or
ACL publication. Release/exit then leaves Bluetooth unavailable until reboot,
even when links and credits later settle. Only fully accounted read-only
sessions have a confirmed clean re-grant path. This implements conservative
cleanup under the accepted exclusive controller lifetime; it is not measured
radio cleanup or automatic service recovery.

Revisit with the service/connection tasks when they can establish and qualify
explicit radio-procedure termination, receive continuity and independent USB
accounting. The accepted handle-reuse boundary is also required for reconnect.


## HD Audio jack routing at playback start

Accepted **2026-10-09** in the [native task 5 plan](wip/hda-native.md): sample
headphone presence when the physical playback engine starts, then hold that
route until its next start. This is planned behavior, not native qualification.
**Headphones inserted mid-playback keep the speaker until the next start.**
Unplugging headphones also retains the headphone route until then. A new session
or WRITE while other sessions keep the engine running does not resample presence.
There is no live speaker automute or unsolicited jack-response path in task 5.

Live switching is a separate follow-up task requiring reviewed RIRB/IRQ reception,
tag/command correlation, refill-safe routing and discontinuity policy. Revisit
after native one/eight-session qualification; it does not block task 5's accepted
start-time routing batch. Public grant/session ownership remains unchanged.

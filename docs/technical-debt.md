# Technical debt and implementation tradeoffs

Record concrete limitations of implemented choices here: what we chose, its
cost, and when to reconsider it. This is a working record, not a roadmap or a
commitment to replace every simple implementation. Remove or update entries
when the underlying tradeoff changes.

## Remote transfer memory and staging limits

[Explicit remote transfers](userland/remote-terminal.md#explicit-file-transfer) stream with constant memory and no size limit
(owner decision, 2026-10-08) and read the source twice because the digest is announced first. Data moves in a 64 KiB window of
2 KiB chunks ([measurements](development/experiments/xfer-pipelining/README.md)); native rates after windowing await the owner's
batch. Before it, 15 MiB took 6.5 s up and 7.5 s down natively. In nested QEMU both directions are bound by BSP work per frame.
Guest names are limited to 200 UTF-8 bytes and host paths to 1024.

The mandatory SHA-256 extension excludes stock kitty peers, and transfers are single regular files without resume, compression or
deltas. Exclusive `.NAME.xfer-partial-ID` siblings can survive abrupt death and hold a partial file of any size, which matters on a
USB stick; stale files are never deleted automatically and manual review owns removal. The macOS exclusive-rename path still needs
an owner run. Revisit staging recovery if manual cleanup becomes burdensome, with explicit ownership rules rather than age-based
deletion.

## Regex character classes and back-references

The [libc regex interface](userland/libc-portability.md#regular-expressions-and-utf-8-conversion) decodes UTF-8 but classifies and
folds only ASCII, so Unicode class and non-ASCII case-insensitive searches are incomplete. Owner decision, 2026-10-07: keep the
pinned musl 1.2.5 TRE matcher behavior. BRE back-references stay bytewise under `REG_ICASE`, the backtracking path assumes
single-byte lookahead and does not fully restore variable-width decoder state (UTF-8 back-reference matches and offsets are
unreliable), invalid subject UTF-8 returns `REG_NOMATCH` when reached with no whole-string validation, and collating symbols and
equivalence classes are unsupported. Revisit Unicode tables, locale policy or the engine when a consumer needs them; vi and less
keep the documented limits.

## Remote drop prompt tracking

[Host file drops](userland/remote-terminal.md#dropping-a-host-file) need a known empty root-shell prompt, but OSC 133;B and command
completion do not acknowledge consumption of host input. The client therefore latches uncertainty when it forwards input while a
command is pending: drops stay normal pastes until reconnection (including after an interactive editor), and canceling an edited
line can suppress detection until a later completed command. This trades missed upload offers for never injecting commands into
running programs. Detection supports printable ASCII host paths only, refuses final symlinks to match `xfer`, and falls back to
text for quoted commands too large for the editor. Revisit when a native input-acknowledgment contract can order prompt and input,
and with Unicode command editing or a supported argument-delivery interface.

## SMT placement and later balancing

[Placement](kernel/smp.md#placement-and-migration) prefers idle siblings only between equally loaded logical CPUs; the preemption
push and idle pull still balance logical load and do not guarantee one compute task per core (QEMU showed a push moving a task onto
an occupied sibling while another core idled). The [measurement record](development/experiments/core-placement/README.md) has
the observation and the owner-run native gain (heap ×4 wall 2.352 s to 1.389 s). Revisit topology-aware balancing if owner-run
workloads show later moves erase the benefit. CPUID-unavailable CPUs, older AMD compute-unit encodings and AMD
non-power-of-two thread counts stay isolated until a supported target needs them.

## Retained userspace heap pools

Libc's TLSF allocator reuses freed blocks but keeps every backing pool (at least 64 KiB, no fixed pool-count registry) until process
exit, so a short-lived peak leaves memory mapped for that process; shrinking `realloc` keeps the block capacity and growth may hold
both blocks while copying to preserve 16-byte alignment. Process destruction reclaims everything. Revisit empty-pool release and
in-place aligned growth when long-lived applications make retained capacity or copying material. The allocator and errno assume one
thread per process: add synchronization and thread-local errno with userspace threads.

## Allocation measurement follow-ups

The [allocation benchmark and caller-scoped profile](development/allocation-profiling.md) separate warm heap throughput, heap
expansion and private-page requests (the instrumentation reads HPET and perturbs timings; report accelerator, CPU count, live set
and host context). Standalone `kmalloc`/`kfree` throughput and deeper PMM/VM timing are unmeasured, and pool growth counters
describe backing acquired in a window, not retained memory or fragmentation. Private memory runs in the caller's syscall
([memory](kernel/memory.md#execution)); measure queue and worker costs of the remaining executor services, including HOST
forwarding, before changing allocation policy.

## I/O baseline attribution and coverage

The [I/O/IPC baselines](development/io-ipc-baselines.md) measure elapsed workload boundaries in nested KVM. HOST profiling splits
guest queues, worker service and transport but strongly perturbs the workload (next entry), and transport still mixes
device/daemon/backing service with guest/host scheduling. Host write/sync baselines used tmpfs, so they say nothing about
physical-disk durability cost. Short native and SEND intervals are close to clock overhead and need a longer-batch or
scoped-instrumentation contract. Owner-host results, capability attachment cost, cross-space contention, mixed-workload fairness
and per-process CPU accounting are unmeasured, and the final combined IPC/HTTP/RAM/HOST matrix was deferred when the reliability
milestone closed. Gather coverage before deployment-capacity or fine-grained performance claims, and revisit host attribution
resolution before changing batching or transfer limits.

## Host FILE profiling perturbation

Full HOST profiling strongly perturbs execution: in the nested-KVM groups profiled medians were 13.7–16.1 times their controls, with
the initial BSP queue wait 73.8–78.8% of profiled transfer time ([attribution matrix](development/io-reliability-attribution.md#host-profiling-and-attribution-limits)).
The [controlled experiment](development/experiments/host-profile-slowdown/README.md) reproduced a 14.9× slowdown on prepared
writes, cut to 2.0× by explicit BSP notification, and initial HOST publication now uses the common executor's synchronized idle
notification ([validation](development/io-reliability-attribution.md#host-publication-notification)). The residual combines
timestamp work with unresolved scheduling and transport observation effects, so no constant correction or normal-workload phase
partition is justified. Keep unprofiled controls in comparisons; no permanent counts-only mode or clock change is accepted.
Host-side component timing and durable storage (fixtures are tmpfs with sync off) are separate work.

## Fixed userspace stacks

Plain programs eagerly back [1 MiB stacks](kernel/program-loading.md); validated
[bundles](wip/program-bundles.md) may request up to 8 MiB. The reserved guard
catches ordinary overruns, but a large adjustment can skip it; compiler stack
probing and automatic growth remain absent. Revisit with compiler/runtime and
thread-stack work, retaining the owner's small plain-program default.

## BSP-only allocation and VM mutation

Private memory runs in the caller's syscall (since SMP task 7a) and the heap, physical allocator and scratch mappings are safe on any
CPU, but other allocating services still run on the BSP through the [common executor](../kernel/service/request.c) (a closed
service catalog and one FIFO; see the [request contract](kernel/bsp-service-requests.md)). Serial services, deferred destruction,
worker relocation and shared kernel mapping reuse stay BSP-owned and need their own handoff and invalidation contracts
([follow-ups](wip/scheduling-and-threads.md#serial-services-off-the-bsp)). Long non-preemptible service operations delay other
requests and BSP work, and each user task owns one reusable request allocation plus a caller-only profiling allocation.

The handoff ordering is correctness: display requests are published only after the requester left its task stack and private
address space, resumption reloads CR3 before returning to the task stack, and a wake arriving before a task finishes parking records
a notification without making the running context runnable elsewhere. Preserve these or replace them with an equally defined
ownership and invalidation contract. Revisit when BSP service latency is material or before concurrent use of one private address
space.

## PMM first-fit search under its lock

`pmm_alloc()` searches first fit under the PMM lock, skipping fully allocated 64-bit words and starting at a hint below which every
frame is unavailable; this ended the serialization measured in 7a (lock wait about 150 cycles per call with two page clients,
[record](development/experiments/smp-task7-pmm/README.md#pmm-lock)). Search time can still grow with fragmentation, and `vm_back()`
and heap growth take the lock once per frame. Revisit if a workload shows PMM lock waiting again; batching frames per call is the
next step.

## Scratch-slot false sharing

Each CPU's two scratch slots are adjacent PTEs, so the slots of CPUs 0–3, 4–7 and so on share a 64-byte cache line of the scratch
page table, and the `scratch_busy` flags share lines too; every mapped page uses the slots about a dozen times. In the nested VM two
concurrent page clients each took about 1.9 times as long as one alone (1.4 times with a throwaway build giving each CPU its own
line, [record](development/experiments/smp-task7-pmm/README.md#remaining-concurrency-cost-scratch-slot-false-sharing)). Natively
the cost is small: two clients took 1.15–1.25 times and eight up to 1.5 times ([task 8](development/experiments/smp-task8/README.md#native-thinkpad-check-owner-run)).

Options: spread the slots one line per CPU (an 8 MiB scratch window instead of 2 MiB for all 256 xAPIC IDs, moving the APIC, I/O APIC
and HPET mappings), or walk the active address space through the recursive mapping, which removes most slot use. The owner prefers
the recursive walk (2026-10-06) as a later optimization; frame zeroing would still need a slot or a change to the
"zeroed before mapping" rule. Copy-on-write zero pages were set aside. After topology-aware placement (#451) native page batches
looked slower per client than the task-8 record (two clients 0.148–0.149 s against 0.105–0.115 s) across sessions and older main,
so this is unmeasured; start a revisit with a same-sitting native A/B against main.

## RAM-file page access cost

RAM-file reads and overwrites map each 4 KiB page through the calling CPU's scratch slot, so in nested QEMU they run about 2.1–2.4x
slower than the old single-buffer copies, at 2.4–3 GB/s ([measurements](development/experiments/ram-file-pages/README.md)).
Revisit with a consumer bound by RAM-file reads, for example by mapping runs of pages or a kernel direct map.

## Never-reused kernel heap arena

Kernel heap pools come from a 256 GiB arena whose addresses are never reused, so publishing a pool needs no TLB shootdown and pools
are never removed: freed memory stays with the heap and the arena bounds every pool plus every retired page for the boot. Growth
zeroes and maps its pool with interrupts disabled under the growth lock, and a growth that loses frames to another CPU retires what
it mapped. RAM files keep their data in PMM frames since 2026-10-09 (the heap holds only their page index), and no consumer
approaches the arena. Revisit when `heap_stats` shows arena use or retired bytes growing, or growth latency becomes material.

## BSP userspace and kernel workers

Since SMP task 7b, user tasks run on the BSP beside the kernel workers (presentation, BSP request executor, network, native
filesystem, USB, ACPI). Ties go to the APs first and the BSP pulls only while none of its workers is runnable; a woken worker preempts
a BSP user task at the next interrupt, but there are no priorities and user syscalls run with interrupts masked, so a long BSP user
syscall (a large private allocation zeroing its pages) delays workers and device interrupts. With all four CPUs loaded in the nested
VM ttcp and RAM-file writes stayed within their spread ([7b record](development/experiments/smp-task7b/README.md)), and the SMP
task 8 native check saw no dropped keystrokes or frames under load ([record](development/experiments/smp-task8/README.md#native-thinkpad-check-owner-run)),
though display smoothness and input latency were judged by feel, not measured. Revisit if interactive use shows worker or
presentation latency under load: exclude the BSP by policy, add worker priority, or make long syscalls preemptible.

## ACPI interpreter host limits

The [ACPI host interface](kernel/acpi.md) makes these choices:

- **PCI configuration writes are refused**; AML needing them fails the access and logs function, offset and value. A write path would
  need an ownership rule for driver-owned functions; revisit if a machine logs refusals.
- **The 64 MiB firmware mapping window is never reused** and an operation region is mapped whole (QEMU uses 14 pages, the ThinkPad
  2,127, 8.3 MiB). Revisit if a machine fills it.
- **AML is trusted with hardware**: any I/O port including legacy PCI configuration ports, and device-memory mappings that can alias
  registers the kernel owns (HPET, APICs). Revisit if a firmware access interferes with a driver.
- **The SCI must share the keyboard's I/O APIC**; without a PS/2 route ACPI events are unavailable.
- **Waiting for deferred work runs it inline**, and nothing removes GPE handlers. Revisit when notifications add handlers that wait.

## Power-off and restart limits

[Power-off and restart](kernel/acpi.md#power-off-and-restart) implement the agreed first version of "clean" (user tasks held, pools
flushed). Accepted limits:

- No orderly stop of programs or services (revisit with service supervision); held tasks ignore stop requests until release.
- Failed pools are skipped (their unsynced changes are lost, as with the power button), and one flush failure keeps the system up with
  no forced power-off command.
- Raw disk handles are not flushed; the installer flushes its own writes.
- Control-method power buttons (`Notify(PNP0C0C, 0x80)`) are ignored; the T14 and QEMU use the fixed event. Revisit on such a machine.
- A failed S5 entry freezes the BSP for 10 s (uACPI waits with interrupts disabled).
- The flush-failure, firmware-failure and reset-fallback paths were checked by code inspection only.
- **A button press or `poweroff` during an Update powers off at once without asking**, while the installer rewrites a stick's ESP
  through unflushed raw writes, which could leave the stick unbootable. The installer could hold off power operations while it
  writes; revisit with installer work.

## Embedded controller and battery limits

The [embedded controller and battery](kernel/acpi.md#embedded-controller-and-battery) reader is the smallest that serves the space-bar
widget:

- EC transactions busy-wait on the BSP (up to 500 ms per byte; ThinkPad polls took 1.8–8.3 ms after a first poll of 13.4 ms), so a
  slow EC turns the worker's time slices into polling. Revisit if presentation stutters every five seconds; the EC's GPE could wake
  the worker instead.
- The ACPI global lock is not taken (uACPI's is not recursive and AML may hold it); a `_GLK` request is logged and the T14 has none.
- One controller, one GPE number: only the ECDT or the first `PNP0C09` device, and a `_GPE` naming a GPE block device is refused.
- Two batteries and one adapter; more are ignored and batteries with different power units would give a wrong percentage.
- Polled, not notified: changes appear within five seconds, full capacity is reread only when a battery reappears, and `_Qxx`
  notifications only reach the trace log. Revisit with [ACPI notifications](wip/later-os-directions.md#power-and-acpi), which also
  rereads capacity and cycle count on `Notify(0x81)`.

## Synchronous launch preparation

Each in-flight launch reserves a full 64 KiB metadata capture buffer plus a small header from the kernel heap even for short argument
lists, and the BSP prepares the child with interrupts disabled. The executable file's operation ownership serializes reads, writes and
resizes through image validation and loading, which avoids another whole-image copy but lets large images delay both BSP work and
callers using that file. Revisit staging size and preparation scheduling when larger applications or concurrent launches make this
material; a snapshot or immutable backing could shorten file ownership at a memory and complexity cost.

## Initial terminal editor

Libterm redraws the full visible line on each edit or cursor move and reads one input byte per call, keeping cursor and scroll
behavior explicit and not holding keystrokes a foreground child needs, at a syscall and rendering cost. It assumes exclusive output
use of the space's terminal, accepts only one-cell ASCII and keeps the prompt, line and cursor on screen; Unicode widths and
larger-line viewports are not implemented ([terminal contract](userland/terminal.md)). Revisit changed-span rendering or input
buffering when interactive workloads make the cost material.

[Saved shell history](userland/shell.md#commands-and-quoting) rewrites `home://.history` (up to 64 KiB) on every recorded line. Two
shells saving within milliseconds can lose a line, a killed save can leave a `.history.HEX` file that nothing removes, running shells
don't see each other's new lines, and the file is shared by every space on that home. Revisit if a line loss is noticed in use or the
per-command cost shows natively ([measurements](development/experiments/shell-history/README.md)). Ctrl+R search is deferred.

## Presenter-drawn block cursor

The presenter draws the [block cursor](userland/terminal.md) by recoloring the cursor cell's pixels: the top-left pixel stands for the
cell background (taking the scheme's `cursor` color) and every other pixel takes `cursor_text`. The TTY keeps no cell grid, so a cell
whose top-left pixel belongs to the glyph (blocks, box drawing) inverts the wrong way, and a font with more than two colors per cell
would break the rule. Revisit by keeping a TTY cell grid (character, foreground, background, style) and rendering the cursor cell
with the scheme's colors.

## Early console and post-handoff panics

The [early console and display panic path](kernel/early-console.md#panic-ownership) writes directly to the boot framebuffer before and
after presenter handoff. Taking over another CPU's writer uses a bounded poll count without a clock, GS, locks or scheduler progress,
so a preempted or delayed BSP can exceed it; reporting is then serial-only and normal screen writes stay stopped. Framebuffer faults
during reset or drawing also revoke screen output, and VirtIO panic output stays serial-only (no emergency reset, queue or dedicated
frame). A serial port that stops accepting output stays latched off for the boot so a stuck port cannot block early boot or panic
output. Revisit stronger CPU-stop and takeover coordination with measured native failures, and the retry policy when reliable late
serial recovery is needed.

## Space-layer qualification

The [space-layer milestone](userland/space-layers.md) closed on 2026-10-08 with nested QEMU qualification, and the owner ran the native
Quake check the same day (ThinkPad PXE, main `4332801`, 1920x1080, AC): Super+Down/Up, continued game time and terminal output, held
input, switching away and back, queued-text clearing and hidden-layer Ctrl+C behaved as specified, and `timedemo demo1` ran at 684.5
and 684.7 fps. Acquisition without PRESENT, repeated PRESENT while hidden, capture surviving DISPLAY_RELEASE, shortcut releases after
Super and device/queue-loss propagation were source-inspected only; revisit them when changing the session, input or teardown paths or
when a failure appears.

## Native Renoir presentation qualification

The [read-only Renoir observer](kernel/display.md#read-only-renoir-firmware-timing)
and guarded blank-start copy path implement presentation step 2, accepted
2026-10-09. Timed copies stay **off by default**: ordinary boots observe with
unsynchronized copies, and `display.timing=blank` is an explicit qualification
opt-in. No native Pyxis execution is claimed. The owner must run the
[ThinkPad batch](development/experiments/renoir-presentation/README.md#native-thinkpad-batch):
counter/mode capture, actual first-store start distributions and margins,
copy/fence and representative prefix progress, plus matched moving-Quake camera
clips. Revisit default enablement only after those results pass the measured
eight-line guard and visibility/input/panic checks; insufficient evidence leaves
unsynchronized presentation. Program timing APIs and page flips remain separate.

Register-window identity bounds access without PCI sizing writes; native BAR
allocation length and GOP/HUBP routing are not independently decoded. Only the
single unclaimed `1002:1636`, progressive fixed-timing output is supported.
Other GPUs, multiple active OTGs, power/clock changes or stalled counters report
unavailable rather than wake/modeset hardware. Sparse requalification has a
conservative phase bound which may prevent timed copies until a fresh boot's
dense calibration. CPU-issued prefix timestamps and a final WC fence do not
prove each row's display-fetch visibility. Revisit broader discovery/precision
with concrete native evidence; do not treat the Linux private-buffer model or
QEMU costs as native qualification.

## Native system pointer qualification

The owner accepted native PS/2 deferral on 2026-10-08 and QEMU checks on boot, Bochs and VirtIO displays closed the
[system pointer](interfaces/pointer.md); this native entry stays open. Owner ThinkPad batches on 2026-10-09 (PXE, PS/2 touchpad and
TrackPoint, 1920x1080 boot framebuffer) showed ordinary motion and buttons, tab clicks, text selection, multiplexer selection and
wheel, Super+Esc unlock and click-to-relock in Quake, Super+Up/Down layers with the cursor shown, and the redrawn default arrow and
I-beam all working. **Still unchecked natively:** program cursor image, hotspot, show/hide and bounded warp, and cursor cost samples.
Nested-VM results do not establish native input latency or display performance. Check those items in a later owner ThinkPad batch,
recording revisions, boot/display/device configuration, behavior and cost samples.


An additional ThinkPad owner report on 2026-10-09 found that a plain click selected a cell (no boot revision supplied).
The owner confirmed the merged [click/drag correction](development/system-pointer-qualification.md#click-and-drag-selection)
natively in the boot log, raw terminals and mux on 2026-10-09; that correction no longer needs a native recheck.

## System pointer selection and input limits

[Terminal clipboard](interfaces/clipboard.md) exports completed visible-cell selections as printable ASCII; non-ASCII glyphs,
original tabs, soft wraps and intentional trailing spaces have the [initial delivery limits](#initial-clipboard-delivery-limits).
Mux drag autoscroll and selection across off-view history are absent, and kernel terminals keep visible cells without scrollback. PS/2 is the only
pointer source, with raw counts and relative-mode Synaptics behavior; Bluetooth aggregation, USB HID, acceleration, absolute-mode
scrolling, remote pointer transport and multiple-display composition are separate tracks. The current PS/2 reset hook alone does not
implement the accepted conditional multi-source rules: revisit input routing through the
[pointer source boundary](interfaces/pointer.md#input-source-coordination) when another trusted source is integrated, so independent
masks never release a surviving source's held buttons.

## VirtIO cursor frontend limits

The owner accepted [GTK on X11, relative PS/2 and unscaled 1:1 committed guest geometry](development/qemu.md#hardware-pointer-frontend)
for hardware-pointer qualification (2026-10-08). Source inspection found a no-op native Wayland position warp, scaled and centered GTK
placement differing from the input transform, different SDL channel packing and no VNC position callback, so guest completion and
cursor-inclusive capture do not establish correct placement or colors on other frontends. GTK fit mode may briefly scale during
resizing (qualify with committed dimensions matching GTK content at 1:1), and QEMU 10.2.2's zero-length used completion confirms
command-buffer consumption, not cursor application or scanout timing. The kernel carries no frontend-specific switches and boot and
Bochs keep software composition ([qualification](development/system-pointer-qualification.md#task-4-hardware-qualification)). Revisit
on the next emulator or frontend upgrade, or when broader support is requested, with matched shape, hotspot, position, clipping,
alpha and resize checks.

## Unselected graphical applications

The owner chose to keep Quake, Doom, Mandelbrot and `mousetest` running without input focus. An unattended game in an unselected space
or hidden graphics layer can use a CPU indefinitely and keeps writing its mapped pixels although the presenter copies only the selected
space's chosen surface; programs may pause themselves explicitly. Selecting a space sets no scheduler budget and suspends nothing.
Revisit resource budgets or application-specific idle behavior when concurrent graphical workloads make this a practical problem.

## VirtIO GPU resize limits and runtime retention

The [2D driver](interfaces/graphics.md#live-destination-geometry) handles selected-output size changes with full-frame transfer and
flush. Boot and Bochs geometry is fixed. Acquired application mappings keep their layout (presentation clips them and fills exposed
margins); geometry wakeups and explicit mapping replacement support libterm, Kilo, Mandelbrot and Doom, while other applications keep
their acquired geometry until they adapt. Kilo keeps a two-column, three-row minimum, and adaptive libterm reads need clock READ
authority. There is no vblank guarantee or 3D. Revisit with concrete consumers and with full-frame idle-cost measurements at the new
geometries.

After a runtime driver failure, acquisition, presentation and size queries return unavailable (release still works) and the last screen
may stay stale or blank; a selected VirtIO GPU's failure does not fall back to a separate VGA framebuffer, and VirtIO panic output
stays serial-only. A successful resize frees old GPU backing only after confirmed fenced detach and unreference, whereas runtime failure,
an uncertain bootstrap shutdown, or a PCI claim that ever enabled DMA retains backing, queue and control storage and mappings until
reboot. Revisit failover only with a multi-device policy, and recovery only with a device teardown and reconnect ownership contract.

Old AP-visible TTY buffers use [quiescent acknowledged TLB retirement](kernel/smp.md#memory-and-output-boundaries). A missing
acknowledgement (one one-second deadline, not an ABI guarantee) retains one mapped old TTY/navigation/cursor batch until reboot and
disables further resizing, though the new display keeps working; an eventual acknowledgement alone does not free it. Resize also costs a
whole old/new pixel pair per space, and every CPU's console and kernel-log writes wait on the global output lock during the copy
(0.45–4.39 ms in four-TTY nested-KVM measurements). Revisit with a recovery protocol and measured copy durations.

## Display SUBMIT round trip

Each [SUBMIT](interfaces/graphics.md#slots-and-frame-handoff) is a synchronous BSP request, like every display operation. In nested
KVM it adds 0.11–0.29 ms per frame, so Quake's timedemo runs 20–30% slower; ordinary 72 fps play spends about 1–2% of a CPU
([measurements](development/experiments/frame-handoff/README.md)). Revisit after the native run: a SUBMIT that doesn't go through the
BSP needs its own ownership decision for the session's slot state.

## Screen capture memory and consistency limits

[Screen capture](interfaces/screen-capture.md) admits one pending request, but completed immutable FILEs have ordinary reference
lifetimes and no quota: each retained snapshot costs `4 * width * height` bytes, so a caller can exhaust memory by retaining several.
Revisit admission and retained-image policy with a concrete pressure workload and an authority and lifetime contract. Capture backing
starts uninitialized and relies on the presenter's full repaint to fill every visible pixel, so damage tracking must force full
composition for a pending capture ([frame contract](interfaces/screen-capture.md#frame-boundary-and-lifetime)). Captured bytes freeze one
composition of whole submitted application frames; no vblank or scanout timing is promised, and a stuck
presenter or panic cannot complete a capture. Revisit stronger consistency or bounded recovery with a separate presenter and backing
ownership contract. The [qualification report](development/screenshot-qualification.md) covers QEMU boot-framebuffer, Bochs and VirtIO
comparisons, shown-layer and resize coverage, retained snapshots and BUSY admission; failure cleanup is source-reviewed only.

## Bochs boot-mode scope and aperture retention

The [Bochs driver](kernel/display.md#bochs) supports QEMU's modern register interface with an already enabled firmware DISPI mode. It
cannot restore legacy VGA state from DISPI registers, so disabled modes, GETCAPS state and older interfaces keep firmware output without
mode writes, and a boot framebuffer not starting at BAR0 refuses mode setting rather than rebasing a published mapping. Revisit only
with a concrete device or firmware profile. The prepared WC aperture and PCI register mapping and claim stay until reboot (even after
mode refusal) because they borrow boot leaves outside generic PCI/VM release ownership and protect panic and fallback targets;
reclamation needs a shared-mapping lifetime contract. A failed mode with unverified firmware restoration stops boot with a serial panic.

## Reverse remote terminal discovery

[Reverse connections](userland/remote-terminal.md#reverse-connections) are unauthenticated and unencrypted: a matching non-secret name
selects a host, which then receives the configured shell's authority. The owner accepted this for a trusted development LAN; revisit
with authentication or untrusted networks. Discovery owns UDP port 2324 on net0 exclusively while waiting (a conflicting binding stops
that daemon with a diagnostic), one reverse session runs at a time, and the host tool accepts one per invocation. Beacon cadence adds up
to about a second after discovery opens plus network, scheduling and cleanup delays. Reverse discovery and log following passed the
owner's ThinkPad PXE check; macOS listener behavior is unqualified ([qualification](development/remote-debugging.md#qualification)).
Revisit if a multi-host or unattended workflow needs more.

## Kernel log retention and LAN visibility

The [kernel log](interfaces/kernel-log.md) keeps 256 KiB in static storage, evicting whole oldest lines and discarding oversized ones;
it is volatile and following polls every 100 ms. Reads locate both cursors by walking retained length headers under the ring lock with
interrupts disabled, so logging on other CPUs waits for the walk. Revisit capacity, event-driven following, durable capture or a cached
line index with measured native workloads.

Exposure, accepted by the owner for bring-up on the trusted development LAN: every configured space (including remote shells) gets
read-only log authority, so kernel addresses are readable through the unauthenticated remote terminal; and the packaged
[capture policy](userland/init.md#boot-configuration) gives live Development, installed `pyxis` and Remote whole-screen CAPTURE, so any
program in a granted space, and anyone reaching the remote terminal, can read what any space shows ([screen capture](interfaces/screen-capture.md)).
Revisit default grants, disclosure and capture delegation with user isolation and authentication, or an untrusted network.

Capture is best effort. A fatal interruption of a ring lock owner skips retention, and userspace readers need a working scheduler. Opt-in
kernel UDP capture bypasses those locks but can lose fatal text on early panics without an active NIC, interrupted activation or reset,
failed bounded CPU handoff, carrier loss or stalled DMA; failed completion retains buffers until reboot, there is no retransmission or
persistence, a late receiver misses earlier datagrams, and enabled logging broadcasts kernel addresses and cuts ordinary TX capacity to
15/16 VirtIO or 30/32 RTL8111 descriptors. The ThinkPad's panic and disabled checks passed ([#476](https://git.internal/PyxisOS/pyxis-os/pulls/476))
and a plain enabled boot delivered the whole boot log natively (main `fcf142e`, 2026-10-07, [#480](https://git.internal/PyxisOS/pyxis-os/pulls/480));
RTL's ambiguous-slot duplication and stalled-NIC abandonment are code-inspected only. Revisit those paths, polling budgets and capacity
with recovery or authentication requirements; checked NIC completion cannot guarantee host delivery.

## PS/2 scan-set query compatibility

The ThinkPad ACKs set-2 selection and its query but supplies no set-ID byte, so [keyboard setup](devices/keyboard.md) accepts an absent
ID after a short monotonic wait, keeps wrong IDs and controller errors fatal, and drains queued output before enabling scanning. This
relies on the ACKed selection producing untranslated set 2, native character, modifier and extended-key qualification remains
required, and a drain cannot identify firmware replies delayed past scan start (the expected late `02` maps to no key). Revisit if
native input disproves the selection or a controller supplies delayed contradictory output; a translated set-1 decoder is a separate
compatibility decision.

## Process termination and Ctrl-C

Process handles are non-owning observers. Launch grants WAIT and TERMINATE, TERMINATE stops just that process through the per-task safe
stop, and [execution-group CONTROL](interfaces/execution-groups.md) permits whole-group termination including blocked-operation unwind.
The [shell terminates its foreground job on Ctrl+C](userland/shell.md#interrupting-foreground-commands) stage by stage, immediately and
without a cooperative interrupt ([design](userland/foreground-interruption.md)). Accepted limits of this first slice:

- **Descendants:** only the shell's direct children die; processes a stage launched keep running (opted-in spaces delegate LAUNCH to
  foreground commands, so `pyxis.run` children can outlive interrupted Lua). Remote execution groups have their own group-wide policy.
- **Background jobs** cannot be interrupted and there is no job control; **passthrough holders** cannot be interrupted while held (locally
  only ending the session recovers); a **nested interactive shell** cannot arm, so the outer Ctrl+C ends the whole inner shell.
- **Remote typeahead:** after more than 4 KiB the command does not read, the server stops reading frames until its injection drains, so a
  later Ctrl+C never reaches the kernel (Ctrl+] remains). An out-of-band interrupt from the server would be needed.
- **Startup scripts** hold the right, so Ctrl+C can terminate their foreground command and the script then never starts its session,
  leaving the space without a shell until reboot; no current script runs a foreground command.

Revisit with cooperative interrupts, job control or an explicit descendant lifetime design; native cancellation need not require POSIX
signals.

## Console input completion

Framebuffer console input has no EOF operation. EOF-driven consumers (cksum, tee, wc, sort, tail) need finite file or pipe input or an
[independent terminal session](userland/terminal-sessions.md), whose attachment can end input and whose zero-byte read libc accepts as
EOF; Ctrl-D remains an application-interpreted byte. Revisit completion and its interaction with line editing when interactive
EOF-driven tools are explicitly in scope; ports keep upstream behavior and add no terminal controls or signal handling.

## Readiness scaling

`wait_many` has a general 32-interest bound, with per-call copies and reference retention, about 40 bytes of kernel-stack arrays per
interest, and interests kept in the task's reserved request area; the readiness worker rescans all interests on notification, so large
connection sets make these costs material. A persistent wait-set object (register once, fail at registration, return ready entries
without a per-call input array, epoll/kqueue style) is a proposed later direction, for example for a many-connection web server;
authority, lifetime, capacity and failure rules are open and nothing is agreed, scheduled or added.

## Initial terminal multiplexer limits

The [multiplexer](userland/multiplexer.md) has one window and up to eight panes. Its native terminal subset has no alternate screen,
Unicode widths or detach, and full-screen applications reuse the shell's screen. History keeps 1,024 scrolled-off rows with no reflow or
erased-screen archive; rows cropped by resize are not inserted into history (retaining them is a proposed follow-up), presentation crops
at the session maximum and history storage keeps the largest width seen. Steady text storage is about 2.5 MiB per pane (resize can
briefly double one pane's), with no per-group CPU or memory quota.

A full pane input queue can hold a prefix behind already staged input, since bounded lossless storage cannot bypass a pending paste.
Confirmed closure waits for group cleanup and output EOF, so published HOST work can delay cleanup indefinitely and output grants
delegated outside the group can delay EOF; mux exit or fault requests termination through final controlling-grant closure without
waiting for cleanup. Local keyboard and graphics grants are shared space facilities, so graphical launches share the existing
one-session ownership. Live pane resize depends on the consumer: shell and Kilo observe RESIZED, Links reads geometry once, and vi and
less keep their behavior (revisit with a focused port change). Revisit windows, ratio adjustment, input recovery and quotas when
selected.

## Initial independent terminal limits

[Terminal sessions](userland/terminal-sessions.md) have explicit resize, 4 KiB input and 64 KiB output queues and one attachment.
Creation has no per-space quota (a trusted creator can allocate bounded sessions until allocation fails), and output backpressure has no
deadline, so a controller that stops draining can block writers (hangup wakes terminal calls but does not stop CPU-bound code).
Execution groups supervise separately; the [remote server](userland/remote-terminal.md) bounds admission at four, abandons closing output
after five seconds, and can still wait indefinitely on published HOST work while holding a slot. Four idle or blocked sessions can
exhaust the server, and there is no idle timeout, authentication, encryption, restart or reconnection; host-loopback forwarding limits the
QEMU entry point, but any process reaching it gets the configured shell privileges, with shared roots, space and CPU and no separate
principals or quotas. Live/PXE Remote explicitly enables power: any reachable LAN peer can reboot or power off the machine, an owner-accepted
sole-user home-LAN exposure; installed remote defaults omit it. Revisit power delegation with authentication or a broader deployment.
Address changes invalidate the listener without automatic rebinding. The interactive host renderer shows one `?`
cell for non-ASCII bytes (machine mode preserves data). A full client queue delays reading Ctrl+] behind a paste (close acknowledgment
then bounded at five seconds), a full guest queue likewise holds back Ctrl+C
([process termination](#process-termination-and-ctrl-c)), and host SIGINT/SIGTERM forces disconnect. Revisit admission, authentication and
presentation breadth with a non-development deployment or text consumer; remote resize negotiation and reconnect are separate work.

## Libc compatibility gaps

The [descriptor portability slice](userland/libc-portability.md) supplies open/read/write/close and `lseek`; public O_RDWR and
exclusive O_CREAT|O_EXCL use native constructs ([qualification](userland/libc-portability.md#readwrite-and-exclusive-create-qualification)),
and `fileno` exposes a stream's existing descriptor (agreed 2026-10-08 for the [SDL2 port](development/sdl2.md)). `fdopen` and duplication
are absent: a consumer needs a separately agreed extension, and duplication must first settle shared open-state and cursor ownership.
Descriptor inheritance and cross-process shared offsets are not supplied by the startup-stream grants.

Signals are absent, so tee rejects -i and broken pipes report EPIPE without SIGPIPE (a successful no-op handler would misrepresent
support). Public O_APPEND is absent, so tee rejects -a, and atomic append needs a native operation
([stdio append](#non-atomic-stdio-append)). Polling and nonblocking descriptor I/O, fork/exec-style semantics, buffered output,
buffering controls and wide I/O are outside the slice; pushback is one byte per FILE, scanning covers narrow conversions only and
fixed-width inttypes input (SCN) macros are absent. Revisit only for a concrete consumer, defining the native blocking and lifetime
behavior it needs; no successful placeholder APIs exist.

## Public open creation mode

O_CREAT accepts only mode 0666 as a request for native creation policy; it installs no Unix permissions, ownership or authority, and
other modes (including restrictive ones such as 0600, and opens of existing files) fail with ENOTSUP before lookup. Virtio-fs keeps its
0644 creation request under the host-service identity. This is the accepted policy for the first writable public opens (used by tee).
Revisit with a file permission system, users and ownership, defining mode enforcement and umask together rather than silently discarding
requests callers expect to restrict access.

## Non-atomic stdio append

Append streams query the file size before each native write, so concurrent appenders can overwrite each other, and seeking does not make
the pair atomic. Keep this until concurrent appending needs a native operation that chooses the end and writes under one file operation.
Formatted output stages the full result with `snprintf` (heap allocation and a second pass when the stack buffer is too small), so large
formatted output needs temporary memory; revisit bounded streaming when a consumer makes that material. All FILE output is unbuffered.

## Console line input

Consoles are never read ahead (their input is shared with the parent shell and ISO C treats them as interactive), so line input from
console-backed stdin through `fgetc`, `fgets` or `getline` costs one native read per byte, including a large paste; file and pipe input is
fetched in BUFSIZ blocks by [input read-ahead](userland/stdio.md#input-read-ahead). Revisit only with a terminal input design that can
return unread console bytes to their next owner, not with per-port workarounds.

## Input read-ahead limits

Buffered file bytes are a private copy: if another descriptor or process writes the same file, a stream can return stale bytes until
`fseek`, `rewind` or input `fflush` refetches. `setvbuf` and `setbuf` are absent, so programs cannot size or disable the buffer;
one-byte `ungetc` works. Revisit together with output buffering in a later stdio completeness task
([input read-ahead](userland/stdio.md#input-read-ahead)).

## zlib core profile and qualification

The [zlib development library](development/ports.md#zlib-development-library) keeps unmodified public headers but omits the `gz*` file
helpers (accepted for the screenshot milestone, 2026-10-08), so their declarations have no definitions; in-memory gzip framing works
through the core stream APIs. Revisit the helpers with a consumer, auditing its libc and file-authority needs. Archive and symbol checks
show no runtime correctness; the [screenshot consumer](userland/screenshot.md) observed deflate-to-PNG output that decoded on the host, and
other profiles stay unqualified.

## libpng profile and runtime qualification

The [libpng development library](development/ports.md#libpng-development-library) keeps the conventional read/write APIs with matching
generated configuration and omits the simplified API and architecture acceleration (accepted for the screenshot milestone); revisit with
a consumer or measured cost that needs them. Build evidence is not runtime evidence: the [screenshot consumer](userland/screenshot.md)
qualifies the conventional RGB8 non-interlaced write path through host-decoded captures, while PNG decoding and other write profiles
are unqualified.

## Unexpected native close failures

Libc invalidates a descriptor and its FILE association before one native CLOSE attempt. Today's native outcomes (success or BAD_HANDLE)
leave no owned capability entry; if a future failure or malformed reply makes release uncertain, libc reports the error, discards its
metadata without retrying, and any residual capability survives until process teardown (delaying pipe EOF or EPIPE until then). Open
rollback applies the same policy. Accepted for unexpected failures only, not deferred release. Revisit if CLOSE gains outcomes or
asynchronous release, deciding who owns the capability before adding retries or pending-close storage
([close contract](userland/libc-portability.md#close-failure-and-cleanup)).

## Narrow libc file metadata

Libc offers `mkdir`, `opendir`/`readdir`/`closedir` and `stat`/`lstat`/`fstat` for ports such as [Links](userland/links.md), but native
objects report only a kind and (for files) a size, so `struct stat` has only `st_mode` type bits and `st_size`; code reading other fields
fails to compile rather than seeing invented values.

- **Sizing opens the file** with READ (or WRITE if READ is denied), so a file with neither right cannot be sized, and on a provider URI
  it performs the request (Links sends provider URIs straight to `fopen`). Review proposed failing with ENODEV there, as `opendir` does.
- **Symlinks:** lookup never follows one, so `stat` and `lstat` of a host symlink fail with ENOTSUP and `readdir` reports `DT_LNK`;
  native filesystems have none, `lstat` equals `stat`, and `readlink` is absent.
- **Access checks:** `access` tests current native grants, not mode bits or backing I/O (directory R_OK needs ENUMERATE, W_OK
  CREATE|REMOVE; X_OK and provider routes return ENOTSUP), per the
  [accepted contract](userland/libc-portability.md#native-path-checks-and-removal).
- **Listings** omit `.` and `..`, and a detected concurrent change ends one with EAGAIN. The first ls and mkdir still use libpyxis helpers.

Revisit when native objects gain timestamps or other metadata, a port needs `readlink`, or a port calls `stat` on provider URIs; add
fields only for values the native layer reports.

## File identity across capability paths

The filesystem protocol cannot tell whether two opened file handles name the same file, and path strings cannot (different roots and
directory paths can reach one object, and a descriptive path is not authority). This blocks reliable `#pragma once` in native TCC, which
rejects the directive for now ([TCC contract](userland/tcc.md#remaining-limits)); include guards work. Revisit an identity operation when
needed, defining comparison scope, lifetime and behavior across mounts and replacement, rather than normalizing paths or adding
`realpath` for TCC.

## Program location

A process cannot find where its executable came from: `argv[0]` is the command word as typed, a path is not authority, the process
holds no lookup right to its executable's directory, and the [startup record](interfaces/processes.md#startup-record) carries no
program location, so there is no `/proc/self/exe` equivalent and the [SDL2 port](development/ports.md#sdl2-development-library) reports
`SDL_GetBasePath` unsupported. Ports keeping files beside their executable use fixed locations (DevilutionX hard-codes
`boot://share/devilutionx/`), each with its own path patch, and a program cannot be copied with its files into another directory and run.
Revisit when another port needs this or self-contained bundles become supported. Options, none decided: a read-only directory grant for
the program's own directory at launch, or a descriptive location in the startup record (no access, with `argv[0]`'s weaknesses). Do
not infer the location from `argv[0]` or normalize paths for one port.

## Sleep wake granularity

Per-CPU one-shot LAPIC timers target local deadlines while nominal 120 Hz preemption and HPET timekeeping remain (owner-accepted
2026-10-08; [timekeeping](kernel/timekeeping.md)). The [matched qualification](development/experiments/sleep-wake-granularity/timer.md)
shows SDL mean-frame medians of 25.565 ms before, 24.243 ms with expiry IPIs and 17.415 ms with local deadlines, and Quake capped-loop
medians of 49.223, 59.595 and 70.291 FPS (nested KVM, not native or maximum-latency guarantees). The owner saw capped Quake play much
smoother on the ThinkPad (2026-10-08, main `4332801`; tear line in the top third, consistent with 72 FPS on an unsynchronized 60 Hz
presenter; `timedemo demo1` unchanged at about 684 fps), an observation, not a measured latency. Native sleep and cap latency, LAPIC
power-state behavior and long-running 32-bit HPET extension are unmeasured.

Nanosecond units are a representation, not a precision promise: interrupt-disabled intervals, runnable load, firmware or host stalls and
large due batches still delay execution, and sorted-list insertion and cancellation stay linear with no timer quota. Revisit stronger
bounds or another data structure with a measured consumer; tickless scheduling is out of scope.

## Wall-clock time and clock-source performance

[Monotonic time and deadline sleep](kernel/timekeeping.md) use a shared clock; local timer dispatch and scheduling still delay
execution, nanosecond units promise no precise wakeup, and time with the VM paused need not count. [UTC wall time](kernel/wall-clock.md)
is a whole-second Limine RTC seed plus elapsed monotonic time: firmware accuracy, subsecond alignment and boot handoff delay are
unknown, there is no drift correction or resynchronization, a missing seed is an explicit error but a plausible wrong RTC cannot be
detected, and adjustments must not change monotonic deadlines. [Local time](userland/timezones.md) is userspace. HTTPS certificate
validity depends on this UTC value, so a wrong RTC date can cause wrong acceptance or rejection; revisit authenticated time
synchronization before treating TLS date checks as independent of firmware or hypervisor time. The VirtIO RTC driver is deferred.

**Clock source.** HPET MMIO reads are expensive under virtualization. Unneeded reads for empty scheduler deadline lists and untimed HOST
idle waits were removed ([HOST investigation](kernel/bsp-service-requests.md#profiling-and-scheduling-costs)), and timer passes read the
clock at most once and rearm only for an earlier target, cutting nested-QEMU idle HPET reads by 63% and send-side reads per TCP segment by
about a quarter ([measurements](development/experiments/timer-clock-reads/README.md)); active deadlines and profiling still pay each
remaining read. Boot still requires a memory-mapped HPET. The owner chose
[software-extended HPET first](kernel/timekeeping.md#software-extension-sampling-and-support-limit) (2026-10-03), and since 2026-10-09 the
kernel uses the [TSC](kernel/timekeeping.md#tsc-selection) when every CPU qualifies, removing HPET reads after boot. The running HDA
worker's 5 ms watchdog (about 200 timed wake opportunities per second, absent while parked) still adds timer re-arms and HPET reads
([audio profile](development/experiments/audio-task2/profiling.md)), and per-packet network work and audio refill pay several clock
reads per event ([TCP throughput limits](#tcp-throughput-limits)); cheaper clock sources remain kernel work.

**HPET extension limit (accepted).** 64-bit counters are read directly and 32-bit ones are extended with a shared CAS accumulator, which
publishes to one cache line per advancing read and can retry under contention; BSP maintenance runs every 120 timer deliveries by
default (nominally one second). The accepted support requirement is strictly less than one advancing-counter wrap between incorporated
samples, including boot operations, long interrupt-disabled execution, firmware stalls and debugger or VM pauses. A violating gap needs a
reboot because the low word cannot reconstruct missing wraps, nominal interval validation cannot enforce the bound, and suspend, resume
and migration are unqualified. This applies only while the HPET is the clock; revisit with an independent source or a stronger progress
guarantee if a target falls back to it. Host-KVM observations showed lower call cost for forced low-32-bit extension (native performance
unestablished; the direct profiled allocation median was about 2.6% higher, cause not isolated), and the ThinkPad logged the 32-bit path
and held the clock through a native session of more than 15 minutes on 2026-10-07 ([target notes](targets/t14-gen1-amd/notes.md#native-status)).

**TSC (accepted limits, owner 2026-10-09; native qualified).** On the ThinkPad two cold boots calibrated 2096.063 and 2096.064 MHz (within ±12 ppm of Linux's 2096.061 MHz), clock reads
take about 130 ns and a 15-minute date check held ([measurements](development/experiments/tsc-clock/README.md)). Cross-CPU agreement is checked only at startup (about 2 ms per AP,
no shared floor or runtime watchdog), so later warps go unnoticed and cross-CPU monotonic order rests on that check and the invariant TSC. Calibration costs 100 ms on every boot whose BSP
qualifies with an error bound up to 100 ppm on top of the HPET's crystal error, and CPUID `0x15` is only logged. Nested VMs fall back to the HPET (its reads are too slow for a 100 ppm
calibration and the development VM exposes no invariant TSC). Suspend, resume and migration are unqualified. Revisit with a target that shows a warp or needs better accuracy.

## Doom configuration and save-format limits

[Doom save/load](userland/doom.md#saves) uses checked temporary writes and atomic replacement through the
[filesystem mutations](interfaces/filesystem-mutations.md); saves persist on installed systems and last until reboot on live boots. The
upstream parser trusts saves matching the loaded game data, with no malformed-file validation or separation by PWAD, and interrupted saves
can leave temporary files for manual removal. Configuration persistence is disabled in the pinned generic engine; re-enabling it needs a
writable configuration location and review of its parser and formatting.

## Clang code generation and predefines

Pyxis builds with Clang since the [LLVM toolchain milestone](development/llvm-toolchain.md). Matched nested-KVM measurements on 2026-10-07
(four CPUs) left two regressions against GCC 16: `allocbench heap` at about 13.1 ns/op against 12.0 (about 9%; the TLSF fix for Clang's
narrowed flag stores removed most of an original 33% gap, the rest is uninvestigated, and two early samples of 20.4 and 21.7 ns/op did
not recur), and Quake `timedemo demo1` about 3% slower (median 1560 against 1604 fps). Clang also emits more byte-sized
read-modify-writes (164 against 75 in the kernel) with no other measured cost. Reconsider when an allocation-heavy program (Lua, TCC, the
JVM experiment) measures the heap gap or the LLVM pin moves.

Clang predefines `__INT_FAST8_TYPE__` and `__INT_FAST16_TYPE__` as `signed char` and `short` while libc's `stdint.h` uses `int`, so code
using the compiler macros instead of the header sees a different type. Reconsider if a port relies on them; the fork's target information
could then match the header.

## C++ runtime subset

The [C++ in userspace milestone](development/cxx-userspace.md) shipped a deliberate subset (owner, 2026-10-08):

- **No threads or thread-local storage:** `<thread>`, `<mutex>`, `thread_local` and non-lock-free atomics are absent and local statics
  use single-threaded guards, so thread-starting programs cannot be ported yet.
- **No localization or wide characters:** `<iostream>`, `<locale>`, `<regex>` and wide strings are absent, as are the fmt headers needing
  them (`chrono.h`, `ostream.h`, `std.h`, `xchar.h`, `printf.h`). libc has a "C"-only `setlocale` (no `localeconv`) and `wcslen`, but
  `std::cout` ports still need the other wide-character functions (58 of the 59 names in `<cwchar>`).
- **Most of `<cmath>` is missing:** libc's math subset leaves 161 of the 186 imported names undefined, so first use fails to compile.
- **Exceptions cannot cross C frames** (no unwind tables), so one thrown through a `qsort` comparator terminates the program; uncaught
  ones name mangled types (`St11logic_error`) because the 184 KB demangler is left out.
- **No `<filesystem>`, `random_device` or time zones**, and fmt's license is not staged for the boot payload (a port shipping an fmt
  program must install it).

Revisit threads and TLS with the Clang hosting milestone, and localization, wide characters and `<cmath>` when a selected port such as
DevilutionX needs them. Additions belong in libc or the runtime configuration, never in a port-local stub.

## HD Audio volume control

[Software master/per-space controls](interfaces/audio.md#user-volume-controls)
boot unmuted at 50% (about −30.3 dB); settings do not persist, and software mute
can leave up to the nominal 80 ms of published DMA audio. Media keys are not
decoded, so use the bar or Super+M. [Native listening passed](userland/audio-volume.md#native-qualification).
Revisit persistence/media-key input when ordinary use requires them, and mute
latency with the ring-tuning work.

## HD Audio volume native regression

After volume control and #628, the native two-minute eight-session silent
regression (`pcm 0 0 120` x8) remains unrun; the earlier eleven-minute pass
predates these changes. Native per-space isolation/retention, hidden playback,
the full slider/keyboard interaction matrix, pops/dropouts during level changes,
published-ring mute delay and rapid restart during codec settling remain unqualified. Revisit in the next
ThinkPad batch, retaining each final STATUS/log; current closure establishes
[one-session listening/mute/reboot behavior](userland/audio-volume.md#native-qualification).

## HD Audio scheduling and startup tuning

The [engine](devices/hda.md#progress-and-refill-limits) uses an 80 ms hardware ring;
unprimed QEMU startup produced a 58.667 ms silence gap. Closure establishes no
native latency guarantee. Revisit startup, ring depth and service margins with
latency measurements and separately assigned consumer work.

## HD Audio sustained eight-session playback

Sustained eight-session playback still fails closed in **nested QEMU**: the longer
run tripped the 1 ms WALCLK commit guard after 112.227 s, most likely from a
transient host stall, not proven inadequate mixer throughput. BSP host-thread CPU
was 99.55–99.61% before the two fixes and 93.93–96.40% afterward. Native eight
silent sessions passed eleven minutes. See the [profiling report](development/experiments/audio-task2/profiling.md);
revisit QEMU scheduling/guard evidence separately from native qualification.
[Volume qualification](development/experiments/audio-volume/README.md) also saw
QEMU guard trips during shorter input/start bursts; the accepted bounds are unchanged.

## HD Audio fail-closed recovery

One unsafe-progress or hardware guard trip disables audio until reboot, as
accepted 2026-10-09. STATUS/RELEASE and cleanup remain usable; DMA stays retained.
A controller-reset recovery path needs a separate decision supported by failure
and ownership evidence; the successful native sitting does not remove this limit.

## HD Audio request batching

Bounded request batching remains deferred by the owner. Per-write BSP handoff
and retry/wait traffic remain costs; the [profile](development/experiments/audio-task2/profiling.md)
records them. Revisit after consumer or native measurements justify a batching
proposal without silently changing capacity, queues or safety thresholds.

## Initial clipboard delivery limits

Accepted 2026-10-09 in the [clipboard proposal](wip/clipboard.md#first-delivery-limits),
implemented for local terminal and mux into opted-in stock libterm readers.
See the [interface](interfaces/clipboard.md) and [qualification record](development/clipboard-first-delivery-qualification.md)
for behavior, measured/manual evidence and validation limits.

- **Receivers:** Paste is limited to opted-in stock libterm line readers. vi,
  less, Links and other raw-mode programs refuse Paste until they provide their
  own receiver contract. Revisit when assigning those concrete consumers; do
  not fall back to unframed bytes.
- **One line:** LF/Tab become spaces and insertion needs a fresh Enter after
  completion. Multi-line documents cannot be preserved by these line readers;
  revisit with a multiline/raw-program receiver, preserving newline safety.
- **Selection fidelity:** ASCII-only Copy refuses non-ASCII glyphs, LF-joins
  physical rows and trims trailing spaces, including intentional whitespace.
  Tabs and soft wraps cannot be reconstructed from retained glyph cells. Revisit
  with a verified font mapping and terminal text/provenance work, not by labeling
  arbitrary bytes UTF-8.
- **Storage:** One current item per local/shared layer; RAM only, no history,
  lost at reboot. Revisit history/persistence as separate owner-chosen work.
- **Admission:** 64 KiB text and 8 MiB aggregate current/staging/active-snapshot
  storage; one Paste per space; 5 s unused activation and a separate 5 s total
  Paste deadline. Larger items or slow/stalled delivery refuse/cancel, preserving
  current clipboard contents and never submitting partial insertion. Revisit
  limits only from measured memory/progress needs.
- **Pending input:** Paste refuses while earlier input is queued/staged or the
  decoder is incomplete; it is never saved for later. The user must finish that
  input and issue a fresh gesture. Keep this safety boundary when adding readers.
- **Later consumers/types:** No SDL2/graphics clipboard, FILE/rich objects,
  converter execution or remote/host clipboard bridge in this delivery. SDL2,
  FILE retention and trusted converters follow separate milestone tasks; remote
  bridging needs its own authority and host/guest paste contract.

## SDL2 port limits

The [SDL2 port](development/sdl2.md) covers video, keyboard, pointer, timing and preference paths. Video event waits block on keyboard,
acquired pointer and display readiness ([qualification](development/sdl2-event-wait-qualification.md)); upstream polling remains for
missing or nonwaitable sessions and failed waits, and enabling threads needs a real wakeup sender and a revisit of the readiness cache.
Missing: **audio** (the [PCM grant](interfaces/audio.md) and [QEMU HDA engine](devices/hda.md) exist, but SDL2 has no backend; revisit with a
playback consumer task), **threads** (`SDL_INIT_TIMER` timers and `SDL_CreateThread` fail until userspace threads), **windows** beyond one
fullscreen window, and **text** beyond the shared US layout. Pointer position, program images, show/hide, bounded warp and relative lock use
the [native pointer contract](interfaces/pointer.md). Key repeat is not covered by validation because QEMU's injected PS/2 input has no
typematic repeat.

## DevilutionX port limits

[DevilutionX](userland/devilutionx.md) is personal-use only because its non-commercial licence and libmpq's GPL cannot both be met by a
distributor, so it is an opt-in build that images, CI and bundles never contain. It has no sound, multiplayer, controllers or translations
(the build host has no gettext), and saves go to `home://devilution/`, which is RAM on live boots. Retail `DIABDAT.MPQ` (about 500 MB)
cannot be staged in images, so on installed systems it arrives through [remote transfers](#remote-transfer-memory-and-staging-limits), today
in 15 MiB pieces; revisit with streaming transfers.

## SDL2 and DevilutionX native qualification

The owner checked the shareware build natively on 2026-10-08 (ThinkPad PXE, main `4332801`, 1920x1080, AC): DevilutionX played with
keyboard, touchpad and TrackPoint, key repeat worked in name entry, and with the default "Limit FPS" it ran at 59–65 FPS (mostly 60–62)
over the 1920x1040 content area ([measurements](userland/devilutionx.md#measurements)). A
[standalone bundle](userland/devilutionx.md#standalone-bundle) with retail data on the installed stick is unchecked natively; the owner
deferred it (the shareware result is enough, and 692 MB at the measured native 2.5 MiB/s is roughly 4½ minutes). Revisit when the owner
wants to play the retail data.

## Quake port limits

The [Quake port](userland/quake.md) renders at quakegeneric's fixed 320x240; a resolution switcher would need a video driver with a mode
list behind Quake's Video Modes menu, reallocation of the frame, z-buffer and surface cache, and a check of the renderer's size limits
(upstream reverted 640x480 as unstable). Sound, networking, CD audio and joysticks are absent, and sound needs a native audio device first.
Saves are Quake's trusted text format, and shareware and retail data share `home://quake/id1` so their configuration and saves mix;
QuakeC strings outside the hunk use a 512-entry table whose overflow stops the game with an error. Revisit when a second data set matters.
Config, saves and screenshots follow the [atomic-save limits](#atomic-save-limits); demos are still recorded in place.

## vi port limits

[BusyBox vi](userland/vi.md) displays ASCII only. BRE search and substitution use libc's [regex limits](#regex-character-classes-and-back-references)
through bounded copies (owner decision 2026-10-07), so matching stops at an embedded NUL within a copied slice; revisit bounded or binary
regex interfaces for a concrete consumer. Saves follow the [atomic-save limits](#atomic-save-limits).
`:!` and shell filters need a native launch adapter, the read-only marker probes WRITE authority because truthful file metadata does not
exist, and the recipe's libbb adapter covers only the selected vi and less helpers. Input EOF exits and loses unsaved edits, as upstream does
(Kilo handles it explicitly).

## Atomic save limits

[Quake](userland/quake.md#saves-and-configuration) (config, saves, screenshots) and [vi](userland/vi.md) write a synced `NAME.XXXXXX` file beside
the target and rename it over the target; a failure keeps the old file. Libc cannot sync a directory (a descriptor cannot open one), so a crash
can lose the new name and leave the old contents, and a crash before the rename leaves a stray temporary file that nothing removes. Libc has no
`fdopen`, so Quake reopens the name `mkstemp` reserved. Saving needs directory CREATE and REMOVE, not only file WRITE, with no in-place fallback.
A failing or full write was not exercised. Revisit with a libc directory-sync bridge (see [Git on Pyxis](wip/git-on-pyxis.md)) or `fdopen`.

## less pager limits

The [BusyBox pager](userland/less.md) retains read display lines for backward paging within the selected line-count limit and process
memory, measures screen dimensions once and shows ASCII; BRE search and highlighting inherit libc's
[regex limits](#regex-character-classes-and-back-references). There are no raw escapes, shell commands or live refresh, and a content read
during refill or search blocks, so a stalled producer can delay keys (cached navigation reads nothing). Revisit native readiness through a
proven libc extension for an actual live-stream consumer, and screen resizing when terminal size-change notification is designed.

## tar archive limits

The [BusyBox ustar subset](userland/tar.md) captures the whole input archive or all creation file contents in memory before writing, so large
archives can fail allocation before mutation, recursive creation uses stack by tree depth, and creation names are limited to 99 bytes plus a
directory slash. Compression, GNU/PAX extensions, links, `-C` and stdin/stdout archives are absent. Validation keeps unsafe archives from writing
any member and a read-only root fails on its first mutation, but extraction and output are not transactional (later I/O or authority failures
can leave earlier entries or partial files), directory enumeration is live and close is not sync. Revisit with a larger archive consumer,
and streaming or rollback only with an explicit snapshot or transaction design.

## Links port limits

[Links](userland/links.md) loads every page synchronously, so a slow fetch freezes the interface until the HTTP provider's 30-second budget
ends (in review a server that accepted and never answered left a blank screen for 32 s before "Operation timed out"), and Ctrl+C during the wait
is held until the open returns. Revisit with a native way to wait on a provider open alongside console input. Other limits:

- **Saves and downloads are not durable or atomic beyond the rename:** libc has no directory sync, so a crash can keep an old `links.cfg`, and
  downloads (including Overwrite) are written in place under their final name. On a live boot `home://links/` is RAM.
- **Fixed screen size,** read once without resize notification.
- **Sockets compiled in but unreachable:** the port's socket functions fail, so `ftp://` and `finger://` report "Host not found".
- **Remote pages can link to local roots** (`host://`, `home://`, `system://`), and following the link opens the local object. Without scripting
  a page cannot read or send what it opens, matching a local link the user chooses to follow, though desktop browsers refuse such navigation.
  Revisit before Links gains POST, cookies or providers that act on requests.

HTTP-side limits are under [HTTP redirects](#http-redirects) and [response metadata through fopen](#response-metadata-through-fopen).

## Virtio-fs runtime resource retention

The first [virtio-fs transport](devices/virtio-fs.md) reserves queue storage and device mappings before AP startup. A runtime failure masks
interrupts, disables bus mastering and attempts reset, but keeps the claim, 40 KiB of ring and payload allocations, CPU-side queue
bookkeeping and mappings until reboot, since even a confirmed reset does not make changing shared kernel mappings safe without a TLB
invalidation and reader-lifetime contract (the display's quiescent TLB-flush helper does not establish this transport's reader or DMA
lifetime). There is no reconnect, and never free an outstanding DMA buffer just because a request timed out. Idle daemon disconnection may
not be visible until the next request, with no heartbeat. Revisit with that ownership contract and a defined teardown and reconnect
lifecycle.

## Shared split-queue scaling and validation

The [shared queue helper](devices/virtio-queues.md) supports multiple direct chains and allocates storage for the selected queue size, and its
request-ID uniqueness and completion validation scan queue-sized arrays. Current filesystem and entropy queues are small and serialized, so no
scaling result is established; revisit the scans if a measured block or network workload makes them material. Runtime validation covers the
migrated serialized consumers and the block driver's eight outstanding writes, and two concurrent block reads completed out of order with
correct ticket association and full readback. Allocation, malformed-completion and reset failure paths have code inspection only (no fault
injection was authorized); revisit before expanding storage reliability claims.

## Virtio-blk failure and validation limits

The [block driver](devices/block-storage.md) latches write failure after an ordinary write or flush error: later writes and flushes fail until
reboot while reads continue on a healthy transport, so a later flush cannot hide an earlier persistence failure but a transient backend write
error needs a reboot to resume writes. Revisit with a filesystem consumer and an explicit error-acknowledgment and recovery contract; never
silently retry writes that may already have modified storage. Runtime device failure retains the PCI claim, queue bookkeeping, DMA allocations and
mappings even after confirmed reset (each prepared device reserves up to 512 KiB of payload storage plus control buffers and rings, scaling with
the inventory, with no reconnect or reclamation); revisit with shared-mapping TLB invalidation, DMA ownership and a teardown lifecycle, since a
timeout alone cannot release storage the device can still reach. Physical hardware and power-loss persistence have no coverage, and normal QEMU
restart and readback cannot establish either; revisit durability evidence before promising filesystem recovery or production-data support.

## GPT snapshot and profile limits

[GPT discovery](devices/gpt.md) publishes one snapshot per device. Exclusive installer raw claims exclude mounts and refresh the snapshot on
release, but there is no hotplug, external-mutation detection or automatic repair, borrowed views last only until the next scheduling point,
and health does not track later transport failure or changes during raw writes; external host writers are unsupported. Revisit generation
tracking and broader replacement lifetimes with a concrete consumer. The supported profile is GPT 1.0 on 512-byte or 4 KiB blocks, at most 256
entries and 64 KiB per array; unsupported revisions, larger layouts, reserved attributes and legacy or hybrid MBRs expose no map, and one valid
copy gives a read-only degraded map only when the other is absent or invalid (I/O errors, timeouts and unsupported metadata prevent fallback).
These bounds can exclude usable media; revisit only for a consumer with explicit resource limits and a recovery policy.

## Installer authority and retained pools

The [installer disk service](devices/installer-authority.md) supplies explicit raw claims and immutable boot sources, and the
[native installer](userland/installer.md) establishes target consent (the kernel does not interpret `SAFE_TO_WIPE`). Any retained npfs pool blocks
an exclusive raw-write claim on its device, even read-only and after all handles close, so opening a volume to inspect a marker cannot be followed by
raw formatting of that device in the same boot (owner-accepted direction; no pool teardown or installer bypass). The installer instead inspects each
volume's root marker through raw reads and the format library, overlaying a committed journal in memory with no write before consent, so rejected
consent leaves the disk untouched and accepted targets are wiped rather than receiving a persisted replay. Pool retirement with an explicit ownership
contract is a prerequisite of the later live-install flow, which can inspect through normal mounts and install in the same boot. Read-only raw handles
acquire no claim and promise no snapshot against raw writes. Physical-media, power-loss and uncertain-failure evidence stays separate from emulated
operation.

## Installer inspection and recovery limits

Consent validates GPT, npfs headers and the complete committed journal, then checks allocation and mappings on root-marker paths; it does not prove
whole-filesystem ownership or inspect unrelated files, and a large committed log must be read in full before eligibility is known (inspection keeps
descriptors, not the log). Revisit with large-log measurements or a need for whole-filesystem qualification. [System-update inspection](userland/system-updates.md)
validates allocation bitmaps, live catalog records and the system inode file, root and cleanup chain against writable-mount admission, with no
whole-pool ownership check or ordinary file reads; its bounded FAT32 reader follows required paths on the fixed 512 MiB ESP (at most 64 KiB of
configuration and 64 bytes of revision text) and checks traversed FAT copies and chains only. A healthy GPT and compatible empty-journal pool permit
rebuilding missing or damaged ESP contents, while readable foreign or invalid disk bindings and raw I/O or allocation failures refuse, the optional
revision record cannot override binding checks, and a selected committed journal is refused without loading or replaying it. Revisit these bounds with a
new installed layout or a general FAT service; whole-pool checking is fsck's role.

Installation writes fresh metadata and boot files without securely erasing free space. Update replaces the whole ESP (discarding unrelated ESP files)
with no fallback entry; an interrupted replacement is rebuilt by booting live media and choosing Update while the GPT and pool stay eligible
([recovery record](development/experiments/system-updates-task2/README.md)), and an interrupted installation can leave a partial disk without rollback
or repair. Revisit fallback and atomic replacement with an agreed in-system update design. VirtIO and per-device qualified USB support writable native
mounts (USB write and cache synchronization is implemented), and the first native [USB installation](devices/usb-installation.md) on 2026-10-05
wrote an expendable stick from PXE live media: writable mounting, persistence across a synced power-off and one Update round trip passed
([owner record](targets/t14-gen1-amd/usb-bringup.md#2026-10-05-first-native-installation-c4)). Power loss during writes, uncertain I/O, other
devices, ports or the dock path, and physical firmware are unqualified, and the internal NVMe is unsupported.

## Updates from before boot init

Update recognizes only the current installed form of the [system layout](userland/system-layout.md) (boot init's normal and rescue entries). An
installation from before boot init, such as 0.0.2, reports damaged or missing boot files and an unknown revision and is rebuilt; the pool is
unaffected, and because the previous revision is unknown that Update removes no program directories. Revisit if another older form needs a direct
Update.

## RAM volumes

Boot init makes each configured `ram` volume (such as the live `home://`) as a subdirectory of one private RAM directory the kernel hands it as
the `ram` resource. No ABI creates a detached RAM directory, so only boot init can make RAM volumes, all share one RAM filesystem, and only
memory limits their size. Add an ABI for creating RAM volumes when a second user, such as a per-session scratch volume or a size limit, needs one.

## Boot init and space creation

[Boot init](userland/init.md#boot-configuration) and the [space factory](userland/init.md#space-creation) have these limits:

- Space creation panics on memory exhaustion, and only boot init creates spaces, early in boot; make creation fallible before users can create spaces.
- A failed launch returns only a status, so the caller cannot tell a rejected request from a space created and left unstarted (revisit with the
  space manager). Spaces are never destroyed.
- No limit on how many spaces a configuration creates, though each costs about 8 MiB at 1080p; the owner chose not to add one (2026-10-06) because
  installed hardware has ample memory and the rescue entry recovers an override that exhausts it.
- Space inits must be `boot://` archive entries and hold no mount authority (`sync --disk` is unavailable, `sync PATH...` works).
- Every new space's first process receives that space's console, keyboard, pointer, display and space grants, so the installer holds input, display
  and title authority it does not use. Configuration errors are Lua messages and call statuses are numbers.
- Spaces created one after another usually start their inits on the same AP, since blocked inits do not count toward load (boot used to place
  all inits first, on CPUs 1–3); balancing moves runnable tasks later. Revisit if interactive latency suffers.

## Rescue set programs

Installed systems run ordinary programs from `bin://`, but the boot archive's rescue set still carries `textfs` and `httpfs`: the init scripts start
those providers before any shell and a launch failure stops a script, so without them a system whose `bin` volume is missing would get no shell even
from the rescue entry. Revisit once inits can start providers from `bin://` with a fallback or tolerate a missing provider. Related limits of the
[program stage](userland/system-updates.md#program-stage): only executables move (`share/`, `sdk/` and configuration stay in `boot://` because programs
name those paths), builds without a Git revision share `bin/unknown`, the installer holds the boot archive twice in memory while filtering (about
90 MiB today), and spaces get `bin://` read-only with only the installer writing it.

## Interim program revision directories

Each Update copies every moved program into a new `bin/REVISION` directory even when unchanged, and the pool keeps two complete revisions. The
directory follows the running kernel's revision, so programs cannot be updated without a new kernel and ESP. The
[system layout](userland/system-layout.md#programs) accepted this as interim; revisit when a final program update scheme is designed.

## Archive-only network configuration

Network profiles live only in `boot://config/network.lua`, so on an installed system changing them needs an Update, while spaces can change through
the pool override ([system layout](userland/system-layout.md)). The next follow-up is moving network configuration onto the pool with the same
override pattern.

## USB image updates and firmware qualification

The [raw USB image builder](development/usb-image.md) creates fresh images and replaces the sample pool and its identities on every rebuild, with no
preservation of installed data, rollback or atomic physical update; the manual copy procedure relocates backup GPT on larger media but does not expand
the pool. Installed systems use the [native installer](userland/installer.md)'s pool-preserving Update, and the raw image stays a development artifact
(revisit preservation only if raw images become a delivery format). Emulated USB boot has reached the shell, but an intermittent
[pre-kernel Limine file-open failure](development/qemu.md#usb-firmware-file-open-failure-before-kernel-entry) remains unexplained, and successful
retries do not qualify firmware boot reliability or physical-controller behavior; revisit with firmware and USB I/O diagnosis in the assigned
hardware stage.

## RTL8111 initial-state support

The [RTL8111 preparation path](devices/rtl8111-hardware.md#controller-preparation) rejects D3hot wake without `NoSoftRst`, since that transition
can discard assigned BARs and the temporary identity probe saves only Command/PMCSR; such a controller stays unavailable while boot continues.
Revisit PCI configuration restoration if owner-run native qualification meets this state.

## RTL8111 runtime limits

The [RTL8111 I/O path](devices/rtl8111-hardware.md#ethernet-io) supports XID `541` only. Each prepared controller retains two contiguous 68 KiB
rings and one page for hardware tally snapshots, and unselected hardware stays inactive. Runtime failure attempts reset and disables DMA and delivery
but retains claims, buffers and shared mappings until reboot, and the first binding has no fallback or switching; revisit reclamation with a
teardown and SMP invalidation contract. [Qualification](development/rtl8111-qualification.md) covers sustained VFIO traffic and an owner-run native
cold/PXE boot with the dock attached, without imported firmware; native unplug, device-owned TX at carrier loss, every PHY speed and gigabit line rate
are unqualified, and the native wired and VFIO Wi-Fi results come from different environments and cannot isolate a throughput bottleneck. No jumbo
reassembly, offloads, firmware interpreter or automatic restart exists (a measured firmware requirement would need a focused import with provenance and
redistribution terms). Hardware tallies are reachable only through the internal GDB capture helper; revisit when a network status command is assigned,
as there is no public statistics ABI or tally polling.

## Initial net0 selection limits

[Automatic selection](devices/net0-selection.md) requires complete discovery and reported carrier; VirtIO without STATUS needs an explicit selector,
and an incomplete inventory leaves the setup owner waiting while the shell stays available. Selection binds once until reboot, with no controller
fallback or lease revalidation on link-up, so a cable moved to another port needs a reboot. Revisit with drain and teardown ownership and DHCP link-up
policy, not a second binding or lease authority.

## Virtio-net runtime resource retention

The external interface's first unique configuration binding lasts until reboot: address clearing preserves it and controller switching or fallback
after failure is unsupported, and unselected prepared controllers keep their boot resources with DMA and delivery off. The
[network transport](devices/networking.md#virtio-net-transport) uses two nine-page contiguous allocations (72 KiB) for rings and packet buffers, and a
runtime failure attempts reset and disables delivery and DMA but retains the PCI claim, allocations and mappings until reboot, for the same
shared-mapping lifetime reason as virtio-fs. Revisit runtime switching and both drivers' reclamation with a teardown, packet draining and SMP
invalidation contract, not implicit fallback.

## Host filesystem request storage and enumeration

The [native virtio-fs backend](devices/virtio-fs.md#native-directory-and-file-objects) uses the largest record in each user task's reusable
4,928-byte request allocation (including a 4 KiB read/write buffer) plus a separate eager 720-byte profile allocation, which kernel workers do not
allocate. Every user task pays both even if it never uses HOST or profiling; this keeps submission allocation-free and guarantees cleanup capacity.
Revisit lazy provisioning if task counts or memory pressure make it material, with explicit failure, BSP handoff and cleanup ownership and no extra
fixed registry. The native enumeration ABI returns one name per call and the backend requests a fresh 4 KiB READDIR batch discarding unused entries,
so large listings can transfer trailing names repeatedly, with no attribute or data cache or directory snapshot (revisit batching with a consumer and
host-change semantics). Host executable loading captures at most 128 MiB per selected image into reclaimable BSP-owned pages, without a coherent
snapshot if the host edits the file in place meanwhile; callers must avoid in-place changes. There is no aggregate concurrent-capture budget;
physical exhaustion rejects. Revisit snapshot semantics or aggregate admission with a measured consumer need.

## Initial TCP listener limits

Native [TCP listeners](devices/tcp.md#listening-and-admission) bind only the exact configured NIC IPv4 address (no wildcard, loopback listener,
ephemeral bind or reuse). Four listeners and their pending and accepted connections share 32 global transport records, TIME_WAIT can block admission
below the per-listener backlog, and there is no per-space quota or protection against exhausting the budget; revisit with concurrent-server demand and
an authority and accounting policy, never by evicting live records. The echo consumer serves four clients with bounded output and fair service but
has no idle-client or output-drain deadline, so four stalled clients can hold all slots (the [remote server](userland/remote-terminal.md) has its own
supervision and closing-output deadline); revisit expiration only if a consumer needs it.

## TCP throughput limits

After the [network throughput](development/network-throughput.md) work and the TSC clock, native send reaches 101.4 MiB/s with 8 KiB writes and 98.7 MiB/s with 2 KiB writes,
and receive 91.2 MiB/s into a discard sink when `ttcp -r` runs from the local shell (ThinkPad, 2026-10-09, main `86b36e5` plus TSC). The earlier per-call bound (2 KiB writes at
44 MiB/s against 70) has essentially disappeared, along with most of the clock-read cost per packet. Remaining limits:

- **Measure from the local shell.** `ttcp -r` driven through a concurrent remote-terminal session was erratic (11.8–58.5 MiB/s) against 91.2 MiB/s from the local shell on the
  same boot, as the network-throughput record already says.
- **A second loss in one window waits for the retransmission timeout:** one native 2 KiB send in six stalled about 1.09 s, because fast retransmit repaired the first missing
  segment and lwIP resent the second only on its timeout. Watch for repeats before changing loss recovery.
- **Receive into a RAM file is consumer-bound:** into `tmp://` it reaches 45–57 MiB/s while the advertised window falls close to zero; the writing program is the limit, not TCP.
- **Not implemented:** path-MTU discovery (routed peers get 536-byte segments), window scaling (windows stop at 65,535) and SACK.

## DHCP maintainer and client limits

[DHCP](devices/dhcp.md) renewal, rebind, expiry and background discovery live in the trusted setup session, and detected failures attempt to clear
IPv4 before stopping. An unexpected maintainer fault or indefinite scheduling stall leaves no independent kernel lease-expiry backstop and needs a
reboot; revisit with explicit supervision or kernel deadline ownership, not two uncoordinated lease authorities. DHCP does not probe address conflicts
or persist leases across reboots (revisit conflict detection for networks with competing static addresses), new programs receive the current DNS while
existing ones keep their startup `DNS_SERVER`, unassigned input accepts broadcast replies (a server ignoring the BOOTP broadcast flag can prevent
acquisition), and clearing or replacing IPv4 invalidates concrete endpoints and listeners without restarting services that hold them.

## UDP ICMP errors and ephemeral selection

The first [UDP implementation](devices/networking.md#udp-datagrams-and-deadlines) silently drops traffic for unbound ports and delivers no received ICMP
errors, so an absent remote listener looks like packet loss until a receive deadline expires. Add bounded, rate-limited ICMP error generation and safe
matching of quoted packets before claiming full UDP host conformance, keeping completed and retired calls immune to late errors. Generic ephemeral
binding scans 49152–65535 from a rotating cursor, which is no defense against off-path reply guessing; the [DNS client](userland/dns.md) picks random
ports from [hardware-backed randomness](devices/randomness.md), and the generic allocator's policy needs a revisit for other consumers. Network authority and
resource bounds are system-wide, not per space.

## Shell redirection side effects and file aliases

File redirection opens all targets before truncating outputs, but creation, truncation and launch are separate operations with no rollback: a failed
open can leave newly created files and a failed resize or later launch can leave truncated outputs. Redirecting an output onto an input file destroys its
contents before the child reads them, even through different path aliases, because no same-file identity check exists. stdout and stderr keep independent
offsets when both name one file, so their writes can overwrite each other (descriptor duplication and merged output are not implemented). Accepted for the
first redirection scope; revisit if alias-safe copying or shared-position output becomes a requirement, noting that batch launch promises no filesystem
rollback ([shell redirection](userland/shell.md#file-redirection-and-stdin)).

## Pipe scheduling and resource limits

Native pipes have fixed 64 KiB storage and 4 KiB per-call transfers. Copied readers compete for bytes and copied writers may interleave, with no
guaranteed atomic write size or strict fairness; there is no per-process pipe-memory quota, a holder of unused endpoint copies can delay EOF or EPIPE
indefinitely, and there are no nonblocking operations, deadlines, wait sets or direct cancellation (group termination detaches blocked readers and writers
safely). Revisit when a multi-producer or multiplexed consumer needs them; [shell streams](userland/shell-streams.md) documents launch ownership.

## Batch launch after publication

Batch launch prepares all one to eight children before any can execute and starts none on failure, but ordinary ungrouped batches do not cancel a running
child when a sibling faults, an observer closes or the launcher exits (group supervision stops members on final CONTROL loss), so a child waiting on
terminal input or other work can run indefinitely after a peer finishes, and filesystem creations or truncations before launch stay visible after a
preparation failure. Foreground pipelines wait for all children, so an unrelated or terminal-blocked stage can keep the shell waiting after the last stage
finished, and last-stage success does not hide earlier diagnostics but lets scripts continue (no pipefail). Revisit scoped cancellation or larger batches for
a concrete lifecycle requirement ([shell pipelines](userland/shell.md#foreground-pipelines)).

## Exact line limits in head

Head's line mode reads one byte per backend call so it never consumes past the requested newline, so stdio read-ahead cannot serve it and long lines cost one
call per byte (byte mode keeps bounded bulk reads). Revisit buffering or a native bounded-delimiter read only when a consumer needs both throughput and exact
stream consumption, without silently discarding read-ahead. Multi-file headers and extra head options are outside scope
([head usage](userland/shell.md#bounded-input-with-head)).

## Endpoint cancellation and capacity

[Endpoints](interfaces/endpoints.md) have no external cancellation, wait sets or wait-for-capacity, and admission to a full endpoint fails immediately. The
live-work limit is sixteen delivery records: queued messages, unfinished provider receipts and CALL outcomes awaiting collection use them, and genuine
exhaustion reports QUEUE_FULL (completed work is reclaimed synchronously at final receipt release, which fixed the earlier throughput failures recorded in
the [I/O and IPC baseline](development/io-ipc-baselines.md)). A provider can hold all sixteen slots by leaving receipts unfinished, a deadline releases the caller
but not a delivered receipt or the provider's attachment handles, and calls without a deadline can wait forever (including self-calls or cycles between
blocked single-task processes). Control notifications stay deliverable at full capacity if the provider finishes the retained work. Revisit with asynchronous
service scheduling and explicit cancellation and wait APIs, preserving delivery and outcome reporting and receipt ownership, never revoking delivered
attachments silently, and without retrying or pacing away admission failures. Group termination already cancels its callers and detaches receivers while
preserving outside provider receipts.

## Service startup failure before publication

The namespace publication command waits on the provider's registration endpoint. Providers can report setup failure before registering (the launcher
acknowledges it and optional startup continues), but if a launched provider exits or faults without reporting, the parent cannot wait for either IPC or
process exit and startup can stay blocked; a provider CALL deadline bounds its own wait, not the parent's RECEIVE. The manually invoked pipe and IPC
benchmarks have the same limit (a companion faulting before its readiness or result message leaves the coordinator in RECEIVE). Revisit with endpoint and
process wait sets or a bounded receive facility, never inferring readiness from launch success, adding automatic restart, or describing a CALL deadline as
process termination (see [process termination](#process-termination-and-ctrl-c) and [I/O and IPC baselines](development/io-ipc-baselines.md)).

## Provider calls through synchronous file helpers

The shared FILE helpers and ordinary path and libc opens submit calls without a deadline (native provider OPEN exposes an explicit caller deadline), so a live
provider that stops replying can block file readers and shell input redirection indefinitely; provider exit or withdrawal releases the waits but is not
automatic. The immutable text service does no blocking work, and the HTTP fetch library bounds one fetch to thirty seconds or an earlier caller deadline, which
does not bound queueing or invocation through the shared helpers. Revisit caller-controlled bounded file and open waits alongside cancellation and wait sets,
with no hidden retries or global timeout.

## HTTP framing compatibility

The [HTTP library](userland/http-fetch.md) rejects duplicate or list Content-Length, folded fields, non-CRLF headers and unsupported transfer or content
codings, and the pinned chunk decoder rejects excessive framing overhead, so some valid origins fail before the body limit. Close-delimited responses cannot
prove an orderly EOF was meant to end the content. Revisit when widening client compatibility, never silently accepting ambiguous framing or publishing partial
bodies.

## HTTP redirects

[Redirects](userland/http-fetch.md#redirect-chains) remain GET-only and capped at ten
hops. Custom-CA instances refuse any origin crossing, including HTTP upgrades;
there is no hidden public-only trust fallback. Revisit broader trust/replay only
with a concrete consumer and a separate policy decision.

## Response metadata through fopen

The [FILE accessor](userland/http-fetch.md#redirect-chains) exposes successful-open
status/media type/final URL, but failed fopen still reports only errno. Revisit
failed-response diagnostics or representation discovery when a consumer needs them.

## HTTPS trust and platform limits

The [HTTPS provider](userland/https.md) uses a pinned Mozilla-derived PEM export without Mozilla's additional trust constraints, verifies chains, names and
dates, and has no revocation source or policy, so a revoked certificate can stay accepted while other checks pass; trust updates are manual image and ports
updates followed by provider restart. Revisit richer constraints, revocation and update policy for a deployment that needs them, without silently downloading
policy. It accepts DNS names only (numeric-address URIs fail as unsupported because IP-only SAN verification and SNI are unimplemented; do not route them
through DNS/CN matching), and scheme authority and optional custom roots do not confine destinations (revisit destination policy for isolation).

Entropy comes from the [hardware-backed random capability](devices/randomness.md): the kernel ChaCha20 generator seeds from VirtIO when present, otherwise
checked CPU RDSEED/RDRAND, with no independent source mixing. An initial or required seed failure stops random reads and TLS operations needing new material
until a later full attempt succeeds, provider startup failure can leave HTTPS unpublished, and initial TCP identity failure disables new kernel TCP connections
for that boot. The owner must confirm the generator and startup path natively (QEMU CPU reads are guest evidence). UTC remains subject to the
[wall-clock limits](#wall-clock-time-and-clock-source-performance). TLS buffers, chain depth and the 2 MiB counted allocation cap deliberately reject oversized
handshakes; controlled connections show fit for the measured roots and peers, not all chains or suites, and deadline checks between operations do not preempt
CPU-bound cryptography. Revisit with demonstrated endpoint or scheduling demand and bounded ownership, never weakening verification or silently raising budgets.

## HTTP provider responsiveness

Each [HTTP/HTTPS provider](userland/http-fetch.md) task performs synchronous fetches, so while it fetches, existing snapshot reads and lifecycle processing wait
behind it and retired bodies can keep occupying the storage budget. A fetch has a 30-second budget or earlier caller deadline, ordinary file helpers still submit
unlimited IPC waits, queued work can compound the delay, and cancellation cannot interrupt a blocking network operation instantly. Revisit with asynchronous
service work and wait sets; no worker-process or thread framework is included.

## Configured mount discovery latency

Deferred by the owner on 2026-10-04 from the review of [PR #389](https://git.internal/PyxisOS/pyxis-os/pulls/389) (`0d82e57`). With `CONFIG_XHCI=y`, configured
GUID mounts wait for sealed boot discovery across all controllers and terminal GPT scans even when the selected disk is VirtIO, so slow USB discovery can delay
a startup mount such as `system://` or exhaust its deadline (enumeration and mount requests have existing 30-second budgets, and waiting adds no mount budget).
The native startup cost is unmeasured; on a later native USB mount boot record discovery completion, GPT scan completion and mount readiness with the controller
topology and target backend, and revisit latency policy with those numbers while preserving duplicate-GUID detection and explicit partial-discovery results.

## Configured GUID and boot-device identity

Deferred by the owner on 2026-10-04 from the same [PR #389 review](https://git.internal/PyxisOS/pyxis-os/pulls/389). The accepted observed-uniqueness policy permits
the sole observed matching GUID under partial discovery, so if the intended internal system disk is not observed (not ready, or no driver) removable media with that
GUID can supply `system://`, and if both matching disks are observed the duplicate prevents mounting and can block startup; a GUID authenticates neither disk nor
contents. Revisit selection before supporting installed systems on NVMe or another internal-disk backend. Preferring the boot device's identity is only a review
proposal, with its discovery, authority and lifetime contract undiscussed; current read-only USB boot selection is unchanged.

## Native mount design limits

[Native mounts](devices/native-readonly-filesystem.md) select one configured GPT disk identity, explicit partition and volume, with access governed by capability
grants (no principals or persistent permissions). Observation exposes identity and shared-pool capacity but not usage, quotas or charged bytes. Concurrent external
image modification, hotplug and unmount are unsupported, and mounted pool state (dirty contents and errors) survives final handle closure until reboot; revisit
teardown with explicit synchronization, shared-mapping and dirty-data ownership.

Implementation bounds, not format limits or aggregate memory admission: 32 worker jobs with a cooperative 30-second deadline; adapter storage of 1 MiB and 1,024
wrappers; per pool up to 4 MiB of cached file payload plus entry metadata, a 512 KiB metadata read cache plus keys, and for a writable pool 520 KiB for 128 journal
images and encoding buffers; a retained allocation bitmap of one bit per block rounded to 4 KiB (32 KiB for 1 GiB, about 8 MiB for 256 GiB) read and validated at
mount and never evicted; mount scans the selected volume's inode file to build a free list whose reclaimed slots keep their inode allocation as a list node until
reuse, so large inode files can exhaust memory or mount slowly (revisit compact free-slot storage or a pool budget, preserving NO_MEMORY/LIMIT versus corrupt-image
reporting); executable capture of one image up to 128 MiB per selected executable into reclaimable pages outside the wrapper and cache limits, with no aggregate staging budget; userspace root selection
bounded to 16 entries within 64 KiB of startup and capture storage. Memory pressure wakes the filesystem worker after allocator work to flush dirty data and return
whole clean cache chunks to VM (failed writeback preserves dirty chunks, the allocating call is not retried, and kernel heap backing stays mapped); revisit
reclaim granularity and admission with measured pressure workloads and BSP ownership intact.

All native reads and writes go through the BSP worker, including cache hits; directory lookup is linear, and directory, inode-file and indirect pages have a best-effort
[128-page clean cache](devices/npfs-metadata-cache.md) with linear physical-home lookup that pressure can empty ([measurements](development/experiments/npfs-metadata-cache/README.md):
lower warm-open time, no payload or sync gain; revisit indexing or size with a larger working set). Contiguous file data and journal payload use bounded runs, checkpoint
groups adjacent homes, and an optional pressure-reclaimable 128 KiB per-pool gathering buffer is best effort, while device limits can split runs and fragmented writes
wait on separate requests ([batching record](development/experiments/npfs-io-runs/README.md); revisit broader scheduling with a consumer workload, see also the
[task-3 measurements](development/experiments/native-filesystem-task3/README.md)).

Qualification: uncertain backing failure and allocation pressure are source-reviewed; committed-journal recovery has run
([adapter qualification](devices/filesystem-native-adapter.md#task-3-validation)); the
[disk-full and retained-open follow-up](development/experiments/npfs-runtime-qualification/README.md) exercised delayed ENOSPC, background retention, one-time error
reporting, recovery after freeing space, reads after unlink, final-close reclamation and pending detached cleanup after an unclean restart (stopping before
reclamation, not mid-batch); and the [populated-pool review](development/experiments/native-filesystem-task3/populated-pool-review.md) validated the retained-bitmap
fix. No fault injection or physical-media validation is claimed.

## Native filesystem design limits

The [format design rules](devices/filesystem-readonly.md#design-rules) have [implemented codecs and host tools](../fs/docs/npfs-host-tools.md), and Caelum owns the
mounted inode, cache and writer state. Accepted limits: 64 volume slots, about 513 GiB per-file block-pointer capacity and linear directory lookup (revisit when a
workload exceeds them; reserved bytes and feature flags give extension points), and volumes can exhaust the shared pool with quotas and starvation policy deferred.
128 MiB is the initial 256 GB target setting, not a measured optimum, and journal capacity is chosen per pool with no v1 resize.

One transaction commits and checkpoints at a time; cleanup delays space reuse and large shrinking truncates stall further writes and resizes of the inode. Create and
file rename no longer flush unrelated cached files, replacement rename flushes the moved file before discarding the old name, and shrink flushes its target (both can
still fail on that file's own delayed allocation), while rename without replacement makes the directory edit durable without synchronizing moved-file contents, and cache
exhaustion and explicit or background sync still flush the pool ([namespace record](development/experiments/npfs-namespace-writeback/README.md)). Revisit target-only
shrink writeback with a partial-block and pending-growth contract if a workload must truncate despite its own disk-full error. The accepted sync completion point is
durable COMMITTED with checkpointing continuing before the next commit, and the writer's free-inode list avoids per-create scans at mount-time cost
([task-3 record](development/experiments/native-filesystem-task3/README.md)).

Creation and modification times are signed 64-bit Unix nanoseconds clamped on write, so extreme dates lose precision, wall-clock values can repeat or move backwards
(not unique change counters), and a missing clock leaves the timestamp explicitly unknown without failing the mutation; revisit for wider dates or a stronger
change-detection contract. Unknown required features refuse opening, unknown read-only-compatible features refuse writes (including recovery), unknown compatible
features are ignored, and conflicting valid headers need repair (accepted rules). Kernel read-only mounts and image-only tools refuse committed journals, the Linux FUSE
adapter provides a RAM replay view, writable fsck replays, and the owner accepted no home-metadata checksums in v1 (once the journal clears, later corruption is not
always detectable; revisit with a feature-gated layout change if integrity needs justify it). Committed replay and pending detached cleanup after an unclean restart have
runtime coverage ([adapter](devices/filesystem-native-adapter.md#task-3-validation), [follow-up](development/experiments/npfs-runtime-qualification/README.md)), while arbitrary
mid-batch interruption stays source-reviewed. Formatting, checking and inspection need unchanged standalone regular images and cooperating locks, stage replay payloads in
memory (large images can exhaust host memory), and neither repair arbitrary damage nor reclaim cleanup lists. Physical-media wear is unmeasured and native latency and QEMU
target bytes (nested VM) do not qualify SSD endurance.

The [read-only Linux mount](development/npfs-linux-mount.md) also accepts npfs partition devices. Its source must stay unchanged for the whole mount (image locks only
coordinate cooperating tools, and devices have no writer exclusion), Linux can cache that view, and directory handles keep sorted metadata snapshots that can exhaust host
memory for very large directories. Committed journals are fully validated and replayed into RAM without source writes, with the payload and sorted home-block index
living until unmount plus a temporary pool-sized target bitset, so large pools or journals can exhaust host memory and fail the mount; the recovery view shares fsck's
payload validation, not its writable admission or sequence increment, and the [task-2 record](development/experiments/npfs-fuse-task2/README.md) did not measure worst-case
replay memory or startup. Native timestamp xattrs preserve unknown status and creation time that Linux attributes cannot represent, and access and change time and
allocated-block accounting are not native metadata. Revisit snapshot and caching policy, or a shared-writer protocol, with large-directory or recovery workloads.

GPT selection, automatic mounting and host writes are separate follow-ups: a host writer needs an explicit sharing boundary with Caelum's writer, and per-user visibility
awaits the [users milestone](wip/users-and-authority.md) (today every volume is visible to the mounting user).

## Execution-group shutdown

[Execution groups](interfaces/execution-groups.md) support termination and cleanup completion. Published BSP/HOST loans finish before their caller retires, so a stalled
host operation can delay completion indefinitely; killing members cannot roll back completed external effects or recall capabilities delegated outside the group, and
completion excludes legitimately external owners and independent protocol maintenance after native ownership ends. Revisit bounded HOST cancellation when transport
ownership can be revoked safely (a timeout is not permission to free lent process state). Group and member allocation has no quota beyond available storage, and the
remote server's limit of four sessions is not a descendant or memory quota. Foreground interruption is separate work.

## System-information observation limits

[System information](interfaces/system-information.md) caches one guest-visible BSP CPU brand and the online logical count at boot, so it cannot describe a
heterogeneous machine, hotplug or a process CPU allowance (revisit when CPU lifecycle or scheduling contracts change). Allocator counters exclude permanent
reservations and expose system-wide usage to every READ holder, so keep the **Memory (allocator)** label; installed memory needs its own authoritative source. The
embedded source commit identifies the kernel's base checkout, not whether inputs were modified, and no dirty suffix or clean-tree attestation exists; revisit when
release or support workflows need that distinction, after agreeing which inputs count.

## Fastfetch first-port boundary

The [implemented port](userland/fastfetch.md) uses explicit native-URI JSON/JSONC configs, one-shot text or JSON output and eleven selected modules, and is packaged in
the normal image. Automatic config discovery, config and cache writes, dynamic refresh, image logos, Lua and executable or network helpers are excluded, so upstream
configs may meet unsupported diagnostics or fallback behavior and the port adds no strict option validator. Narrow terminals keep upstream layout so lines may wrap
(use `--logo none` or shorter formats). Disk reports only explicitly selected native roots with observation authority; shared-pool capacity is labeled separately, usage,
totals, quotas and percentages stay unavailable until the filesystem observation contract defines them, and bindings or volumes sharing a pool ID repeat its capacity, so
rows must not be summed. Folder and glob filters are unsupported (their Unix path grammar is not a binding selector). Revisit individual features, layout or Disk values only
with a concrete native use case and contract, never inferred used/free arithmetic or changed upstream formatting.

## xHCI hardware profile and runtime retention

Native xHCI initialization is [enabled by default](devices/usb-xhci.md) (owner request, 2026-10-05); broader controller and recovery qualification are pending and the default
expands no supported profile. `CONFIG_XHCI=n` skips native controller preparation and workers. The [initial controller](devices/usb-xhci.md) has QEMU coverage and limited
owner-reported ThinkPad evidence, with independently discovered controllers, and requires firmware memory decoding for a page-aligned BAR0 prefix, interpreted extended
capabilities within that 4 KiB prefix, 64-bit DMA, 4 KiB pages and MSI-X; other profiles, power management and insertion after the startup snapshot are unsupported. QEMU
advertises zero scratchpads and 32-byte contexts while the [ThinkPad run](targets/t14-gen1-amd/usb-bringup.md) saw nonzero scratchpads and 64-byte contexts and traversed the
dock's USB 3 hub with root and descendant storage probes; nondefault PSI mappings and BIOS ownership handoff are unmeasured and recovery is unexecuted. The first native
installation mounted one qualified stick writable from a built-in port, which qualifies no other controller, port or device.

USB 2 root-port reset has no explicit connect-debounce interval: the startup snapshot waits 20 ms only after the driver powers a port, with no link-settling wait when power was
already on or port power control is absent, so a USB 3 link still initializing after controller reset can be missed and stay unreserved until reboot (a vendor reset-delay quirk
also needs evidence). Revisit debounce and bounded settling with physical evidence. A legacy handoff timeout leaves the OS-owned request asserted and firmware may release BIOS
ownership later, but Pyxis neither retries nor reclaims the controller that boot; settle the timeout rollback policy with firmware ownership evidence. BAR sizing saves and
restores the assignment but is not compared with the original bootstrap base (revisit with PCI mapping and profile validation, keeping one authority for mapping identity).

Each controller worker admits its own commands serially and checks notifications and health every ten milliseconds even when idle (up to 100 polling opportunities per second),
and the shared MSI-X vector notifies every active controller per delivery, so unrelated workers may wake; CPU wakeups and laptop power cost are unmeasured, and rings, polling
and deadline budgets are initial choices (revisit event-driven waiting and health-poll costs once transfers provide a workload). Runtime stop retains claims, mappings,
slot and command records and DMA backing until reboot, even after confirmed halt, following shared-VM ownership; reclamation belongs with the VM/device lifetime work, not an
allocator-lock workaround.

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

The [mouse milestone](wip/bluetooth-mouse.md#proposed-firmware-integration)
subsequently accepted pinned cold assets and reuse only of qualified compatible
warm builds, with provisional development evidence explicitly assigned. Task 3's
[firmware readiness record](development/experiments/bluetooth-firmware-readiness/README.md)
documents mirrored, licensed assets, bounded secure upload, real boot events and
mandatory DDC. Warm skip/DDC is measured; native cold upload remains unqualified.
Capture cold version/boot parameters and qualify upload, boot and version on the
owner's next native batch. Successful loading alone does not satisfy the accepted
full transport, encrypted bond/reconnect and HID/pointer compatibility rule.

### USB interrupt-IN initial profile and failure retention

The private [interrupt-IN path](devices/usb-interrupt-in.md) follows the owner's initial profile for
[Bluetooth task 3a](devices/ax200-bluetooth.md#accepted-interrupt-in-decisions): boot-present, root-connected full-speed endpoints only, so behind-hub periodic endpoints and
other speeds (including HID consumers on them) are unavailable; revisit admission and periodic/TT handling when a device needs another profile, with its descriptors and
hardware evidence. Qualification covers AX200 passthrough behind emulated xHCI, not native periodic transfers. For the internal AX200, active removal may quarantine the whole
controller and stop unrelated storage, retaining backing until reboot, and a STALL is a terminal stream failure with DMA backing and ring identity retained until reboot and
no automatic recovery, so a stalled stream cannot resume that boot and controller-wide removal handling is the usual case for a persistent receive. Revisit with separately
scoped endpoint and device retirement and periodic recovery before widening hotplug or recovery guarantees; confirmed halt alone does not change the retention contract.

## USB descriptor bounds and per-port preparation

[Enumeration](devices/usb-enumeration.md) inspects every advertised configuration with an initial 4 KiB descriptor and control budget (a larger configuration makes
inventory incomplete) and retains up to 512 validated interface records per controller (overflow is partial); unknown or vendor classes are valid unbound
observations. Storage probing, kernel block registration and configured mount authority use per-device support across controllers, and qualified disks mount
writable. Revisit the bounds with concrete descriptor or topology requirements.

All advertised ports get input and output contexts and an EP0 ring and control buffer before AP startup, four pages and range records per port even when empty.
That fits QEMU's eight ports but uses the shared VM range budget and can fail preparation on larger controllers; revisit with physical port-count evidence (runtime
allocation and reclamation need the VM ownership work, not allocator locks). The BOT consumer reserves a four-device bulk pool per controller in one 528 KiB DMA
arena plus a 64 KiB non-DMA read scratch buffer, exhaustion is an explicit per-device unsupported result, and unused storage backing stays until reboot. Two captured
I/O slots per supported disk and snapshot capacity are reserved before AP startup and GPT USB scans share one scratch buffer. Hub discovery uses a pre-AP descendant
pool (32 per controller, capped by Slot capacity after reserving roots) in one owned DMA arena that keeps all backing, 512 KiB per controller, even with no hub, and
allocation failure can fail that controller's preparation. Revisit these budgets and the root reservation policy with topology and resource evidence, without runtime
mapping outside the VM contract.

Topology and profile limits: the shared startup deadline can expire on large trees (exhausted branches are partial); low-speed paths are unqualified; QEMU's hub
exercises full-speed descendants only and its SuperSpeedPlus profile is not exercised. The owner-reported ThinkPad runs exercised full-speed descendants behind
high-speed hubs and the dock's SuperSpeedPlus USB 3 hub with reads from its SuperSpeed storage descendant
([native run](targets/t14-gen1-amd/usb-bringup.md#2026-10-04-read-only-storage-and-usb-3-hub-follow-up)); recovery, other link or firmware profiles and broader TT
qualification are pending. Only standard symmetric Gen1/Gen2 one- and two-lane downstream links attach (absent or ambiguous controller profiles stay partial; do not
pick a speed ID), and categorical inventory omits directional rates and lane counts, with SSP isochronous byte budgets uninterpreted until non-control scheduling
needs them. Hub descendants are not monitored after publication, so idle downstream removal retains slots and backing until reboot (root removal retires the subtree
and active request errors quarantine the controller); revisit with hotplug and lifetime work. USB 3 inspection omits SET_SEL and SET_ISOCH_DELAY, so complete
inventory is not full USB 3 conformance (revisit with path-latency accounting before power management or non-control scheduling, never sending zero placeholders), and
USB 3 traversal keeps the conservative USB 2 stability and recovery delays with no explicit warm-reset retry.

Each device admits one active control request and each admitted BOT device serializes private bulk exchanges. Owned stalls have bounded endpoint recovery (including TT
cleanup and safe dequeue retirement), while other early errors, deadlines or removal during active work stop the whole controller and retain unresolved DMA until
reboot. BOT probes and kernel block reads ran 512/4096-byte reads and large-LBA SCSI commands in QEMU, including hub descendants and several controllers; GPT waits for
terminal USB discovery and scans retained candidates without treating partial discovery as a global failure. Stall, TT and reset recovery, active abandonment, ring
wrap and nonzero alternate selection are source-reviewed without forced-error validation (revisit with natural device evidence).

The [BOT/SCSI probe](devices/usb-storage.md) accepts one non-composite BOT interface, no streams and one LUN; multiple LUNs, other interface shapes and READ CAPACITY (16)
protection-enabled geometry are unsupported. Configured native GUID authority supports USB mounts after sealed discovery and terminal GPT scans (observed uniqueness is
accepted under partial discovery, so unseen disks may hide another matching GUID; duplicate observed matches fail and selected-disk errors never fall back), and installer
raw authority accepts retained USB candidates after discovery is sealed, permitting partial USB coverage while refusing lost registry records or incomplete VirtIO
bookkeeping, with sole-eligible selection and typed consent applying only to observed disks (the native C.4 installation listed its stick while unsupported EHCI kept
coverage partial). Normal boots grant no raw service, and write claims exclude mounted or claimed devices and latched write failure. Revisit inventory coverage and
target selection with further native evidence, never inferring a complete inventory from a successful installer list. A runtime READ rejection with valid sense fails only
its ticket (transport stays READY), whereas failed sense, real transport failure, timeout and unsafe host states are terminal; these paths are source-reviewed only. NOT
READY media (including 04h/01h becoming ready) fail immediately, and bounded UNIT ATTENTION retries implement no spin-up policy; revisit a bounded wait only with natural
device evidence within the media deadline.

## USB writable-media qualification limits

C.1 requires known WP-clear protection and a successful real blocking SYNCHRONIZE CACHE (10) before enabling writes and flushes. MODE SENSE (6) captures only its four-byte
header, with fallback to the eight-byte MODE SENSE (10) header only on current ILLEGAL REQUEST invalid-opcode or invalid-field rejection; unknown protection, unusable
optional headers and clean qualification rejection leave healthy media readable but not writable, and no MODE SELECT or write-cache change is attempted. Do not infer
writable or flush support from vendor IDs or successful reads; revisit compatibility only with natural device responses needing a bounded extension.

Two compatibility watchpoints from merged [PR #395](https://git.internal/PyxisOS/pyxis-os/pulls/395) stay deferred (the first natively qualified stick, the C.4 install
target, triggered neither). A device that cleanly rejects SYNCHRONIZE CACHE stays read-only even if its firmware has no volatile write cache; querying the caching mode
page and Write Cache Enable state is a possible extension guided by device evidence, not an accepted alternative or proof of durability. And an optional MODE SENSE or
qualification synchronization exchange that breaks transport fails the whole media probe even if earlier reads succeeded (returning to read-only after a successful reset
is only a proposal needing a recovery policy and verified transfer ownership). Revisit both after observing an affected expendable device.

The five-second exchange deadline and shared boot-media deadline also bound synchronization, so a slow genuine flush can retire a capable device; revisit with measured
physical flush latency. QEMU completion and restart checks qualify no firmware, cache behavior or power loss, and only one physical stick has qualified. A failed runtime
write or flush, or an abandoned published mutation, latches write failure for the boot (reads stay admitted on healthy transport and the filesystem may keep its own
writeback error), with no replay or later successful flush clearing either uncertainty; revisit recovery only with an error-acknowledgment and ownership contract. Mutation
failure and abandonment, MODE SENSE fallback, unsupported flush and malformed qualification responses are source-reviewed only.

## USB controller and transport coverage

xHCI is the only USB host-controller driver. EHCI, OHCI and UHCI controllers (such as the ThinkPad's Realtek DASH EHCI) remain unsupported inventory records, `lsusb`
reports partial coverage, and disks behind them are invisible to configured mounts and the installer. [USB storage](devices/usb-storage.md) uses Bulk-Only Transport only: a
device offering UAS as an alternate is used through BOT (throughput cost unmeasured), a UAS-only device is unsupported, and classes other than hubs and storage, including
HID, stay unbound. Revisit when a target device or workflow needs another controller type, UAS or a USB input class, adding each through the existing
[layer boundaries](devices/usb-installation.md#layers-and-ownership).

## Random generator trust and availability

The [BSP-owned ChaCha20 generator](devices/random-generator.md) seeds and reseeds from the selected VirtIO or CPU source (global construction, demand-driven 40-byte
seed and reseed policy and unchanged grant accepted 2026-10-08). The [qualification](development/experiments/random-generator/generator.md) covers the RFC vector, erasure,
both natural reseed triggers, TLS and cancellation, and nested-KVM latency and throughput; native performance and CPU seed supply under heavy load are unmeasured.
Hardware and hypervisor trust remains: CPU boot and runtime checks catch specific obvious failures, not bias or malicious hardware, and mixing one selected source gives
no independence or entropy certification. A required reseed that cannot complete stops random reads and TLS operations needing new material until a later full attempt
succeeds, and permanent source failure disables them until reboot (an accepted availability trade-off including carry-clear exhaustion under load, with no stale-seed or
predictable fallback). Requests pay BSP scheduling and share eight slots (unconsumed completions included); state compromise exposes buffered and future output until an
independent reseed, completed slots and caller memory may retain delivered bytes, and whole-VM snapshots or clones can duplicate initialized state. Revisit independent
source mixing, snapshot recovery, a persistent seed or per-CPU state only with a threat model or measured need, and native seed and performance qualification when owner
hardware is available, keeping trust claims separate from the health and RFC checks.

## PS/2 mouse synchronization and routing

The [PS/2 mouse](devices/mouse.md) realigns packets only by the first byte's always-set bit, so a byte lost inside the device can give wrong motion or buttons for a few
packets, and its IRQ 12 route must share the keyboard's I/O APIC (otherwise the mouse is unavailable). Reconsider if native packets show drift a short inter-byte timeout
would catch, or a target routes IRQ 12 elsewhere. Only that stream is supported: the ThinkPad touchpad stays in firmware relative mode without scrolling or multi-finger
input and TrackPoint motion arrives mixed into the same stream; Synaptics absolute mode is a revisit for gestures or scrolling. USB HID mice need a HID boot-protocol driver
on the private interrupt-IN path (root-connected full-speed only, see [USB interrupt-IN](#usb-interrupt-in-initial-profile-and-failure-retention)), and Doom has no mouse
support.

## Lua build runtime limits

[Lua](userland/lua.md) supplies io/os, pure-Lua modules and native build helpers; `file:setvbuf`, `io.popen`, `os.execute`, `os.clock`, `os.setlocale`, debug, full math and
dynamic modules are absent, and `os.time` accepts wall time only (calendar-table conversion needs a `mktime` policy for ambiguous and nonexistent local input). Revisit each
for a concrete consumer. `os.tmpname` reserves a real exclusive empty file that callers must remove, and `io.tmpfile` creates then immediately unlinks one, so abrupt death
between those operations or a failed unlink can leave a named file with no stale-name cleanup (revisit atomic anonymous creation only for a lifecycle need; reserved names
are not ISO C `tmpnam`). `pyxis.run` inherits live C streams, omitting closed ones, but cursors, append mode, pushback and read-ahead belong to the parent runtime, so child
streams begin at zero, unread buffered pipe bytes stay in Lua and `io.input`/`io.output` rebinding is local; revisit shared stream state only with a native ownership design.
C-locale `strftime` supports standard conversions and E/O forms without width or flag extensions, `%z` loses historical offset seconds at minute precision (`tm_gmtoff` keeps
them), a `tm_zone` designation is borrowed until timezone-cache replacement or exit, and locale selection and reverse calendar conversion are deferred.

## Sorted ls memory and live file details

Native [ls](userland/ls.md) collects all names of a directory before sorting, so memory grows with entry count and name bytes and exhaustion reports failure without a
truncated listing. Terminal colors may add one file lookup and up to two content bytes per non-program file and long format queries sizes, none forming a snapshot with
enumeration, so they can fail after names were collected (unknown sizes stay explicit; unreadable script prefixes fall back to the regular-file color). Terminal names show
one printable ASCII cell per byte with `?` for control and non-ASCII bytes (plain output preserves bytes). Revisit memory and lookup costs with directory workloads that
exhaust memory or list slowly, and Unicode presentation with an agreed character-width contract; ThinkPad and disk-backed listing qualification is unperformed (evidence is
nested QEMU with archive, RAM and HOST directories).

## Native cp staging and recovery limits

[cp](userland/cp.md) uses exclusive sibling temporary files and held-directory rename and removal. Accepted 2026-10-07: other writers must leave the temporary file and name
untouched until completion because native mutation APIs do not bind a name to the held file identity. Source data stays live (copying its initial size), so concurrent
overwrites can mix contents and same-file aliases replace the object. Staging needs destination CREATE/WRITE_FILES/REMOVE, not permission to write an existing file alone, and
has no direct-truncation fallback. Recursive copy (`-r`) keeps a partial tree after a failure with no rollback, and there is no recursive remove, so it is deleted by hand; trees beyond 32 levels or
65,536 entries are refused. Interruption can leave a named temporary file or a `.cp-tree-` marker, unconfirmed creation or publication is reported without retry or removal, failed cleanup can leave partial storage, and there is no stale-file sweeper or crash-durability
guarantee, so operators must establish which names exist before removing anything. Revisit reservation and publication primitives if cp must tolerate another writer changing
its temporary, and cleanup policy when persistent use needs recovery from interrupted copies. Provider sources, native disk copies, durability and ThinkPad usage are
unqualified (evidence covers archive, RAM and HOST copies in nested QEMU).

## Bluetooth HCI connection handle reuse boundary

Accepted 2026-10-08 for [runtime HCI task 2](devices/bluetooth-hci.md): fail closed if the controller reuses a previously disconnected connection handle, because independent event
and ACL endpoint ordering cannot establish which link delayed bytes belong to and a new generation alone is insufficient. Known retired-link ACL is discarded without hiding the
disconnect event or disabling storage. This restricts repeated connections within one controller lifetime; establish and measure a safe retirement and reuse boundary in the
connection and reconnect tasks before durable bonded reconnect can qualify (it does not relax native closure).

## Bluetooth runtime re-grant after radio work

The [HCI adapter](devices/bluetooth-hci.md#progress-and-failure) sets a sticky dirty flag on non-read-only command publication, connection admission or ACL publication, so release
or exit then leaves Bluetooth unavailable until reboot even if links and credits later settle; only fully accounted read-only sessions have a confirmed clean re-grant path. This is
conservative cleanup under the accepted exclusive controller lifetime, not measured radio cleanup or automatic service recovery. Revisit with the service and connection tasks once
they can establish explicit radio-procedure termination, receive continuity and independent USB accounting (the handle-reuse boundary is also required for reconnect).

## HD Audio jack routing at playback start

[Headphone presence](devices/hda.md#codec-and-stream-activation) is sampled only
at physical playback start. Mid-playback insertion keeps the speaker, and removal
keeps the headphone route, until the next start. Live switching is a separate
follow-up needing unsolicited-response transport and refill-safe routing policy.

## sbase tail follow and sort limits

The sbase [`tail`](userland/wc-tail-sort.md#tail) refuses `-f` and `-F`, because
Pyxis has no operation that waits for a file to grow and a polling loop would
not follow one honestly. A log or growing file cannot be followed. Revisit when a
native change-notification or wait-for-growth operation exists for the relevant
providers, and implement following on it; do not add polling or a fake success.

[`sort`](userland/wc-tail-sort.md#sort) uses libc `qsort`, an unstable heapsort,
so under `-u` the line kept from several that compare equal under the selected
keys can differ from a stable sort. It also holds all input in memory, accepts
`-m` without streaming a merge, and does not check writes to its `-o` file, as
upstream. Revisit with a stable libc sort or a patch to sort if a consumer
depends on the retained line, input too large for memory, or reliable `-o`
errors. Word splitting, character classes and folding use libutf's tables, with
no locale collation; revisit with locale support.

## Temporary bundle grant policy

The [bundle design](wip/program-bundles.md) accepted on 2026-10-09 temporarily
delivers every available ordinary grant, including optional grants at launch,
without consent in the unpacked development path. This is not a permanent security
contract; metadata never creates rights or obtains system-only authority, and programs inspect actual
startup grants. Revisit with users/permissions and the recorded required-grant,
in-context optional-grant, trusted-picker, stable-identity and revocation model.

## Published development bundle revisions

Unpacked [bundles](wip/program-bundles.md#app-views-grants-and-lifetime) rely on the
publisher leaving a selected revision unchanged and retained. Read-only app/root
grants and held native handles do not freeze writable aliases or protect unopened
assets from deletion. Revisit with installation/update lifetime and revision GC;
editing or deleting a live revision is unsupported.

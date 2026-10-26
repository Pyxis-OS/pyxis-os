# Technical debt and implementation tradeoffs

Record concrete limitations of implemented choices here: what we chose, its
cost, and when to reconsider it. This is a working record, not a roadmap or a
commitment to replace every simple implementation. Remove or update entries
when the underlying tradeoff changes.

## Git writable-close cost and editing limits

Git syncs every writable file on close, including checkout files, beyond syncing
before publication. This may add native clone/pull cost; measure it on vela before
narrowing the rule. Commit currently requires `-m`; revisit editor-based message
entry when that interaction is assigned. See the [Git reference](userland/git.md).

## Shared trust configuration

Git currently has explicit per-origin `http.<url>.sslCAInfo`; Go's default roots
still come from the pinned boot CA bundle. Private CAs therefore need separate
application configuration, and httpfs/Go do not share a system trust setting.
Revisit a proper shared store (for example `system://config/trust/`) when its
authority and update design is assigned; do not infer ambient host trust.

## Forwarder retarget representation

Owner-accepted (2026-10-14): [forwarder](interfaces/forwarders.md) RETARGET stays
within one representation; native/exported switches return WRONG_TYPE. A service
slot therefore cannot replace a native resource with an exported provider (or
conversely), even with the same protocol. Revisit with the **provider-backed
device handles** prerequisite in [interchangeable providers](wip/capabilities-and-providers.md#interchangeable-providers),
designed with the compositor. No kernel FILE bridge is assigned in this slice.

## Multiple-interface lifecycle qualification

The [multiple-interface core](devices/network-interfaces.md) is delivered, with
[native two-wired-NIC evidence](development/experiments/network-interfaces-native.md)
for overlapping prefixes, scoped operations, independent DHCP and selected
data/DNS/Remote/log sources. QEMU exercised data-carrier off/on; an owner-present
native same-port NCM unplug/replug preserved built-in management and recovered
the same interface/address epoch. That run carried no active data flow. Held
TCP/UDP work across address/default/lease changes, exhaustive cancellation/fault/
copyout interleavings and native stopped-debugger selection are not qualified
by those runs. These combinations may still hide lifecycle or cleanup defects;
ordinary successful traffic is not evidence for them. Revisit before depending
on retained-flow behavior during radio lifecycle or controller withdrawal, with
an owner-scheduled native window for physical actions. No retained-flow failover
or migration is implied; USB emergency transport remains separately unsupported below.

Automatic preference has QEMU VirtIO routing and synthetic NCM notification
coverage. Known-speed NCM versus a simultaneously usable unknown-speed peer is
unqualified: the isolated nested fixture's VirtIO never acquired IPv4; its cause
is unassigned. A native equal-rate round passed new-route fallback and management
checks, but its harness cleanup failed. Unequal-rate native preference remains
unrun; revisit on the two-NIC rig with the scheduled 2.5G switch. AX200 rates
remain unreported by its legacy data backend.
See the [record](development/experiments/automatic-network-preference.md).

## USB NCM readiness after warm reboot

Two owner-run ThinkPad warm-reboot attempts reached built-in-NIC Remote while
USB NCM was prepared/bound but link down, without IPv4 and reporting DHCP
`wait-link`: `native-dns-usb-01` and `native-unplug-01`. Their last wait-link
report ages were 3.003 s and 2.794 s, not total boot-to-ready times. For the
unplug attempt, the next saved snapshot 19.119 s later was ready, with a bound
report already 17.745 s old. No reboot timestamp establishes the full delay;
the owner estimates the failed unplug preflight began about one minute after
the reboot. Both fresh attempts subsequently passed on the same respective
boots. See the [native record](development/experiments/network-interfaces-native.md#owner-present-usb-withdrawal-and-recovery).

A working management NIC therefore does not establish readiness of the USB data
NIC, and one-shot qualification/consumer startup can refuse prematurely. The DNS
supplement now polls readiness for 30 s; the unplug observer's initial check is
still one-shot. Revisit NCM link-notification, PHY and DHCP startup timing with
boot-relative observations before depending on prompt unattended warm-reboot
readiness. No root cause or hardware recovery-time bound is established.

## USB network emergency transport

Owner-accepted (2026-10-14): [CDC-NCM dock Ethernet](wip/steam-deck.md#9-dock-ethernet)
delivers normal UDP logs, Remote and xfer; fatal UDP and stopped-kernel GDB
over USB remain unsupported. A dock-only Steam Deck therefore has no emergency
network diagnostics. Revisit before relying on network GDB for Deck driver work,
with a bounded controller-wide polled xHCI ownership handoff; a NIC-only handoff
cannot protect the shared event/command/interrupter state.

## AX200 hardware rfkill qualification

The T14 Gen 1 AMD's F8/Fn+F8 left AX200 hardware rfkill clear in native Pyxis;
no physical radio switch is known. Stage 2 therefore does not qualify live
assertion, scan refusal or release/recovery; those paths remain source-inspected.
Revisit with a control/target that actually asserts the line, before relying on
radio-kill transitions. See the [native record](development/experiments/ax200-passive-scan.md#final-restacked-native-smoke-and-rfkill-limit).

## Broad Wi-Fi connected routes

Owner-accepted (2026-10-20): overlapping Wi-Fi leases are allowed; connected
preference precedes prefix length, and a lease address equal to another
interface's gateway/DNS is refused. A short prefix can still make destinations
outside higher-preference connected routes on-link, as on Linux; this can
redirect or stall that traffic when Wi-Fi is selected. Revisit with the planned
[network trust profiles](devices/network-interfaces.md#network-trust-profile-direction),
before treating an untrusted network like a trusted LAN. No minimum-prefix rule
or trust-profile enforcement is implemented.

## AX200 open-link qualification follow-ups

Stage 3 qualifies ordinary ownership, foreground cancellation and AP-loss/recovery,
but direct native CANCEL, setup-deadline expiry, read-only-observer lifetime and
initiating-root CUT remain source-inspected. The native AP used channel 6; a live
firmware 1–13 map does not qualify RF on 12/13. Matched wired controls while associated/
under load and quantitative Wi-Fi throughput are unmeasured, so no network-cost
claim follows. Revisit these checks before WPA2 security/concurrency qualification
and performance claims; use the retained [stage-3 packet/record](development/experiments/ax200-open-ap.md).

## AX200 WPA2 qualification limits

Accepted for stage-4 closure (chronium, 2026-10-22): all four limits below remain
unqualified; accepting them does not establish the missing behavior.

Native GTK rekey is unqualified (router interval zero), as is held-link recovery
from worker death (worker lifecycle control is session-private). Host/QEMU checks
do not establish native rekey or worker recovery; revisit with a suitable rekey
AP and ordinary worker-management authority or an observed failure.

Independent over-air no-plaintext/replay evidence and firmware PN progression/
retention are also unqualified. Software key/PN checks and payload hashes cannot
prove those properties; revisit with approved monitor/firmware evidence before
end-to-end confidentiality/replay claims. See the
[WPA2 qualification record](development/experiments/ax200-wpa2.md).

## RTL8922AE startup recovery

The accepted [first firmware cut](wip/rtl8922ae-wifi.md#stage-2-firmware-transport-before-scan)
allows one startup attempt per boot and retains uncertain DMA ownership until
reboot; its native warm-reboot firmware/stop profile is qualified. Before vela
Wi-Fi is everyday-usable,
implement and qualify a real retry/reset recovery path with proven command/DMA
retirement. This bring-up limit must not become the everyday recovery policy
(chronium, 2026-10-22).

## Remembered Wi-Fi keys before the credential manager

Accepted for the connection stage (chronium, 2026-10-23): remembered networks
must reconnect across reboots until manual disconnect, and their key may be
stored unencrypted until the credential manager exists. Whoever can read that
store can read the key; limit delegated access, without claiming encryption.
Revisit the store and key delivery with the credential manager. This accepts
the interim storage design, not a claim that persistence is implemented yet.
See the [RTL connection requirement](wip/rtl8922ae-wifi.md#accepted-decisions-chronium-2026-10-23).

## Wi-Fi idle/worker cost baseline

[Stage 1](development/experiments/ax200-resource-closure.md) captured wired throughput but no native
idle/BSP-worker CPU cost; production tools expose no such accounting. Stage 2
adds IRQ and worker load without a before figure, so throughput alone cannot
exclude idle/CPU regressions. Revisit when an idle-cost measurement exists,
using the retained stage-1 image for the matched before run.

## USB NCM QEMU post-burst recovery

In the isolated Linux NCM gadget/QEMU fixture, Remote reconnect times out after
an ordinary bounded UDP burst in both the original and conditional-wake paths;
initial DHCP, ping and content hashes pass. The shared receive-path or fixture
cause is unassigned, so post-burst recovery is not qualified by these runs.
Revisit before using this fixture to certify burst/reconnection recovery or
changing RX handoff/credit handling. See the
[comparison and qualification limits](development/experiments/usb-cdc-ncm/rx-backpressure.md#conditional-wake-review-follow-up);
this is not evidence of a native failure.

## USB NCM undrained data path

An NCM adapter whose bulk IN is not read may flood 802.3x PAUSE frames; on
2026-10-19 a prepared, unbound adapter stalled the owner's unmanaged switch
until it was unplugged. The driver now always drains a prepared transport and
bounds provider backpressure ([NCM receive](devices/usb-cdc-ncm.md#retained-routes-and-recovery)),
but the adapter keeps its configuration and data path whenever Caelum stops
servicing it: a lost transport awaiting or abandoning recovery, xHCI controller
failure, kernel panic, a debugger stop, halt, and power-off or restart where the
port keeps power. Any of these can stall the LAN until the adapter is unplugged
or reset. Revisit before leaving an NCM adapter connected unattended or adding
another switch-facing USB NIC: try SET_INTERFACE alternate setting 0 on clean
power-off/restart and when recovery stops, then check with a host PAUSE counter
on an isolated port whether that silences each adapter.

## USB HID native coverage

[Report-mouse/shared-scroll task 1](development/experiments/pointer-input-quality/README.md#native-task-1-closure)
is qualified for ASUS motion/clicks/wheel and G502/Lightspeed motion/buttons/
free-spin/Cmd+R/replug. Keychron pointer, horizontal, high-resolution/Feature and
independent multi-ID behavior still lack native evidence. Revisit those paths
when the owner supplies a matching device/run, using the opt-in diagnostics;
task closure does not claim them. ASUS back/forward buttons are an accepted limit,
with no further work assigned.

The [USB boot input](devices/usb-hid.md) is natively qualified for a full-speed root
keyboard and a composite dongle ([record](development/experiments/usb-hid/README.md#native-owner-thinkpad)).
Not exercised natively: the real dock or switch hub chain with low and full-speed leaves
(letters, locks, repeat and fresh chords; unplugging and replugging a leaf without its hub,
with held-key removal, held-button lock revocation and buttonless-survivor lock
preservation); input during USB storage or Bluetooth work with trace records for admission or
loss; and keyboard LED output. Low-speed scheduling and high-speed hub
and transaction-translator routing are source-reviewed only, and QEMU has no composite or
low-speed fixture. Steam Deck lizard mode would need its actual boot interfaces, and
Bluetooth HID is separate. Revisit when a native batch includes the dock or when a device
fails to bind; luna stages these runs through the owner and records image revision,
controller, descriptors, link speeds and hub path (`lsusb -n` shows only the boot snapshot).

## USB keyboard lock lights

Lock state is shared by all keyboards and PS/2 lights follow it, but a USB keyboard
never receives a LED output report, so its Caps/Num/Scroll lights stay as the
firmware or keyboard left them. Sending one needs a control transfer on an already
bound device, and the HID runtime worker owns endpoint 0 only while it admits a
device; a lock change would have to queue a step there (and one at bind). Revisit
when a USB keyboard is the only keyboard on a target, or with the next HID runtime
change. See [lock lights](devices/keyboard.md#lock-lights).

## USB switch subtree removal

[Device/subtree removal](devices/usb-interrupt-in.md#collection-loss-and-retirement)
preserves unrelated devices when hardware fences succeed. The shared
[topology owner](devices/usb-hubs.md#bounded-runtime-topology-recovery) now admits
returning/new supported hubs and USB 2 HID children, including another prepared
controller. Runtime storage discovery is descriptive only: BOT/raw-block binding,
pool reattachment and HCI readmission still require reboot. Old generations retain
backing under finite budgets, so repeated reconnections eventually refuse further
admission, including NCM recovery. Revisit storage recovery with an explicit media
identity, mount lifetime and durability contract; revisit reclamation when a real
reconnection workload exhausts the retained pools. Native vela hub/HID and NCM
regression qualification is pending in the
[recovery record](development/experiments/usb-hub-recovery/README.md).

## Assertion panic backtraces

`KASSERT` calls ordinary `panic()`, which prints its message and halts without a
stack walk. The owner's ThinkPad boot of #852 (`63fffdb4`, 2026-10-14) showed only
the xHCI notification assertion, leaving its caller unavailable on screen.
This applies to BSP and AP assertions; it is not an observed presenter failure.
Use the opt-in [network debugger](kernel/network-debugger.md) where entry is
available. Revisit with a concrete panic-diagnostics task; no assertion-stack
reporting change is part of USB retirement.

## Remote transfer memory and staging limits

[Explicit remote transfers](userland/remote-terminal.md#explicit-file-transfer) stream with constant memory and no size limit
(owner decision, 2026-10-08) and read the source twice because the digest is announced first. Data moves in a 64 KiB window of
2 KiB chunks ([measurements](development/experiments/xfer-pipelining/README.md)). Natively, 700 MiB now takes about 19.5 s up and
28 s down (36 and 25 MiB/s); before windowing, 15 MiB took 6.5 s up and 7.5 s down. The native bound is unattributed. In nested QEMU
both directions are bound by BSP work per frame.
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
in-place aligned growth when long-lived applications make retained capacity or copying material. Gate 8 serializes metadata
and supports cross-thread free/realloc. Concurrent misses can prepare separate pools outside the guard; each published pool is
retained until process exit. Revisit surplus-pool disposal only if measured concurrent growth makes retained capacity material.

## Retained timezone designations

The threaded time conversion cache interns escaped `tm_zone` strings until process exit, keeping them valid across concurrent
TZ replacement. Repeated selection can retain additional designations even after zone bytes are released. This is the accepted
borrowed-pointer contract; revisit reclamation only with a consumer that can use owned results and demonstrates material retained
capacity, rather than invalidating escaped pointers.

## TLS bootstrap launch cost

Gate 7 prepares guarded TCB backing and binds FS through separate BSP VM transactions on every libc process launch, including
TLS-free C programs. The owner accepted the measured native launch cost of +6–9% (about +0.11 ms/launch) on 2026-10-12;
IPC and pages were unchanged. See [qualification](development/experiments/threads-task7/README.md#owner-native-cost).
Initial-thread static TLS could avoid its guarded reservation; siblings would remain guarded. This is a much-later follow-up,
not gate 8 work: revisit when launch-heavy shell scripts or native builds make startup cost material, preserving caller-only
FS authority, template alignment, destructor lifetime and context pins.

## Process exit with live threads

Libc runs `atexit` handlers, `.fini_array` and stdio shutdown only from the initial thread after every libc worker has been
collected ([gate 8 exit behaviour](userland/threads.md#creation-and-collection)). Returning from `main` or calling `exit` while
a sibling is still alive forces process teardown instead: no exit handlers run and buffered output is not flushed. SDL and other
threaded programs that exit without joining their threads therefore lose `atexit` work and unflushed output. The
[SDL2 port](development/sdl2.md#threads-and-timers) stops its own timer thread in `SDL_Quit`, but cannot join application threads.
Revisit (direction, not a design): find a Pyxis-native primitive for process-exit handlers, an `atexit`-like hook that is safe
with live sibling threads, and build libc's `atexit` on top of it.

## Allocation measurement follow-ups

The [allocation benchmark and caller-scoped profile](development/allocation-profiling.md) separate warm heap throughput, heap
expansion and private-page requests (the instrumentation reads HPET and perturbs timings; report accelerator, CPU count, live set
and host context). Standalone `kmalloc`/`kfree` throughput and deeper PMM/VM timing are unmeasured, and pool growth counters
describe backing acquired in a window, not retained memory or fragmentation. Private memory parks the caller and runs on the BSP executor or VM worker
([memory](kernel/memory.md#execution)); the page profile therefore includes queue and handoff cost. Measure the queue and worker costs of the
other executor services, including HOST forwarding, before changing allocation policy.

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
[bundles](userland/program-bundles.md) may request up to 8 MiB. The reserved guard
catches ordinary overruns, but a large adjustment can skip it; compiler stack
probing and automatic growth remain absent. Revisit with compiler/runtime and
thread-stack work, retaining the owner's small plain-program default.

## Go command lookup

Accepted (chronium, 2026-10-23): Go `exec.LookPath` returns unsupported; Go
programs cannot probe native command availability or launch subprocesses. The
common C resolver also owns bundle/grant/ABI admission, which the no-cgo runtime
does not duplicate. Revisit when a shared command-policy reuse contract is
settled; do not add a broker or weaker lookup. See [Go adapters](wip/go-runtime.md#current-adapter-delivery).

## Private-memory reservation metadata

Private reservations have no per-process count limit. Each empty sparse RESERVE
uses kernel-heap ownership and VM range records, and identity lookup walks the
process registry linearly. Many small reservations can exhaust shared kernel
metadata and increase call latency despite using little backing. Revisit count
admission and indexing when runtime reservation workloads establish a suitable
budget; retain BSP ownership. See [private memory](kernel/memory.md#sparse-memory-limits).

## Sparse private-memory retained infrastructure

Sparse decommit and failed commit release user data frames, but empty page tables
remain until process destruction; trimmed extents can retain bounded token
storage. Wait withdrawal still scans global buckets once with interrupts off.
Every address WAIT/WAKE on sparse memory linearly searches committed extents in
`owned_wait_token`, and extents do not merge. Consequence: scavenging need not
return all kernel metadata, withdrawal is outside the 256-page work bound, and
address-wait cost grows with heap fragmentation. Revisit when native Go
allocation/scavenging or contended-wait measurements make these costs material;
preserve mapping-incarnation withdrawal and BSP VM ownership. See [private memory](kernel/memory.md#sparse-memory-limits).

## BSP-only allocation and VM mutation

Private memory and DISPLAY mapping mutations run on BSP under process VM
admission; pending drainage uses a worker without blocking the common executor.
Other serial services and shared kernel range reuse still need their own handoff
and invalidation contracts. The owner accepts the remaining BSP MEMORY round
trip (native A/B averages about 1.6–4.2 microseconds added per mutation);
see [task 4 qualification](development/experiments/threads-task4/README.md).
Keep the generic zero-root/borrower fast path; no task-count bypass. BSP
ownership itself is being removed (chronium, 2026-10-22): a kernel milestone
after the work in flight replaces it with per-address-space mutation, per-CPU
frame allocation and IPI shootdown, keeping acknowledged root departure and
guaranteed cleanup storage. See the [milestone index](wip/boot-sdk-ports.md#planned-next-kernel-milestone).

The accepted IPC cost disproportionately affects short messages. Native
[#704 qualification](development/experiments/ipc-copy-leases/README.md#owner-native-ab)
shows SEND64 recovering 13 ns/message (319 -> 306 ns); CALL64/4096 and SEND4096
have no measurable change. CALL's roughly +0.45 microseconds versus pre-#700
remains after batching leases. Reply staging allocation/free, the extra payload
copy and syscall root accounting are untested candidates, not established
causes. The owner parks profiling until threads task 8, when IPC and allocation
paths are touched anyway; preserve authority, bounded lifetimes, root drainage
and late-output semantics. Gate 8 changes libc allocation/I/O, not kernel reply staging, payload copies or syscall root
accounting; its owner-native comparison is the task-8 review input for any separately assigned residual investigation.
Gate 6 adds roughly 100 ns to CALL64 fastest; the
[#733 native follow-up](development/experiments/threads-task6/README.md#scheduler-pointer-follow-up)
recovers at most 25 ns, within noise. Its residual cause is unmeasured; revisit
with the same task-8 IPC/allocation work, without further profiling now.

## PMM first-fit search under its lock

`pmm_alloc()` searches first fit under the PMM lock, skipping fully allocated 64-bit words and starting at a hint below which every
frame is unavailable; this ended the serialization measured in 7a (lock wait about 150 cycles per call with two page clients,
[record](development/experiments/smp-task7-pmm/README.md#pmm-lock)). Search time can still grow with fragmentation, and `vm_back()`
and heap growth take the lock once per frame. Revisit if a workload shows PMM lock waiting again; batching frames per call is the
next step.

## Multi-message MSI

Owner (2026-10-14): `pci_msi_prepare()` enables exactly one MSI message, even
when a function can use more; vela's xHCI controllers advertise eight. Each MSI
device therefore gets one vector, shared by all its interrupt sources: xHCI uses
only interrupter zero. Revisit when xHCI, NVMe or network queues want more than
one interrupter or queue vector.

## Extended external interrupt destinations

Local x2APIC IDs/IPIs are 32-bit, but IO-APIC and MSI/MSI-X physical targets are
limited to 255 and fail explicitly above it. No interrupt remapping is supplied;
platforms needing extended external destinations cannot use these routes.
Revisit with native hardware requiring interrupt remapping; the dense-CPU scratch
window remains separately limited to 256 CPUs. See [SMP](kernel/smp.md#local-apic-and-external-destinations).

## Scratch-slot false sharing

Each CPU's two scratch slots are adjacent PTEs, so the slots of CPUs 0–3, 4–7 and so on share a 64-byte cache line of the scratch
page table, and the `scratch_busy` flags share lines too; every mapped page uses the slots about a dozen times. In the nested VM two
concurrent page clients each took about 1.9 times as long as one alone (1.4 times with a throwaway build giving each CPU its own
line, [record](development/experiments/smp-task7-pmm/README.md#remaining-concurrency-cost-scratch-slot-false-sharing)). Natively
the cost is small: two clients took 1.15–1.25 times and eight up to 1.5 times ([task 8](development/experiments/smp-task8/README.md#native-thinkpad-check-owner-run)).

Options: spread the slots one line per CPU (an 8 MiB scratch window instead of 2 MiB for 256 dense CPU indices, moving the APIC, I/O APIC
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

Bulk FILE buffers also come from this non-shrinking heap. Their peak live data
is bounded by 16 MiB × concurrently reading/submitting processes (providers count
separately); metadata and pool rounding add overhead. After a burst, freed buffers
and their charges retire, but the peak backing stays with the heap until reboot,
reusable there rather than returned to the PMM. This can permanently reduce RAM
available to other subsystems on low-end machines. Revisit when defining the
low-end worst-case memory target or implementing heap reclamation; see the
[bulk-read contract](wip/bulk-file-reads.md#accepted-contract).

## Kernel VM range record array

Kernel-space range records are a fixed static array of 4096 entries (`VM_MAX_RANGES`, 56-byte records, 224 KiB) with a linear
free-slot scan, because early boot cannot use the heap. The cap is independent of RAM: every task stack and separately allocated
DMA buffer takes a record, and a 12-CPU laptop boot used 196 of the previous 256. Exhaustion fails with `NO_MEMORY`, for example
in task creation. Revisit with a static seed for early boot followed by heap-grown records.

## BSP userspace and kernel workers

Since SMP task 7b, user tasks run on the BSP beside the kernel workers (presentation, BSP request executor, network, native
filesystem, USB, ACPI). Ties go to the APs first and the BSP pulls only while none of its workers is runnable; a woken worker preempts
a BSP user task at the next interrupt, but there are no priorities and user syscalls run with interrupts masked, so a long BSP user
syscall delays workers and device interrupts (large private allocations now yield between bounded steps on the VM worker). With all four CPUs loaded in the nested
VM ttcp and RAM-file writes stayed within their spread ([7b record](development/experiments/smp-task7b/README.md)), and the SMP
task 8 native check saw no dropped keystrokes or frames under load ([record](development/experiments/smp-task8/README.md#native-thinkpad-check-owner-run)),
though display smoothness and input latency were judged by feel, not measured. Nested, with user tasks saturating CPU 0, a woken
audio worker waited 1.1 ms (4 CPUs) and 2.6 ms (1 CPU) on average, mostly behind them, and audio restarts rose. Natively, with 12 CPUs, the reaper waited
tens of microseconds. CPU 0 worker precedence is therefore parked, with an accepted design: a woken worker gets 2 ms of CPU
ahead of CPU 0's user tasks ([record](development/experiments/worker-precedence/README.md)). Revisit if a native workload
keeps every CPU busy (all-CPU builds) or interactive use shows worker or presentation latency under load.

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
- **Arming the power button waits at most 1 s for quiet.** Firmware that keeps raising events past that is armed over (logged once), so
  a boot-latched or in-operation press can still count as fresh. A work item already running when the bound passes finishes first.
  Revisit if a machine logs `events still arriving after 1 s`.

## ACPI table export limits

The [table export](kernel/acpi.md#table-export) reads uACPI's array on request:

- **A fatal uACPI namespace failure resets its table array** (`uacpi_state_reset`), so the export answers UNAVAILABLE exactly
  when the firmware is most broken; non-fatal AML load errors keep the tables. A kernel-held copy taken after
  `uacpi_initialize()` would survive the reset; revisit if such a machine turns up.
- **A table is copied with interrupts off on the BSP**, once, the first time it is opened (the ThinkPad DSDT is hundreds of KiB,
  well under a millisecond). The snapshots stay for the boot, bounded by the installed tables. Revisit if a machine publishes
  multi-megabyte tables.

## Power-off and restart limits

[Power-off and restart](kernel/acpi.md#power-off-and-restart) implement the agreed first version of "clean" (user tasks held, pools
flushed). Accepted limits:

- No orderly stop of programs or services (revisit with service supervision); held tasks ignore stop requests until release.
- Failed pools are skipped (their unsynced changes are lost, as with the power button), and one flush failure keeps the system up with
  no forced power-off command.
- Raw disk handles are not flushed; the installer flushes its own writes.
- Control-method power buttons are limited to `PNP0C0C` Notify `0x80` and `INTC10CC` power events ([ACPI](kernel/acpi.md#power-off-and-restart)); only Vela's `INTC10CC` route is natively qualified, and the T14 and QEMU use the fixed event. Other firmware routing and extra GPEs stay unqualified; revisit on such a machine.
- A failed S5 entry freezes the BSP for 10 s (uACPI waits with interrupts disabled).
- The flush-failure, firmware-failure and reset-fallback paths were checked by code inspection only.
- **A button press or `poweroff` during an Update powers off at once without asking.** The installer now writes a healthy ESP through
  native FAT views, syncing and verifying each file ([system updates](userland/system-updates.md#replacement-and-verification)), and clean power
  drains admitted FAT operations and flushes the mount. It still holds the installer mid-sequence, so the fixed boot files can be left
  partially replaced (no cross-file atomicity or rollback), and the raw rebuild of a damaged ESP has no such flush. The installer could
  hold off power operations while it writes; revisit with installer work. The Ctrl+Alt+Delete overlay behaves the same.

## Remote power-request observation

The private [native chainload qualification helper](development/experiments/grub-limine-native.md#preservation-and-helper-limits)
waits for the old Remote connection to close after a power request. Abrupt
power-off can leave it silent until the watchdog reports an unknown outcome;
chronium visually confirmed the ThinkPad off on 2026-10-21. Reboots can be
resolved by authenticated fresh-boot checks; physical power-off needs an
independent observation. Revisit helper completion before the next unattended
native power gate; never retry power solely because transport closure was absent.

## Power overlay limits

The [Ctrl+Alt+Delete power overlay](kernel/power-overlay.md#emergency-coverage) is an emergency screen with accepted limits:

- The presenter task reads the chord. A presenter that never returns to its input loop (a CPU holding the output lock forever, the
  BSP stuck with interrupts off, a kernel fault) cannot open it; neither can panic or a stopped debugger. Use the power button.
  Revisit with bounded output-lock waits in the presenter, which need a decision because they change ordinary presentation.
- A stuck ACPI worker or pool flush leaves it at "flushing pools" with no time limit and no forced reset.
- A display resize waits while it is open. Its text is ASCII in the kernel font, and the failure shows a numeric status.
- Natively, the installed-system flush is unchecked until the next stick update ([native results](development/experiments/power-overlay/README.md#native-results)).

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

## UCSI client limits

The [UCSI client](kernel/acpi.md#usb-type-c-connector-interface-ucsi) is the
smallest consumer that reads and acknowledges connector changes:

- **Notifications.** Only command completion and Connect Change are enabled,
  although UCSI requires PPMs to support power operation mode, connector partner,
  power direction, battery charging status and error notifications too. Changes
  of those kinds are not reported until something in Pyxis consumes them.
  Error reporting would also need GET_ERROR_STATUS.
- **Coverage.** Only UCSI 1.x over the ACPI `_DSM` transport, one device, and at
  most eight connectors.
- **Failures.** A failed command, missed deadline or malformed reply parks the
  client until reboot; there is no reset or retry. A rejected completion is
  acknowledged first, but a connector change pending at or after parking stays
  unacknowledged, so firmware may keep repeating its notification.
- **Slow AML.** Deadlines are checked only after each `_DSM` returns. A `_DSM`
  that never returns holds the ACPI worker, including power-button handling,
  with no enforceable bound; that is inherent to running AML on the worker.
- **The ThinkPad (T14 Gen 1 AMD) parks.** Its `UBTC` mailbox keeps VERSION 0
  after `_DSM` function 2 refreshes, and no `Notify(UBTC)` arrives, so the
  client parks after 10 s with "firmware published no VERSION". Parking is
  harmless: no ACPI errors, and exit latency is unchanged. The cause is
  unknown, because no ThinkPad table dump exists. Candidates to check once
  tables exist:
  - an EC-availability flag set from `_REG`, which the `_DSM` checks;
  - an `_OSI`-gated path;
  - a different `_DSM` revision.
- **Natively qualified on vela only** (run 5, 2026-10-15). The repeated
  `_Q16`/`Notify(UBTC)` queries stopped: none in 120 s on battery or on AC,
  against 779 and 891 without the client. Other firmware, including the
  ThinkPad above, is unqualified.

Revisit when power or partner information has a consumer, when another board
reports a different UCSI version, or when the ThinkPad's ACPI tables are
dumped.

## Per-launch environment cost

The process environment (#832) adds work to every launch. Measured per `echo`
launch in nested QEMU with GDB counts, against main before #832: BSP requests
rose from 17–18 to about 25 (three environment requests, six more memory
requests, two forwarder requests), and retired objects from 2 to about 43, of
which about 36 are environment cells freed on CPU 0. Natively on the ThinkPad,
`exit-latency` medians are 768–770 µs against 683–688 µs before #832, about
80 µs more per launch. Revisit when launch cost matters, for example for
shell scripts or build tools that start many short programs: fewer BSP round
trips per startup, and sharing cells instead of retiring them per process.

## Synchronous launch preparation

Each in-flight launch reserves a full 64 KiB metadata capture buffer plus a small header from the kernel heap even for short argument
lists, and the BSP prepares the child with interrupts disabled. The executable file's operation ownership serializes reads, writes and
resizes through image validation and loading, which avoids another whole-image copy but lets large images delay both BSP work and
callers using that file. Revisit staging size and preparation scheduling when larger applications or concurrent launches make this
material; a snapshot or immutable backing could shorten file ownership at a memory and complexity cost.

## Private native capture limits

[Private capture](userland/native-jobs.md#private-capture-and-deadlines) requires a trusted private RAM root and reachability audit.
The 1 MiB per-stream polled trigger/cap is not a backing quota, and admission/retirement has no hard wall-time guarantee. Unknown
direct admission retains captures until caller/session teardown. Revisit bounded backing or recovery when a consumer requires it.
[Native pipelines](userland/native-jobs.md#native-pipelines-and-the-event-pump) copy finite input before launch, capture final stdout
and combined stderr, and have no live producer or stderr stage attribution. Revisit streaming producers when a consumer needs them;
[Lua](userland/lua.md#owned-native-jobs) and exact libuv adapters expose this profile; descendant execution remains outside it.

## Private job context cleanup after caller failure

Owner accepted 2026-10-12: the surviving creator removes private RAM `job_work`
directories after confirmed client retirement. Creator failure or unconfirmed
admission can strand them until reboot. Delegated provisioning trees are removed
after confirmed retirement of a shell-owned group, including plain foreground
exact-shell launches. Arbitrary declaring bundles outside such a scope retain
them until reboot in the console, or accumulate until the enclosing mux/Remote
pane/session group is cleaned. PROCESS exit alone cannot retire a
script's successors or services. The trees remain outside public roots; unknown
launch is never replayed. Revisit a surviving cleanup supervisor with
[Continuum, the Asterism supervisor](wip/spaces.md#asterism) and its family,
alongside [nested execution groups](wip/nested-execution-groups.md).

Owner accepted 2026-10-20: any bundle may declare `job_provisioner` under grant-all.
Each foreground launch can therefore retain another RAM tree beyond process exit,
until the cleanup above. Revisit consent/narrowing when grants stop being grant-all.

## Initial terminal editor

Libterm redraws the full visible line on each edit or cursor move and reads one input byte per call, keeping cursor and scroll
behavior explicit and not holding keystrokes a foreground child needs, at a syscall and rendering cost. It assumes exclusive output
use of the space's terminal, accepts only one-cell ASCII and keeps the prompt, line and cursor on screen; Unicode widths and
larger-line viewports are not implemented ([terminal contract](userland/terminal.md)). Revisit changed-span rendering or input
buffering when interactive workloads make the cost material.

[Saved shell history](userland/shell.md#commands-and-quoting) rewrites `home://.history` (up to 64 KiB) on every recorded line. Two
shells saving within milliseconds can lose a line, a killed save can leave a `.history.HEX` file that nothing removes, running shells
don't see each other's new lines, and the file is shared by every space on that home. Revisit if a line loss is noticed in use or the
per-command cost shows natively ([measurements](development/experiments/shell-history/README.md)).

## Neovim language-server limits

[Server root views](userland/neovim.md#server-roots) are an editor guard, not a read-only grant: a header shown as
`clang_resources://...` stays writable through the editor's `home://` path to the installed toolchain, only roots a bundle manifest
declares get a view, and with several clangd sessions a view buffer joins the first one. clangd runs `-j=4` because libc reports
no CPU count. Revisit the views when a space can delegate a server's roots to the editor read-only, and the worker count when
libc reports CPUs.

## Presenter-drawn block cursor

The presenter draws the [block cursor](userland/terminal.md) by recoloring the cursor cell's pixels: the top-left pixel stands for the
cell background (taking the scheme's `cursor` color) and every other pixel takes `cursor_text`. The TTY keeps no cell grid, so a cell
whose top-left pixel belongs to the glyph (the generated blocks, shades and diagonals) inverts the wrong way, and a font with more than two colors per cell
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
An enabled network debugger attempts local panic text before takeover, but these
same display limits can leave partial or absent text; successful GDB inspection
remains independent and does not invoke fatal UDP concurrently.

## Failures before the first pixel

Owner-accepted (2026-10-14): failures before the early console starts stay
invisible on machines without serial. `limine_entry()` starts the
[early console](kernel/early-console.md) only after checking Limine's responses,
copying the memory map and checking the framebuffer against its reservation;
until then a panic reports only to serial. Exception handlers arrive later still,
in `arch_init()`, so a fault before them, including the first write to a
framebuffer the loader left unmapped, hangs or resets with nothing on screen.
vela, the ThinkPad and most laptops have no serial port, so either case is a
black screen. vela's first boots stopped this way at the framebuffer check, and
finding that took a diagnostic kernel. Revisit if another failure before the
first pixel is hit on hardware, or before adding a loader or target. Options:
start the console after only the layout checks, install exception handlers
before the first framebuffer write, or check the loader's mapping first.

## Unchecked hardware assumptions

Owner (2026-10-14): vela exposed assumptions that held on every earlier machine
(QEMU, the ThinkPad). A later audit round across the kernel should look for
similar ones: alignment, size multiples, address ranges, legacy devices
(PIT, PS/2, serial) and firmware defaults. The owner opened the audit on
2026-10-15; a read-only source pass of `arch/`, the boot adapter and the drivers
at main `cfa02e41` produced the list below. Nothing in it was run on hardware.

Fixed so far:
- **Firmware enables upstream bridge bus mastering.** ThinkPad AX200 CPU MMIO
  worked with its root port `00:02.3` COMMAND=`0x0003` (BME off), while DMA and
  MSI never arrived. PCI now enables and verifies upstream BME before handing
  an endpoint DMA authority; drivers cannot rely on firmware forwarding defaults
  ([PCI ownership](devices/pci.md#driver-owned-resources)). Confirmed natively
  2026-10-19: ALIVE and five interrupts/packets arrive with upstream BME enabled.
  ThinkPad port `00:02.4` also boots with BME off; it serves the RTS522A card
  reader (`10ec:522a`, `04:00.0`), whose DMA is not runtime-qualified. Other
  ThinkPad ports sampled had BME on; vela boot COMMAND remains unknown.
  Linux runtime state is not boot evidence.
- **Framebuffer size a whole number of 4 KiB pages.** Limine reports its exact
  bytes; 2880x1800 ends mid-page. Fixed 2026-10-14.
- **A working 8254 PIT.** The local APIC timer was calibrated on PIT channel 2,
  which vela gates; it now uses CPUID `0x15` or the HPET
  ([timekeeping](kernel/timekeeping.md)). Fixed 2026-10-14.
- **x2APIC off at boot.** vela's firmware enables it (#835).
- **xHCI extended capabilities within the first 4 KiB of BAR0.** vela's start
  32 KiB in; the walk now maps its own window up to 256 KiB
  ([xHCI](devices/usb-xhci.md)). Fixed 2026-10-14.
- **MSI-X on xHCI controllers, and single-message MSI devices.** vela's xHCI
  controllers have only MSI, advertising eight messages; xHCI falls back to one
  MSI message ([multi-message MSI](#multi-message-msi)). Fixed 2026-10-14.
- **Framebuffer below 4 GiB.** vela's is above 1 TiB; Caelum maps it by physical
  address within the CPU's width, and Limine 12.9.3 (#846) no longer drops it
  from its page tables when it is the highest memory map entry.
- **Root-port links up as soon as the xHCI controller runs.** vela's USB 3
  stick trained after the single startup snapshot and was never enumerated;
  a bounded settle (100 ms, up to 400 ms while links train) now precedes it
  (#863). Fixed 2026-10-15.
- **NVMe needs MSI-X and a single active namespace.** A controller without
  MSI-X now uses one MSI message, and the first eligible of up to eight active
  namespaces is served while the others are reported
  ([NVMe](devices/nvme.md#namespaces)). Fixed 2026-10-15; the MSI path is
  checked only on a locally patched QEMU.
- **HD Audio matches two exact PCI identities, and audio DMA has no
  address-constrained allocator.** Admission is now by class with an explicit
  quirk table, and DMA is allocated within the controller's reach
  ([HD Audio](devices/hda.md#admission-and-controller-quirks); remaining limits
  in [coverage](#hd-audio-controller-and-codec-coverage)). Fixed 2026-10-15;
  vela plays natively (#881).

**Physical RAM coverage (owner accepted 2026-10-15).** The PMM excludes usable
RAM at or above 64 GiB physical with one warning; lower RAM remains available.
The 256 GiB heap arena's four-times-coverage invariant is unchanged. Horse, the
owner's desktop, has 64 GB RAM: address holes can place some RAM above that
span, which remains unused. Revisit map-sized PMM coverage and heap-capacity
accounting when Pyxis runs on horse or another 64 GB+ machine. See the
[hardware recovery record](development/experiments/hardware-recovery/README.md).

Noticed, not fixed:
- **An HPET is required.** Boot panics without the ACPI table. Linux on vela
  disables its HPET ("dysfunctional in PC10"); Caelum reads it only until the
  TSC is selected, but a CPU that rejects the TSC keeps it as the clock, idle
  included.
- **One MCFG region.** A valid table with several ECAM regions (several
  segments or bus ranges) disables all PCI discovery.
- **A linear framebuffer is required.** The boot adapter tolerates its absence,
  but the display layer panics without it or a supported GPU, so a headless or
  `PixelBltOnly` machine cannot boot.
- **MSI needs 64-bit addressing without per-vector masking.** The shared MSI
  helper refuses other layouts, so an MSI-only xHCI or NVMe controller with
  maskable or 32-bit MSI is refused. Pin interrupts are not routed for either.
- **VirtIO devices need MSI-X,** which the specification makes optional.
- **Limine's xAPIC response is refused when the CPU supports x2APIC,** even if
  the loader legitimately stayed in xAPIC mode.
- **SystemCMOS operation regions have no handler.** vela's EC query `_Q18`
  aborts; a bounded handler is in review (#868).
- **The framebuffer must be 32-bit with 8-bit channels.** A 10-bit GOP mode
  panics before the first pixel.
- **MSI targets the BSP by an 8-bit APIC ID.** A BSP x2APIC ID above 255 panics
  without interrupt remapping.
- **Fixed boot table capacities:** 256 memory map regions (vela has 57), 256
  logical CPUs and a 256x128 early console grid, which larger screens show
  top-left.
- **Legacy ports assumed or probed without audit:** COM1 at `0x3f8` (probed,
  bounded), the i8042 PS/2 controller and the 8259 PIC mask ports.

## Space-layer qualification

The [space-layer milestone](userland/space-layers.md) closed on 2026-10-08 with nested QEMU qualification, and the owner ran the native
Quake check the same day (ThinkPad PXE, main `4332801`, 1920x1080, AC): Super+Down/Up, continued game time and terminal output, held
input, switching away and back, queued-text clearing and hidden-layer Ctrl+C behaved as specified, and `timedemo demo1` ran at 684.5
and 684.7 fps. Acquisition without PRESENT, repeated PRESENT while hidden, capture surviving DISPLAY_RELEASE, shortcut releases after
Super and device/queue-loss propagation were source-inspected only; revisit them when changing the session, input or teardown paths or
when a failure appears.


## Backtraces and symbols

A kernel panic or assertion prints its message but no stack: `panic()` walks
no frames. On 2026-10-14 a ThinkPad assertion in `usb_host_notify()` showed one
line, and the cause had to be found from the code. Userspace programs are P1F,
which carries loadable bytes only ([P1F output](development/llvm-toolchain.md#p1f-output)),
so a user fault cannot be named by function either; an ELF with symbols exists
only when a build asks for one.

Consequence: native failures that can't be reproduced in QEMU need the network
debugger or a diagnostic build. Directions, none decided (owner, 2026-10-14):

- Kernel: walk frames in `panic()` and name them from the kernel ELF, which boot
  already keeps mapped read-only with its symbol table.
- Programs: a P2F revision that is a superset of P1F, adding an optional symbol
  table. Stripping that table yields a valid P1F, which is what the versioned
  format allows. Alternatives: symbol files beside the program in its bundle, or
  a build ID that the host resolves against kept ELFs, as `debug.image` does for
  the kernel.

Revisit with the next invisible native failure, or before the fault reporting
work in [control, events and faults](wip/control-events-faults.md).

## Renoir inherited pitch and firmware reservations

[Native flip inventory](development/experiments/renoir-flip-inventory/README.md)
confirms BAR0/GOP correlation and the 512 MiB UMA range. The owner accepted
the unsheared native GOP image as evidence of a 7680-byte effective stride for
raw pitch `0x780` on 2026-10-09. Preserve that register; the Linux convention
would normally write `0x77f`. Other inherited modes remain unqualified. The owner accepted
Linux-derived exclusions and pre-OS PSP/SMU residual risk on 2026-10-09. Revisit
reservations if new firmware/client ranges appear; retain the low prefix and
last-16-MiB guard. The [flip backend](kernel/renoir-flip.md) defaults on where qualified; its
[remaining native qualification](#renoir-flip-backend-qualification) is below.
Interrupts, three surfaces and blanking stay deferred.

## Renoir flip backend qualification

The [flip backend](kernel/renoir-flip.md) is natively qualified for
normal flips and Quake/Chocolate Quake play (`9254f5c8`, owner, 2026-10-10).
Unqualified on the ThinkPad: timeout recovery, remote screenshots with a visible cursor and selection, and the owner's ordinary input
checks (tabs, selection, layers, lock and relock) not reported in that run. The
paired disabled/enabled boots with camera clips, compose/copy costs and poll
counts were not taken as a set. No native timeout injection is authorized.
Revisit after a change to its failure, panic or capture paths; the paired steps are in the
[backend record](development/experiments/renoir-flip-backend/README.md#paired-native-qualification--luna-stages-owner-judges).

Owner decision, 2026-10-14: qualifying flips default on, with `display.flip=0`
for rollback, after additional tear-free DevilutionX play. The deliberate native
panic with flips on showing its message passed on 2026-10-10; see [panic visibility](kernel/renoir-flip.md#failure-fallback-and-panic).

## Presenter dirty regions

Whole-frame skipping avoids unchanged frames across boot, Bochs, VirtIO and
Renoir, but each visible change still composes and copies/submits a whole frame.
Software pointer motion, selection and small UI changes therefore retain the
full-frame pixel budget. Native whole-frame-skipping qualification removes about
99% of idle compose/copy work, while continuous software-pointer motion retains
about 2.1 ms of measured compose/copy elapsed time per frame; see the
[record](development/experiments/presenter-frame-skipping/README.md#native-abab-result--2026-10-10).
Dirty-region tracking remains deferred. Revisit when profiling the remaining
visible-change costs or a measured consumer bottleneck; any partial-frame design
must preserve capture, overlays, frame handoff and backend ownership proofs.

## Native Renoir presentation qualification

The [read-only Renoir observer](kernel/display.md#read-only-renoir-firmware-timing)
failed native qualification on the owner's ThinkPad batch 2, 2026-10-09, PXE main
`11d35fa6`, info logging. Period estimates varied from 13.887 to 20.785 ms,
including a hardware-qualified 40.7 ms uncertainty. Default boot logged sixteen
observation losses. `blank` admitted only 13 of about 6000 copies yet showed
worse tearing and a couple of frames of input delay.

The safety follow-up keeps one preparation summary, sends losses to trace,
rejects bounds outside blank minus eight lines, and uses the same software
cadence/full observation in both modes. Blank starts are opportunistic: no
phasing, sleeping or polling. The existing period estimator remains inaccurate;
EDID anchoring and its overhaul were dropped by the owner. Default remains
unsynchronized `observe`. Run the
[two-boot safety recheck](development/experiments/renoir-presentation/README.md#native-safety-recheck)
before claiming the scheduling/logging regression resolved. The observer’s
tearing effect and counter-derived timing remain unqualified. The separately qualified
flip backend does not qualify the observer; revisit its safety recheck
without default timed-copy enablement.

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

[Terminal clipboard](interfaces/clipboard.md) exports completed visible-cell selections as bounded UTF-8;
original tabs, soft wraps and intentional trailing spaces have the [initial delivery limits](#initial-clipboard-delivery-limits).
Mux drag autoscroll and selection across off-view history are absent, and kernel terminals keep visible cells without scrollback. [USB boot mice](devices/usb-hid.md) and PS/2 now share the accepted multi-source
button and loss rules. Bluetooth input, acceleration,
remote pointer transport and multiple-display composition remain separate work.
Revisit each when a concrete consumer or native device needs it.

## Pointer gestures and alternative transports

The pointer has no pinch, swipe/rotation, momentum or explicit touch-contact API.
The [pointer quality reference](devices/pointer-input-quality.md) keeps these separate,
with tapping off. Vela's precision HID-over-I2C touchpad is implemented and natively qualified, but
no SMBus/RMI4 transport or other I2C-HID hardware is. Revisit gesture/contact work
for a concrete consumer, or another transport if native PS/2 capabilities cannot
meet the palm/two-finger contract. USB gamepads/Deck mode switching remain later work.

## Terminal mouse input reporting

Shared scroll events are available, but local TTY, mux and remote terminal do
not yet deliver xterm modes 1000/1002 and SGR 1006 mouse reporting to terminal
programs. Links does not request terminal mouse input and Neovim disables it;
device qualification does not establish application wheel behavior. Revisit in
the separate terminal input-reporting task, with explicit modes, selection
interaction and matching local/mux/remote qualification.

## Raylib native audio and backend follow-up

Vela's native raudio stream/WAV playback and stop/reopen passed on 2026-10-23.
Other hardware, native device loss, vela 2880x1800/exclusive CPU cost and APIs
outside the selected profile remain unqualified. Revisit with the first consumer
of those paths; the listening check is not a broader quality/performance claim. Direct
native graphics/miniaudio backends stay deferred unless measured SDL copy or
conversion cost justifies them; see the [reference](development/raylib.md).

## Raylib extreme-motion hiccups

Accepted (chronium, 2026-10-22): vela's 1920x1080 PXE check still showed a rare
visible hiccup under extreme mouse motion/scrolling, about once per 20 seconds,
after SDL geometry-query reuse. Ordinary drawing/input was satisfactory; no
further work is assigned. Revisit if ordinary interactive use needs smoother
latency or before claiming full-panel vela performance; 2880x1800 and native CPU
cost remain unqualified. See the [record](development/experiments/raylib/README.md).

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

## Idle blanking leaves LCD backlights on

[Idle blanking](kernel/display.md#idle-blanking) draws black pixels but leaves
LCD backlights on, observed on the ThinkPad. Blanked screens still consume
backlight power and incur backlight wear. Revisit with later AMD backlight
control or ACPI `_BCM` support.

## Display SUBMIT round trip

Each [SUBMIT](interfaces/graphics.md#slots-and-frame-handoff) is a synchronous BSP request, like every display operation. In nested
KVM it adds 0.11–0.29 ms per frame, so Quake's timedemo runs 20–30% slower; ordinary 72 fps play spends about 1–2% of a CPU
([measurements](development/experiments/frame-handoff/README.md)). Revisit after the native run: a SUBMIT that doesn't go through the
BSP needs its own ownership decision for the session's slot state.

## Print Screen native qualification

The [Print Screen binding](interfaces/screen-capture.md#print-screen-delivery) is qualified in QEMU only. The
native key/event path and an ordinary save into the owner's home remain unchecked;
the earlier native screenshot-command check does not cover this system binding.
Revisit at the owner's native Print Screen check before claiming native coverage.

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
with authentication or untrusted networks. Discovery owns UDP port 2324 on the `management.remote` interface exclusively while waiting (a conflicting binding stops
that daemon with a diagnostic), one reverse session runs at a time, and the host tool accepts one per invocation. Beacon cadence adds up
to about a second after discovery opens plus network, scheduling and cleanup delays. Reverse discovery and log following passed the
owner's ThinkPad PXE check; macOS listener behavior is unqualified ([qualification](development/remote-debugging.md#qualification)).
Revisit if a multi-host or unattended workflow needs more.

## Parked remote handoff shells

Remote [`session` handoffs](userland/shell.md#session-handoff) retain one parked
shell with its stack, heap and grants per chained handoff, as accepted by the
owner. Disconnect still reclaims the whole execution group. Revisit with an
acknowledged observer transfer to the daemon if deep or long-lived chains make
the retained resources material; no handoff-depth quota is supplied.

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
The [shell](userland/shell.md#interrupting-foreground-commands) offers cooperative interruption to opted-in direct stages on first Ctrl+C,
then forces the owned foreground subtree on the second. After cancelled direct
stages retire, remaining descendants are stopped before the prompt. Stages
without a receiver and existing script/capture/helper scopes keep forceful stop.
Remaining limits:

- **MEDIUM — cooperative interruption inside scripts:** owned scopes (scripts,
  captures, relays and services) still force-stop their execution group on the
  first Ctrl+C. An opted-in program started from a `.pxs` cannot shut down
  gracefully through Ctrl+C. Revisit first-press cooperative delivery inside
  scripts with an explicit group/supervisor contract (chronium, 2026-10-22).
- **Background jobs** cannot be interrupted and there is no job control.
  **Passthrough holders** receive Ctrl+C as byte 3, so a surviving descendant
  retaining it after the direct foreground stage exits also makes the subgroup
  retirement wait uninterruptible through that key. The local escape is ending
  the session; Remote disconnect or pane close uses its owning cleanup boundary.
  A **nested interactive shell** cannot arm, so outer Ctrl+C ends the inner shell.
  Revisit passthrough-holder interruption with an explicit supervisor escape
  when descendant job control is designed; do not reinterpret raw byte 3.
- **Remote typeahead:** after more than 4 KiB the command does not read, the server stops reading frames until its injection drains, so a
  later Ctrl+C never reaches the kernel (Ctrl+] remains). An out-of-band interrupt from the server would be needed.
- **Startup scripts** hold the right, so Ctrl+C can terminate their foreground command and the script then never starts its session,
  leaving the space without a shell until reboot; no current script runs a foreground command.

Revisit with job control or an explicit supervisor/descendant lifetime design; native cancellation need not require POSIX signals.

## Normal non-raw terminal presentation

A normally exiting program that never held PASSTHROUGH can leave SGR colours,
scroll margins or cursor visibility changed. Automatic foreground cleanup skips
it to avoid per-command terminal records (accepted, chronium, 2026-10-23).
Use shell `reset` to normalize, or `clear` to erase the screen/history. Revisit
only if an ordinary non-raw consumer needs tracked presentation ownership; raw
and abnormal scopes already normalize after subtree retirement.

## Console input completion

Framebuffer console input has no EOF operation. EOF-driven consumers (cksum, tee, wc, sort, tail) need finite file or pipe input or an
[independent terminal session](userland/terminal-sessions.md), whose attachment can end input and whose zero-byte read libc accepts as
EOF; Ctrl-D remains an application-interpreted byte. Revisit completion and its interaction with line editing when interactive
EOF-driven tools are explicitly in scope; ports keep upstream behavior and add no terminal controls or signal handling.

## Readiness scaling

`wait_many` retains its transient 32-interest bound and rescans its in-flight interests on notification. Persistent
[readiness sets](interfaces/readiness-sets.md) provide targeted, budgeted registration for pipes, console/terminal and TCP.
Other sources and ordinary FILEs lack set hooks. Go uses the set with dynamic descriptor metadata; its reusable nodes and geometric
index retain high-water backing until process exit. Revisit source coverage and metadata reclamation with concrete consumers;
networking task 4 must account for the separate TCP connection-pool limit. See the
[qualification record](development/experiments/go-readiness-set/README.md).

The 16 MiB metadata budget does not bound BSP worker time. Bounded, yielding
work units are not a CPU quota or a wall-time guarantee: concurrent enrollment,
fanout and cleanup raised a separate process's nested first-output p50 from
4.602 to 10.052 ms (loaded p95 12.310 ms). Go also issues one native UPDATE per
collected event while holding its registry lock, up to 64 per batch. Revisit
worker scheduling and adapter batching with networking workloads; preserve
registration/revocation ordering rather than claiming the memory budget provides
latency isolation. Current correctness runs do not measure interrupt-off maxima.


## Terminal profile limits

The [`pyxis` profile](userland/terminal.md#tty-output-controls) has a fixed
single-cell UTF-8 repertoire, synthetic clipped bitmap styles and no combining
or wide-cell layout. Nineteen Latin-1 scalars use replacement. Origin mode,
character insert/delete/erase, horizontal margins and colon colours remain
absent; the kernel/mux sequence parsers must stay in step. Scheme configuration
and the Neovim Unicode profile follow-up are separate work. Revisit with a
consumer needing broader text or missing controls. The owner's
[far-future scalable-font direction](wip/later-os-directions.md#neovim-follow-ups)
is unassigned.

## Initial terminal multiplexer limits

The [multiplexer](userland/multiplexer.md) has one window and up to eight panes. Its native terminal subset has no Unicode widths or
detach. History keeps 1,024 scrolled-off rows with no reflow or
erased-screen archive; rows cropped by resize are not inserted into history (retaining them is a proposed follow-up), presentation crops
at the session maximum and history storage keeps the largest width seen. With 12-byte cells, steady live/alternate/history storage is about 9 MiB per pane at maximum geometry (resize can
briefly double one pane's), with no per-group CPU or memory quota.

A full pane input queue can hold a prefix behind already staged input, since bounded lossless storage cannot bypass a pending paste.
Confirmed closure waits for group cleanup and output EOF, so published HOST work can delay cleanup indefinitely and output grants
delegated outside the group can delay EOF; local mux exit or fault requests termination through final controlling-grant closure without
waiting for cleanup. Remote daemon supervision remains until outer and all tracked pane groups complete. Local keyboard and graphics
grants are shared space facilities, so graphical launches share the existing
one-session ownership. Live pane resize depends on the consumer: shell, Kilo and Neovim observe RESIZED, Links reads geometry once, and vi and
less keep their behavior (revisit with a focused port change). Revisit windows, ratio adjustment, input recovery and quotas when
selected.

## Initial remote multiplexer limits

[Remote mux](userland/remote-terminal.md#remote-multiplexer) is keyboard-only with host paste, xfer refusal and no reattachment or
per-pane machine completion. Two mux connections share four total slots, including closing groups; attributed HOST cleanup can delay
reuse indefinitely. Synchronous capture/cwd work can delay input, resize rollback is per screen/frame, and queued host bytes cannot be
clipped retroactively. A saturated close can miss FINAL despite eventual cleanup. The [native check](development/experiments/remote-multiplexer-resize.md#owner-native-qualification)
qualifies pane Neovim/TCC/live resize, not native failure recovery. Revisit these limits when encountered in use; pointer/clipboard,
safe-Paste and pane-aware transfer need separate assignment.

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
Address changes invalidate the listener without automatic rebinding. The interactive host renderer follows the bounded UTF-8 terminal repertoire and
emits U+FFFD for unsupported scalars or invalid bytes (machine mode preserves data). A full client queue delays reading Ctrl+] behind a paste (close acknowledgment
then bounded at five seconds), a full guest queue likewise holds back Ctrl+C
([process termination](#process-termination-and-ctrl-c)), and host SIGINT/SIGTERM forces disconnect. Revisit admission, authentication and
presentation breadth with a non-development deployment or text consumer.
Interactive Remote negotiates live resize; machine geometry remains explicit.
Reconnection remains separate work.

## Libc compatibility gaps

The [descriptor portability slice](userland/libc-portability.md) supplies open/read/write/close and `lseek`; public O_RDWR and
exclusive O_CREAT|O_EXCL use native constructs ([qualification](userland/libc-portability.md#readwrite-and-exclusive-create-qualification)),
and `fileno` exposes a stream's existing descriptor (agreed 2026-10-08 for the [SDL2 port](development/sdl2.md)). `fdopen` and `dup`
share one descriptor object and cursor ([stdio](userland/stdio.md#opening-and-ownership)); `dup2` exists, `fcntl` and `freopen` are absent.
Descriptor inheritance and cross-process shared offsets are not supplied by the startup-stream grants.

Signals are absent, so tee rejects -i and broken pipes report EPIPE without SIGPIPE (a successful no-op handler would misrepresent
support). Public O_APPEND exists, but tee still rejects -a because the sbase
restriction patch (`ports/sbase/patches/0002-restrict-tee-options-and-close-outputs.patch`) predates it; atomic append needs a native operation
([stdio append](#non-atomic-stdio-append)). Polling and nonblocking descriptor I/O, fork/exec-style semantics
and full wide stdio are outside the slice; output is unbuffered unless `setvbuf` or `setbuf` selects
[buffering](userland/stdio.md#output-buffering); byte pushback is one byte per FILE (wide I/O has one scalar), scanning covers narrow conversions only and
fixed-width inttypes input (SCN) macros are absent. Revisit only for a concrete consumer, defining the native blocking and lifetime
behavior it needs; no successful placeholder APIs exist.

## Public open creation mode

O_CREAT accepts only mode 0666 as a request for native creation policy; it installs no Unix permissions, ownership or authority, and
other modes (including restrictive ones such as 0600, and opens of existing files) fail with ENOTSUP before lookup. Virtio-fs keeps its
0644 creation request under the host-service identity. This is the accepted policy for the first writable public opens (used by tee).
Revisit with a file permission system, users and ownership, defining mode enforcement and umask together rather than silently discarding
requests callers expect to restrict access.

## Non-atomic stdio append

Append streams and `O_APPEND` descriptors query the file size before each native write, so concurrent appenders can overwrite each
other, and seeking does not make the pair atomic. Keep this until concurrent appending needs a native operation that chooses the end and writes under one file operation.
Formatted output stages the full result with `snprintf` (heap allocation and a second pass when the stack buffer is too small), so large
formatted output needs temporary memory; revisit bounded streaming when a consumer makes that material. FILE output is unbuffered by
default; `setvbuf` and `setbuf` select [full or line buffering](userland/stdio.md#output-buffering).

## Console line input

Consoles are never read ahead (their input is shared with the parent shell and ISO C treats them as interactive), so line input from
console-backed stdin through `fgetc`, `fgets` or `getline` costs one native read per byte, including a large paste; file and pipe input is
fetched in BUFSIZ blocks by [input read-ahead](userland/stdio.md#input-read-ahead). Revisit only with a terminal input design that can
return unread console bytes to their next owner, not with per-port workarounds.

## Input read-ahead limits

Buffered file bytes are a private copy: if another descriptor or process writes the same file, a stream can return stale bytes until
`fseek`, `rewind` or input `fflush` refetches. `setvbuf(_IONBF)` makes later reads of one FILE exact, but programs cannot size the
read-ahead buffer; one-byte `ungetc` works. Revisit with a consumer that needs input buffer sizing
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
objects report type/size plus independently valid identity and modification time
([metadata contract](interfaces/file-metadata.md)). Callers must check `st_valid`;
ownership, mode permissions and access/change times remain absent.

- **Sizing opens the file** with READ (or WRITE if READ is denied), so a file with neither right cannot be sized, and on a provider URI
  it performs the request (Links sends provider URIs straight to `fopen`). Review proposed failing with ENODEV there, as `opendir` does.
- **Symlinks:** lookup never follows one, so `open`, `stat` and `lstat` of a host symlink fail with ELOOP and `readdir` reports `DT_LNK`;
  native filesystems have none, `lstat` equals `stat`, and `readlink` is absent.
- **Access checks:** `access` tests current native grants, not mode bits or backing I/O (directory R_OK needs ENUMERATE, W_OK
  CREATE|REMOVE; X_OK and provider routes return ENOTSUP), per the
  [accepted contract](userland/libc-portability.md#native-path-checks-and-removal).
- **Listings** omit `.` and `..`, and a detected concurrent change ends one with EAGAIN. The first ls and mkdir still use libpyxis helpers.

Revisit when a port needs other metadata, `readlink`, or calls `stat` on provider URIs; add
fields only for values the native layer reports.

## File identity across capability paths

The [metadata protocol](interfaces/file-metadata.md) compares live native objects
while an original reference is held. HOST bind aliases, cross-session comparison
and continuity after full close remain excluded; unknown provider identity can
prevent safe comparisons. Revisit stronger backing support
when a consumer needs these excluded comparisons; path normalization cannot supply it.

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

**Clock source.** HPET MMIO remains expensive under virtualization. The
[interrupt cache](kernel/timekeeping.md#clock-reads-in-timer-handling) removes
redundant scheduler expiry reads and supplies audited worker observation/parking
and metadata timestamps; public clocks and deadline admission stay precise.
[Task 1 qualification](development/experiments/clock-cache.md) compares the
unchanged installed package workload. Hardware waits, audio refill and active
network deadlines still pay for precise reads. Admitted
[KVM time](kernel/timekeeping.md#kvm-selection) avoids HPET reads without invtsc,
but its shared cross-CPU floor adds atomic contention; conservative CPU ordering
can still require CPUID exits. Boot still requires HPET, and guests without KVM
admission or a qualified invariant TSC retain it.
[Explicit precision authority](interfaces/clocks.md) is implemented; revisit the remaining source cost when a measured hot path or
cross-CPU contention warrants it.

**KVM host progress.** An indefinitely odd conversion version prevents a reader
from progressing; persistent invalid or saturated samples leave the shared floor
unchanged. There is no runtime source watchdog or safe switch back to stale HPET
extension state. Revisit before supporting host failure recovery, suspend/resume
or guest migration; do not infer those guarantees from ordinary vCPU movement.

**KVM ordering on AMD and older Intel guests.** Without advertised serializing
LFENCE on an AMD guest, or without RDTSCP on Intel, precise reads use
`CPUID; RDTSC; CPUID`: two exits per reading. The zero-exit measurement was on
an Intel host with RDTSCP. Revisit the vendor-qualified ordered-read sequences
when measuring an AMD KVM host; do not infer its cost from that result.

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
qualifies with an error bound up to 100 ppm on top of the HPET's crystal error. On Intel, CPUID `0x15` supplies the frequency and that measurement only cross-checks it (vela, 2026-10-14). Nested VMs without admitted KVM time can
fall back to HPET: its reads are too slow for a 100 ppm calibration and the
earlier development-VM baseline exposed no invariant TSC. Suspend, resume and migration are unqualified. The TSC-deadline timer stays unused (the ThinkPad's Zen 2 lacks it, and using it only where present would split QEMU from native), and runtime source switching and a userspace counter mapping are out of scope.
[Clock precision](interfaces/clocks.md) and KVM time are implemented; the
[qualification records](development/experiments/clock-precision.md) retain their
configuration boundaries. Runtime native warp detection remains deferred;
revisit with a target that shows a warp or needs better accuracy.

## Doom configuration and save-format limits

[Doom save/load](userland/doom.md#saves) uses checked temporary writes and atomic replacement through the
[filesystem mutations](interfaces/filesystem-mutations.md); saves persist on installed systems and last until reboot on live boots. The
upstream parser trusts saves matching the loaded game data, with no malformed-file validation or separation by PWAD, and interrupted saves
can leave temporary files for manual removal. Configuration persistence is disabled in the pinned generic engine; re-enabling it needs a
writable configuration location and review of its parser and formatting.

## clangd without a preamble

Pyxis builds no preamble (`PrecompiledPreamble::Build` is unsupported and `CanReuse` is false), so clangd reparses
every header on each edit, and code completion and signature help parse the whole file for each request (fork commit
`3d69e83`), so latency grows with the includes of the file. On vela an edit took about 145 ms to fresh diagnostics
(2026-10-18). Its include and pragma records are keyed by
`FileEntry`, valid only within one FileManager, which holds only because no preamble outlives its parse. Revisit when
a real project's completion feels slow; supporting preambles also needs those keys replaced by a cross-manager
identity.

## clangd on Pyxis limits

clangd runs with the stated [limits](development/hosted-tools.md#clangd): one async worker unless `-j` is given (libc has no
processor count), two 8 MiB eager stacks per open file within the 64-thread bound, no `--check` (`real_path` with tilde
expansion is not supported), no user config or cache directory, no signals or process ID, and `llvm::call_once` waits by
spinning. Revisit with the first real project that opens many files, or if libc gains a processor count.

## Hosted-tool launch loading

Owner-native [task 5 measurements](development/experiments/hosted-clang-task5/README.md#native-owner-qualification-2026-10-13)
on the ThinkPad USB toolchain volume qualify the first compile/link/run, with
one remaining deferred cost. Bounded npfs read aggregation has
[native A/B qualification](development/experiments/npfs-read-runs.md#native-ab-2026-10-13):

- **No selected-image reuse across launches:** each compiler/linker invocation
  recaptures and loads its image, so warm repeated invocations still pay the
  launch cost. Revisit for launch-heavy native builds when image reuse, its
  authority and invalidation rules are assigned. Task 5 adds no cache or reuse
  mechanism.
- **Optional native checks not run:** the launcher-withheld (compile-only) driver
  configuration and the memory probe's peaks were not run on the ThinkPad
  ([native driver results](development/hosted-tools.md#native-driver-results)).
  The probe stays unmerged; run them when a native session has time.

## USB read aggregation responsiveness

[Bulk npfs read aggregation](devices/usb-storage.md#bulk-read-aggregation) holds the
shared BSP worker for each grouped transfer of up to 64 KiB. Whether a large capture
read delays other users of the shared worker is not qualified: the native concurrent
check never ran (its second session never became ready), and the QEMU observation is
not a native substitute. Revisit with the next native session that can hold two
sessions, or before adding further bulk consumers.

## Clang code generation and predefines

Pyxis builds with Clang since the [LLVM toolchain milestone](development/llvm-toolchain.md). Matched nested-KVM measurements on 2026-10-07
(four CPUs) left two regressions against GCC 16: `allocbench heap` at about 13.1 ns/op against 12.0 (about 9%; the TLSF fix for Clang's
narrowed flag stores removed most of an original 33% gap, the rest is uninvestigated, and two early samples of 20.4 and 21.7 ns/op did
not recur), and Quake `timedemo demo1` about 3% slower (median 1560 against 1604 fps). Clang also emits more byte-sized
read-modify-writes (164 against 75 in the kernel) with no other measured cost. Reconsider when an allocation-heavy program (Lua, the
JVM experiment) measures the heap gap or the LLVM pin moves.

Clang predefines `__INT_FAST8_TYPE__` and `__INT_FAST16_TYPE__` as `signed char` and `short` while libc's `stdint.h` uses `int`, so code
using the compiler macros instead of the header sees a different type. Reconsider if a port relies on them; the fork's target information
could then match the header.

## C++ runtime subset

The [C++ in userspace milestone](development/cxx-userspace.md) shipped a deliberate subset (owner, 2026-10-08):

- **C++ thread profile:** gate 8 supplies native synchronization and thread-local exception bookkeeping, including static
  guards and checked unwinder locks. [Detached threads](userland/threads.md#creation-and-collection) now back `std::thread::detach`
  and `std::async`. Wider non-lock-free compiler atomic helpers are still absent; revisit if a reached consumer needs them, independently of the native parking profile.
- **C-only locale:** the matching gate 7 compiler adopts the native locale, wide FILE/UTF-8 and entropy closure; full locale databases
  and wide scanning remain absent.
- **Most of `<cmath>` is missing:** libc's math subset leaves 161 of the 186 imported names undefined, so first use fails to compile.
- **Exceptions cannot cross C frames** (no unwind tables), so one thrown through a `qsort` comparator terminates the program; uncaught
  ones name mangled types (`St11logic_error`) because the 184 KB demangler is left out.
- **No `<filesystem>` or time zones**, and fmt's license is not staged for the boot payload (a port shipping an fmt
  program must install it).

Revisit remaining math/locale facilities when a reached consumer needs them. Additions belong in
libc or the runtime configuration, never in a port-local stub.

## HD Audio volume control

[Software master/per-space controls](interfaces/audio.md#user-volume-controls)
boot unmuted at 50% (about −30.3 dB); settings do not persist, and software mute
can leave up to the nominal 80 ms of published DMA audio. Media keys are not
decoded, so use the bar or Super+M. [Native listening passed](userland/audio-volume.md#native-qualification).
Revisit persistence/media-key input when ordinary use requires them, and mute
latency with the ring-tuning work.

## HD Audio volume native regression

The two-minute eight-session silent regression passed on the ThinkPad on
2026-10-15 ([#881](devices/hda.md#qualification-and-remaining-scope)). Native
per-space isolation/retention, hidden playback,
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
The [SDL audio qualification](development/experiments/sdl-audio/README.md) saw the 1 ms commit guard trip at single-session
starts on many fresh nested boots, as often for the native `pcm` tool as for SDL, and the completion-notification guard twice;
see its trip counts. [OpenArena qualification](development/experiments/ioquake3-softpipe.md)
also observed DMA-commit and unstable-position shutdown in nested QEMU; brief
PCM output worked, but reliable sustained audio remains unqualified. Track the
independent diagnosis in [#1193](https://git.internal/PyxisOS/pyxis-os/issues/1193).
**Process teardown can stall audio** (found in the [EDuke32 sound qualification](development/experiments/eduke32-sound/README.md#nested-qemu-engine-failures)):
in nested QEMU, quitting a large game trips the 20 ms service-horizon guard and disables audio until reboot. Reproduction: four
CPUs, play `pcm 1000 500 20` in the Read-only space, then quit EDuke32 started by its `app/bin` path (no audio session, about
160 MiB of eager private memory) in Development; three of three valid boots tripped at the quit. Process destruction runs on CPU 0 with interrupts off,
where the audio worker also runs. **Native (2026-10-13):** EDuke32's quit took 21.8 ms of teardown and tripped the guard, as did
Chocolate Quake's (39.7 ms); play did not trip. Stream recovery (#807) and bounded, interrupts-on teardown address it; revisit
with native restart counts at quits once both land.

## HD Audio timing recovery and worker starvation

Since 2026-10-13 a timing fault restarts the stream with a counted discontinuity
instead of disabling audio ([timing recovery](devices/hda.md#timing-recovery)).
Each recovery is still an audible gap, and restarts that keep failing for 2 s
fail the engine until reboot. Hardware faults keep that fail-closed rule, with no
controller reset.

The faults come from the BSP audio worker missing service. Final process
teardown used to run in the BSP scheduler loop with interrupts off: natively
21.8 ms for EDuke32 and 39.7 ms for Chocolate Quake, nested 51–95 ms
([record](development/experiments/audio-recovery/README.md)). The BSP reaper now
releases it in bounded steps; nested, the longest interrupts-off step is under
0.25 ms and quits no longer trip
([record](development/experiments/bounded-teardown/README.md)).

Natively, every remaining restart followed a large ALLOCATE (about 80 ms
interrupts-off for 128 MiB). ALLOCATE is now stepped the same way on the VM
worker; nested, no restart falls inside an allocation
([record](development/experiments/bounded-allocate/README.md)).

Natively the stepped ALLOCATE run had no restarts. The 4.6 ms native reaper
queue delay recorded earlier was the diagnostic's own log lines. Without them,
the reaper waits tens of microseconds natively, so CPU 0 worker precedence is
parked ([record](development/experiments/worker-precedence/README.md)). Kernel log
lines and local console writes no longer draw in the writer
([kernel console rendering](#kernel-console-rendering)). RELEASE, image loading
and DISPLAY services still run as single interrupts-off spans (DISPLAY 7–20 ms
natively at Chocolate Quake start). Revisit with native `audio:` restart counts
at game start and during play.

## Kernel console rendering

Since 2026-10-14, writers to a kernel TTY (kernel log records and local console writes)
change only its cells under that TTY's own lock, and the CPU 0 presenter draws
the visible terminal once per frame with interrupts on
([kernel terminal rendering](kernel/display.md#kernel-terminal-rendering)).
Before that, each scrolled row moved the whole screen image in the writer, with
interrupts off and a global lock held, which stopped CPU 0 (about 2.1 ms per row
natively).

What remains is presenter work: during an output flood, every frame is a full
redraw of the visible terminal. Nested at 1280×800 that takes 0.67–0.8 ms on
average, at most 3.7 ms, with interrupts on. Collecting the changed rows takes
about 50 µs with interrupts off (one outlier of 1.1 ms)
([record](development/experiments/console-rendering/README.md)). The native
cost at 1920×1080 is not yet measured. Revisit if presentation under a flood
competes with audio or input: draw only changed rows when scrolling by a
fraction of the screen, or cap redraws per second.

## HD Audio request batching

Bounded request batching remains deferred by the owner. Per-write BSP handoff
and retry/wait traffic remain costs; the [profile](development/experiments/audio-task2/profiling.md)
records them. Revisit after consumer or native measurements justify a batching
proposal without silently changing capacity, queues or safety thresholds.

## Initial clipboard delivery limits

Accepted 2026-10-09 in the [clipboard interface](interfaces/clipboard.md#bounds-and-qualification),
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
- **Selection fidelity:** UTF-8 Copy LF-joins physical rows and trims trailing
  ASCII spaces, including intentional whitespace. Tabs, soft wraps and unsupported
  source scalars cannot be reconstructed from retained cells. Revisit with
  terminal text/provenance work; safe Paste remains ASCII-only.
- **Mux receiver qualification:** ASCII Paste reports no opted-in reader in the
  [UTF-8 control/changed fixtures](development/experiments/terminal-utf8/README.md).
  Revisit receiver registration before claiming successful Paste there.
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
- **Later types/bridges:** FILE/rich objects, converters and remote/host bridging
  remain separate milestone tasks. They need retained-capability, conversion and
  host/guest authority contracts before extending the existing text stores.
- **SDL2 activation:** Only delivered physical Copy/Paste commands authorize
  [graphics text access](interfaces/clipboard.md#graphics-and-sdl2). Menus, Cut,
  background queries and ambiguous batch/filtered events refuse; apps inspecting
  global modifiers need the command-event snapshot for shared chords. Revisit
  with a concrete consumer and explicit approval, retaining private action identity.
  Bundles request `clipboard_local` and `clipboard_shared` (`publish` and `paste`
  rights, owner decision 2026-10-13), but a shell supplies them only on foreground
  and session launches and only the stores it holds, so a bundle declaring them
  optionally still runs without them and its Copy and Paste then refuse
  ([program bundles](userland/program-bundles.md)).

## SDL2 port limits

The [SDL2 port](development/sdl2.md) covers video, keyboard, pointer, timing, threads, timers, audio output and preference paths. Video event waits
block on keyboard, acquired pointer and display readiness ([qualification](development/sdl2-event-wait-qualification.md)); upstream
polling remains for missing or nonwaitable sessions and failed waits, and for threaded programs without a declared `pipe/create` grant,
which have no [wakeup sender](development/sdl2.md#threads-and-timers). SDL thread priorities other than normal are unsupported, and
`SDL_DetachThread` still reports unsupported and leaves libc's record and stack reserved until exit, and that uncollected record
also makes exit skip orderly finalizers; revisit by moving the port onto `pthread_detach`, which libc now collects. Programs
exiting with live threads skip orderly finalizers ([process exit](#process-exit-with-live-threads)).
Missing: **audio capture** and output device selection, **windows** beyond one
fullscreen window, and **text** beyond the shared US layout. Displays report no **refresh rate**, so SDL gives 0 (unknown); EDuke32's frame
limiter divided by it and hung until its port assumed 60 Hz. Revisit when a display reports its mode timing. The
[SDL game ports](development/sdl-game-ports.md) Chocolate Doom (built without SDL_mixer) remains silent; Chocolate Quake and EDuke32
ship as bundles declaring the audio and threads grants and play sound. Pointer position, program images, show/hide, bounded warp and relative lock use
the [native pointer contract](interfaces/pointer.md). Key repeat is not covered by validation because QEMU's injected PS/2 input has no
typematic repeat.

## SDL audio latency

The [SDL audio backend](development/sdl2.md#audio) keeps the native 80 ms PCM queue near full with fixed 1,024-frame buffers. Its
worst-case output latency is **calculated, not measured**, at about 140–160 ms: 60–80 ms queued plus up to the 80 ms hardware ring.
Capping the queue at two buffers with timed sleeps would remove about 40 ms, at the cost of more wakeups and less margin before an
audible gap. The owner deferred tuning on 2026-10-13: revisit after the native ThinkPad measurement, not before.

## DevilutionX port limits

[DevilutionX](userland/devilutionx.md) is personal-use only because its non-commercial licence and libmpq's GPL cannot both be met by a
distributor, so it is an opt-in build that images, CI and build bundles never contain. It has no multiplayer, controllers or translations
(the build host has no gettext), and saves go to `home://devilution/`, which is RAM on live boots. Retail `DIABDAT.MPQ` (about 500 MB)
cannot be staged in images, so on installed systems it arrives through [remote transfers](#remote-transfer-memory-and-staging-limits), today
in 15 MiB pieces; revisit with streaming transfers.

## SDL2 and DevilutionX native qualification

The owner checked the shareware build natively on 2026-10-08 (ThinkPad PXE, main `4332801`, 1920x1080, AC): DevilutionX played with
keyboard, touchpad and TrackPoint, key repeat worked in name entry, and with the default "Limit FPS" it ran at 59–65 FPS (mostly 60–62)
over the 1920x1040 content area ([measurements](userland/devilutionx.md#measurements)). Retail data on the installed stick is
unchecked natively; the owner deferred it (the shareware result is enough). Stage 2 task 5 runs it from the personal `diablo` package.

## EDuke32 port limits

[EDuke32](userland/eduke32.md) is personal-use only, an opt-in build that images, CI and build bundles never contain. It has no network
play, controllers or OpenGL renderers. Its sound runs as a bundle with SDL's audio callback thread; its
[recipe](../ports/eduke32/README.md) still leaves out loguru's signal handlers and stack traces and the audio library's async tasks,
used only by decoders the build omits. The port also works around [libc gaps](../ports/eduke32/README.md#libc-gaps) that need decisions
first: it keeps its own working directory (no `chdir` or `getcwd`), loops reads because libc `read` returns one native transfer, under
4 KiB, where POSIX code expects whole reads from files, and still matches its data cache on size;
it has not adopted the new optional modification time. Revisit timestamp-aware caching when this port is next changed,
and working directories or whole reads if more ports need them. Pyxis displays report no refresh rate, so its frame limiter assumes 60 Hz.

## SDL game ports native qualification

The owner checked the [SDL game ports](development/sdl-game-ports.md) on the ThinkPad on 2026-10-09 (PXE build of #646's head `0bb9731f`
with `DUKE3D_DATA`, info logging, wired, AC, 1920x1080). The SDL path cost about 2.0 ms per frame in Chocolate Quake's timedemo (about 1.2 ms in
QEMU) and about 1.6 ms in Chocolate Doom's, from an earlier batch ([Quake](development/experiments/chocolate-quake/README.md#native-results),
[Doom](development/experiments/chocolate-doom/README.md#native-results)). Chocolate Quake played, with clearly more tearing than native Quake:
about three drifting tear lines at once, an input to the [Renoir flip qualification](#renoir-flip-backend-qualification).
EDuke32 played smoothly at the native resolution with smooth pointer motion; its SDL cursor showed through the system pointer, some tearing
appeared, and a mouse suits it better than the trackpad. Still pending natively: Chocolate Doom's matched configuration and a play
check, and EDuke32's level load time, frame rates capped and uncapped, and save, load and quit. Revisit when the owner
next batches native checks.

## Quake port limits

The [Quake port](userland/quake.md) renders at quakegeneric's fixed 320x240; a resolution switcher would need a video driver with a mode
list behind Quake's Video Modes menu, reallocation of the frame, z-buffer and surface cache, and a check of the renderer's size limits
(upstream reverted 640x480 as unstable). Sound, networking, CD audio and joysticks are absent; sound would need a Quake-specific adapter to the native [PCM audio sessions](interfaces/audio.md) and its own qualification ([Chocolate Quake](userland/chocolate-quake.md) already plays effects through SDL2).
Saves are Quake's trusted text format, and shareware and retail data share `home://quake/id1` so their configuration and saves mix;
QuakeC strings outside the hunk use a 512-entry table whose overflow stops the game with an error. Revisit when a second data set matters.
Config, saves and screenshots follow the [atomic-save limits](#atomic-save-limits); demos are still recorded in place.

## Rust worker scheduling observations

The [native std profile](wip/rust-std.md#worker-yield--accepted-chronium-2026-10-26)
uses the accepted weak CPU-hint `yield_now`: it does not force a reschedule or
promise fairness, so polling contention can consume a timeslice. Blocking
primitives use real address waits. `available_parallelism` returns Unsupported
because online CPU count does not describe the caller's effective eligibility;
a consumer may therefore need an explicit worker count. Revisit a native yield
only with a concrete workload, and expose truthful effective eligibility before
replacing that error. This gate adds neither scheduler controls nor a fake count.

## Hexyl upstream I/O errors

The optional hexyl 0.17.0 port retains its upstream read loop: persistent input
errors can keep it running until interrupted or stopped by a job deadline.
Include mode can miss errors during final buffer drop. Generic fixes are not
carried in the [Pyxis port](../ports/hexyl/README.md); revisit the read loop when
upstream fixes it, and requalify error handling on a version update.

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
can lose the new name and leave the old contents, and a crash before the rename leaves a stray temporary file that nothing removes. Quake
writes through the descriptor `mkstemp` returns, with `fdopen`. Saving needs directory CREATE and REMOVE, not only file WRITE, with no
in-place fallback. A failing or full write was not exercised. Revisit with a libc directory-sync bridge (see [Git on Pyxis](userland/git.md#authority-and-failure)).

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

HTTP-side limits are under [HTTP redirects](#http-redirects) and [navigation metadata](interfaces/http-transactions.md#navigation-representation).

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
copy gives a read-only degraded map when the other is absent or invalid; the documented larger-device exception also admits two agreeing
copies as degraded (I/O errors, timeouts and unsupported metadata prevent fallback).
These bounds can exclude usable media; revisit only for a consumer with explicit resource limits and a recovery policy.

A native tool to relocate the backup GPT and protective MBR under explicit write authority is deferred: larger-device sticks remain read-only on Pyxis and need `sgdisk --move-second-header` on another OS until then; revisit when installer/disk tooling lands.

## Owned ESP attempt cleanup

[Coexistence installation](wip/nvme-coexistence.md#3-installer-coexistence-mode)
retains immutable ESP payload attempts, about 45 MB each, without automatic
removal: manual entries may still reference them. Vela's 1 GB ESP fills after
about 20 attempts (earlier with existing contents). Revisit ownership-aware cleanup
with installer task 5's owned-entry activation.

## Replaced previous attempt after pending completion

An Update that completes through pending-activation Review leaves the attempt
the replaced menu's previous entry booted on the ESP: the boot record no
longer binds it, so its hashes are unknown and cleanup does not touch it
(chronium, 2026-10-21). Each such completion leaves one unreferenced attempt
directory under `EFI/pyxis`, reducing ESP space for later Updates. Revisit with
ownership-aware attempt cleanup or `pkg upgrade` retention
([pkg upgrade](wip/pkg-upgrade.md)).

## Installer authority and retained pools

The [installer disk service](devices/installer-authority.md) supplies explicit raw claims and immutable boot sources, and the
[native installer](userland/installer.md) establishes target consent (the kernel does not interpret `SAFE_TO_WIPE`). Any retained npfs pool blocks
a whole-disk or overlapping raw-write claim, even read-only and after all handles close, so opening a volume to inspect a marker cannot be followed by
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

Copied-program verification after pool sync uses ordinary npfs FILE reads and
can reuse retained data pages; it proves the filesystem view matches the source,
not fresh device read-back. The owner's 2026-10-11 instant stick-verification
observation is consistent with that cache path; its actual hit rate was not
measured. ESP publication separately synchronizes and reopens native FAT files, checks
held-object identity and hashes complete contents; it does not prove cache bypass. Revisit a stronger
program read-back guarantee only after an owner decision; progress coverage
keeps the verification behavior unchanged.

Installation writes fresh metadata and boot files without securely erasing free
space. Classic Update stages immutable attempts and retains current/previous
payloads plus independent pinned rescue and static fallback. The fallback covers
an absent primary configuration, not a corrupt-but-present one. Only explicit
reviewed recovery reinitializes a damaged Classic ESP. Pending activation needs a
fresh Update target/completion Review and typed `complete`, reusing its committed
attempt and retention generation; malformed/ambiguous state needs manual
disposition. [Dirty ESP FAT](#interrupted-update-dirty-fat-recovery) can block
completion until external repair; the publisher never clears the dirty bit.
There is no automatic retry, rollback or repair. Interrupted Install
can leave a partial disk. Revisit stronger guarantees only with an agreed update
and recovery contract. VirtIO and per-device qualified USB support writable native
mounts; the [native publication record](development/experiments/installer-publication/README.md#owner-run-native-gate--2026-10-20)
separates the Classic prior-UI vela pass from the corrected frontend's QEMU
qualification. The [native ThinkPad manual chainload round](development/experiments/grub-limine-native.md)
qualifies its NVMe Install/Update path; automatic Coexist activation and vela
remain separate gates.

## FAT32 parser in the kernel

The owner accepted kernel placement for [FAT32 mounts](devices/fat32-mounts.md)
(2026-10-11), parsing user-supplied media in the kernel. Despite bounded validation,
a malformed or malicious volume may hang or crash the kernel. Revisit before
automatic mounting of arbitrary removable media, the server/public profile or
multi-user work; a userspace provider is the alternative. Native RO/RW mounts
and the trusted workflow are implemented. Final-control close retains bounded
state/exclusion until authorized reacquisition and retirement or reboot, which
can exhaust admission after controller failures. Revisit controller recovery
if retained instances exhaust mount admission in ordinary use.

## FAT32 geometry and hardware qualification

[FAT32 volumes](devices/fat32-volumes.md) accept healthy GPT with matching
512- or 4096-byte device/FAT sectors and the bounded mirrored-FAT profile. Legacy/hybrid MBR and bare FAT media refuse,
so typical unpartitioned or MBR-formatted retail sticks need another layout.
Revisit discovery with a concrete removable-media requirement. FAT12/16, exFAT,
wider Unicode mutation, repair, resizing and hotplug are not supplied; native
[file/directory move limits](#directory-moves-outside-ram-and-npfs) remain separate.

SD cards work only behind a supported block transport. The ThinkPad RTS522A needs
its own reader driver/SD stack; revisit with that hardware assignment, as recorded
in [NVMe coexistence](wip/nvme-coexistence.md#alternative-the-microsd-card-reader).
The FAT engine adds no storage driver.

Qualification covers disposable VirtIO and USB BOT images in QEMU, including
virtual boot ESP provenance, reference drainage and power sealing with files held
open. Physical scratch-USB and native ESP checks remain pending; nested-VM evidence
establishes neither native-media latency nor unplug, torn-sector or power-loss
behaviour. The [workflow timing record](development/experiments/fat32-trusted-workflow.md#cost-and-backing)
is inconclusive, so it supports no performance-neutrality claim. Revisit with
owner-routed physical-media runs before extending hardware guarantees.

The owner will schedule the pending [native read-only ESP run sheet](devices/fat32-volumes.md#owner-present-native-esp-run-sheet).
Every native ESP step requires explicit owner approval and presence. The run sheet
permits no real ESP write; any write qualification needs separate approval/presence
and the consumer's ownership/adoption policy. An authorized RW mount factory may
deliberately admit the boot ESP RW, but the boot-origin issuer/view stays RO.
Milestone closure does not perform or authorize a native run.

## Interrupted Update prepared record

An Update stopped between its prepared and committed boot record needs the
staged committed copy renamed into place from another system before Update can
complete it ([interrupted Update](userland/installer.md#interrupted-update)).
The menu is unchanged meanwhile, so the machine still boots. Revisit with an
in-Pyxis resume, such as Update recognizing the staged committed copy.

## Interrupted Update dirty-FAT recovery

An interrupted Update can leave ESP FAT dirty. Pyxis refuses writable admission,
so even a recognized committed pending record cannot complete until external
filesystem repair. Revisit with a follow-up that validates and clears recoverable
dirty state under the owned writer with proven recovery; that behavior is not
implemented here. The [publication record](development/experiments/installer-publication/README.md#review-corrections-and-interrupted-completion--2026-10-20)
separates dirty-FAT refusal from completion on externally repaired copies.

## Boot publication activation

The 0.0.10 pre-alpha upgrade plan removes the previous boot entry, predecessor
package set and automatic rollback. A failed new release boot can require
rescue, installer Update or reinstall. The independent static rescue pair and
interrupted-publication recovery copies do not select a prior release. Revisit
recoverable failed-release upgrades before Pyxis promises that guarantee or
enters a beta update channel ([0.0.10 upgrade milestone](wip/pkg-upgrade-0.0.10.md)).

Classic normal-build publication is owner-authorized after its prior-UI
vela gate. Native foreign-file preservation remains unqualified: that medium was
freshly wiped. Native power cuts, torn sectors and devices ignoring FLUSH are
unqualified; prior QEMU sector-prefix cuts covered the missing-primary window
and both directory orderings only. Retain the non-extending 30-second interval,
both final payload hashes and all validation/durability barriers. The corrected
action/completion UI has separate QEMU evidence, including dirty-FAT refusal
and completion after external repair. Classic Update now requires its exact
origin marker alongside geometry and pool identity; unmarked layouts refuse
without legacy adoption or falling through to Coexist. Marker qualification and
review remain separate from the prior-UI native gate.
Coexist keeps immutable staging/manual entries until full integrated
Install/Update/completion QEMU qualification and a separate activation record.
Revisit these limits at that activation or before broader native safety claims;
evidence lives in the [publication record](development/experiments/installer-publication/README.md).

## NVMe native qualification and temporary raw authority

The [NVMe backend](devices/nvme.md) passed read-only ThinkPad qualification and
the [2026-10-21 native Coexist Install/Update round](development/experiments/grub-limine-native.md).
The earlier task-1 whole-namespace-only admission limit was superseded by
[task 3's partition-bound authority](devices/installer-authority.md); it no
longer blocks the installer ESP stage. Other targets, including vela's SN7100S,
still need their own write authorization and qualification. The supported
profile's namespace/queue, firmware and shutdown bounds remain in the NVMe
reference. Revisit with a concrete unsupported controller or a separately
approved native gate; no general write or interrupted-power guarantee follows
from the ThinkPad round.

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

## Empty bin volume

Installs from before the core archive have a `bin` volume holding `bin/REVISION` program copies. Update removes those copies once no retained boot
entry binds them, but the volume stays, empty, because no disk ABI removes a volume. It costs one volume table slot. Revisit when volumes can be
removed, or drop it at the next reinstall.

## Install downloads staged in RAM

The install space stages httpfs bodies in RAM, and httpfs caps RAM staging at 64 MiB of open downloads. pkg closes each download after
extracting it, so the limit is the largest single package in `base` (nvim, about 31 MiB). A package past the cap fails at the pkg step
after the pool stage. Revisit when a base package nears it: raise the cap for installer-init or stage on the new pool's `http-work` volume.

## Install checks only base's index entry

`pkg check base` reads only the index, which lists no dependencies, so a dependency missing for this ABI is found only after the pool
stage, leaving a partial install. Revisit if repositories publish partial base sets, for example by listing dependencies in the index.

## Installed bundle runtime reads

Neovim's bundled runtime lives in its installed package on npfs alongside its executable.
The [one-/four-CPU QEMU samples](development/experiments/bundles-in-bin/README.md#installed-startup-cost)
show substantially slower internal startup from npfs than the live archive.
Configuration/backend differences prevent attributing this to a particular
operation; native synchronous file/directory access and whole-tree installation
remain the current path. Revisit runtime read costs/caching in a separately
assigned filesystem or bundle revision-cache task; no optimization is included
in bin lookup.

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

## Initial interface selection limits

[Automatic selection](devices/net0-selection.md) requires complete discovery and reported carrier; VirtIO without STATUS needs an explicit selector,
and an incomplete inventory leaves the setup owner waiting while the shell stays available. Selection binds once until reboot, with no controller
fallback or lease revalidation on link-up, so a cable moved to another port needs a reboot. Revisit with drain and teardown ownership and DHCP link-up
policy, not a second binding or lease authority.

## Virtio-net runtime resource retention

Each interface's first unique controller binding lasts until reboot: address clearing preserves it and controller switching or fallback
after failure is unsupported, and unbound prepared controllers keep their boot resources with DMA and delivery off. The
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

## Transitional shell @name rejection

B8 rejects unquoted redirect-target `@name` to catch obsolete retained-response
scripts before side effects; quoted names remain literal. Owner direction,
2026-10-16: revisit once old scripts are no longer a concern, then make `@name`
an ordinary filename. See the [shell reference](userland/shell.md#retired-request-syntax).

## Shell redirection side effects and file aliases

File redirection opens all targets before truncating outputs, but creation, truncation and launch are separate operations with no rollback: a failed
open can leave newly created files and a failed resize or later launch can leave truncated outputs. The shell now proves explicit outputs distinct from
every stage's FILE stdin before resizing, including capability-path aliases.
Argument files remain outside this check (`cat file > file` can destroy input). stdout and stderr keep independent
offsets when both name one file, so their writes can overwrite each other (descriptor duplication and merged output are not implemented). Accepted for the
redirection scope; revisit argument-file protection or shared-position output when required, noting that batch launch promises no filesystem
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

[Endpoints](interfaces/endpoints.md) have no external cancellation or wait-for-capacity, and admission to a full endpoint fails immediately. Receivers support
mixed readiness waits, which reserve no delivery. The
live-work limit is sixteen delivery records: queued messages, unfinished provider receipts and CALL outcomes awaiting collection use them, and genuine
exhaustion reports QUEUE_FULL (completed work is reclaimed synchronously at final receipt release, which fixed the earlier throughput failures recorded in
the [I/O and IPC baseline](development/io-ipc-baselines.md)). A provider can hold all sixteen slots by leaving receipts unfinished, a deadline releases the caller
but not a delivered receipt or the provider's attachment handles, and calls without a deadline can wait forever (including self-calls or cycles between
blocked single-task processes). Control notifications stay deliverable at full capacity if the provider finishes the retained work. Revisit with asynchronous
service scheduling and explicit cancellation and capacity waits, preserving delivery and outcome reporting and receipt ownership, never revoking delivered
attachments silently, and without retrying or pacing away admission failures. Group termination already cancels its callers and detaches receivers while
preserving outside provider receipts.

## Service startup failure before publication

`service start` waits on the provider's registration endpoint before publishing it in the environment. Providers can report setup failure before registering (the launcher
acknowledges it and optional startup continues), but if a launched provider exits or faults without reporting, the parent still uses blocking RECEIVE
and startup can stay blocked; a provider CALL deadline bounds its own wait, not the parent's RECEIVE. The manually invoked pipe and IPC
benchmarks have the same limit (a companion faulting before its readiness or result message leaves the coordinator in RECEIVE). Revisit with endpoint and
process readiness together (now available through `wait_many`) or a bounded receive facility, never inferring readiness from launch success, adding automatic restart, or describing a CALL deadline as
process termination (see [process termination](#process-termination-and-ctrl-c) and [I/O and IPC baselines](development/io-ipc-baselines.md)).

## Provider calls through synchronous file helpers

The shared FILE helpers and ordinary path and libc opens submit calls without a deadline (native provider OPEN exposes an explicit caller deadline), so a live
provider that stops replying can block file readers and shell input redirection indefinitely; provider exit or withdrawal releases the waits but is not
automatic. The immutable text service does no blocking work. HTTP uses one operation deadline, thirty seconds by default or an explicit absolute deadline,
but that does not bound invocation through deadline-free shared helpers. Revisit caller-controlled bounded file and open waits alongside cancellation and wait sets,
with no hidden retries or global timeout.

## HTTP framing compatibility

The [HTTP library](userland/http-fetch.md) rejects duplicate or list Content-Length, folded fields, non-CRLF headers and unsupported transfer or content
codings, and the pinned chunk decoder rejects excessive framing overhead, so some valid origins fail before the body limit. Close-delimited responses cannot
prove an orderly EOF was meant to end the content. Revisit when widening client compatibility, never silently accepting ambiguous framing or publishing partial
bodies.

## HTTP redirects

[Provider-owned redirects](userland/http-fetch.md#redirect-chains) remain capped at ten
hops. GET, POST and DELETE share the policy: 301/302/303 switch to GET, while
307/308 preserve the method and frozen body. Custom-CA instances refuse origin
crossings; there is no hidden public-only trust fallback or transport-error
replay. Revisit broader trust/replay only with a concrete consumer and a separate
policy decision.

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

## HTTP provider throughput

**Pyxis must be able to hit good network targets overall in the future.**
B6's complete-before-success snapshots retain a measured native 1 MiB GET cost:
12.467 → 13.650 ms (+9.5%, 80.21 → 73.26 MiB/s) after local RAM READ removed
per-read provider IPC. Receive/spooling precedes payload reading; POST improves
36.932 → 29.753 ms (−19.4%). [Owner native A/B′ and attribution](development/experiments/capability-http.md#owner-native-ab-prime-2026-10-15).
The ThinkPad TCP reference is 70.5 MiB/s send / 85 MiB/s receive, a measurement,
not a contract. Revisit before HTTP-heavy consumers (package fetching, Git, the
Claude harness) and whenever HTTP falls measurably short of native TCP; track
the end-to-end gap with each relevant change. Persistent backing retains provider
I/O; these RAM snapshot results do not qualify its throughput.

## HTTP preparation credentials and trust

Owner-accepted interim, 2026-10-15: `http prepare` copies credentials from finite
native header FILEs and captures the selected root's frozen trust profile.
Credentials therefore live as header-file bytes; trust can only be chosen per
root, not per configured endpoint. Revisit with credential storage before making
this the final credential-input or trust-selection design. See the
[configured endpoint contract](interfaces/http-transactions.md#configured-endpoints).

## HTTP provider responsiveness

The [prepared provider](interfaces/http-transactions.md#private-backing-and-limits) uses
native completions and cooperative TCP/TLS progress, but DNS ID/port randomness
still waits synchronously up to **3 s**, and TLS entropy up to **5 s**, bounded by
the request deadline. STATUS/ABORT cannot run during those waits or local
cryptography/service handoffs. Revisit a bounded random-completion facility when
consumer responsiveness requires it; never return fabricated entropy-WANT results.

## Go DNS TCP fallback

Accepted (chronium, 2026-10-23): Go's default resolver uses the configured
`DNS_SERVER`/`dns_udp` path only. A truncated response reports unsupported TCP
fallback rather than substituting a wider TCP scope, so some names may not
resolve. Add a scope-preserving DNS TCP delegation if a real lookup hits this
limit. See [Go networking](wip/go-runtime.md#decision-3--dns-and-the-first-resolver-profile).

## HTTP shared-instance admission

The [native HTTP provider](interfaces/http-transactions.md#private-backing-and-limits)
admits one active exchange per provider process, shared by its independent roots
and aliases until framing/spooling or abort drains. Long downloads can deny
Claude/Git/URI admission. Retained FILEs remain readable under serialized native
completion admission, with a shared bounded RAM backing budget and no eviction;
private npfs is the explicit large-response option.
Per-consumer provisioning/concurrency is deferred; revisit isolation or admission
before Git/Claude integrations run alongside downloads.

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
contents. Revisit selection when native installed NVMe systems or another internal-disk backend are qualified. Preferring the boot device's identity is only a review
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
still fail on that file's own delayed allocation), while rename without replacement stages the directory edit in the running transaction without synchronizing moved-file contents, so it is not durable on
return ([mutation contract](interfaces/filesystem-mutations.md)); cache exhaustion and explicit or background sync still flush the pool ([namespace record](development/experiments/npfs-namespace-writeback/README.md)). Revisit target-only
shrink writeback with a partial-block and pending-growth contract if a workload must truncate despite its own disk-full error. The accepted sync completion point is
durable COMMITTED with checkpointing continuing before the next commit, and the writer's free-inode list avoids per-create scans at mount-time cost
([task-3 record](development/experiments/native-filesystem-task3/README.md)).

The npfs worker can busy-loop while storage is sealed or draining: maintenance
is disabled, but its unchanged periodic deadlines eventually become past and
parking returns immediately. This pre-existing shutdown-path limit wastes BSP
CPU; revisit worker parking on admission-state changes, preserving queued-job
drainage and cleanup wakeups (`kernel/fs/npfs.c`, `npfs_worker`).

Near-full npfs pools reserve up to eight allocatable blocks per namespace
operation and four per writeback step. While the transaction holds frees, these
conservative reservations can force a commit/checkpoint on each operation even
when its actual allocation is smaller. Revisit reservation precision with a
measured near-full workload; preserve undo and pre-staging admission
([running writeback](devices/explicit-durability.md#npfs-running-transaction)).

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

## CPU layout-aware placement

The scheduler uses load, allowed sets and SMT siblings without hybrid capacity
or cache-cluster preference. Equal placement choices can therefore leave fast
cores unused or lose useful locality. The [accepted later direction](wip/cpu-layout.md)
(chronium, 2026-10-25) requires truthful core/cache/cluster and capability metadata;
revisit after HWP-only qualification, possibly after 0.0.9. It is not part of
the current HWP implementation or a proved cause of the measured single-worker gap.

## System-information observation limits

[System information](interfaces/system-information.md) caches one guest-visible BSP CPU brand and the online logical count at boot, so it cannot describe a
heterogeneous machine, hotplug or a process CPU allowance (revisit when CPU lifecycle or scheduling contracts change). Allocator counters exclude permanent
reservations and expose system-wide usage to every READ holder, so keep the **Memory (allocator)** label; installed memory needs its own authoritative source. The
embedded source commit identifies the kernel's base checkout, not whether inputs were modified, and no dirty suffix or clean-tree attestation exists; revisit when
release or support workflows need that distinction, after agreeing which inputs count.

## Fastfetch first-port boundary

The [implemented port](userland/fastfetch.md) uses explicit native-URI JSON/JSONC configs, one-shot text or JSON output and eleven selected modules, and is delivered as
the `fastfetch` program package. Automatic config discovery, config and cache writes, dynamic refresh, image logos, Lua and executable or network helpers are excluded, so upstream
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

The private [interrupt-IN path](devices/usb-interrupt-in.md) supports boot/runtime low/full/high-speed HID on USB 2 paths, including new/returning supported hubs; HCI remains root/full-speed.
Runtime HCI attachment, USB 3 HID and broader HID report profiles remain unavailable. The 32 retained HID admissions and shared topology pools can exhaust until reboot.
Ordinary runtime admission has 32 retained records; known NCM route prefixes have
one protected fresh-attempt share in both core records and host contexts. Failed
attempts consume backing too. Three consecutive failures or eight attempts during
one continuously connected/flapping period pause that connection with one warning
until observed stable unplug/replug. NCM retains its pending recovery and unspent
route protection through the pause; exhausted retained backing stops it visibly.
Stable disconnect resets the connection budget, not retained capacity.
This bounds a faulty branch but may require a deliberate unplug after repeated
flaps. Revisit reclamation and retry policy with native recovery evidence, keeping
fresh identities and hardware fences.
Revisit broader profiles or reusable resources for a concrete device, with a bounded lifetime design.

Device/subtree loss has fenced local retirement. Failed fences, malformed/unowned events and controller errors can still disable the whole controller until reboot;
automatic interrupt STALL recovery and DMA reclamation are absent. Revisit for a concrete recovery requirement, preserving ownership.
The [HID qualification record](development/experiments/usb-hid/README.md) does not establish native periodic-transfer coverage.

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
needs them. Supported boot/runtime USB 2 and USB 3 hubs have bounded notification streams; one BSP topology owner admits new/returning hubs and USB 2 low/full/high-speed
HID children. Runtime storage stays descriptive, with no BOT/raw-block admission or automatic pool reattachment; runtime Bluetooth and USB 3 HID binding remain excluded.
Device/subtree removal has child-first fenced retirement; failed fences can quarantine the controller. Retired records and DMA remain until reboot. Revisit broader
profiles and reclamation with concrete requirements. USB 3 inspection omits SET_SEL and SET_ISOCH_DELAY, so complete
inventory is not full USB 3 conformance (revisit with path-latency accounting before power management or non-control scheduling, never sending zero placeholders), and
USB 3 traversal keeps the conservative USB 2 stability and recovery delays with no explicit warm-reset retry.

Each device admits one active control request and each admitted BOT device serializes private bulk exchanges. Owned control/bulk stalls have bounded endpoint recovery
(including TT cleanup and safe dequeue retirement). Admitted HID leaf loss can use per-device endpoint/slot retirement; deadlines, unowned/corrupt events, failed fences
and active non-HID removal can stop the whole controller, retaining unresolved DMA until reboot. BOT probes and kernel block reads ran 512/4096-byte reads and
large-LBA SCSI commands in QEMU, including hub descendants and several controllers; GPT waits for
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

Known WP-clear BOT media qualifies either real synchronization or the owner-accepted
[write-through assumption](devices/usb-storage.md#write-and-cache-qualification)
for clean current ILLEGAL REQUEST `20/00` or `24/00` synchronization rejection.
No caching page/WCE is queried. Its ordered FLUSH is a no-op, so power loss can
lose data the device claimed written, as with Linux's write-through assumption.
Revisit with actual cache-reporting/power-loss evidence; do not infer persistence
from vendor identity or successful ordinary writes.

Unknown protection, unusable MODE SENSE headers and other clean sync rejections
stay read-only. Optional qualification exchanges that break transport still fail
the whole probe even after successful reads. Recovery to readable service after a
safe reset remains deferred; revisit only with natural device evidence and verified
transfer ownership. No MODE SELECT/cache-mode change is attempted.

The five-second exchange deadline and shared boot-media deadline also bound synchronization, so a slow genuine flush can retire a capable device; revisit with measured
physical flush latency. QEMU completion and restart checks qualify no firmware, cache behavior or power loss, and only one physical stick has qualified. A failed runtime
write or flush, or an abandoned published mutation, latches write failure for the boot (reads stay admitted on healthy transport and the filesystem may keep its own
writeback error), with no replay or later successful flush clearing either uncertainty; revisit recovery only with an error-acknowledgment and ownership contract. Mutation
failure and abandonment, MODE SENSE fallback, unsupported flush and malformed qualification responses are source-reviewed only.

## USB stick write throughput

On the ThinkPad's SanDisk 32 GB USB 3.2 stick, Pyxis writes a 32 MiB file at
about 3–12 MB/s, varying widely between samples and boots, while Linux writes
25–60 MB/s on the same stick. Larger SuperSpeed WRITE commands (up to 512 KiB)
gave no repeatable gain ([sweep record](development/experiments/usb-superspeed-transfers.md#native-write-sweep-2026-10-14)).
Each workload issues about 15,200 small 4 KiB WRITEs at every command size, and
slow samples spend their time waiting for the device in the data and status
phases. The likely cause is the filesystem's small-write pattern (metadata,
journal and reclamation) on media that handles scattered small writes poorly;
this is unproven. Consequence: copying to USB sticks is several times slower than
on Linux. Revisit with the next storage task: attribute the small WRITEs to
their npfs sources before proposing a change.

FAT32 batches ordered phase barriers, but still allocates clusters during
mutations and repeats chain-validation reads. The [native policy comparison](development/experiments/explicit-durability-measurements.md)
reduced the fixed staging+sync envelope from 11.6–12.1 s to 9.5–10.1 s; it does
not measure the full installer or prove improved fragmentation. Revisit remaining
read/preflight costs with a matched workload before changing allocation. Near-full
batch admission and different media still need separate measurements.

## USB controller and transport coverage

xHCI is the only USB host-controller driver. EHCI, OHCI and UHCI controllers (such as the ThinkPad's Realtek DASH EHCI) remain unsupported inventory records, `lsusb`
reports partial coverage, and disks behind them are invisible to configured mounts and the installer. [USB storage](devices/usb-storage.md) uses Bulk-Only Transport only: a
device offering UAS as an alternate is used through BOT (throughput cost unmeasured), and a UAS-only device is unsupported. Checked boot keyboard/mouse HID and the
bounded AX200 HCI binder now join hubs and storage; other class profiles remain unbound. Revisit when a target device or workflow needs another controller type, UAS
or broader HID support, adding each through the existing
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
would catch, or a target routes IRQ 12 elsewhere. The calibrated T14's
[pointer input](devices/pointer-input-quality.md) now defaults to absolute EW/AGM
palm/typing rejection and paired scrolling, with TrackPoint as a separate source.
Native query geometry,
physical-button/TrackPoint routing and authored contact tuning are qualified on
the owner's calibrated T14 by the 2026-10-13 runs, including #773 `fef995a7`
EW/AGM secondary delivery, paired scroll/drag and good typing response.
Other touchpad identities and tuning remain unqualified and stay relative,
even when queries supply usable geometry; ps2.synaptics=1 cannot bypass the
calibrated-profile gate. ps2.synaptics=0 forces relative on all pads. Without
matching calibration, relative input does not qualify palm/scroll support.
Revisit admission of another profile with that hardware's native query,
geometry and contact evidence; never infer square units from one axis.
[USB mice](devices/usb-hid.md) are independent sources; report-wheel and the
reserved-ID boot-wheel paths are implemented, while other unknown extensions
remain undecoded. Doom has no mouse support.

## Lua build runtime limits

[Lua](userland/lua.md) supplies io/os, pure-Lua modules and native build helpers; `file:setvbuf`, `io.popen`, `os.execute`, `os.clock`, `os.setlocale`, debug, full math and
dynamic modules are absent, and `os.time(table)` raises upstream's error for DST gaps and unsettled folds ([calendar limits](#calendar-and-encoding-profile)). Revisit each
for a concrete consumer. `os.tmpname` reserves a real exclusive empty file that callers must remove, and `io.tmpfile` creates then immediately unlinks one, so abrupt death
between those operations or a failed unlink can leave a named file with no stale-name cleanup (revisit atomic anonymous creation only for a lifecycle need; reserved names
are not ISO C `tmpnam`). `pyxis.run` inherits live C streams, omitting closed ones, but cursors, append mode, pushback and read-ahead belong to the parent runtime, so child
streams begin at zero, unread buffered pipe bytes stay in Lua and `io.input`/`io.output` rebinding is local; revisit shared stream state only with a native ownership design.
C-locale `strftime` supports standard conversions and E/O forms without width or flag extensions, `%z` loses historical offset seconds at minute precision (`tm_gmtoff` keeps
them), a `tm_zone` designation is borrowed until process exit, including across cache replacement; locale selection is deferred.

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
has no direct-truncation fallback. Recursive copy (`-r`) keeps a partial tree after a failure with no rollback, and it is deleted with `rm -r` ([rm](userland/rm.md)); trees beyond 32 levels or
65,536 entries are refused. Interruption can leave a named temporary file or a `.cp-tree-` marker, unconfirmed creation or publication is reported without retry or removal, failed cleanup can leave partial storage, and there is no stale-file sweeper or crash-durability
guarantee, so operators must establish which names exist before removing anything. Revisit reservation and publication primitives if cp must tolerate another writer changing
its temporary, and cleanup policy when persistent use needs recovery from interrupted copies. Provider sources, native disk copies, durability and ThinkPad usage are
unqualified (evidence covers archive, RAM and HOST copies in nested QEMU).

## Recursive rm limits

`rm -r` ([reference](userland/rm.md)) has no rollback and no atomic tree removal, so a failure or Ctrl+C leaves a partial tree, and the operator finishes it by running the command again. It stops at 64 levels, skips host symbolic links and special entries, and cannot remove a host bind mount. The native interface has no conditional remove, so a directory moved out of the tree mid-run is still emptied through its held handle and a name replaced after emptying can lose an empty directory in its place. Against a concurrent changer it gives up on a directory after several changes in a row with no removal between them, and otherwise keeps going. Revisit a handle-bound or conditional REMOVE if a consumer needs removal under concurrent writers, and deeper trees if a real tree needs them.

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

## HD Audio controller and codec coverage

[Admission](devices/hda.md#admission-and-controller-quirks) is by HD Audio class,
but native output still needs an exact codec profile (vendor, AFG subsystem and
pins) and, for controllers without a PCIe capability, a listed coherence quirk.
Other laptops with an HDA controller therefore report `no qualified native codec
profile` or fail closed at coherence setup rather than playing through a guessed
route. Immediate-command controllers have no unsolicited-response path, which the
live jack switching above would need. Revisit when another board is targeted:
add its profile from its codec inventory, or consider a checked generic parser.

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

The [bundle design](userland/program-bundles.md) accepted on 2026-10-09 temporarily
delivers every available ordinary grant, including optional grants at launch,
without consent in the unpacked development path. This is not a permanent security
contract; metadata never creates rights or obtains system-only authority, and programs inspect actual
startup grants. Revisit with users/permissions and the recorded required-grant,
in-context optional-grant, trusted-picker, stable-identity and revocation model.

## Published development bundle revisions

Unpacked [bundles](userland/program-bundles.md#app-views-grants-and-lifetime) rely on the
publisher leaving a selected revision unchanged and retained. Read-only app/root
grants and held native handles do not freeze writable aliases or protect unopened
assets from deletion. Revisit with installation/update lifetime and revision GC;
editing or deleting a live revision is unsupported.

## Native libuv first-slice limits

The [native adapter](../ports/libuv/README.md) is limited to one thread, 31 opened
stream/process interests per loop and synchronous filesystem calls. Children start
bundles by `.pxb` path and inherit native or exported FILE descriptors for the standard streams, but each
FILE starts with an independent cursor at zero; sharing the parent's cursor is unsupported. Value-only peripheral
APIs are omitted (the `uv_os_*` queries Pyxis can answer are listed in the
[libuv notes](../ports/libuv/README.md#operating-system-queries); passwd, group,
priority, CPU, memory and load queries stay refused). Luv consumes the native exit reasons and reports unsupported
PIDs, signals and omitted entry points as ENOSYS ([Lua 5.1 limits](#lua-51-and-luv-limits));
Serving providers in a libuv loop needs a
receiver adapter. Shared-process threads require a separate milestone.

## Calendar and encoding profile

Libc's [`mktime`](userland/timezones.md#c-interface) fails with ENOTSUP for wall times in a DST gap and for folds that a
nonnegative `tm_isdst` cannot settle, including standard-offset changes. Callers that expect glibc's adjustment across
gaps or its choice in folds get an error instead. [`iconv`](kernel/userspace.md#foundational-libc)
converts only UTF-8, ASCII, ISO-8859-1 and UTF-16LE/BE: Neovim reports other `fileencoding` values as unconvertible
(it converts Latin-1, Latin-9 and the Unicode forms itself), and Git's BOM-detecting `UTF-16` working-tree encoding fails.
There is no transliteration. Revisit when a port or user needs another encoding or gap normalization; each addition
imports its musl table or rule explicitly.

## Lua 5.1 and luv limits

The [`lua51` interpreter](../ports/lua51/README.md) is a development bundle without an interactive mode. Its standard libraries omit
`io.popen`, `os.execute`, `os.clock`, `os.setlocale`, `file:setvbuf` and C modules until libc has
`popen`/`system`, `clock`, locales and dynamic loading; libc now has `setvbuf`, which the port's patch predates. `os.time(table)` returns `nil` for DST gaps and unsettled
folds ([calendar limits](#calendar-and-encoding-profile)). luv reports TCP, UDP, DNS, watches, signals, work queues,
callback-style filesystem calls, IDs and the libuv profile's omitted introspection as ENOSYS. The libuv adapter now accepts FILE
descriptors for a child's standard streams, but luv spawn with an inherited FILE descriptor (for example a redirected stdout) is not
qualified. compat-5.3's Lua modules and LPeg's `re.lua` are not staged. Revisit when a script needs one of these, and with sockets or threads in libuv.

## Directory moves outside RAM and npfs

Owner accepted 2026-10-12: directory moves support RAM and writable npfs only;
FAT32/host return BAD_OPERATION, with no cross-store copying. FAT32 needs atomic
runtime identity/held-child locator repair, on-disk `..` updates and qualification
of its ordered failure behavior; host external-name races need their own contract.
Revisit when directory moves on either store are required. See [native rename](interfaces/directories.md#atomic-rename-and-directory-moves).

## HTTP staging worker

Owner accepted 2026-10-13: shell body producers write a bounded pipe and a trusted
worker checks every staging write and final close. **To look into later:** direct
body FILE WRITE plus a reliable producer I/O-completion contract could remove
this process/copy/IPC cost. Nested controls measured an extra 4.371 ms per empty
producer (2.977–4.949); whole-POST paired differences cross zero. Revisit against
[worker costs](development/experiments/http-shell.md#matched-worker-costs-and-get-control);
exit status alone misses ignored or uncertain writes. Capacity and declared
isolation remain unchanged by that future comparison.

Private NPFS provisioning leaves can remain as empty linked directories after
startup/provider death; closing/unlinking payloads is not secure erasure.
Revisit directory lifecycle cleanup before frequent service restarts or long-lived
HTTP-heavy deployments. RAM leaves disappear with the boot's private tree.

## Userspace kernel log writer

Only holders of an explicitly delegated Caelum console grant can add userspace
records to the kernel log today; the installer uses WRITE alone for diagnostics.
Revisit a properly designed, authority-scoped record writer when other trusted
services need structured log output.

## NVMe host memory buffer

Caelum does not allocate or enable the optional NVMe Host Memory Buffer. A
DRAM-less controller can provide the supported NVM command profile without it,
but lookup/cache performance may suffer; the owner's Linux view of vela's
SanDisk SN7100S used 32 MiB. No Pyxis native performance comparison is recorded.
Revisit after NVMe 2.0 read-only native qualification and measured storage costs,
with explicit DMA ownership and disable/reset retirement before releasing HMB
memory. [Compatibility scope](development/experiments/nvme-task1/README.md#vela-nvme-20-scope--assigned-2026-10-14).


## Paired service captures are leaf-only

Owner-accepted, 2026-10-15: a service carrying direct LAUNCHER authority cannot
start through `svc=$(service start ...)`; use a named bootstrap service instead.
The unpublished capture has no durable execution-group owner after publication,
so it must be able to stop and wait for the direct provider on publication
failure. Revisit when nested execution groups gain an explicit durable owner
and release rule, before Git, Clang and native builds.

## Script parity trusts only #!boot://shell.pxe

Owner-accepted, 2026-10-15: a script receives `child_launcher` and pipe creation
only when its shebang names exactly `boot://shell.pxe`. A script naming
`bin://shell.pxe`, `bin://shell.pxb` or any other shell path gets neither, so its
pipelines and child launches fail, even though the program is the same shell.
The trust is by exact URI, opened from the shell's held read-only boot-archive
root, because the launch library can decide it from the one interpreter string
it parses.
A shell also gets script parity only from launchers that supply `interpreter_root`
(boot init, `session`, remote and multiplexer launchers); shells started by other
launchers, such as the FAT32 init helpers, get none. `interpreter_root`'s
provenance is by convention, not authenticated: boot init seeds it and `session`
and the Remote launchers forward it, but launch admission accepts any private CAP
of that name from a caller holding LAUNCH, so such a launcher can mint one for a
shell it creates. That shell can only pass on grants it holds, which the launcher
already had, so nothing is escalated, and no route replaces a boot-seeded shell's
root. Revisit when an installed system's own shell path (such as `bin://` on the
pool) needs script parity, or when interpreter trust moves to a declared grant or
bundle identity. [Script mode](userland/shell.md#script-mode).

## Concurrent output to one file

The shell truncates a redirected output file once and passes it with append
policy, so sequential commands in a script follow each other. Concurrent writers to
one file, such as background commands or `> out 2> out`, append non-atomically
and can overwrite one another's bytes. This is the
[non-atomic stdio append](#non-atomic-stdio-append) limit; revisit it with a native
operation that chooses the end and writes in one file operation.

## Synchronous DesignWare input service

Deferred: GPIO interrupt input and non-spinning DesignWare I2C completion are
outside touchpad tasks 3/4; revisit under a separate implementation go to reduce
idle wakes and active spinning, preserving transfer/deadline ownership.
[Measured costs and limits](development/experiments/vela-hid-i2c-polling/README.md#native-sweep-2026-10-16).


## Clickpad tuning

Accepted as-is on 2026-10-18. Native vela checks covered strip-start motion,
initial paired selection, scrolling and repeated presses with the earlier 1 mm
retained bound. The 3 mm grace has build and 20-case QEMU algorithm evidence,
without a new native 3 mm run; initial qualification remains 1 mm. Further
refinement waits for practical use with an actual GUI. The
[record](development/experiments/clickpad-tuning/README.md) separates native and
QEMU evidence; the Kconfig defaults remain revisitable tuning.

**ThinkPad right click remains open** (low priority, physical buttons available).
Its current-stick bottom-right/two-finger right-click loss is not attributed.
The available log proves PS/2 reporting, but not selected mode; source inspection
shows the calibrated absolute path decodes the clickpad switch as primary and
bypasses HID software-button mapping. Confirm the actual mode before changing
it. Native `ps2.synaptics.diagnose=1` qualification waits for epsilon to finish
with the ThinkPad; no PXE staging is part of this slice.

## Textual PATH translation

PATH-aware launches accept only an absent PATH or the exact diagnostic projection
of the held [command list](userland/command-search.md). Other text refuses; it
cannot add authority. Revisit when a port needs explicit translation into a
narrowed list of directories already held by its caller.

## Recursive removal of large trees

In the [durability ABBA](development/experiments/explicit-durability-measurements.md)
on the ThinkPad's 32 GB SanDisk, removing the `clang-hosted` tree (8,781 entries)
took 232.6 s and 237.0 s with the old barrier policy and 32.5 s and 25.1 s with
batching (the current policy), about 3 ms per entry. The owner also found `rm -r`
of an old toolchain on their stick slow (2026-10-20). Suspects, none profiled:
per-entry npfs namespace operations even with batched barriers, the per-directory
cwd-chain safety walk in `rm -r`, and USB small-write latency.

Consequence: large cleanups take tens of seconds per ten thousand entries.
Revisit with one profiled `rm -r` of a copy of that tree.

## pkg extraction stalls before the first file

On chronium's installed stick (0.0.7a, 2026-10-20), `pkg install clang-hosted`
(372 MB) fetched at 13.9 MB/s and verified to 100%. The extract line then stayed
at 0/8774 files for about 30 s before the first file counted (see the
[install progress display](interfaces/packages.md)). Suspects, none profiled:
creating the staging directory or a sync on the packages volume before extraction
starts, reading and parsing the archive index or manifest before the first write,
and the extract counter updating only after an initial pass.

Consequence: a large install shows no extraction progress for tens of seconds.
Revisit with one profiled install.

## Package root lookups re-read the set

Each lookup through a space's `pkg://` view reads `current` and the
set's whole command projection, validates it and frees it again
([reaching package files](interfaces/packages.md#reaching-package-files)).
Nothing is cached between lookups, so a set switch is seen at once.

**Consequence.** In QEMU (nested KVM, 4 CPUs, 2026-10-21) with 4 commands and 1
root in the set, an open through the then `fonts://` root, the same view, cost
about 335 µs, against 2 µs for a miss in `tmp://`. The projection read grows
with the set (968 bytes per record, at most 1 MiB); the cost at that bound was
not measured. Programs that open
many package files pay it per open; a held directory handle avoids it.

**Revisit** when a consumer opens many package files by URI or a set nears the
index bound: keep the last capture per volume, keyed by the active set
number, in the view, as the `sdk://` view will (decision 44 of
[the `sdk://` view](wip/sdk-view.md)).

## TCP vendor traversal cost

Native TCP/UDP queues and retirement use bounded worker batches, but pinned
lwIP still traverses TCP PCB lists for binding, timers and address changes with
interrupts enabled. Native UDP also scans all endpoints for each inbound binding
lookup and each ephemeral-port candidate. Dynamic storage removes the old small
count caps, so these passes can delay other network work at large populations.
Revisit indexed/resumable TCP and UDP lookup before claiming Caddy-scale service
latency; native throughput and fairness remain unqualified.

## Declared service creation under grant-all

Accepted (chronium, 2026-10-23): a bundle explicitly declaring `service/create`
can receive the launcher's held general endpoint/IPC constructor, privately.
Under grant-all it can create/export endpoints and consume their metadata, not
only create completion receivers. Revisit at first-launch required/optional grant
consent: expose the right to the user and narrow it to a completion-receiver
constructor if needed. Ordinary children do not inherit it implicitly.

### ImGui system clipboard

The native ImGui SDL profile keeps its context-local clipboard buffer because
widget actions run after the originating physical input has been delivered;
they cannot reuse SDL's gesture-bound system clipboard authority. Cross-program
copy/paste is unavailable in this profile. Revisit with the first ImGui consumer
needing it, designing deferred gesture authority rather than adding ambient
clipboard access. [Profile](development/library-dev-packages.md#dear-imgui).

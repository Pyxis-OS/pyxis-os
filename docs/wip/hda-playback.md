# HD Audio playback milestone proposal

Status: **three defaults accepted 2026-10-08; first controller/codec task
merged in [#553](https://git.internal/PyxisOS/pyxis-os/pulls/553); sessions,
mixing and IRQ refill implemented in
[#557](https://git.internal/PyxisOS/pyxis-os/pulls/557), awaiting owner review/merge**.
Prepared from main `67e14be` and
the completed [QEMU investigation](../development/audio-investigation.md).
The investigation probes stay unmerged. The owner assigned task 2 on
2026-10-08, combining periodic refill and per-space session/mixing steps below,
with the accepted call/write/admission semantics. Its
[baseline, accepted policies and qualification](../development/experiments/audio-task2/README.md)
record fresh main `780f5d2`, no-playback observations and the remaining
hidden-space/start-stop/failure defaults, accepted 2026-10-08. Exact PCM and
saturated mixing were observed, but an eight-admitted nested-QEMU run exceeded
the service horizon and failed closed; sustained eight-session playback is not
guaranteed.
The [task 1 report](../development/experiments/audio-task1/README.md) retains
private engine qualification and matched no-audio/engine-idle observations.
Native and consumer tasks still require their own assignments.

The proposed goal is one analog playback engine, bounded per-space PCM sessions
and native ThinkPad speaker/headphone qualification. QEMU comes first. Recording,
HDMI/DP, USB/dock audio, Bluetooth audio, arbitrary devices, surround sound,
exclusive hardware passthrough, suspend/resume and a general sound-server
framework are outside this milestone. SDL2 and Quake adapters are later,
separately assigned consumer work; they are not prerequisites for establishing
an honest native PCM interface. No SDL2 or pointer changes are included here.

## Accepted decisions

Accepted by the owner through the orchestrator on **2026-10-08**, after the
[review of #549](https://git.internal/PyxisOS/pyxis-os/pulls/549):

1. **Ownership, authority and mixing:** one BSP-owned kernel HDA worker and
   mixer in `kernel/audio/`, with exclusive, process-owned per-space sessions
   through a named `audio` grant; at most **eight active sessions** system-wide.
   Hardware/DMA ownership stays with the worker. The grant provides session
   authority, not PCI, DMA or another space's controls. Mixing runs on the BSP,
   so its cost and refill margin still need measurement.
2. **Format:** **48 kHz S16LE stereo PCM**, with conversion and resampling in
   userspace. The kernel combines session PCM; it adds no resampler. Both the
   measured QEMU route and the supplied ALC257 analog DAC capabilities support
   this format. Advertised native support does not qualify native playback.
3. **Starting buffer tuning:** **eight 10 ms DMA periods (80 ms)** and an
   **80 ms copied queue per session**. The DMA depth was revised from four periods on 2026-10-08 because QEMU
   can fetch 8192 bytes in a burst, exceeding the earlier 7680-byte ring.
   These are starting tuning values to revisit natively, not
   hard real-time or audible-latency guarantees. A timely refill can substitute
   zeros for producer starvation; a missed hardware refill can replay old cyclic
   DMA frames before detection. Measure and report underrun, discontinuity and
   recovery limits before freezing tuning.

The accepted task-specific call contract below and current task assignment
settle public call behavior and scope. Session priming, hidden-space playback
and refill failure/stop policy are accepted in the task 2 report. Numeric ABI encoding
and rights bits are implementation choices; native route preference and jack
switching remain later task-specific review items. Proposed details below are distinct from the accepted decisions.

## Closure alternative and owner confirmation

The owner's established practice, conveyed on **2026-10-08**, is **QEMU closure
with native checks in a later ThinkPad batch**. Carry this forward as the closure
alternative: after the production tasks pass QEMU qualification, retain native
AMD/ALC257 playback, speaker/headphone switching, stop/underrun behavior and
usable latency as explicit technical debt for the later owner batch. The
ThinkPad remains reserved for Bluetooth work until available.

**Confirm this choice with the owner at milestone closure.** It is not a current
request for another decision or permission to close early. At that gate, present
the QEMU evidence and remaining native checks; the owner confirms QEMU closure
with retained debt or requires native qualification before closure. QEMU closure
must not claim native sound, and the supplied Fedora codec dump is inventory
and state evidence, not native Pyxis playback qualification.

## Accepted session call contract

Accepted task 1 defaults, **2026-10-08**. The task 2 implementation now exports the
[PCM session interface](../interfaces/audio.md), including native message layouts,
protocol/right constants and exact validation precedence. The table below retains
the accepted call behavior; the interface reference describes its implementation.

| Call | Request after the standard message header | Result / behavior |
| --- | --- | --- |
| ACQUIRE | No additional fields | Exclusive process-owned session in the grant's space; return fixed format, queue capacity and session generation. |
| WRITE | `uint64_t buffer`, `uint64_t length` (caller address, bytes) | Copy and commit all requested frames or none; success returns the accepted byte count. |
| STATUS | No additional fields | Return queue capacity/free frames, session generation, starvation and hardware-discontinuity counters and terminal state. |
| RELEASE | No additional fields | Discard queued PCM, cancel uncommitted writes and release ownership; no reply payload. |

The named audio grant authorizes calls only within its own space. WRITE, STATUS
and RELEASE require the acquiring process; copying/closing a handle does not
transfer/release its session. Process exit releases ownership. ACQUIRE fails
with CALL_BUSY for an already acquired session, including repeat acquisition
by its owner; CALL_LIMIT for admission beyond eight active sessions;
CALL_NO_MEMORY for allocation failure; CALL_UNAVAILABLE for absent, unsupported
or failed audio. Missing authority, a wrong space or a wrong session owner is
CALL_DENIED. Exact validation precedence is documented with the implementation.

WRITE is nonblocking, at most **4096 bytes**, with four-byte stereo-frame
alignment. Oversize is CALL_LIMIT; malformed alignment/request is CALL_BAD_REQUEST
and inaccessible memory is CALL_BAD_BUFFER. A full queue is CALL_WOULD_BLOCK,
accepting no data and preserving reply storage. No caller buffer survives return.
Zero length is a no-op after request, authority, ownership and buffer validation.
WAIT_WRITABLE is level-triggered and guarantees room for a maximum-size write;
it reserves nothing, and terminal failure reports WAIT_ERROR. STATUS allows a
producer to choose a smaller write from the available complete-frame capacity.
The accepted calls have no separate write deadline; WAIT_MANY supplies existing
wait deadlines. No audio drain guarantee, DMA mapping or audible frame position
is exported. Already mixed hardware frames may outlive release until consumed.

## Proposed ownership and lifetime

Hardware-specific PCI/MMIO/interrupt operations stay behind architecture helpers;
controller/codec policy and mixer state live in `kernel/audio/`. Follow the
[PCI claim/reset](../devices/pci.md) and [BSP ownership](../kernel/smp.md) contracts.
Prepare mappings and coherent physical DMA on the BSP before AP startup. Runtime
BSP request adapters perform session allocation and teardown; neither an AP
syscall nor an IRQ handler changes DMA or codec state. An IRQ captures bounded
status and wakes the owning worker; the worker performs command correlation,
period accounting and refills. HDA's frame clock drives playback; HPET deadlines
bound command/refill/watchdog waits. Do not use the 120 Hz preemption tick as
an audio clock or retain the probe's permanent polling as the production design.

CORB/RIRB has one command in flight initially, checked codec identity, explicit
unsolicited handling and bounded timeout. A late solicited reply cannot become
the next command's response. Codec graphs, connection indices, amplifier offsets
and power transitions are validated before enabling a pin. Choose a supported
controller explicitly; do not select the GPU or dock by finding the first PCI
multimedia function. Native AMD `1022:15e3` is a separate qualified match, not
proof that every HDA-compatible controller works.

One hardware output stream and BDL belongs to the worker for the boot. Sessions
own copied queue storage, never DMA mappings. The worker alone advances queue
consumption and hardware period generations. With eight sessions, the proposed
PCM queues total 122,880 bytes; the 80 ms DMA payload is 15,360 bytes before
page/alignment rounding. Counters, request staging and descriptor storage are
additional. Admission must fail explicitly when capacity/allocation is unavailable;
there is no silently dropped ninth session. Mix cost scales with active sessions
and must be measured under one- and multiple-CPU load, especially on the BSP.

Acquisition is process-owned, analogous to keyboard/display sessions. Grant
copies do not transfer an acquired session; process exit releases it. A child
can acquire only after the previous producer releases ownership. Release or
exit invalidates the generation, cancels uncommitted writes, discards queued
PCM and silences that source in subsequent mixed periods. Frames already handed
to hardware may remain audible until consumed: other spaces must not be reset
just to flush one producer. Close/stop semantics must expose that limit. No
worker dereferences a caller buffer after a syscall/request relinquishes it.

A trusted startup may omit the grant. Ordinary producers can affect only their
own PCM and any explicitly granted per-session volume/pause controls. Session
acquisition uses identity/authority checks rather than global device access.
Continuing in a hidden space is proposed independently of keyboard/pointer focus;
user-visible global mute/volume controls require their own authority and owner
policy. The first tasks must settle that policy before shipping a control UI.

## Proposed queue, underrun and failure behavior

Copied writes return the amount accepted, with complete-frame alignment and
explicit full-queue and terminal results. The accepted contract makes writes
atomic and copied, with cancellation before their commit point. WRITE has no
separate deadline; wait deadlines do not revoke committed frames. A writable wait reports real queue capacity, avoiding SDL-style 1 ms
polling. Position/status distinguishes submitted, mixed and hardware-consumed
frames; hardware DMA progress is not a promise that a sound has reached the
speaker. The probe's truncated first captures demonstrated why a drain operation
needs an honest device-specific contract or an explicitly unsupported result.

Initially idle hardware stays stopped; start/reset sequencing belongs to the
worker. During playback, empty sessions contribute zero frames while the mixer
continues for other sessions. A producer starvation counter differs from a
missed hardware refill: record both with a discontinuity generation. On refill
lateness, derive which periods are still safe from observed hardware position
and sequence; do not overwrite an owned/current period or replay stale buffers.
Cyclic DMA can replay previously filled periods if the worker misses a whole
lap before detection. Already-replayed sound cannot be undone; report a hardware
discontinuity and stop/reset rather than claiming uninterrupted zero-fill. If
modulo position cannot establish how many laps elapsed, do not invent an exact
consumption count. The assigned refill/session task must define detection, counters and recovery thresholds
and measure this scheduling limit. Ten-millisecond interrupts do not guarantee
a deadline in nested KVM.

A stalled command, lost stream progress or FIFO/descriptor fault stops admission
and reports unavailable. Mask delivery, request RUN clear/reset, stop rings and
confirm link reset/bus-master disable before any reuse. If ownership cannot be
proved, retain/quarantine DMA and preserve unrelated device workers. Start with
explicit reboot recovery; automatic controller restart and hot removal are
separate scopes. Do not treat client starvation as a reason to quarantine the
controller or all other sessions.

## Proposed task sequence and review gates

1. [x] **Accepted session contract and native inventory.** Document
   ACQUIRE/WRITE/STATUS/RELEASE, process ownership/exit, copied atomic writes,
   writable readiness and distinct admission errors. Retain the supplied
   [ALC257 dump](../development/audio-investigation.md#native-handoff) as inventory.
   Public ABI encoding/session implementation belongs to task 4; native
   speaker/headphone route/jack policy belongs to task 5.
2. [x] **QEMU controller and codec engine.** Implement production PCI claim,
   CORB/RIRB, checked graph traversal and one discovered analog output route.
   Keep arch/device and BSP boundaries explicit. Qualify known PCM through WAV,
   independent left/right signals, command wrap and stop ownership with normal
   builds, interactive boots and debugger inspection. Polling may be a bounded
   bring-up step, not the completed runtime implementation.
3. [x] **Periodic output and refill.** Add owned interrupt delivery, BDL/position
   accounting, silence on starvation and explicit discontinuity. Review recovery
   thresholds; exercise sustained playback, ordinary producer pauses, close/reopen
   and normal concurrent guest activity. Measure position/clock agreement and
   queue-to-output behavior. Revisit the proposed period before freezing policy.
4. [x] **Per-space sessions and bounded mixing.** Review exact reply packing,
   protocol/right constants, validation precedence, priming and hidden-space
   policy before implementing the public calls. Implement only the reviewed
   grant/calls and BSP request bridge, with copied queues and generation-aware
   cancellation/exit. Qualify two distinct simultaneous signals, silent/active
   spaces, denied authority, exclusive acquisition and capacity admission.
   Measure BSP cost and refill margin with one and multiple CPUs. Add only the
   concrete native PCM producer needed to exercise the accepted interface.
   Task 2 implements steps 3–4 together. The [qualification report](../development/experiments/audio-task2/README.md)
   records exact single-producer and eight-source saturated PCM, pause/exit and
   reacquisition, eight admissions/ninth refusal, and matched idle/BSP costs.
   Eight admitted producers later exceeded the service horizon and failed closed;
   admission is not a sustained-playback guarantee. Strict absolute DMA progress,
   zero-gap startup and native playback are unqualified. The current integration image built and
   passed basic PCM/absent checks; a later paused repetition failed closed before
   debugger attachment. Publication and exact-head CI remain delivery gates.
5. [ ] **Native AMD analog qualification.** Propose speaker/headphone route
   and jack policy from the supplied ALC257 graph before native binding.
   Bind `1022:15e3` after verifying
   capabilities and the actual codec route. Inspect licensed/pinned fixups where
   needed; require owner speaker/headphone evidence, sustained output under load,
   underrun/recovery, stop/reset and usable latency. No physical-host access
   while another agent owns it. If the owner confirms the native-batch alternative
   at closure, explicitly defer this task, record the outstanding checks in
   technical debt and leave native qualification open for the later owner batch.
   If blocked by hardware-specific behavior, report it and return scope decisions
   to the owner.
6. [ ] **Documentation closure.** Record implemented contracts and measured
   limits, move this milestone to the appropriate subsystem reference and update
   links. Confirm the closure choice with the owner: QEMU closure with explicit
   native debt for the later ThinkPad batch, or native qualification before
   closure. Carry only owner-confirmed deferred work into technical debt. Publish
   no success claim for SDL2, Quake, recording or other devices.

The owner's first implementation assignment combined the contract review and
private controller/codec engine steps above. The [engine reference](../devices/hda.md)
records the implemented boundary; the unmerged consumer qualifies it without
shipping a tone, ABI or sessions. Task 2 now implements periodic refill
and session/mixing together with the measured limits above; native and consumer
work remain later assignments.

Tasks are focused PRs, each assigned by the owner after its predecessor is
reviewed. No probe cherry-pick is implied by accepting this proposal. Production
code must be reviewed for lifecycle/interrupt/refill behavior the probes did not
implement. No new tests, boot automation or CI workflow is proposed.

## Later consumer work

**Quake:** replace `snd_null` with the real upstream sound mixer and a native
producer/position adapter. Conversion remains in userspace. Resolve its cyclic
buffer/play-cursor assumptions against the accepted session contract and test
actual sound. The owner assigns this in ports after the kernel interface exists.

**SDL2:** implement a native backend around the accepted grant, format conversion,
writable waits, pause, close and discontinuity. Its audio core normally starts
a thread even for queued audio. Pyxis currently has one task per process and
SDL2 thread creation fails, so genuine callback/thread execution requires
separate owner acceptance and assignment under the
[threads direction](scheduling-and-threads.md). Backend-owned callback execution
would still need real safe scheduling, lifetime and synchronization; it cannot be faked by success or
pumped only when a game polls events. Until that prerequisite is solved, do not
claim DevilutionX or SDL audio support.

## Native and evidence limits

The supplied Fedora dump identifies Realtek ALC257 `0x10ec0257`, subsystem
`0x17aa5081`, on AMD `1022:15e3`, with speaker pin `0x14` and headphone pin
`0x21`, both advertising EAPD and analog DACs supporting 48 kHz S16 stereo.
It records advertised topology and Fedora state, not a qualified Pyxis cold-init
sequence, amplifier/power quirks, interrupt/position reliability or physical
latency. QEMU closure with a later native batch follows the owner's established
practice and is carried forward for owner confirmation at closure; native
checks remain open until measured. Recording/HDMI/USB/ACP/suspend remain separate directions.
See the [full report and evidence](../development/audio-investigation.md) for
exact measured revisions and source references.

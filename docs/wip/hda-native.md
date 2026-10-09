# Native AMD HD Audio / ALC257: task 5 proposal

Status: **proposal only; three defaults below await the owner**. Assigned
2026-10-09; prepared from fresh main `26770a0c` on a separate documentation
branch. Implementation depends on the session/refill work in
[#557](https://git.internal/PyxisOS/pyxis-os/pulls/557) and
[userland #168](https://git.internal/PyxisOS/pyxis-userland/pulls/168).
No implementation or native experiment yet. Coordinate the owner-run ThinkPad
batch with its Bluetooth work.

## Accepted boundary

The owner accepted task 2 delivery with its nested-QEMU eight-session limitation
on **2026-10-09**, deferred batching and required **native eight-session playback
for HDA milestone closure**. A single unsafe-progress/hardware guard trip still
disables audio until reboot. Native evidence decides whether controller-reset
recovery needs a separate proposal; recovery is not part of this assignment.
See the [milestone](hda-playback.md) and
[accepted limitation in #557](https://git.internal/PyxisOS/pyxis-os/src/branch/audio/sessions-mixer/docs/technical-debt.md#hd-audio-sustained-eight-session-playback).

Retain one BSP worker/mixer, eight process-owned per-space sessions, 48 kHz
S16LE stereo, eight 10 ms hardware periods, 80 ms copied session queues and
4096-byte atomic writes/writable waits. Focus does not select audio output.
Public calls/grants stay unchanged. SDL2/Quake, recording, HDMI/USB/ACP,
suspend/resume, batching and timekeeping remain separate. Numeric native guard
or tuning changes require another owner decision.

## Inventory and supported route

The supplied Fedora [ALC257 dump](../development/experiments/audio-investigation/thinkpad-alc257-codec.txt)
has SHA-256 `3c1799d0ec3d75c96a4a3cdf6a032728bb59eae1100b1c2e22c636bff18509d8`;
the shared copy matches. It identifies codec address 0, AFG `0x01`, vendor
`0x10ec0257`, subsystem `0x17aa5081`, revision `0x100001`. The target inventory
associates it with analog AMD **`1022:15e3`, `07:00.6`**. GPU `1002:1637`, ACP
`1022:15e2` and dock audio are distinct devices, never analog fallbacks.

| Output | Pin / default configuration | DAC path | Advertised controls |
| --- | --- | --- | --- |
| Fixed internal speaker | `0x14` / `0x90170110` | Sole connection to `0x02` | Output, mute, EAPD; fixed pin has no presence policy |
| Headphone jack | `0x21` / `0x04211020` | Index 0 → `0x02`; index 1 → `0x03` | Output/headphone, mute, EAPD, presence detection and unsolicited events |

Both DACs advertise stereo 48 kHz/16-bit PCM. Their amp offset/max step is `0x57`
with 0.75 dB steps; pin amps provide mute, not gain steps. Power, EAPD `0x2`,
Fedora's headphone selector index 1, mute and gains are captured state, not a
cold-init recipe. The dump has no Pin Sense result or vendor-coefficient setup.
A shared DAC `0x02` route is graph-supported, but native sound is unqualified.

## Three owner decisions

1. **Default: one native output pair through DAC `0x02`.** Explicitly match the
   AMD controller and ALC257/Lenovo identity and verify the graph again. Select
   headphone connection index 0, sharing the existing physical mixed stream with
   the speaker route. Program/read back D0, converter gain at its advertised
   0 dB offset, selector, pin control, EAPD, format and stream tag. Both pin amps
   start muted; only the chosen output is unmuted. Idle mutes/disables both pins
   and detaches the converter. Alternative: retain distinct DAC `0x02`/`0x03`
   routes, adding a second converter's power/gain/stream state; no native evidence
   currently requires that complexity.
2. **Default: headphone priority with checked stop/switch/restart on jack change.**
   Headphone presence selects headphones; absence selects speaker. At start and
   jack change, two checked presence readings 20 ms apart must agree; otherwise
   keep both muted and fail closed. Live events trigger the stopped routing
   sequence below, with a counted gap/discard of already mixed DMA while session
   ownership and software queues survive. Alternative: sample only at
   playback start, deferring live switching; insertion would not immediately mute
   an already playing speaker.
3. **Default: native qualification includes two ten-minute eight-session runs
   on each output**, plus one-session/channel, jack, pause/reacquire and ordinary
   concurrent-use checks below. Use the same native CPU count/configuration and
   repeated no-audio baseline. A shorter QEMU check cannot close the milestone.
   Alternative: choose a longer owner batch duration (for example 30 minutes per
   run); that extends evidence without changing driver capacity or guard policy.

These defaults are proposals, not accepted decisions. Binding/guard derivation
below is required implementation work after acceptance, not a new public API.

## Controller bring-up beyond adding a PCI ID

Retain the [PCI claim/reset](../devices/pci.md) and
[BSP DMA/allocation](../kernel/smp.md) contracts. Before driver DMA, stop firmware
streams/rings and establish ownership. Validate BAR extent, HDA version, GCAP
stream counts/address width, ring sizes, one usable output descriptor and reset
completion; do not hardcode QEMU's `GCAP=0x4401` or descriptor index. Check every DMA address/alignment and establish coherence/snoop behavior before
using cached PCM.

Inspect the actual PCI revision/subsystem, power state, MSI layout and command
bits. Current MSI support requires a single-message 64-bit capability without
per-vector masking; unsupported native capability is explicit UNAVAILABLE, not
an implicit INTx or polling fallback. Record that blocker and propose a bounded
PCI-helper change if hardware needs it. Keep controller selection explicit;
reject ambiguous candidates instead of choosing the first multimedia function.

Pinned [Linux v6.12 controller reference](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/sound/pci/hda/hda_intel.c)
(`adc218676eef25575469234709c2d87185ca223a`) classifies `15e3` with AMD southbridge
quirks: no Intel TCSEL write, ATI-style snoop control, AMD FIFO-position handling,
and a conservative 40-bit DMA ceiling. Its snoop path updates PCI byte `0x42`
(mask `0x07`, enabled value `0x02`). Its 32-frame FIFO position correction is not
an 8192-byte PCI-prefetch bound. Inspect these behaviors against the actual
controller before adopting a narrowly matched, checked native profile; do not
copy every upstream power-management or probe-retry policy.

The [Realtek reference at the same revision](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/sound/pci/hda/patch_realtek.c)
uses the ALC269 family path for ALC257. No exact `5081` table entry was found in
that snapshot; a Lenovo vendor fallback includes ThinkPad ACPI/click handling.
That does not prove a required speaker coefficient/GPIO fixup. Begin with checked
standard verbs and the observed graph. If silence, pops or jack behavior needs
a vendor fixup, identify its exact match and sequence before proposing it.
Those files carry GPL-2.0-or-later notices; no source is copied here. A needed
fixup must retain licence/provenance/local-change and image notices under the
[repository licensing policy](../../LICENSING.md), with owner mirror setup before
imported build inputs. Do not guess coefficient/GPIO values or add mic/LED features.

### Native progress and guard derivation

`QEMU_CODEC_BURST_BYTES=8192` and the 1 ms HPET/WALCLK commit bounds model QEMU's
timer-driven codec. The measured 112 s WALCLK guard trip is consistent with a
transient nested-host delay around a 5–6 µs mix; that cause is inferred, not
traced, and establishes no native throughput limit. Keep those constants restricted to the QEMU
profile. **Re-derive or replace them for AMD rather than inherit them.**

Read the native stream FIFO properties after format programming; establish what
LPIB, any position buffer and WALCLK actually measure, their visibility/skew,
DMA prefetch/headroom and wrap/IRQ-coalescing behavior. FIFO size alone is not a
proof of the maximum outstanding DMA fetch. Derive a native reclaimed-period
margin from the controller's documented bounds plus progress during the checked
commit window. Record the rationale and before/after timing observations, not
just a successful tone or a histogram maximum. Bring any proposed numeric guard
or tuning change to the owner before applying it. If a safe native profile
cannot be established, report the blocker instead of enabling unqualified RUN.
Native evidence may motivate reset recovery later; one guard trip still means
unavailable until reboot in this task.

## Jack commands, interrupts and parked lifetime

The current QEMU transport disables unsolicited delivery, handles only stream
IRQs, stops command rings before RUN and discards unsolicited responses during
commands. A native event path therefore needs a single BSP-owned RIRB drain
separating checked solicited responses from codec/tag notifications. Enable
controller unsolicited/RIRB interrupt delivery while playing; the ISR captures
bounded status and wakes the worker, never performs routing, allocation or verbs.
Coalesce jack hints, correlate responses, and reject overflow/ambiguous ownership;
an event must never become the next command's response. Standard behavior comes
from [Intel HDA 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf),
§§3.3.7, 3.3.14, 4.4.2 and 7.3.3.14–15.

Arm/read back the codec tag and controller reception before start-time sensing.
Drain pending jack hints and resample before unmute/RUN; a start cannot knowingly
publish a stale route. Measure event-to-mute latency; this is not instantaneous
hardware automute. Unknown tags are never command replies.

Parked playback stops response/command DMA and MSI, with both pins muted;
no idle jack polling is added. The next start rearms reception and rereads presence. During live routing changes, the engine stops before verb waits because
the existing 100 ms command deadline can exceed the 20 ms playback service
horizon. Mute both pins, require the settling pair to agree, program/read back
one route and restart pending PCM. Discard mixed DMA with a discontinuity for
active sessions; retain generation/ownership and unmixed software queues. Keep
before/after DMA checks and cleanup/WRITE generation checks intact.
A checked normal routing restart is distinct from recovery after a guard fault.
Jack changes do not transfer session authority or follow input focus.

## Owner-run native batch after implementation acceptance

1. **Inventory and baseline before code changes.** When the ThinkPad is free,
   collect `uname -r`, `lspci -vvnn -s 07:00.6`, firmware and jack/boot state;
   the existing codec dump is sufficient inventory. Freeze an image revision,
   CPU count, display scene, devices and clock source; capture two matched
   no-audio presenter/idle observations on the unbound-controller baseline, then
   repeat parked-engine observations after native bring-up. Use existing logs,
   presenter/worker counters and tools; unavailable native CPU accounting is
   reported unavailable, never replaced with QEMU's guest/host split. If separate
   timekeeping work lands, refresh the baseline before attributing audio cost.
2. **Cold initialization and one session.** PXE boot with headphones absent,
   then present on a separate cold boot. Record controller/codec/route diagnostics
   through ktrace and the existing remote log receiver; normal success remains
   one `audio: ready ...` line. Run `pcm 1000 500 10`, `pcm 1000 0 10` and
   `pcm 0 500 10`. Check audible output/channel separation on headphones, speaker
   behavior, hidden-space continuation and silence/reacquisition after release.
   Exercise insertion/removal during playback and while parked; report switching
   gaps, pop/noise and speaker leakage. The accepted unprimed start is not a
   zero-gap promise. Run
   `pcm --pause-ms 250 --repeat 2 --gap-ms 150 1000 500 2` for starvation and
   normal release/reacquisition. Failure must preserve shell/cleanup service.
3. **Eight sessions and load.** Prepare a validation-only nine-shell live/PXE
   layout through the existing boot-init configuration: Development, Remote and
   Audio 1–7, with no autoplay. Production has only three shell spaces; a single
   space cannot own eight sessions. In eight distinct spaces run
   `pcm 1000 500 1200`; timestamp acquisition of the eighth and keep all eight
   active for at least ten minutes before stopping one. The ninth must return
   CALL_LIMIT. Repeat twice on speaker and twice on headphones, with the same
   CPU count and ordinary space switching/shell work. A saturated mix can clip
   by design; record unintended dropouts, stuck/repeated chunks, global silence
   and guard trips separately. Stop producers individually; others continue.
   Verify final silence and fresh one-session acquisition. Observe native timing,
   refill/IRQ/clock counts and STATUS starvation/discontinuity results with
   existing instrumentation; do not halt the running guest for inspection.
4. **Closure evidence.** Record exact image/dependency revisions, configuration,
   sample durations/ranges, route and complete failure logs. Keep raw captures
   locally or in PR evidence; docs retain concise summaries. Native eight-session
   output must pass without fail-closed before milestone closure. Any guard trip
   stops qualification, preserves DMA and requires reboot; report it for a
   separate recovery/batching/guard decision rather than weakening the policy.

Proposal validation is source/dump/document review only. No QEMU boot, native
execution, driver code, self-test, fault injection, new CI or benchmark framework
was performed or added. Stop after this proposal for owner review.

# Native AMD HD Audio / ALC257: task 5 proposal

Status: **three owner decisions accepted 2026-10-09**. Prepared from fresh main
`26770a0c` on a separate documentation branch. Implementation is authorized after
this accepted-decision update is pushed and the session/refill dependencies
[#557](https://git.internal/PyxisOS/pyxis-os/pulls/557) and
[userland #168](https://git.internal/PyxisOS/pyxis-userland/pulls/168) are merged.
The owner reports both merged. The forwarded root PCI inventory below settles
MSI compatibility; no native implementation or qualification is claimed yet.
Coordinate the owner-run ThinkPad batch with its Bluetooth work.

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

## Accepted owner decisions (2026-10-09)

1. **One native output pair through DAC `0x02`.** Explicitly match the AMD
   controller and ALC257/Lenovo identity and verify the graph again. Select
   headphone connection index 0, sharing the existing physical mixed stream.
   Program/read back D0, converter gain at its advertised 0 dB offset, selector,
   pin control, EAPD, format and stream tag. Both pin amps start muted; only the
   chosen output is unmuted. Idle mutes/disables both and detaches the converter.
2. **Presence sampled at playback start.** The owner accepted the staged
   alternative: checked headphone presence selects headphones; absence selects
   speaker before the physical engine starts. Keep that route until the engine
   next stops and starts. Another session's ACQUIRE or WRITE during continuing
   mixed playback is not a new hardware start. Inserting headphones mid-playback
   therefore keeps the speaker until the next start; unplugging headphones keeps
   the headphone route until then. Live jack switching and its unsolicited-response
   transport are a separate follow-up, not task 5. See the
   [accepted routing debt](../technical-debt.md#hd-audio-jack-routing-at-playback-start).
3. **Proportionate native batch.** One attended ten-minute eight-session run on
   speaker and one on headphones, plus a second ten-minute speaker run checked
   only at completion through each producer's STATUS output and the complete log.
   Include one-session/channel, start-time routing, pause/reacquire and ordinary
   concurrent-use checks. More runs only if counters indicate a problem. Freeze
   CPU count/configuration and repeat the no-audio baseline. Native eight-session
   output remains required for milestone closure.

These are accepted plan choices, not evidence of implementation or native sound.
Binding and guard derivation below introduce no public API. One-trip fail-closed
until reboot remains accepted; native evidence decides on any later reset recovery.

### Task-specific choices accepted 2026-10-09

1. **Native commit limit: option A, 20 ms.** After programming the stopped
   stream format, take `F = round_up(FIFOS + 1, 4)` bytes. Before committing a
   reclaimed period, require headroom greater than `F + 4 + 3840` bytes:
   48 kHz S16LE stereo consumes 192,000 bytes/s, or 3840 bytes in 20 ms; the
   additional four bytes are one frame. Check that this reserve fits the ring
   before RUN. Bound the whole pre-observation, mix/copy/publication and
   post-observation interval, including the final clock read, to less than
   20,000,000 ns and 480,000 ticks of the 24 MHz WALCLK. Final headroom must
   exceed `F + 4`. QEMU retains its existing 8192-byte/1 ms profile. Native
   FIFOS, progress behavior and commit timings still need batch evidence.
2. **D3+BME: option B, retain the PCI refusal.** Add no handoff helper.
   `pci_begin_mmio_probe` still requires BME clear before a wake or memory-decode
   change and rejects reset-causing D3hot wake. Trace the actual boot COMMAND
   (Mem/BME), PM state, PME and NoSoftRst in ktrace before attempting bring-up;
   report a specific unavailable reason if that state cannot be used. The
   supplied Linux runtime-suspended state does not establish Pyxis boot state.
   Only native evidence of D3+BME at boot can reopen a separate helper decision.
3. **Native coherence: option A accepted 2026-10-09.** Replace the undocumented
   `0x42` admission gate with standard PCIe Device Control No Snoop Enable.
   With BME off, clear only bit 11 using a 16-bit write, preserve other controls
   and verify the resulting word before allowing DMA. A missing/malformed
   capability, invalid state or refused clear remains fail-closed. Keep `0x42`
   as read-only ktrace evidence; never write it. Cached mappings, barriers,
   address limits and ownership stay unchanged. Native playback still needs
   the owner's sitting.

## Controller bring-up beyond adding a PCI ID

Retain the [PCI claim/reset](../devices/pci.md) and
[BSP DMA/allocation](../kernel/smp.md) contracts. Before driver DMA, stop firmware
streams/rings and establish ownership. Validate BAR extent, HDA version, GCAP
stream counts/address width, ring sizes, one usable output descriptor and reset
completion; do not hardcode QEMU's `GCAP=0x4401` or descriptor index. Check every DMA address/alignment and establish coherence/snoop behavior before
using cached PCM.

### Owner-supplied root PCI inventory

Read-only `lspci -vvnn -s 07:00.6`, ThinkPad Fedora **2026-10-09**, with
`snd_hda_intel` bound; forwarded by Claude from the owner's root capture. Record
these facts here, not as edits to the supplied dump:

| Property | Captured value / consequence |
| --- | --- |
| Subsystem / BAR0 | `17aa:5081`; `fd3c0000`, 32-bit non-prefetchable, 32 KiB |
| Command / interrupt | Mem+, BusMaster+, DisINTx+; interrupt pin C |
| MSI at `0xa0` | Enable+, Count=1/1, Maskable-, 64bit+; current single-message 64-bit helper fits, no helper change needed |
| PM v3 at `0x50` | D3, NoSoftRst+, PME-Enable+; Linux runtime-suspended it. Check and establish D0 in Pyxis; do not assume firmware state |
| PCIe endpoint | DevCtl RlxdOrd+, NoSnoop+, MaxPayload 128, MaxReadReq 512; inspect actual snoop/coherence behavior before DMA |
| Vendor capabilities | Conventional `0x48` and extended `0x100`, unknown contents; no speculative writes |

Captured Mem+/BME+/MSI+ are Linux ownership state, not the Pyxis start sequence.
Revalidate capabilities, reset and power transitions at native boot. NoSnoop+
is permission/state, not proof that our cached DMA mappings are coherent. Keep
controller selection explicit; reject ambiguous candidates and leave INTx off.

Pinned [Linux v6.12 controller reference](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/sound/pci/hda/hda_intel.c)
(`adc218676eef25575469234709c2d87185ca223a`) classifies `15e3` with AMD southbridge
quirks: no Intel TCSEL write, ATI-style snoop control, AMD FIFO-position handling,
and a conservative 40-bit DMA ceiling. Its snoop path updates PCI byte `0x42`
(mask `0x07`, enabled value `0x02`), without checking readback. The accepted
Pyxis contract uses PCIe NoSnoop clearing instead and never writes this legacy
byte. Its 32-frame FIFO position correction is not an 8192-byte PCI-prefetch
bound. Do not copy every upstream power-management or probe-retry policy.

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
just a successful tone or a histogram maximum. The owner accepted the FIFO-derived
20 ms profile above; further numeric guard or tuning changes require another
decision before applying them. If a safe native profile
cannot be established, report the blocker instead of enabling unqualified RUN.
Native evidence may motivate reset recovery later; one guard trip still means
unavailable until reboot in this task.

## Start-time routing and parked lifetime

Sample headphone Pin Sense through the BSP worker's existing command path before
RUN, with both pin amps muted. Validate the expected native pin capability and
program/read back one output route; an unavailable/error result cannot guess a
speaker route. No jack verbs run during active playback. Commands may use the
existing bounded waits while RUN is stopped; the 20 ms running service horizon
is unchanged. Normal stop mutes/disables both pins and detaches DAC `0x02`;
command/response DMA and MSI park as today. The next start samples presence again.
No idle poll, live switch, route-change DMA discard or new discontinuity policy
is added. Sessions/generations, software queues, focus independence and all DMA
ownership checks remain unchanged.

### Separate follow-up: live jack switching

Deferred by the owner, not authorized here. It will need its own proposal for
persistent RIRB drain/IRQ delivery, codec/tag correlation, presence settling,
refill-safe command scheduling and switch/discontinuity policy. Today unsolicited
responses are disabled/discarded and command rings stop before RUN. Do not add
that path merely to sample presence at start. The
[HDA specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf)
remains the command/register reference; deferred event work does not change task 5.

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
   Insert/remove headphones during playback to confirm the route stays fixed,
   then stop all output and restart to verify the new presence is selected. Also
   check parked changes. Report pop/noise and whether the start selected its
   intended output; live speaker automute is explicitly deferred. The accepted unprimed start is not a
   zero-gap promise. Run
   `pcm --pause-ms 250 --repeat 2 --gap-ms 150 1000 500 2` for starvation and
   normal release/reacquisition. Failure must preserve shell/cleanup service.
3. **Eight sessions and load.** Prepare a validation-only nine-shell live/PXE
   layout through the existing boot-init configuration: Development, Remote and
   Audio 1–7, with no autoplay. Production has only three shell spaces; a single
   space cannot own eight sessions. In eight distinct spaces run
   `pcm 1000 500 660`, starting all eight within one minute so at least ten
   minutes overlap. Timestamp the eighth acquisition and each completion; use a
   longer command duration only if setup takes longer. The ninth must return
   CALL_LIMIT. Run once attended on speaker and once attended on headphones, with
   the same CPU count and ordinary space switching/shell work. A saturated mix can clip
   by design; record unintended dropouts, stuck/repeated chunks, global silence
   and guard trips separately. Stop producers individually; others continue.
   Verify final silence and fresh one-session acquisition. For the second speaker
   run, let all producers finish naturally: Ctrl-C skips their final STATUS output.
   Check all eight before-release STATUS summaries and the complete log only at
   the end; missing successful completion/STATUS is not a pass. Extra runs follow
   only counter findings. Observe native timing,
   refill/IRQ/clock counts and STATUS starvation/discontinuity results with
   existing instrumentation; do not halt the running guest for inspection.
4. **Closure evidence.** Record exact image/dependency revisions, configuration,
   sample durations/ranges, route and complete failure logs. Keep raw captures
   locally or in PR evidence; docs retain concise summaries. Native eight-session
   output must pass without fail-closed before milestone closure. Any guard trip
   stops qualification, preserves DMA and requires reboot; report it for a
   separate recovery/batching/guard decision rather than weakening the policy.

Plan validation so far is source/dump/document review only. No native execution
or driver code is claimed by this update. Implementation is authorized after
publishing this update and verifying the dependency merges; preserve the accepted
native-batch/guard boundaries and stop the implementation PR for owner review.

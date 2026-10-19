# Power-overlay sound qualification

2026-10-19. Baseline main `3c0e0b54`; initial runtime `cd3334a1`, restart-fade
fix `3d4763e7`. The candidate uses
the baseline's SDK, userspace (`1836d2ae`) and ports (`cbc017ee`) bundles;
fs `d2fb97c5` and lwIP `a1aadb91` are unchanged. Kernel configuration is
byte-identical. Ordinary kernel/image builds use the published LLVM 23.1.3
builder `3d69e83`, with no game-data overrides. The owner-made WAV and jsfxr
preset are retained under MPL-2.0; [conversion and provenance](../../../assets/ui/power/README.md)
describe the 28,334-frame, 113,336-byte static PCM payload. There is no new
heap allocation or audio session slot.

## Matched controls

QEMU 10.2.2, nested KVM, Q35, `-cpu host -smp 4 -m 2G`, fresh Fedora OVMF
variables, standard VGA, VirtIO network/RNG, `intel-hda` + `hda-output`, WAV
capture at 48 kHz S16 stereo. Both images use the stock live configuration.
The exact-main CI image is the baseline; the candidate rebuilds the kernel
and image with `make -j8 kernel image PREBUILT='sdk userspace ports'`.

Each boot runs one `pcm 440 220 2` warmup, then three matching invocations
through Remote. The tools report producer time; their existing 200 ms tail
wait is not an audible-drain guarantee. Read-only debugger snapshots follow
completed playback, never the timed controls.

| Observation | Baseline | Candidate |
| --- | --- | --- |
| Producer time, three samples (s) | 1.898909 / 1.929704 / 1.924726 | 1.899580 / 1.899641 / 1.890375 |
| Writes per invocation | 94 | 94 |
| Discontinuities / engine recoveries | 0 / 0 | 0 / 0 |
| Maximum observed DMA commit (µs) | 558.64 | 566.34 |
| After controls | engine parked, slots empty | engine parked, slots empty |

These short sequential nested-VM sets establish functioning controls, not an
isolated performance change or native timing. Initialization and opening the
overlay before a button action produce no audio in either boot.

## UI and lifecycle checks

Monitor-injected navigation and keyboard/pointer Cancel activation produce
the initial compiled clip. WAV samples match the converted source with the existing
master gain: peak 640 at boot master 50%, and 20,992 at unity. Master mute and
0% produce exact zeros. Six rapid unity-gain restarts match successive source
prefixes byte-for-byte, followed by one complete tail; there is no voice
stacking. Holding navigation does not repeat the sound. Opening, hover and
Escape stay silent. A concurrent `pcm 440 220 2` completes with zero
discontinuities; the UI consumes none of the eight session slots.

The broader UI exercise records one existing QEMU DMA clock-limit recovery.
The clip is discarded on that path, and later presses and PCM playback work;
this does not qualify sustained playback against the existing 1 ms QEMU guard.
The [volume qualification](audio-volume/README.md#cost-and-limits) records the
same guard limitation. A separate UI-only boot captures six complete,
byte-exact 50% clips including pointer Cancel, six starts/stops, zero recoveries
and a 597.56 µs maximum commit. A boot without HDA preserves overlay navigation
and Cancel, with no pending source or session afterward.

QEMU's `-no-reboot` observes guest reset about 22 ms after activating Reboot,
well before the 590 ms clip can finish. This is a functional non-wait check,
not a native shutdown-latency measurement. The shared 80 ms DMA ring, codec
power-up and headphone settling are unchanged; committed samples cannot be
retracted by a restart.

Raw WAVs, screenshots, commands and debugger records are outside the repository
under `/shared/present/power-overlay-sound/`.

## Native result and restart correction

The owner checked the first candidate on ThinkPad speakers, starting at 50%:
navigation and Cancel were heard, mute was silent, with no perceived delay or
stacking. Rapid Left/Right presses produced pops. Headphones were not tested.
The original restart could jump directly from a nonzero waveform sample to
the clip start; the speaker result does not isolate that from analog effects.

The correction keeps one voice: continue the outgoing waveform through a 240-frame
(5 ms) linear fade, then start the new clip at zero. Repeated requests during
that fade coalesce without extending it. Build-time 5 ms fades force the asset
endpoints to zero without changing the supplied WAV/preset. Converter admission
is bounded to 64 KiB and one second; a one-second source succeeds, while
44,101 frames, a two-second source and oversized trailing data are rejected
with explicit errors before header generation.

The replacement QEMU capture contains twelve unity-gain restart transitions.
Each matches the outgoing waveform followed by its 240-frame linear fade with
zero differing samples; every fade-to-new-start join is exactly zero to zero.
The first fade sample retains the waveform's natural adjacent-sample change,
rather than adding the old hard-reset jump (1,792–12,288 sample units at those
positions). Source endpoints are exactly zero. Stable muted and 0% intervals
are all zero; a following PCM control completes with zero discontinuities, and
the parked snapshot has no active fade, restart or session. This run also
encounters one existing nested-QEMU clock-limit recovery (maximum observed
commit 1.701 ms); it does not qualify the unchanged hardware timing guard.

The owner re-listened to `644e92ee` on ThinkPad speakers on 2026-10-19:
rapid presses were clean and the clicks sounded good. This qualifies the
speaker restart correction on that machine; headphones remain untested.

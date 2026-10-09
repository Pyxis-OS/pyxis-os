# Volume control: QEMU qualification

Baseline `da867b24`, volume code `819892ff` (later docs head `cee00717`), built
with LLVM 23.1.3/49e2c1a using ordinary `make -j16 image`. Baseline userland
`f6f4f69d`, volume userland `49488395`; ports `0f0aa443`, fs `b427df29`, lwIP
`a1aadb91` unchanged. Final integration uses main `31af2224` (including clipboard and program capacity),
userland `b8d542e`, and the canceled-drag fix `2c130e53`; smoke checks are separate
from these matched measurements.
Raw WAVs, JSON, debugger logs and command history stay local under `build/volume/`.

QEMU 10.2.2 Q35, nested KVM, `-cpu host`, four CPUs (one socket/four cores),
8 GiB, fresh raw OVMF pair, standard VGA 1280×800, VirtIO network/RNG,
`intel-hda` + `hda-output`, WAV 48 kHz S16LE stereo. Default three-shell layout,
Caelum selected for idle. Info logging, XHCI enabled, npfs flush30s, HPET clock.
Capture runs use the same AHCI CD device as their baseline. CPU/presenter runs
instead use matched VirtIO-SCSI CD images, `-rtc base=utc`; do not mix those sets.

## Captures

Existing `pcm --tail-ms 300 1000 500 2` supplies 96,000 triangle-wave frames
with amplitudes8192/4096. Select levels with the real bar keyboard controls;
0/1/10/25/50/100%, then combined master50/space50, mute and unmute.
Every command completed with READY, zero discontinuities, ordinary writable
waits and starvation2, including zero/muted output. Release/reacquisition kept
the space's50% setting. Playback continued while Caelum was shown.

| Master % | Space % | Master Q16 | Captured peak L/R | Steady frames checked | Mismatches |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 100 | 0 | 0/0 | Entire zero interval | 0 |
| 1 | 100 | 66 | 8/4 | 94,976 | 0 |
| 10 | 100 | 123 | 15/7 | 94,976 | 0 |
| 25 | 100 | 350 | 43/21 | 94,976 | 0 |
| 50 | 100 | 2001 | 250/125 | 94,976 | 0 |
| 100 | 100 | 65536 | 8192/4096 | 96,000 including startup burst | 0 |
| 50 | 50 | 2001 | 7/3 | 94,976 | 0 |

Expected samples derive independently from the agreed dB formula, rounded Q16
coefficients and signed truncation toward zero, applying space then master.
All101 table entries match the formula. The existing 1,024-frame startup burst
and silence are kept in raw WAV; the table's steady spans begin at source frame
1024. Unity also checks that burst and matches all96,000 baseline PCM frames.
The0→1%,0→10%,10→25% and combined unmute captures each match the initial1,024
frames including the exact240-frame ramp. Muting during playback leaves a
published tail, then exact zeros. The fixed-muted command that included a brief
read-only debugger inspection is excluded from unprofiled guard/cost evidence.
No debugger stops active playback in the retained cost runs.

## Cost and limits

Measurements use existing /proc thread accounting (CLK_TCK100), and manual
read-only GDB `hbreak space_present`, 10ns HPET counter at
`0xfffffe80402020f0`, `finish`, counter delta. Each presenter sample includes
host/debugger/preemption/device variation. Separate guest_time, BSP thread total
and whole-QEMU total; do not subtract guest_time from utime to infer idle host
userspace cost (accounting skew produced small negative deltas).

Three retained windows per set: idle15s without a client; active5s immediately
following command submission inside `pcm 1000 500 8`. One preliminary tone then
three retained tones; all four complete naturally before stopped-state inspection.
Percentages below mean one host CPU. Guest values are guest_time, total BSP
includes QEMU/KVM work in that thread, and QEMU totals include other threads.

| Set/workload | BSP guest % | BSP total % | Whole QEMU % |
| --- | ---: | ---: | ---: |
| Initial baseline idle | 5.87–6.67 | 7.27–8.67 | 12.13–14.80 |
| Volume idle | 4.87–5.00 | 6.93–7.13 | 11.93–12.07 |
| Repeat baseline idle | 5.33–7.00 | 7.80–10.60 | 12.87–17.40 |
| Initial baseline one source | 15.20–17.20 | 31.20–35.80 | 43.00–45.80 |
| Volume one source | 14.60–14.80 | 30.00–30.80 | 39.40–40.80 |
| Repeat baseline one source | 14.40–14.80 | 30.00–31.80 | 39.40–42.00 |

| Set | Presenter8 median/range ms | Maximum commit µs | Refills/IRQs |
| --- | --- | ---: | ---: |
| Initial baseline | 1.521585 /1.234280–1.901900 | 743.49 | 3256/3249 |
| Volume | 1.367940 /1.267990–1.722800 | 749.80 | 3256/3252 |
| Repeat baseline | 1.598875 /1.315800–1.870510 | 746.35 | 3256/3252 |

Every retained active set had four starts/stops and healthy parked state. Commit
maxima cover the existing observation/mix/copy/barrier/check interval, not isolated
mixer CPU time. No added counters or profiling instrumentation. Counts and
ranges overlap; no isolated speedup, regression or native equivalence claim:
sequential phases, three short windows, eight debugger samples and shared nested
host. An unrelated capacity QEMU began during volume idle/presenter, persisted
through volume active, restarted during repeat-baseline idle, then exited before
repeat-baseline active. Owned peers/builds were stopped during each cost phase.

Two long baseline commands tripped the existing1ms commit/WALCLK guard, so
retained active windows are five seconds inside healthy `pcm 1000 500 8` runs.
This is short-run nested-QEMU evidence, not sustained eight-session qualification
or native cost. The accepted guard/ring/capacity remain unchanged. Native safe
speaker/headphone listening is recorded separately in the
[native qualification](../../../userland/audio-volume.md#native-qualification).

## Integration and UI

Ordinary source integration builds passed, including the default layout and a
local nine-shell variant (Development, Remote, Audio1–7, no autoplay). HMP input
exercised hover without keyboard focus, full-bar/popup connection, click mute,
track/drag, wheel5%, keyboard steps/endpoints/mute/Escape, inactive-space controls
without a focus switch, and repeated acquire/release. Read-only stopped-state
inspection confirmed retained levels and distinct space settings. With Mousetest,
explicit popup focus retained the same keyboard/pointer owners and cleared held
keys; Escape restored content. Under relative lock G stayed out of the popup,
M changed master mute, and Super+Escape unlocked. Final canceled-drag check kept
its original target and consumed movement/wheel across a space switch until Left
released, then accepted a fresh action on the new control.

A real `screenshot tmp://volume.png` plus confirmed `xfer send` downloaded a
1280×800 PNG including the popup and cursor. RGB comparison with the monitor's
same static screen found **zero differing pixels**. Battery layout and narrow
whole-widget omission were code-inspected; native battery/media keys and resizing
input-loss races are not new hardware qualification.

Eight-source attempts in the nine-shell image retain the existing QEMU limit.
An extended manually launched mix failed at the WALCLK guard after141.707s of
captured output, max monotonic commit907.65µs, with peak590 below the settled
master50% bound1000. A faster launch tripped WALCLK during its first producer
(max615.97µs/83refills); neither is a sustained eight-session pass. A separate
one-frame/60s-tail attempt could not establish eight held sessions before a
1.19643ms commit fault and earlier tails expiring; the proposed ninth returned
UNAVAILABLE, **not CALL_LIMIT**. No eight-source cost window or new ninth-session
capacity pass is claimed. Saturation-before-master and the full S16 settled bound
are source-inspected; those captures did not reach saturation. All slots cleaned
up, shutdown retained backing and controls became disabled. Do not infer that
volume fixes the QEMU guard; the later native eight-session regression remains
[open](../../../technical-debt.md#hd-audio-volume-native-regression).

Final main integration (`2a9e1a80`, userland `b8d542e`) passed ordinary source
kernel/SDK/runtime/ports/userspace/image build with the restored default layout.
Its unprofiled `pcm --tail-ms 300 1000 500 2` completed at master50/space100,
READY, starvation2/discontinuity0. No UI input or debugger interrupted playback.
Task-owned guests/clients/debuggers were stopped afterward.

# Presenter frame-skipping qualification

2026-10-10, [accepted task](../../../wip/presenter-frame-skipping.md).

## Inputs

Control A: main `4b6550a6`, #670 merged. Captured before code edits: full source
image with `LOG_LEVEL=info DISPLAY_CURSOR_PROBE=1 DISPLAY_TIMING=off`, existing
LLVM23.1.3 builder. Candidate B: `0c22b18e`, same options, rebuilt kernel and
verified unchanged SDK/userland/ports. Pins: userland `1b6d26af`, ports `cf348aa6`,
fs `b427df29`, lwIP `a1aadb91`. No compiler rebuild or dependency change.
Sealed sets: `build/frame-skip-A/` and `build/frame-skip-B/`, REVISION and
SHA256SUMS. Both initrds SHA-256
`e4c6dba0858c466c6c2cb0b6bf7db0f1422b0f612f092fc515b13a2d522298e7`.

Manual QEMU 10.2.2, Q35/nested KVM, max CPU, four CPUs, 2 GiB, fresh OVMF vars,
standard VGA/1280x800 GOP, VirtIO-SCSI CD/RNG, relative PS/2, no NIC/disk/USB/audio.
Development prompt; no clicks, wheel or space changes in cost windows. Four
12-second idle/motion/idle/motion windows per boot; motion injects relative x/y
at four reports/s, reversing within the body. Compare complete interior probe
samples, excluding boundaries. Local logs and captures remain in `/tmp`, not Git.
Compose/copy spans include preemption and probe overhead; these are elapsed
costs, not CPU profiling or native estimates. B reports every 120 service starts:
frames/skips include completed prior ticks, so use interval differences. Renoir
validation totals include idle checks while confirmations stop; compare totals
per elapsed window or service count, not cumulative per-confirmed-frame ratios.
Its next motion metrics expose the retained idle validation costs.

## QEMU A–B–A–B result

Final-code interleaving was **A2, B2, A3, B3**; A1 captured the pre-edit
baseline, and earlier B runs checked transitions before the final snapshot-fence
fix. Only the final-code interleaving is tabulated. A kernel SHA-256 is
`d8acb7adf92c9b649bd54c773ef8a1a00546b9728d80de683a214ae734cd174f`;
B is `a478a32b8a967ef7bb787ec20ea5208955095935bd5758f8416334b0a3c5525f`.
Each row combines two complete interior windows, 8.000–8.076 seconds each.
Endpoints are frame counts for A and service counts for B. Every reported frame
has one composition and one CPU scanout copy.

| Run/mode | Sample endpoints, two windows | Frames/compose/copy per window | Motion reports | Compose mean/frame | Copy mean/frame |
| --- | --- | ---: | ---: | ---: | ---: |
| A2 idle | 840–1320; 2160–2640 | 480 | 0 | 0.399–0.404 ms | 0.328–0.335 ms |
| B2 idle | 1200–1680; 2520–3000 | 0 | 0 | unavailable | unavailable |
| A3 idle | 1440–1920; 2760–3240 | 480 | 0 | 0.384–0.570 ms | 0.351–0.675 ms |
| B3 idle | 1320–1800; 2640–3120 | 0 | 0 | unavailable | unavailable |
| A2 motion | 1440–1920; 2880–3360 | 480 | 31–32 | 0.391–0.408 ms | 0.340–0.344 ms |
| B2 motion | 1800–2280; 3240–3720 | 32 | 32 | 0.405–0.431 ms | 0.398–0.413 ms |
| A3 motion | 2040–2520; 3480–3960 | 480 | 32 | 0.414–0.438 ms | 0.363–0.420 ms |
| B3 motion | 1920–2400; 3360–3840 | 32 | 32–33 | 0.379–0.550 ms | 0.325–0.505 ms |

Idle B has **480 service ticks and 480 skips**, with zero compose/copy total
increase in each window. Motion B has 480 services, 448 skips and 32 frames:
about four frames/s rather than the control's about 60/s. One B3 motion report
coalesced before rendering. All runs route 96 moves over their complete two
motion windows. Compose-plus-copy totals during the interior motion windows
fall from 351–412 ms for A to 23–34 ms for B. Shared-host variation is visible
in A3's second idle window and B3's first motion window; sparse B frames are not
consistently cheaper per frame. The demonstrated gain is avoided pixel work,
not a faster composition/copy or total BSP CPU measurement. Device service,
output-lock sampling and remaining generation checks continue while idle.

Local `/tmp/frame-skip-{A2,B2,A3,B3}.log` and `*-windows.txt` retain the
cumulative samples and host boundaries. No raw logs/captures are committed.
QEMU has no DCN, so full idle Renoir validation and native tearing/latency remain
unqualified until the owner runs the paired boots.

## Other local checks

Full source baseline build, changed probe image, ordinary default image, Bochs
800×600 image and native-option image pass with the existing builder. Later
images reuse verified unchanged SDK/userland/ports. Source and independent
integration review cover generation completeness, AP/BSP writes during frames,
slot consumption, capture leases, panic and idle device service. No new ABI,
GPU write fields, tests, fault injection or committed benchmark infrastructure.

Firmware-path checks include key echo after idle, text selection spanning rows
(generation matches completed-frame acknowledgment), tabs, screenshot PNG
creation, and mousetest custom shape/graphics/layer/exit. Ctrl+Alt+Delete renders
the independent power overlay, arrows change selection, Escape restores content.
Bochs 800×600 repeats echo/capture/idle skip. VirtIO repeats echo/motion/capture
and, with emulated HDA added only for functional checks, volume popup level change
and close. Read-only GDB inspection confirms control and cursor queues drained
while idle; QMP scanout omits VirtIO's separate host cursor, so its defined/visible
state is checked independently. The normal image boots with probe disabled/zero
and no probe lines; Mandelbrot submits changed views and retains the three slots.
A static-view skip does not consume a pending slot.

A separate local Bochs image fixture changes only staged live.lua to
`multiplexer = true` in Development, preserving source pins. Two pane echoes and
Ctrl+B/% splitting render correctly, with idle counters staying at 42 frames
while service/skip counts advance. This fixture is outside the cost comparison;
no dependency source edits or pin changes are published. Resize is inspected,
not runtime-qualified here: headless QEMU supplied no host window resize. No
forced wedged writer, timeout or panic run is added.

Native-option QEMU checks the absent-Renoir refusal path; it cannot check DCN
idle validation. Local raw captures/GDB output remain in `/tmp`. Default logging
adds no new lines. The complete [native-option steps](#native-qualification--luna-stages-owner-checks)
below cover the remaining host qualification.

## Native qualification — Luna stages, owner checks

Ask the orchestrator to have Luna stage **A then B then A then B**, preserving
rollback. A is `4b6550a6`; B is the reviewed code head. Build both with:

```sh
make -j16 image LOG_LEVEL=info LOG_UDP=1 DISPLAY_FLIP=1 \
  DISPLAY_FLIP_METRICS=1 DISPLAY_TIMING=off DISPLAY_CURSOR_PROBE=1
```

Use the existing flip-on metrics PXE entry, on AC, same 1920x1080 GOP,
`display.flip=1 display.flip.metrics=1 display.cursor.probe=1 display.timing=off
 log.udp=1`. Record exact kernel/initrd hashes. Keep one verified identical
initrd across all boots, retaining the owner's existing Quake/Chocolate Quake
data and consumer configuration; Luna stages, Alpha does not. Owner warms ten seconds
at the Development prompt, then 30 seconds each idle/motion/idle/motion using
PS/2 touchpad/TrackPoint within the body, without clicks or space changes.
Note UTC boundaries; use complete interior 120-tick samples and cumulative
counter differences. Retain masked startup and metrics logs.

Require backend=flip, no timeout/FAILED, identical visible content and unchanged
tearing. Compare frames/compositions/copies/flips per second, service/skip counts,
reports/screen moves, compose/copy totals per window and remaining validation
costs. Idle B should keep about 60 service ticks/s with no unchanged frames;
continuous visible motion still requires full frames. Compare responsiveness
with native Quake and Chocolate Quake, then return to an idle prompt. No claim of
native improvement until the owner reports this paired evidence.

Check key echo after idle, pointer motion/shape/hide and lock/unlock, selection,
tabs and layer changes, local output and mux in an explicitly configured
`multiplexer = true` space, volume and power-overlay open/cancel,
and `screenshot tmp://idle.png` from an unchanged screen. Capture must complete
and include its retained pointer. No forced timeout/panic/wedged-writer exercise
is added. Native results are pending.

# Presenter frame-skipping qualification

2026-10-10, [implemented contract](../../../kernel/presenter-frame-skipping.md).

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
about four frames/s rather than the control's about 60/s. One interior B3
window has 33 input reports against 32 completed frames: input counters include
current service while completed-frame counters end at the prior tick. All runs
route and render 96 moves over their complete two motion windows. Compose-plus-copy totals during the interior motion windows
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
adds no new lines. The [native results](#native-abab-result--2026-10-10) below
cover paired-boot counters; the short functional pass remains pending.

## Native A–B–A–B result — 2026-10-10

Owner-run ThinkPad on AC, 1920×1080 GOP, four fresh PXE boots in order
**A1, B1, A2, B2**. Nobody touched the laptop during these windows. Luna built
A from `4b6550a6` and B from reviewed #676 head `87e03014`, each with the same
synthetic tool code commit `dc237005c9171cf4e3f463fcd4a5c44f1b50f225` cherry-picked.
The [tool and hands-free procedure](https://git.internal/PyxisOS/pyxis-os/pulls/682)
are separate from this implementation; the tool does not change the skip rule.
No compiler or consumer change is part of B.

Reported kernel SHA256 prefixes: A `228d15f8…`, B `9e8fb752…`. **A's identical
initrd, prefix `21db2ccf…`, was used for all four boots**; Luna's independently
built B initrd differed due to non-reproducible payloads and was excluded.
Only hash prefixes and pre-cherry-pick revisions were supplied; they are not
full artifact identities or Luna's final cherry-pick commit IDs. Local sealed
hashes above are not hashes of these native builds.

Build flags: `LOG_LEVEL=info LOG_UDP=1 DISPLAY_FLIP=1 DISPLAY_FLIP_METRICS=1
 DISPLAY_TIMING=off DISPLAY_CURSOR_PROBE=1 POINTER_SYNTHETIC=1`. PXE options:

```text
display.flip=1 display.flip.metrics=1 display.cursor.probe=1 pointer.synthetic=schedule display.timing=off log.udp=1
```

The source stayed on Caelum's terminal body (1920×1048 below the 32-pixel bar),
centre (960,556), radius 128. Each boot settled ten seconds, then ran thirty
seconds each idle, motion, idle, motion, and stopped. There were no synthetic
clicks, buttons, wheel or space changes. The masked logs remain at
`/shared/present/batch2/ab676-{A1,B1,A2,B2}.log`, not in Git.

These are independently checked differences of the boundary lines' cumulative
counters. A boundary names the window **starting** there: idle-1 is bounded by
`idle-1`→`motion-1`, motion-1 by `motion-1`→`idle-2`, idle-2 by
`idle-2`→`motion-2`, and motion-2 by `motion-2`→`done`. Setup/settle is excluded.
Each table label names the measured interval, not the literal closing marker.
Every confirmed frame has one composition and one scanout copy in these runs.

| Run | Interval | Wall seconds | Frames/compositions/copies | Compose total, ms | Copy total, ms | Screen moves |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| A1 | idle-1 | 29.998 | 1784 | 2185.7 | 1703.0 | 0 |
| A1 | motion-1 | 30.001 | 1786 | 2236.5 | 1670.7 | 1788 |
| A1 | idle-2 | 30.000 | 1786 | 2276.6 | 1670.7 | 0 |
| A1 | motion-2 | 30.000 | 1785 | 2197.4 | 1675.1 | 1790 |
| B1 | idle-1 | 29.992 | 16 | 21.2 | 16.3 | 0 |
| B1 | motion-1 | 29.994 | 1767 | 2188.8 | 1617.6 | 1779 |
| B1 | idle-2 | 30.015 | 18 | 21.7 | 16.3 | 0 |
| B1 | motion-2 | 29.985 | 1774 | 2166.5 | 1626.7 | 1789 |
| A2 | idle-1 | 30.000 | 1786 | 2173.4 | 1677.4 | 0 |
| A2 | motion-1 | 30.002 | 1786 | 2219.5 | 1659.2 | 1792 |
| A2 | idle-2 | 29.998 | 1785 | 2229.8 | 1670.9 | 0 |
| A2 | motion-2 | 30.001 | 1786 | 2248.4 | 1698.5 | 1789 |
| B2 | idle-1 | 30.002 | 16 | 19.4 | 14.5 | 0 |
| B2 | motion-1 | 29.992 | 1769 | 2181.8 | 1625.7 | 1780 |
| B2 | idle-2 | 30.004 | 18 | 21.7 | 16.3 | 0 |
| B2 | motion-2 | 29.996 | 1779 | 2170.6 | 1631.0 | 1791 |

Idle A presents 59.47–59.54 frames/s and spends 3.85–3.95 seconds of measured
compose-plus-copy elapsed time in each thirty-second window. Idle B presents
0.53–0.60 frames/s, spending 33.9–38.0 ms: about **99% less measured idle pixel
work** and 99% fewer presented frames. B's 16–18 frames are expected visible
metrics/boundary-log redraws on Caelum, not unchanged-frame submissions.
These windows mean no pointer motion, not no content changes.

Continuous motion still requires full frames: A 59.5 frames/s, B 58.9–59.3,
with similar composition/copy cost per frame. B avoids idle work rather than
making individual frames faster. Integer pixel rounding, coalescing and pending
frame snapshots account for differences between routed moves and rendered
frames. Boundary input counters can advance while the completed-frame counter
still refers to the previous presentation.

All four logs contain the complete schedule with no abort, FAILED, pinned-failure
or nonzero timeout line; all presenter metrics report `backend=flip`. Renoir
validation, input/device service and normal pacing remain active while idle.
Compose/copy spans are elapsed measurements, including preemption/log overhead;
this result is not a total BSP CPU measurement. It does not qualify forced
failure/panic/wedged-writer paths.

### Owner functional pass on B2

After the synthetic source stopped, the owner checked the same B2 kernel
(`#676` head `87e03014` plus the tool), with qualified flips on:

- `screenshot tmp://idle.png` completed.
- Typing after idle, pointer motion, selection and tabs work and look right.
- Ctrl+Alt+Delete power-overlay cancellation and the volume popup work.
- Native Quake and Chocolate Quake presentation work and look right. Native
  Quake timedemo was approximately 620 fps, reported as "just as solid"; this
  is an owner observation, not a matched timedemo performance comparison.

**Native counter and short functional qualification are complete.** Chocolate
Quake has a separate observed input problem: motion stops at the left/right
screen edges and unlocking does not work, appearing to hide rather than capture
the cursor. Normal Quake works. The owner considers this independent of frame
skipping and the orchestrator is routing it separately; this PR makes no consumer
mouse changes and does not claim that diagnosis is established.

### Integration after qualification

Current main `4cb736f8` was merged after the native run. Its terminal styles/RGB
change conflicts with generation tracking in `kernel/fb/tty.c`: retained cells
now store glyph, foreground, background and attributes together. The resolution
compares all four fields for visual invalidation, preserves glyph-only selection
invalidation, and compares resolved background colours for graphics-margin
invalidation. No skip, pacing, frame-handoff or GPU-write contract changed.
Native results above belong to `87e03014` plus the tool; they are not a native
run of this integration revision. Integration code is `1b0f5bd8` (main `4cb736f8`). Its full source image builds
with the existing LLVM23.1.3 builder and inherited published main pins (userland
`1162d729`, ports `4047297d`, unchanged fs/lwIP); no dependency PR or unpublished
pin. Probe enabled, synthetic source disabled. QEMU10.2.2 Q35/nestedKVM/maxCPU,
four CPUs, 2 GiB, fresh OVMF, standard/Bochs/VirtIO each 1280×800, relative PS/2,
VirtIO-SCSI CD/RNG, no NIC/disk/USB/audio:

- Firmware: echo, idle skip, same-glyph red→blue/bold repaint, styled selection
  (read-only GDB verifies glyph/colour/attributes retained), screenshot command.
- Bochs and VirtIO: echo after startup and unchanged frames held while service
  and skip counts continue. VirtIO's QMP scanout does not qualify its separate
  host cursor pixels.

Source/integration review, whitespace and document links pass; task-owned VMs
and debugger sessions are stopped. No native run of `1b0f5bd8` is claimed.
Exact-head CI is reported on the PR; the owner reviews and merges.

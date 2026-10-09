# Completed-frame handoff

Measured on 2026-10-09 for step 2 of the accepted
[presentation-timing plan](../../../wip/presentation-timing.md): graphics
programs hand over whole frames through SUBMIT
([contract](../../../interfaces/graphics.md#slots-and-frame-handoff)).

## Method

**Configuration.**
- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 4 CPUs, 8 GiB, 1280x800, display off.
- **Builds:**
  - **Before:** main `ba51452c`.
  - **After:** this branch on `ba51452c`, with userland `fbb28d0` and ports
    `a975d07`. Rebased onto main `476cd2c0` as userland `f6f4f69` and ports
    `0f0aa44` with no change to these files; the rebased build was rechecked
    with Quake and Mandelbrot only.
- **Timing patch:** the local presenter timing patch from
  [RAM staging](../presentation-staging/README.md#method), applied identically
  to both builds.

**Workloads,** all on one boot per build and backend:
- **Presenter:** the last three 10 s timing summaries, first with the
  Development shell idle, then with `quake` playing its attract-mode demos.
- **Quake throughput:** `quake +timedemo demo1`, five runs.
- **SDL:** the [SDL delay workload](../sleep-wake-granularity/sdl-delay.c), three
  runs. Its work time covers drawing a 640x480 texture and presenting it at
  1280x768.

## Results

**Presenter,** P50 in ms; ranges cover the three summaries. VirtIO ran twice
per build.

| Backend, workload | Before: composition | Before: end | After: composition | After: end |
| --- | --- | --- | --- | --- |
| Std VGA, idle | 0.348–0.351 | 0.293–0.294 | 0.346–0.356 | 0.290–0.297 |
| Std VGA, Quake | 0.305–0.313 | 0.283–0.286 | 0.304–0.323 | 0.266–0.285 |
| VirtIO, idle | 0.317–0.344 | 0.586–0.840 | 0.344–0.365 | 0.709–0.854 |
| VirtIO, Quake | 0.297–0.364 | 0.651–0.861 | 0.331–0.352 | 0.731–0.829 |

The presenter is unchanged. VirtIO's end phase moved between runs of the same
build by as much as between builds; it includes host-side transfer work.

**Quake `timedemo demo1`,** frames per second:

| Build | Std VGA | VirtIO |
| --- | --- | --- |
| Before | 1584, 1547, 1593, 1510, 1547 | 1629, 1602, 1565, 1607, 1540; 1534, 1538, 1528, 1468, 1496 |
| After | 1245, 1251, 1213, 1266, 1265 | 1064, 1131, 1177, 1163, 1186; 1041, 1089, 976, 832, 1083 |

**SDL work per frame,** mean in ms: on std VGA 1.06, 0.99 and 1.01 before,
1.09, 1.61 and 1.18 after. On VirtIO 1.06–1.16 before, 1.21–1.35 after.

**Where the cost is.** Two local Quake variants, std VGA, submitted only the
first frame and then kept drawing:
- **into one held slot:** 1454, 1377, 1428, 1475 and 1405 fps;
- **rotating through all three slots:** 1445, 1465, 1446, 1502 and 1476 fps.

Rotation costs nothing measurable, so three slots don't hurt the cache. The
cost is SUBMIT itself: a synchronous BSP request, as every display operation
already was. In nested KVM it adds about 0.11–0.16 ms per frame on std VGA
and about 0.29 ms on VirtIO, where the BSP is busier presenting.
- **Ordinary play:** at Quake's 72 fps that is about 1–2% of one CPU.
- **Timedemo:** throughput falls 20–30%, because it renders as fast as it can.
- **Natively,** the request's interprocessor interrupts are much cheaper.
  That is the owner's run below.

**Drops.** A local counter, never committed, counted SUBMITs during ordinary
Quake on VirtIO. Every 600 submissions, 505–506 frames were shown and 94–95
were reported as dropped: 60.7 frames shown and 11.3 dropped per second, the
72-to-60 mismatch.

**Checks,** all in QEMU on this branch:
- **Slot rules:** a throwaway probe compiled in the guest with `tcc`.
  Submitting slot 0 returned slot 1 to render next. Re-submitting the pending slot, the current slot or
  slot 3 each returned BAD_REQUEST. A back-to-back SUBMIT reported its drop.
- **Programs:** Quake, Doom, Mandelbrot, mousetest and SDL showed whole frames.
  Mousetest's ink survived redraws into other slots.
- **Resize on VirtIO:** a host resize through VNC SetDesktopSize, during each
  program:
  - Mandelbrot, Doom and SDL replaced their slots and redrew at the new size;
  - Quake, which doesn't adapt, stayed clipped as before.
- **Capture:** `screenshot` from the remote space captured Quake, Mandelbrot,
  Doom and SDL frames, pointer included.
- **Cleanup:** allocator use stayed at 43.82 MiB after five complete sessions
  and after two mousetest sessions killed with Ctrl+C.
- **Not exercised:** allocation failure during ACQUIRE or REPLACE, which was
  reviewed in code only.

## Native steps for the owner

On the ThinkPad, wired and on AC, at the panel's native mode. Use two PXE
builds, each with `/shared/present/timing.py TREE` applied before
`make -j16 image`:
- main `ba51452c`;
- this branch.

1. **Idle:** leave the Development shell idle at its prompt for 40 s, then run
   `log` and note the last three `present-timing` lines.
2. **Moving Quake:** run `quake` and play `e1m1` while turning steadily. After
   40 s, quit and note the last three `present-timing` lines.
3. **Throughput:** run `quake +timedemo demo1` three times and note the fps
   line each time.
4. **Tearing:** during steady turning in ordinary Quake, take a short camera
   clip on each build, slow motion if the phone has it. Note whether a
   horizontal tear line still appears.

**What to expect.** Quake should no longer show two of its own frames mixed
in one presented frame. The presenter's copy to scanout is still not
synchronized with the panel, so a tear line from that copy may remain; the
timing steps address it. Step 3 shows the native cost of SUBMIT.

## Limits

- Three slots cost 23.7 MiB per graphics session at 1920x1080.
- Each SUBMIT is a BSP request round trip. If the native cost matters, a
  SUBMIT that doesn't go through the BSP would need its own ownership
  decision.
- No damage tracking: programs redraw every pixel of each frame.

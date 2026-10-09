# Chocolate Doom against native Doom

Measured on 2026-10-09 for task 1 of the
[SDL game ports](../../../wip/sdl-game-ports.md) milestone: the cost of the SDL2
path, comparing [Chocolate Doom](../../../userland/chocolate-doom.md) with the
native [Doom](../../../userland/doom.md) on the same demo.

## Method

**Configuration.**
- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 4 CPUs, 8 GiB, 1280x800, display off. The content area is 1280x768.
- **Build:** this branch, on main `da867b24` with the plan from #620. After
  rebasing onto main `31af2224`, one timedemo each gave 916 fps native and
  353 fps for Chocolate Doom's default, within the ranges below.
- **Sound:** both sides silent. Native Doom has no sound; Chocolate Doom is
  built without SDL2_mixer and run with `-nosound -nomusic`.

**Workload.** `-timedemo demo1` with the shareware IWAD's own demo, 5026
gametics; both engines play it in sync. Five rounds per backend, each round
running every configuration once in turn. Upstream's report counts realtics of
1/35 s, so each figure carries about 0.2–0.6% quantization.

**Configurations,** selected for Chocolate Doom with `-extraconfig`:

| Name | Picture | Settings |
| --- | --- | --- |
| Native | 960x600, integer scale 3, drawn straight into the display slot | — |
| Chocolate default | 1024x768, 4:3, one nearest-neighbour stretch | `force_software_renderer 1` |
| Chocolate matched | 960x600, the same as native | also `integer_scaling 1`, `aspect_ratio_correct 0` |
| Chocolate two-stage | 1024x768 with linear smoothing | `force_software_renderer 0`: a 1280x800 integer upscale, then a linear pass |

## Results

Frames per second; median, then all five runs.

| Configuration | Std VGA | VirtIO |
| --- | --- | --- |
| Native | 946: 931, 956, 988, 946, 946 | 941: 983, 941, 941, 972, 921 |
| Chocolate default | 365: 253, 369, 292, 367, 365 | 364: 353, 363, 364, 382, 370 |
| Chocolate matched | 376: 368, 378, 383, 376, 373 | 378: 319, 378, 378, 385, 370 |
| Chocolate two-stage | 239: 239, 239, 245, 239, 235 | 237: 237, 241, 241, 151, 220 |

The low outliers (253, 292, 151) are single runs on a shared host; their
neighbours in the same round are normal.

- **The SDL path:** Chocolate Doom at the same picture runs at about 40% of
  native Doom's rate, 2.65 ms per frame against 1.06 ms.
- **Default scaling:** the accepted default, a 4:3 stretch, costs about the
  same as the matched integer picture.
- **Two-stage scaling:** it costs about 1.5 ms more per frame (237 against
  364 fps). The accepted default stays: `force_software_renderer` on.

**Where the extra time goes.** Two local variants of the matched configuration,
never committed, run interleaved with the shipped build on std VGA:
- **Shipped:** 345, 363, 336, 351 and 352 fps; median 351.
- **No whole-surface copy and SUBMIT:** the SDL backend submits only the first
  frame. 418, 420, 412, 414 and 419 fps; median 418, so the copy of the
  1280x768 window surface and its SUBMIT cost about 0.46 ms per frame. The
  [frame handoff](../frame-handoff/README.md) measured SUBMIT alone at about
  0.11 ms.
- **No full-window clear either:** Chocolate Doom's `SDL_RenderClear` removed
  as well. 441, 451, 448, 433 and 439 fps; median 441, so the clear costs
  about 0.12 ms.
- **The rest,** about 1.2 ms per frame against native, wasn't separated: the
  expansion of the 8-bit frame into a 32-bit texture, SDL's software stretch,
  and the differences between the two engines' loops.

**Checks** (QEMU, std VGA):
- **Starting:** `chocolate-doom` with no arguments found
  `boot://share/doom/DOOM.WAD`, identified it as shareware, played the demo
  loop and took keyboard control.
- **Mouse:** with `-warp 1 1`, relative motion turned the view through the
  SDL port's pointer lock.
- **Menus:** the menu worked, and Quit Game showed upstream's ENDOOM screen
  through textscreen before returning to the shell.
- **Configuration:** `home://chocolate-doom/chocolate-doom.cfg` was written on
  quit with `force_software_renderer 1`.
- **Timedemo exits:** both programs report the timedemo through upstream's
  `I_Error`, so they exit with status -1, and Chocolate Doom saves no
  configuration on that path.

## Native steps for the owner

On the ThinkPad, wired and on AC, at the panel's native mode, with this
branch's PXE build:

1. **Native:** run `doom -timedemo demo1` three times and note each report.
2. **Chocolate default:** run `chocolate-doom -nosound -nomusic -timedemo demo1`
   three times.
3. **Chocolate matched:** put `integer_scaling 1` and `aspect_ratio_correct 0`
   in a file, say `home://matched.cfg`, and run
   `chocolate-doom -nosound -nomusic -extraconfig home://matched.cfg -timedemo demo1`
   three times.
4. **Play:** run `chocolate-doom`, play a little of E1M1, and quit through the
   menu.

**What to expect.** The ratio between native and Chocolate Doom should
narrow natively, where memory copies and the SUBMIT round trip are cheaper.

## Limits

- The 1.2 ms not attributed above includes engine differences, so this isn't a
  pure SDL figure.
- Timedemos measure rendering throughput, not the 35 Hz feel of ordinary play.

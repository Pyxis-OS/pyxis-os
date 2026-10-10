# Chocolate Quake against native Quake

Measured on 2026-10-09 for task 2 of the
[SDL game ports](../../sdl-game-ports.md) milestone: the cost of the SDL2
path, comparing [Chocolate Quake](../../../userland/chocolate-quake.md) with the
native [Quake](../../../userland/quake.md) on the same demo.

## Method

**Configuration.**
- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 4 CPUs, 8 GiB, 1280x800, display off. The content area is 1280x768.
- **Build:** this branch on main `11d35fa6`, with userland `c939df1` and the
  ports branch.
- **Sound:** both sides silent. Native Quake has no sound, and Chocolate
  Quake's sound initialization fails because the SDL2 port has no audio.
- **Data:** the image's shareware `pak0.pak`.

**Workload.** `+timedemo demo1`, 969 frames; both engines play the same demo.
Five rounds per backend, each round running every configuration once in
turn. Quake reports frames per second from its own clock; one run takes about
1–2.5 s.

**Configurations:**

| Name | Rendered | Picture | Command |
| --- | --- | --- | --- |
| Native | 320x240 | 960x720, integer scale 3, drawn straight into the display slot | `quake +timedemo demo1` |
| Chocolate default | 320x200 | 1024x768, SDL's nearest-neighbour stretch to 4:3 | `chocolate-quake +timedemo demo1` |
| Chocolate matched | 320x240 | 1024x768, the same stretch | `chocolate-quake +vid_forcemode 4 +timedemo demo1` |

Chocolate Quake has no integer-scale setting: SDL scales its 320x240 logical
size to fit, 3.2 times in a 1280x768 area. The matched configuration renders
native Quake's resolution but presents about 14% more pixels.

## Results

Frames per second; median, then all five runs.

| Configuration | Std VGA | VirtIO |
| --- | --- | --- |
| Native | 1106: 1205, 1098, 1106, 1141, 1032 | 1146: 741, 1256, 1186, 1146, 1141 |
| Chocolate default | 461: 441, 282, 504, 490, 461 | 486: 532, 482, 486, 499, 394 |
| Chocolate matched | 467: 467, 468, 481, 457, 463 | 474: 519, 481, 474, 465, 462 |

The low outliers (282, 741, 394) are single runs on a shared host; their
neighbours in the same round are normal.

- **The SDL path:** Chocolate Quake at native Quake's resolution runs at
  about 42% of native Quake's rate, 2.14 ms per frame against 0.90 ms on std
  VGA. The difference, about 1.2 ms, matches across both backends.
- **Default resolution:** 320x200 costs about the same as 320x240.
- **Compared with Doom:** the [Chocolate Doom measurements](../chocolate-doom/README.md)
  found about 1.6 ms per frame for the same SDL path, at a similar presented
  size.

**Idle CPU** (the frame sleep): host CPU time used by the whole QEMU process over
20 s, in 1/100 s ticks, with each game in its demo loop. The variant without
the frame sleep is a local scratch build, never committed.
- **Shell idle:** 278, and 260 afterwards.
- **Chocolate Quake:** 686.
- **Chocolate Quake without the frame sleep:** 2321, about one more CPU busy.
- **Native Quake:** 568.

**Checks** (QEMU, std VGA):
- **Starting:** `chocolate-quake` with no arguments found
  `boot://share/quake/id1/pak0.pak` and played the demo loop.
- **Sound:** sound initialization reported "SDL not built with audio support"
  and the game continued silently.
- **Play:** `+map e1m1` started the level; the arrow keys moved and relative
  mouse motion turned the view.
- **Saves:** `save s1` wrote `home://chocolate-quake/id1/s1.sav`, and a new
  run with `+load s1` restored the saved position.
- **Quitting:** Quit in the menu showed upstream's end screen; a key returned
  to the shell, and `config.cfg` had been written beside the save.
- **Console log:** `-condebug` appended each console line to
  `qconsole.log` through libc `O_APPEND`, after upstream's startup delete.
- **Memory:** about 288 MiB above idle while running, as the
  [port README](../../../../ports/chocolate-quake/README.md#memory) records.

## Native results

The owner ran the native steps on 2026-10-09 on the ThinkPad: wired, on AC,
at the panel's native mode, PXE build of `0bb9731f` (#646's head) with
default info logging. Values are approximate, one summary per configuration:

| Configuration | fps | Per frame |
| --- | --- | --- |
| Native | about 630 | 1.59 ms |
| Chocolate default | about 275 | 3.64 ms |
| Chocolate matched | about 277 | 3.61 ms |

- **The SDL path:** about 2.0 ms per frame natively, against about 1.2 ms in
  QEMU. The expected narrowing didn't happen: the ratio is about 2.3 natively
  against about 2.4 in QEMU. The panel's 1920x1080 mode presents larger
  pictures than QEMU's 1280x800, so the figures aren't matched to QEMU's.
- **Play:** worked. Tearing was clearly worse than native Quake's: about three
  tear lines at once, one about a third of the way down and another 100–150
  lines lower, drifting noticeably. This is an observation, not a diagnosis; a
  beat between the 72 Hz frame pacing and the 60 Hz panel, with unsynchronized
  copies, is one possibility. It is an input to the
  [Renoir flip qualification](../../../technical-debt.md#renoir-flip-backend-qualification).

## Limits

- The 1.2 ms includes engine differences between WinQuake and Chocolate
  Quake, and the larger presented picture, so it isn't a pure SDL figure.
- demo1 is short, so one interruption on the host moves a whole run.
- Timedemos measure rendering throughput, not the 72 Hz feel of ordinary play.

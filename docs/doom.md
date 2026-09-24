# Doom

The ports repository builds a pinned [doomgeneric](https://github.com/ozkl/doomgeneric)
with a Pyxis adapter. The ordinary image includes the executable and license,
but no game data. No wall-clock, PCI, VirtIO or kernel-interface change is needed.

## Local game data

Supply a WAD you own when building or running:

```sh
make run CPUS=4 DOOM_WAD=/shared/assets/doom-wad/DOOM.WAD \
  DOOM_DEMOS=/shared/assets/doom-demos
```

`DOOM_WAD` is copied to `app://share/doom/DOOM.WAD`. The optional `DOOM_DEMOS`
directory supplies `e1m1sec.lmp` and `e1m2sec.lmp`; it requires `DOOM_WAD`.
These are local inputs, not repository files or downloaded dependencies. A
subsequent build without those overrides removes the staged data from its
archive. Normal CI builds remain data-free.

In the application-space shell:

```text
doom
doom -warp 1 1
doom -playdemo app://share/doom/e1m1sec.lmp
doom -playdemo app://share/doom/e1m2sec.lmp
```

Use `-iwad path` to select another accessible WAD. Demo playback returns to the
shell at its end. The two local demos identify version 109 (Doom 1.9).

## Controls and resources

Arrows move/turn; Ctrl fires, Space uses, Shift runs, Alt strafes, and comma/period
strafe left/right. Escape opens the menu. F10 then Y quits to the shell.
Super+Left/Right remains space navigation. The port releases held game keys on
focus loss or input reset, blocks while inactive and excludes inactive time
from its elapsed game clock.

Doom requires the shell's named display, keyboard and clock capabilities, with
DRAW, INPUT and READ/SLEEP rights respectively. It acquires exclusive display and
keyboard sessions, and releases them on quit or error. Existing process cleanup
also handles a fatal fault. WAD/demo reads use libc streams and ordinary file
capabilities.

Rendering uses a 320x200 game buffer scaled by an integer to fit the space's
content area, with black borders. Pixel conversion respects native channel
shifts and pitch. Presentation retains the existing single-buffer contract;
tearing is possible. There is no display refresh synchronization or pixel-aspect
correction.

## Boundaries

This is single-player keyboard gameplay and demo playback. No audio, networking,
mouse, save/load, configuration persistence, demo recording or timedemo reporting.
Save/load menus report that the feature is unavailable. The corresponding
command-line load/record options and alternate pixel-format/scaling options are
rejected; no successful save is fabricated. See the ports recipe's README for
source pin, patch scope and build details.

Save-game replacement needs native filesystem removal/rename semantics before
it can be exposed through libc; this is tracked in [technical debt](technical-debt.md#doom-save-games-and-configuration).

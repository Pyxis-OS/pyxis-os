# Doom

The ports repository builds a pinned [doomgeneric](https://github.com/ozkl/doomgeneric)
with a Pyxis adapter. The ordinary image includes the executable and Doom 1.9
shareware data: run `doom` in the application-space shell to play the first
episode. No wall-clock, PCI, VirtIO or kernel-interface change is needed.

The unchanged shareware WAD is pinned under `third_party/doom-shareware`, with
its source, checksums and redistribution terms. Its license and provenance ship
at `app://share/licenses/doom-shareware/`, separately from the engine's GPL
license. The shareware license permits free redistribution; it does not grant
permission to charge for the data or redistribute a retail WAD.

## Local game data

Override the shareware data with a WAD you own when building or running:

```sh
make run CPUS=4 DOOM_WAD=/shared/assets/doom-wad/DOOM.WAD \
  DOOM_DEMOS=/shared/assets/doom-demos
```

`DOOM_WAD` is copied to `app://share/doom/DOOM.WAD`. The optional `DOOM_DEMOS`
directory supplies `e1m1sec.lmp` and `e1m2sec.lmp`, and can also be used with the
default shareware WAD. These overrides are local inputs, not repository files.
A subsequent build without those overrides restores shareware and removes the
local demos from its archive. Normal CI images include only the shareware data.

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

## Saves

F2 opens the save menu; F3 opens load. F6 selects a quicksave slot on its first
use, then F6/Y overwrites it and F9/Y reloads it. Quit and run `doom` again to
load the same slots, or use `doom -loadgame 0` for the first slot (slots 0–5).

Saves live under `home://doom/saves/<iwad-name>/`. Shareware uses `doom1.wad`,
registered/Ultimate Doom uses `doom.wad`, and other missions use upstream's
canonical IWAD name. The engine detects the game mode from the data; renaming
the input WAD does not mix shareware and retail saves. The first save creates
these directories using the `home` capability. Saves survive process exit,
**not reboot**: the filesystem is still RAM-backed.

A save writes its own exclusively reserved temporary file, checks writes and
close, then atomically replaces the destination. Failure preserves the previous
save and reports an error. Simultaneous saves to the same slot use the last
successful replacement. An interrupted save can leave `temp-*.dsg` files, which
can be removed manually; later saves do not reuse those names.

The upstream format assumes trusted saves and matching game data. Short reads
stop with an error and slot descriptions are bounded, but the parser is not
hardened against arbitrary malformed files. Different PWADs on the same IWAD
still share slots. See [filesystem mutations](../interfaces/filesystem-mutations.md) for the
native removal/replacement semantics.

## Boundaries

This is single-player keyboard gameplay, save/load and demo playback. No audio,
networking, mouse, configuration persistence, demo recording or timedemo
reporting. Recording, timedemo and alternate pixel-format/scaling options are
rejected. See the ports recipe's README for source pin, patch scope and build
details. Configuration and timedemo remain in
[technical debt](../technical-debt.md#doom-configuration-and-save-format-limits).

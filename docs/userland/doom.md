# Doom

The ports repository builds a pinned [doomgeneric](https://github.com/ozkl/doomgeneric)
with a Pyxis adapter. The `doom` game package carries `doom.pxb`,
[Chocolate Doom](chocolate-doom.md) and the shareware IWAD; run `doom` in an
application-space shell after installing it.

## Game data

Configure your network repository with [`pkg`](../interfaces/packages.md#package-client),
then install the package and launch:

```text
pkg install doom
doom
doom -warp 1 1
doom -timedemo demo1
```

The bundle reads its own package, read-only, as `pkg://doom` and reads
`pkg://doom/DOOM.WAD` by default. The same package carries
[Chocolate Doom](chocolate-doom.md). A missing package refuses launch, including
when an explicit `-iwad path` is supplied. `-iwad` still selects another
accessible WAD once the bundle's required grants are available.

Live and installed systems use the same package root. Live package installs
last until reboot; there is no archive fallback or local seed repository.
Legacy image data inputs still exist pending the
[archive cut](../wip/packages.md#tasks), but do not supply this bundle's data root.

`-playdemo path` plays an accessible demo and returns to the shell when it ends.
`-timedemo demo1` plays the IWAD's first demo as fast as possible and exits with
upstream's report on stderr: `timed N gametics in M realtics (F fps)`.

## Controls and resources

Arrows move/turn; Ctrl fires, Space uses, Shift runs, Alt strafes, and comma/period
strafe left/right. Escape opens the menu. F10 then Y quits to the shell.
Super+Left/Right remains space navigation. Super+Down shows the terminal and
Super+Up restores graphics; further frames preserve that choice. The port
releases held game keys on focus loss or input reset, requiring a fresh press
after a space or layer switch. Hidden and unselected sessions keep polling input,
advancing game time and rendering; focus only controls input eligibility. Pause
or the single-player menu pauses gameplay explicitly; demo playback continues
through the menu. Rendering still uses CPU and memory bandwidth while hidden.
A foreground game's terminal also provides the shell's
[Ctrl+C interrupt](shell.md#interrupting-foreground-commands).

Doom requires the shell's named display, keyboard and clock capabilities, with
DRAW, INPUT and READ/SLEEP rights respectively. It acquires exclusive display and
keyboard sessions, and releases them on quit or error. Existing process cleanup
also handles a fatal fault. WAD/demo reads use libc streams and ordinary file
capabilities.

Rendering uses a 320x200 game buffer scaled by an integer to fit the space's
content area, with black borders. Pixel conversion respects native channel
shifts and pitch. Each frame is drawn into a held display slot and
[submitted](../interfaces/graphics.md#slots-and-frame-handoff) whole; the
borders are never drawn, so every slot keeps them black. There is no display
refresh synchronization or pixel-aspect correction.

At each frame boundary Doom queries destination geometry. A fresh generation
replaces its slots and recomputes integer scaling and black borders; the game
engine keeps its fixed frame size. A destination too small for scale one keeps
the old mapping and clipped output until growth. Replacement failure also
keeps the old buffer usable, with no allocation retry on every frame; another
geometry generation permits a new attempt. These checks continue while hidden or
unselected, and replacement preserves the chosen layer.

## Saves

F2 opens the save menu; F3 opens load. F6 selects a quicksave slot on its first
use, then F6/Y overwrites it and F9/Y reloads it. Quit and run `doom` again to
load the same slots, or use `doom -loadgame 0` for the first slot (slots 0–5).

Saves live under `home://doom/saves/<iwad-name>/`. Shareware uses `doom1.wad`,
registered/Ultimate Doom uses `doom.wad`, and other missions use upstream's
canonical IWAD name. The engine detects the game mode from the data; renaming
the input WAD does not mix shareware and retail saves. The first save creates
these directories using the `home` capability. On installed systems `home://`
is the pool's home volume and saves survive reboot once written back, within
the periodic flush interval (30 seconds by default) or at `sync home://`. On
live boots `home://` is RAM and saves last until reboot.

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

This is single-player keyboard gameplay, save/load, demo playback and
timedemo. No audio, networking, mouse, configuration persistence or demo
recording. Recording and alternate pixel-format/scaling options are
rejected. See the ports recipe's README for source pin, patch scope and build
details. Configuration remains in
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1381).

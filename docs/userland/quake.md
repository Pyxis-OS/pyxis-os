# Quake

The ports repository builds a pinned [quakegeneric](https://github.com/erysdren/quakegeneric),
id Software's GPL WinQuake software renderer behind a small platform interface,
with a Pyxis adapter. The `quake` game package carries `quake.pxb`,
[Chocolate Quake](chocolate-quake.md) and the shareware data; after installing
it, run `quake` in an application-space shell to play with keyboard and mouse. There is no sound, networking or CD audio.

## Game data

Configure your network repository with [`pkg`](../interfaces/packages.md#package-client),
then install the package and launch:

```text
pkg install quake
quake
quake +map e1m1
quake +timedemo demo1
```

The bundle reads its own package, read-only, as `pkg://quake`, with Quake's
data under `id1/`. The same package carries
[Chocolate Quake](chocolate-quake.md). A missing package refuses launch,
including when an explicit `-basedir PATH` is supplied. Arguments keep Quake's
meaning: `-basedir` replaces `pkg://quake` and `-writedir` replaces
`home://quake` once the bundle's required grants are available.

Live and installed systems use the same package root. Live package installs
last until reboot; there is no archive fallback or local seed repository.
Legacy image data inputs still exist pending the
[archive cut](../wip/packages.md#tasks), but do not supply this bundle's data root.

## Controls and resources

Quake's default bindings apply: arrows move and turn, Ctrl fires, Space jumps,
Shift runs, Alt strafes, 1–8 select weapons and `/` selects the next one.
Escape opens the menu and backquote the console. Quake requests native pointer
locking once after its first presentation and applies mouse motion and buttons
only while locked, so moving the mouse turns and looks up and down; the left
button fires and the right moves forward. The kernel can refuse or later revoke
the lock; keyboard play and rendering continue. Super+Esc unlocks, and a fresh
left click on the game surface lets Quake request the lock again. On a first run
without a saved configuration, the middle button selects the next weapon rather
than toggling mouse look. The
wheel is unbound by default (`bind mwheelup "impulse 10"` binds it).
Super+Left/Right remains space navigation. Super+Down shows the terminal and
Super+Up restores graphics; further frames preserve that choice.

Focus loss or an input reset releases every held key and button, including
modifiers shared by left/right keys and mouse buttons. A button held across a
space or layer switch must be pressed again. Hidden and unselected sessions keep
polling input, advancing game time and rendering; focus only controls input
eligibility. Open the menu or console, or use Quake's Pause binding, to pause
single-player gameplay explicitly. Rendering continues using CPU and memory
bandwidth while hidden. A foreground game's terminal also provides the shell's
[Ctrl+C interrupt](shell.md#interrupting-foreground-commands).

Quake requires the shell's named display, keyboard and clock grants and uses
the pointer grant when present. Without a pointer grant or a working mouse it
prints a note and plays from the keyboard. It acquires exclusive display,
keyboard and pointer sessions and releases them on quit or a fatal error;
process cleanup also handles a fault. Pointer counts reach Quake unscaled, so
Quake's `sensitivity` setting is the only scale.

The console reports `Unknown command "volume"` at startup: the default
configuration sets a sound variable that this soundless build does not have.

Rendering uses a 320x240 game buffer scaled by the largest integer that fits
the content area, with black borders, converting the 8-bit palette to the
display's channel shifts. Each frame is drawn into a held display slot and
[submitted](../interfaces/graphics.md#slots-and-frame-handoff) whole. Outside `timedemo`, the loop sleeps to Quake's 72 Hz frame
cap. The video mode is fixed at 320x240.

## Saves and configuration

Generated files go to `home://quake/id1/`, created on first start: `config.cfg`
(written on quit), saves (`save NAME`, F6 quicksave, the Save and Load menus),
demos and screenshots. That directory is searched before the game data. Shareware
and retail data share it. On a live boot `home://` is a RAM volume, so these files
survive quitting but not reboot. An installed system keeps `home://` on the pool
(see [system layout](system-layout.md#roots)), so they persist across reboots.

`config.cfg`, saves and screenshots are replaced atomically: the data goes to a
random `NAME.XXXXXX` file beside the target, which is synced, closed and renamed
over it. A failed write or rename keeps the old file, removes the temporary file
and prints `Couldn't write NAME: reason`; a failed save omits `done.`. Demos are
recorded in place. The [atomic-save limits](https://git.internal/PyxisOS/pyxis-os/issues/1407)
cover what a crash can lose.

## Timedemo

`timedemo demo1` replays the first demo as fast as possible and prints frames,
seconds and frames per second to the console and the shell's terminal.

These historical runs predate the package-root migration.

| Configuration | Samples (fps) | Median |
| --- | --- | --- |
| QEMU 10.2.2/KVM, 4 CPUs, nested VM, development host, 2026-10-04 | 1617.0, 1653.0, 1622.7, 1351.3, 1588.1, 1592.1, 1598.2 | 1598.2 |
| Native ThinkPad T14 Gen 1 AMD, PXE, owner, 2026-10-04 | 634.2 (969 frames, 1.5 s) | — |
| Native ThinkPad, PXE, owner, 2026-10-07, after the boot display driver (#468) | 667.1 | — |

That run booted the ISO as a read-only virtio disk, because this host's stock
QEMU crashed reading it through AHCI. A review run through the CD-ROM path with
the AHCI-fixed QEMU (see [QEMU troubleshooting](../development/qemu.md#ahci-cd-rom-crash-before-kernel-entry)) measured
1660.5 fps. Natively the owner played with both the touchpad and the TrackPoint;
that sample is a single run. The VM figures come from a faster host and are not a
prediction of native speed.

## Boundaries

This is single-player play, save/load, demos and timedemo. No audio, networking,
CD audio, joystick, video-mode switching or mission packs. See the ports recipe's
README for the source pin, patch scope and build details, and
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1402) for deferred work.

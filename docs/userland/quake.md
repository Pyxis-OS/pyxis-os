# Quake

The ports repository builds a pinned [quakegeneric](https://github.com/erysdren/quakegeneric),
id Software's GPL WinQuake software renderer behind a small platform interface,
with a Pyxis adapter. The ordinary image includes the executable and the Quake
1.06 shareware data: run `quake` in the application-space shell to play the
first episode with keyboard and mouse. There is no sound, networking or CD audio.

The unchanged shareware `pak0.pak` is pinned under `third_party/quake-shareware`
with its source, checksums and id's license texts. Those texts ship at
`app://share/licenses/quake-shareware/`, separately from the engine's GPL
license. The shareware license permits free distribution of the shareware only;
it grants nothing for the registered or retail data.

## Local game data

Use retail data you own by pointing `QUAKE_DATA` at its `id1` directory when
building or running:

```sh
make run CPUS=4 QUAKE_DATA=/shared/quake/retail
```

`pak0.pak` is required and `pak1.pak` optional, in either letter case. They are
staged as `app://share/quake/id1/pak0.pak` and `pak1.pak`. These are local
inputs, not repository files; a later build without `QUAKE_DATA` restores the
shareware data and its license texts. Normal CI images include only shareware.

In the application-space shell:

```text
quake
quake +map e1m1
quake +timedemo demo1
```

Arguments are Quake's own. `-basedir` replaces `app://share/quake` and
`-writedir` replaces `home://quake`.

## Controls and resources

Quake's default bindings apply: arrows move and turn, Ctrl fires, Space jumps,
Shift runs, Alt strafes, 1–8 select weapons and `/` selects the next one.
Escape opens the menu and backquote the console. Mouse look is on at every
start, so moving the mouse turns and looks up and down; the left button fires
and the right moves forward. On a first run without a saved configuration, the
middle button selects the next weapon rather than toggling mouse look. The
wheel is unbound by default (`bind mwheelup "impulse 10"` binds it).
Super+Left/Right remains space navigation.

Focus loss or an input reset releases every held key and button, including
modifiers shared by left/right keys and mouse buttons. A button held across a
space switch must be pressed again. An inactive session blocks on keyboard input
and its time is excluded from game time.

Quake requires the shell's named display, keyboard and clock grants and uses
the pointer grant when present. Without a pointer grant or a working mouse it
prints a note and plays from the keyboard. It acquires exclusive display,
keyboard and pointer sessions and releases them on quit or a fatal error;
process cleanup also handles a fault. Pointer counts reach Quake unscaled, so
Quake's `sensitivity` setting is the only scale.

Rendering uses a 320x240 game buffer scaled by the largest integer that fits
the content area, with black borders, converting the 8-bit palette to the
display's channel shifts. Presentation keeps the single-buffer contract, so
tearing is possible. Outside `timedemo`, the loop sleeps to Quake's 72 Hz frame
cap. The video mode is fixed at 320x240.

## Saves and configuration

Generated files go to `home://quake/id1/`, created on first start: `config.cfg`
(written on quit), saves (`save NAME`, F6 quicksave, the Save and Load menus),
demos and screenshots. That directory is searched before the game data. Shareware
and retail data share it. Like all of `home://`, it survives quitting but not
reboot.

## Timedemo

`timedemo demo1` replays the first demo as fast as possible and prints frames,
seconds and frames per second to the console and the shell's terminal.

| Configuration | Samples (fps) | Median |
| --- | --- | --- |
| QEMU 10.2.2/KVM, 4 CPUs, nested VM, development host, 2026-10-04 | 1617.0, 1653.0, 1622.7, 1351.3, 1588.1, 1592.1, 1598.2 | 1598.2 |

That run booted the ISO as a read-only virtio disk, because this host's QEMU
crashed reading it through AHCI. Native ThinkPad results belong here once the
owner records them.

## Boundaries

This is single-player play, save/load, demos and timedemo. No audio, networking,
CD audio, joystick, video-mode switching or mission packs. See the ports recipe's
README for the source pin, patch scope and build details, and
[technical debt](../technical-debt.md#quake-port-limits) for deferred work.

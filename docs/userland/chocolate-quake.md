# Chocolate Quake

[Chocolate Quake](https://github.com/Henrique194/chocolate-quake) 2.1.0 runs
Quake through the [SDL2 port](../development/sdl2.md), beside the native
[Quake](quake.md). It plays the same staged data, without sound, music or
network play. Ordinary images include it.

In an application space:

```text
chocolate-quake
chocolate-quake +map e1m1
chocolate-quake +timedemo demo1
```

- **Data:** it reads `boot://share/quake`, where the image stages the
  shareware pak, or retail data from `QUAKE_DATA`, for both Quake programs.
  `-basedir PATH` reads another directory.
- **Configuration and saves:** `home://chocolate-quake/id1/`, created on first
  run: `config.cfg` (written on quit), saves, demos, screenshots and the
  `-condebug` log. It is searched before the data. `-writedir PATH` writes
  under `PATH` instead. Files are written in place, as upstream does. That is
  RAM on live boots, so it lasts until reboot; installed systems keep it on
  the pool.
- **Grants:** the space's display, keyboard and clock. With the pointer
  grant, the mouse turns through SDL's relative mode, as upstream; without
  it, the game plays from the keyboard.
- **Quitting:** Quit in the menu, or `quit` in the console, shows upstream's
  end screen; any key returns to the shell.
- **Timedemo:** `+timedemo demo1` prints frames, seconds and frames per second
  to the console and the shell's terminal.

## Video

Upstream's default renders 320x200 and SDL stretches it to 4:3, 1024x768 in a
1280x768 content area, with nearest-neighbour scaling. `vid_forcemode 4`
renders native Quake's 320x240, drawn at the same size; there is no integer
scaling. `vid_describemodes` lists the modes.

Frames reach the display as whole [submitted frames](../interfaces/graphics.md#slots-and-frame-handoff).
The [comparison with native Quake](../development/experiments/chocolate-quake/README.md)
measured the SDL path at about 2.4 times native Quake's time per frame in
QEMU.

## Resources

- **Frames:** between 72 Hz frames the game sleeps rather than checking the
  clock in a loop. Timedemos run flat out.
- **Memory:** upstream's 256 MiB heap is committed at startup, so the game
  holds about 288 MiB while running. A QEMU guest with little memory, such as
  512 MiB, needs room for it.

## Boundaries

- **Left out of the build:**
  - sound and music: SDL2 has no audio here, and the Vorbis, MP3 and FLAC
    codecs aren't built;
  - network play: no SDL2_net and no UDP driver. Single player uses the
    loopback driver.
- **Gamepads:** the SDL2 port's joystick driver is SDL's dummy, which finds
  no devices, so only the keyboard and mouse work.

The [recipe README](../../ports/chocolate-quake/README.md) lists the source pin
and patches. Chocolate Quake is distributed under GPL-3; its licence, SDL2's and
the port notice ship in `boot://share/licenses/chocolate-quake/`.

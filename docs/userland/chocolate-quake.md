# Chocolate Quake

[Chocolate Quake](https://github.com/Henrique194/chocolate-quake) 2.1.0 runs
Quake through the [SDL2 port](../development/sdl2.md), beside the native
[Quake](quake.md). It plays the same packaged data, with sound effects but
without music or network play. The `quake` game package carries it as the
program bundle `chocolate-quake.pxb`; typing `chocolate-quake` selects it.

Configure a network repository with
[`pkg`](../interfaces/packages.md#package-client), then in an application space:

```text
pkg install quake
chocolate-quake
chocolate-quake +map e1m1
chocolate-quake +timedemo demo1
```

- **Data:** its own `quake` package, read-only, supplies `pkg://quake`,
  shared with native Quake. `-basedir PATH` still reads another directory; a
  missing package refuses launch even with that option. Live installs last
  until reboot; live and installed launches have no archive fallback or local
  seed repository.
- **Configuration and saves:** `home://chocolate-quake/id1/`, created on first
  run: `config.cfg` (written on quit), saves, demos, screenshots and the
  `-condebug` log. It is searched before the data. `-writedir PATH` writes
  under `PATH` instead. Files are written in place, as upstream does. That is
  RAM on live boots, so it lasts until reboot; installed systems keep it on
  the pool.
- **Grants:** the bundle [manifest](../../ports/chocolate-quake/manifest.json)
  declares them. Required: memory, clock, display, keyboard and the read-only data
  package. Optional: the
  pointer, so the mouse turns through SDL's relative mode as upstream (without
  it the game plays from the keyboard), and for sound the space's audio
  session, threads for SDL's audio callback thread and pipe creation for SDL's
  event wakeup.
- **Sound:** upstream's SDL sound driver plays through the SDL2 port's
  [audio](../development/sdl2.md#audio) at 44.1 kHz, converted by SDL to the
  native 48 kHz. `-nosound` turns it off. When the optional sound grants are
  absent, the console reports "Couldn't open SDL audio: Couldn't create audio thread"
  and the game runs silent. One process per space can own the audio session,
  so a second sound program in the same space plays silently. The
  [sound qualification](../development/experiments/chocolate-quake-sound/README.md)
  records nested-QEMU timings and the native run sheet.
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
  - music: the Vorbis, MP3 and FLAC codecs aren't built, and there is no CD
    audio;
  - network play: no SDL2_net and no UDP driver. Single player uses the
    loopback driver.
- **Gamepads:** the SDL2 port's joystick driver is SDL's dummy, which finds
  no devices, so only the keyboard and mouse work.

The [recipe README](../../ports/chocolate-quake/README.md) lists the source pin
and patches. Chocolate Quake is distributed under GPL-3; its licence, SDL2's and
the port notice ship in `pkg://quake/licenses/chocolate-quake/`.

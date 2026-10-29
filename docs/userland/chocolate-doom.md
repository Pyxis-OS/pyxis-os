# Chocolate Doom

[Chocolate Doom](https://www.chocolate-doom.org/) 3.1.1 runs Doom through the
[SDL2 port](../development/sdl2.md), beside the native [Doom](doom.md). It
plays the same packaged IWAD with digital effects and its own OPL music through
SDL2_mixer. Network play remains unavailable. The `doom`
game package carries the `chocolate-doom.pxb` bundle. Configure a network
repository with [`pkg`](../interfaces/packages.md#package-client) before
installing it.

In an application space:

```text
pkg install doom
chocolate-doom
chocolate-doom -warp 1 1
chocolate-doom -timedemo demo1
```

- **Data:** its own `doom` package, read-only, supplies `pkg://doom`, shared
  with native Doom. Without `-iwad`, the game searches that root. A
  missing package refuses launch, even with an explicit `-iwad`. Live installs
  last until reboot; live and installed launches have no archive fallback or
  local seed repository.
- **Configuration and saves:** `home://chocolate-doom/`, created on first run.
  Configuration is written on a normal quit. That is RAM on live boots, so it
  lasts until reboot; installed systems keep it on the pool.
- **Grants:** the bundle declares memory, display, keyboard, clock and its
  required read-only data package. The pointer is
  optional. Audio creation and thread management are optional; when supplied,
  SDL2_mixer uses the native PCM output. With the pointer, the game locks the pointer and the mouse turns, as
  upstream; without it, the game plays from the keyboard.
- **Quitting:** Quit Game in the menu shows upstream's ENDOOM screen; any key
  returns to the shell. With the terminal shown, Ctrl+C ends the game.
- **Timedemo:** `-timedemo` reports `timed N gametics in M realtics (F fps)` and
  exits with status -1, as upstream does.

## Sound

The default `snd_sfxdevice 3` and `snd_musicdevice 3` select digital effects
and Doom's own OPL synthesizer. The Options menu controls their volumes
independently. `-nosound`, `-nosfx` and `-nomusic` retain upstream behavior.
The [SDL2_mixer profile](../../ports/sdl2-mixer/README.md) supports WAV streaming
and WAV/AIFF/VOC samples; compressed music packs and external MIDI players are
not part of this build. No sound fonts are required for OPL.

## Video

The 320x200 frame is drawn 4:3 into the content area, 1024x768 in a 1280x768
area, with one nearest-neighbour stretch: `force_software_renderer` defaults
to on. SDL's only renderer here is its software one.
- **Two-stage scaling:** upstream's default renders an integer upscale and then
  a linear pass, both on the CPU. In QEMU that cost about 1.5 ms more per
  frame; set `force_software_renderer 0` in the configuration to use it.
- **Native Doom's picture:** `integer_scaling 1` with `aspect_ratio_correct 0`
  gives the same 960x600 picture as native Doom.

Frames reach the display as whole [submitted frames](../interfaces/graphics.md#slots-and-frame-handoff).
The [comparison with native Doom](../development/experiments/chocolate-doom/README.md)
measured the SDL path at about 2.5 times native Doom's time per frame in QEMU.

## Boundaries

- **Left out of the build:**
  - SDL MIDI/TiMidity and GUS music: the selected SDL2_mixer has no MIDI
    decoder. Doom's own OPL synthesizer remains available;
  - network play: no SDL2_net;
  - PNG screenshots, which are PCX instead;
  - `chocolate-setup`, and the Heretic, Hexen and Strife programs.
- **File and URL helpers:** textscreen's file picker reports itself
  unavailable, and help URLs are printed rather than opened.

The [recipe README](../../ports/chocolate-doom/README.md) lists the source pin
and patches. Chocolate Doom is GPL-2.0-or-later; its licence, SDL2's and the
port notice ship in `pkg://doom/licenses/chocolate-doom/`.

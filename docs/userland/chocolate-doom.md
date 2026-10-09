# Chocolate Doom

[Chocolate Doom](https://www.chocolate-doom.org/) 3.1.1 runs Doom through the
[SDL2 port](../development/sdl2.md), beside the native [Doom](doom.md). It
plays the same staged IWAD, without sound, music or network play. Ordinary
images include it.

In an application space:

```text
chocolate-doom
chocolate-doom -warp 1 1
chocolate-doom -timedemo demo1
```

- **Data:** with no `-iwad`, it finds `boot://share/doom/DOOM.WAD`, the IWAD
  the image stages for both Doom programs.
- **Configuration and saves:** `home://chocolate-doom/`, created on first run.
  Configuration is written on a normal quit. That is RAM on live boots, so it
  lasts until reboot; installed systems keep it on the pool.
- **Grants:** the space's display, keyboard and clock. The pointer is
  optional. With it, the game locks the pointer and the mouse turns, as
  upstream; without it, the game plays from the keyboard.
- **Quitting:** Quit Game in the menu shows upstream's ENDOOM screen; any key
  returns to the shell. With the terminal shown, Ctrl+C ends the game.
- **Timedemo:** `-timedemo` reports `timed N gametics in M realtics (F fps)` and
  exits with status -1, as upstream does.

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
  - sound and music: no SDL2_mixer, and the SDL2 port has no audio;
  - network play: no SDL2_net;
  - PNG screenshots, which are PCX instead;
  - `chocolate-setup`, and the Heretic, Hexen and Strife programs.
- **File and URL helpers:** textscreen's file picker reports itself
  unavailable, and help URLs are printed rather than opened.

The [recipe README](../../ports/chocolate-doom/README.md) lists the source pin
and patches. Chocolate Doom is GPL-2.0-or-later; its licence, SDL2's and the
port notice ship in `boot://share/licenses/chocolate-doom/`.

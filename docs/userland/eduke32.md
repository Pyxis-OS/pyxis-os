# EDuke32

[EDuke32](https://voidpoint.io/terminx/eduke32) runs Duke Nukem 3D through the
[SDL2 port](../development/ports.md#sdl2-development-library) with its classic
software renderer: single player, keyboard and mouse, with sound effects and
music but without network play. It is an opt-in build that needs your own
`duke3d.grp`, and runs as the program bundle `eduke32.pxb`; typing `eduke32`
selects it.

## Personal use only

EDuke32's game code is under the GPL, and its Build engine is under Ken
Silverman's Build licence, which allows free distribution only and is widely
treated as incompatible with the GPL. Together with the retail data, that
makes it a personal build, handled like [DevilutionX](devilutionx.md#personal-use-only):

- an image containing EDuke32 is for the person who built it and must not be
  shared, for example as a published PXE or installation image;
- ordinary images and CI never contain it;
- [build bundles](../development/build-bundles.md) never pack it.

The licences, `buildlic.txt` and a notice ship at
`boot://share/licenses/eduke32/`.

## Building it in

The existing optional image-build gate still requires `DUKE3D_DATA` to name
an owner-local directory containing the game's data. It builds the
[EDuke32 recipe](../../ports/eduke32/README.md) and includes `eduke32.pxb`.
A later build without that gate leaves the program out.

The image builder still accepts and stages that legacy data input until the
[archive cut](../wip/packages.md#tasks). It does not provide the runtime package
root: install `duke3d-data` separately. No game data belongs in this repository.

## Playing

Configure a network repository with
[`pkg`](../interfaces/packages.md#package-client), then in an application space:

```text
pkg install duke3d-data
eduke32
eduke32 -v1 -l1 -s2
```

The second form starts episode 1, level 1 at skill 2, skipping the menus.

- **Data:** the required read-only `duke3d-data` package supplies
  `duke3d_data://`. A missing package refuses launch; explicit data arguments
  keep their upstream meaning after admission. Live installs last until reboot;
  live and installed launches have no archive fallback or local seed repository.
- **Configuration, saves and screenshots:** `home://eduke32/`, created on
  first run and searched before the data: `eduke32.cfg`, `settings.cfg`, the
  `.esv` saves, `eduke32.log`, the data checksum cache and `screenshots/`.
  That is RAM on live boots, so it lasts until reboot; installed systems keep
  it on the pool.
- **Grants:** the bundle [manifest](../../ports/eduke32/manifest.json)
  declares them. Required: memory, clock, display, keyboard and the read-only data
  package. Optional: the
  pointer, so the mouse turns and fires, and for sound the space's audio
  session, threads for SDL's audio callback thread, pipe creation for SDL's
  event wakeup, and the local and shared clipboard for the console's
  right-click paste, which on Pyxis also needs a fresh Ctrl+V.
- **Sound:** upstream's SDL sound driver plays effects and music through the
  SDL2 port's [audio](../development/sdl2.md#audio) at the native 48 kHz; music
  is upstream's default AdLib OPL3 emulation. `-ns` turns sound off and `-nm`
  music, while the device stays open. When the optional sound grants are absent, the log reports "Failed initializing sound: SDL Audio: error
  in OpenAudio" and the game runs silent. One process per space can own the
  audio session. The [sound qualification](../development/experiments/eduke32-sound/README.md)
  records nested-QEMU timings and the native run sheet.
- **Controls:** upstream's defaults: W and S (or keypad 8 and 2) move, Left
  and Right turn, and the left mouse button fires. In menus, the item under the
  system pointer is selected, so move the pointer aside to use the keyboard.
- **Quitting:** Quit in the main menu, or `quit` in the console (the key under
  Escape).

## Video and frames

The game renders 8-bit frames at the full content area, 1280x768 in QEMU's
1280x800 display, and SDL converts them into the display slot. Frames reach
the display as whole [submitted frames](../interfaces/graphics.md#slots-and-frame-handoff).

Between frames the game sleeps rather than checking the clock in a loop.
Pyxis displays report no refresh rate, so the default frame limit
(`r_maxfps -1`, matching the display) paces at 60 Hz. `r_maxfps 0` removes the
limit.

## Resources

Measured in QEMU with KVM in the nested development VM, 4 CPUs, standard VGA,
on 2026-10-09:

- **Memory:** the system's allocated memory rose by about 158 MiB while the
  game ran. Most of it is upstream's 96 MiB data cache, committed at startup.
- **CPU, standing at the start of E1L1:** the whole QEMU process used about
  1,400 host ticks per 20 s, against about 2,700 for a local build without
  the frame sleep and about 310 with only the shell running.
- **Frame rate:** 58.9 fps at the default limit; about 234 fps with
  `r_maxfps 0`.
- **Level loading:** caching E1L1's art took about 5.5 s.

## Boundaries

- **Left out of the build:** OpenGL and Polymer, the Vorbis, FLAC and XMP
  codecs, network play, libvpx and the startup window. Mapster32, the editor,
  isn't built.
- **Gamepads:** the SDL2 port's joystick driver finds no devices.

The [recipe README](../../ports/eduke32/README.md) lists the source pin,
patches and the libc gaps the port works around.

# EDuke32

[EDuke32](https://voidpoint.io/terminx/eduke32) runs Duke Nukem 3D through the
[SDL2 port](../development/ports.md#sdl2-development-library) with its classic
software renderer: single player, keyboard and mouse, without sound or
network play. It is an opt-in build that needs your own `duke3d.grp`.

## Personal use only

EDuke32's game code is under the GPL, and its Build engine is under Ken
Silverman's Build licence, which allows free distribution only and is widely
treated as incompatible with the GPL. Together with the retail data, that
makes it a personal build, handled like [DevilutionX](devilutionx.md#personal-use-only):

- an image containing EDuke32 is for the person who built it and must not be
  shared, for example as a published PXE or installation image;
- ordinary images and CI never contain it;
- bundles never pack it.

The licences, `buildlic.txt` and a notice ship at
`boot://share/licenses/eduke32/`.

## Building it in

Give `DUKE3D_DATA` a directory containing `duke3d.grp` (either letter case),
such as the Atomic Edition's or the Megaton Edition's `gameroot`:

```sh
make -j16 image DUKE3D_DATA="/path/to/gameroot"
make run CPUS=4 DUKE3D_DATA="/path/to/gameroot"
```

The build then:

- builds the [EDuke32 recipe](../../ports/eduke32/README.md);
- stages the game as `eduke32.pxe`;
- stages only `duke3d.grp`, as `boot://share/duke3d/duke3d.grp`. Other files
  in the directory, such as the Megaton Edition's high-resolution packs, are
  for renderers this build leaves out.

The image grows by about 48 MB: the 44 MB `duke3d.grp` and a 3.8 MB
executable. A later build without `DUKE3D_DATA` leaves both out. The data is a
local input and never enters a repository.

## Playing

In an application space:

```text
eduke32
eduke32 -v1 -l1 -s2
```

The second form starts episode 1, level 1 at skill 2, skipping the menus.

- **Data:** read from `boot://share/duke3d/`.
- **Configuration, saves and screenshots:** `home://eduke32/`, created on
  first run and searched before the data: `eduke32.cfg`, `settings.cfg`, the
  `.esv` saves, `eduke32.log`, the data checksum cache and `screenshots/`.
  That is RAM on live boots, so it lasts until reboot; installed systems keep
  it on the pool.
- **Grants:** the space's display, keyboard and clock. With the pointer
  grant, the mouse turns and fires.
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
- **Sound:** SDL2 has no audio here, so sound startup fails and the game
  plays silently.
- **Gamepads:** the SDL2 port's joystick driver finds no devices.

The [recipe README](../../ports/eduke32/README.md) lists the source pin,
patches and the libc gaps the port works around.

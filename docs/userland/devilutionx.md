# DevilutionX

[DevilutionX](https://github.com/diasurgical/DevilutionX) 1.5.5 runs Diablo on
Pyxis through the [SDL2 port](../development/ports.md#sdl2-development-library):
single player, keyboard and mouse, without sound or networking. It is an
opt-in build that needs your own copy of the shareware data.

## Personal use only

DevilutionX is under the non-commercial Sustainable Use License, and it links
libmpq, which is under the GPL. Nobody who distributes the combined program
can meet both licences. So:

- an image containing DevilutionX is for the person who built it and must not
  be shared, for example as a published PXE or installation image;
- ordinary images and CI never contain it;
- bundles never pack it.

The licences of everything linked in ship at
`boot://share/licenses/devilutionx/`, with a notice explaining this.

## Building it in

Give `DIABLO_DATA` a directory containing the Diablo shareware `spawn.mpq`
(either letter case):

```sh
make -j16 image DIABLO_DATA=/path/to/diablo-shareware
make run CPUS=4 DIABLO_DATA=/path/to/diablo-shareware
```

The build then:

- builds the [DevilutionX recipe](../../ports/devilutionx/README.md);
- stages the game as `devilutionx.pxe` and its assets in
  `boot://share/devilutionx/`;
- stages `spawn.mpq` as `boot://share/diablo/spawn.mpq`.

The image grows by about 35 MB: the 25 MB `spawn.mpq`, a 3.4 MB executable and
5.7 MB of assets. A later build without `DIABLO_DATA` leaves all of it out.
`spawn.mpq` is a local input and never enters a repository.

Retail data is not staged. `DIABDAT.MPQ` is about 500 MB, which would stay in
RAM with the boot archive and does not fit the 512 MiB ESP an install copies
the archive to.

## Playing

In an application space, run `devilutionx`. It needs the space's display,
keyboard and clock grants; the pointer is optional, and without it the game
plays from the keyboard.

- **Data** is read from `boot://share/diablo/`.
- **Saves and `diablo.ini`** go to `home://devilution/`. That is RAM on live
  boots, so they are lost on reboot; installed systems keep them on the pool.

Super+Left/Right and Super+Up/Down work as for other graphical programs. With
the terminal shown, Ctrl+C ends the game. Its in-game Quit Game exits the
program.

### Retail data and Hellfire

To play retail data, point `--data-dir` at the directory holding
`DIABDAT.MPQ`, for example from [`host://`](../devices/virtio-fs.md) in QEMU or
from a directory on an installed system's pool. An installed system with an
ordinary image can run a [standalone bundle](#standalone-bundle) instead.

```text
devilutionx --data-dir host://
```

DevilutionX also searches the working directory. Starting it from a directory
that holds Hellfire's `hellfire.mpq` and `hfmonk.mpq`, such as GOG's
`hellfire/` folder, offers Hellfire:

```text
cd host://hellfire
devilutionx --data-dir host://
```

Retail Diablo and Hellfire both played into Tristram this way with the GOG
release, exported read-only. Hellfire's menus, intro videos and Monk class
worked.

## Standalone bundle

An ordinary image can also run DevilutionX from a directory of its own, for
example on an installed system. Such a bundle is personal-use only, like an
image containing the game, and must not be shared.

Build the port once with `DIABLO_DATA` as above. Then copy these files from
`build/ports/devilutionx/stage` into one directory, here `diablo/`:

| From the stage | Into the bundle |
| --- | --- |
| `bin/devilutionx.pxe` | `diablo/devilutionx.pxe` |
| `share/devilutionx/assets/` | `diablo/assets/` |
| `share/licenses/devilutionx/` | `diablo/licenses/`, which must travel with the program |
| `share/devilutionx/source.txt` | `diablo/source.txt` |

Add the game data, such as `DIABDAT.MPQ` or `spawn.mpq`, to the same
directory, then run it with that directory as the data directory:

```text
home://diablo/devilutionx.pxe --data-dir home://diablo
```

Pyxis programs cannot find their own directory, so the game looks for its
assets in `boot://share/devilutionx/assets/` first. When the image has none,
it uses `assets/` inside the data directory. An image that already includes
DevilutionX therefore uses its own assets. Saves and `diablo.ini` still go
to `home://devilution/`.

A bundle can be packed on the host with `tar --format=ustar -cf
devilutionx.tar diablo` and extracted in Pyxis with `tar xf` from `home://`.
`tar` holds the whole archive in memory.

Getting the data there is the hard part.
[Remote transfers](remote-terminal.md#explicit-file-transfer) and HTTP(S)
bodies are limited to 16 MiB per file. The bundle without data, about 9 MB,
fits in one transfer. The MPQ files do not: `spawn.mpq` is 25 MB and
`DIABDAT.MPQ` about 500 MB. They can be split on the host into pieces of at
most 15 MiB, sent one by one, and joined in Pyxis with `cat`:

```text
cat home://DIABDAT.MPQ.00 home://DIABDAT.MPQ.01 > home://diablo/DIABDAT.MPQ
```

The join was checked with `spawn.mpq` in QEMU. For `DIABDAT.MPQ` it means
about 35 transfers.

## Settings and display

- **Frame rate.** Pyxis presents without vertical sync, so the frame rate
  defaults to "Limit FPS". The "Vertical Sync" choice does not limit anything
  here, and "None" renders as fast as the CPU allows.
- **Resolution.** The game fills the display at its native resolution and
  follows a [VirtIO display resize](../kernel/display.md).
- **Cursor.** The hardware cursor option is unavailable, so the game draws its
  own cursor.

### Mouse paths that wait for the system pointer

The [system pointer proposal](../wip/pointer.md) adds kernel-owned pointer
positions, warping and a hidden-cursor state. The SDL2 backend adopts them
after this milestone. Until then:

- **`SetCursorPos`.** DevilutionX's version calls `SDL_WarpMouseInWindow`, for
  example when keyboard or controller navigation moves the cursor. That moves
  only SDL's copy of the position, which the backend keeps from relative
  counts.
- **`SDL_ShowCursor`.** There is no system cursor to show or hide; the game
  draws its own.
- **Hardware cursor.** Reported unsupported. It needs custom system cursors.

## Measurements

Measured on 2026-10-08 on a 1280x800 standard VGA display (1280x768 content
area), in QEMU 10.2.2 with nested KVM and 4 CPUs, in town with the shareware
data. Five samples of the in-game counter, two seconds apart:

| Frame rate control | FPS |
| --- | --- |
| Limit FPS, the Pyxis default | 58.3, 60.8, 56.4, 59.8, 60.8 |
| Vertical Sync, which renders uncapped here | 419, 419, 427, 420, 423 |

Natively, on 2026-10-08, the owner played the shareware on the ThinkPad (PXE
boot of main `4332801`, 1920x1080 internal display with a 1920x1040 content
area, on AC). With "Limit FPS" the counter showed 59–65 FPS, mostly 60–62.
Keyboard, touchpad and TrackPoint worked, including key repeat in name entry.

## Limits

- **Not supported:** sound, multiplayer, game controllers, translations (the
  build host has no gettext) and Hellfire's music and voice.
- **Text input** uses the US layout.
- **Not yet checked natively:** a standalone bundle with retail data on an
  installed system.

[Technical debt](../technical-debt.md#devilutionx-port-limits) records these
limits and the [native check](../technical-debt.md#sdl2-and-devilutionx-native-qualification).

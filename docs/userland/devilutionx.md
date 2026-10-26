# DevilutionX

[DevilutionX](https://github.com/diasurgical/DevilutionX) 1.5.5 runs Diablo on
Pyxis through the [SDL2 port](../development/ports.md#sdl2-development-library):
single player, keyboard and mouse, with sound but without networking. It is an
opt-in build that needs your own copy of the shareware data, and runs as the
program bundle `devilutionx.pxb`; typing `devilutionx` selects it.

## Personal use only

DevilutionX is under the non-commercial Sustainable Use License, and it links
libmpq, which is under the GPL. Nobody who distributes the combined program
can meet both licences. So:

- the personal `diablo` package, which carries DevilutionX and the owner's
  data, is for the person who built it and must not be shared, for example in a
  public package repository;
- images, CI and the public repository never contain it;
- [build bundles](../development/build-bundles.md) never pack it.

The licences of everything linked in ship in the package under
`licenses/devilutionx/`, with a notice explaining this.

## Packing it

`make game-packages GAMES=diablo GAME_RECIPES=/private REPOSITORY=/private/repository`
builds the [DevilutionX recipe](../../ports/devilutionx/README.md) and packs the
owner's private `diablo.lua` recipe, which selects `devilutionx.pxb`, its notices
and the owner's data, as described in the
[package inputs](../../packages/README.md). The same package is used on live and
installed systems; no game data belongs in this repository.

## Playing

Configure a network repository with [`pkg`](../interfaces/packages.md#package-client),
then in an application space:

```text
pkg install diablo
devilutionx
```

Live and installed systems use the same declared package root. Live installs
last until reboot; there is no archive fallback or local seed repository.
The bundle [manifest](../../ports/devilutionx/manifest.json) requires memory, clock,
display, keyboard and its own `diablo` package, read-only. The pointer is
optional; without it the game plays from the keyboard. Sound needs the optional audio, threads and pipe grants, and the
text editor's Copy and Paste the optional local and shared clipboard.

- **Sound** plays through SDL_audiolib and the SDL2 port's
  [audio](../development/sdl2.md#audio). Without the sound grants, the terminal shows "Failed to initialize
  audio (Aulib::init): Couldn't create audio thread" and the game runs silent.
  One process per space can own the audio session. The
  [sound qualification](../development/experiments/devilutionx-sound/README.md)
  records nested-QEMU results and the native run sheet.

- **Data** is read from `pkg://diablo`. A missing `diablo` package
  refuses launch, including with an explicit `--data-dir`. Program assets come
  only from the selected bundle's `app/assets/` through `devilution_assets://`.
- **Saves and `diablo.ini`** go to `home://devilution/`. That is RAM on live
  boots, so they are lost on reboot; installed systems keep them on the pool.

Super+Left/Right and Super+Up/Down work as for other graphical programs. With
the terminal shown, Ctrl+C ends the game. Its in-game Quit Game exits the
program.

### Retail data and Hellfire

To play retail data, point `--data-dir` at the directory holding
`DIABDAT.MPQ`, for example from [`host://`](../devices/virtio-fs.md) in QEMU or
from a directory on an installed system's pool. The owner's personal `diablo`
package can instead carry the retail data itself.

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

Before the package-root migration, retail Diablo and Hellfire both played
into Tristram this way with the GOG release, exported read-only. Hellfire's
menus, intro videos and Monk class worked.

## Settings and display

- **Frame rate.** Pyxis presents without vertical sync, so the frame rate
  defaults to "Limit FPS". The "Vertical Sync" choice does not limit anything
  here, and "None" renders as fast as the CPU allows.
- **Resolution.** The game fills the display at its native resolution and
  follows a [VirtIO display resize](../kernel/display.md).
- **Cursor.** The "Hardware Cursor" option selects the game's supplied SDL
  cursor as the native Pyxis surface image. The kernel currently composes that
  image in software; a VirtIO hardware cursor is still future work. Disabling
  the option keeps the game's own software cursor and hides the system cursor.

### Mouse position and cursor images

The [SDL2 backend](../development/sdl2.md#the-backend) uses kernel-owned pointer
positions and a graphics-owner subscription. Ordinary motion is surface-local;
buttons and wheel continue while the game hides the system cursor. SDL acquires
the pointer after graphics backing exists and releases it before graphics
teardown.

- **`SetCursorPos`.** DevilutionX's version calls `SDL_WarpMouseInWindow`, for
  example during keyboard inventory navigation. The backend requests native
  warp within the shown mapping/destination intersection using current geometry
  identities. A successful request changes the authoritative position; a
  refusal reports an SDL error and leaves it unchanged.
- **`SDL_ShowCursor`.** Changes saved native visibility. The game's software
  cursor therefore draws without a second system cursor above it.
- **Program images.** SDL accepts straight-alpha BGRA images 1 through 64 pixels
  per dimension after game scaling. An oversized color cursor is refused;
  upstream logs the refusal and draws its own software fallback.

The [joint pointer qualification](../development/system-pointer-qualification.md#task-2-and-joint-integration)
checked the game's supplied 33x28 cursor with 86 intermediate alpha levels,
ordinary motion/clicks with its software cursor selected, and native inventory
warp. The game's option name describes its SDL cursor path; genuine VirtIO
hardware-cursor submission belongs to the later pointer task.

## Measurements

These runs predate the package-root migration. Measured on 2026-10-08 on a 1280x800 standard VGA display (1280x768 content
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

- **Not supported:** multiplayer, game controllers and translations (the
  build host has no gettext). Hellfire's music and voice are unchecked.
- **Text input** uses the US layout.
- **Not yet checked natively:** a standalone bundle with retail data on an
  installed system.

[Technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1398) records these
limits and the [native check](https://git.internal/PyxisOS/pyxis-os/issues/1399).

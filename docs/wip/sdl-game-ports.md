# SDL game ports

Proposal, 2026-10-09; the owner's three decisions below are open. Three games
through the [SDL2 port](../development/sdl2.md), smallest first, each compared
with what Pyxis already has:
1. **Chocolate Doom,** against the native [Doom](../userland/doom.md).
2. **Chocolate Quake,** against the native [Quake](../userland/quake.md).
3. **EDuke32,** with its classic software renderer only.

The comparisons isolate the cost of the SDL2 path, including the
[frame SUBMIT](../interfaces/graphics.md#slots-and-frame-handoff).

**Accepted by the owner** (2026-10-09): no audio for any of the three games.
They are built video- and input-only, without SDL2_mixer or audio libraries
wherever the build allows. Sound and music become a separate, later plan after
[volume control](audio-volume.md). Today's SDL2 port has no audio either:
`SDL_INIT_AUDIO` fails, and SDL's audio needs threads the port doesn't have.

## What each game needs

Found by reading upstream; nothing has been built yet.

| | Chocolate Doom | Chocolate Quake | EDuke32 |
| --- | --- | --- | --- |
| **Pin** | 3.1.1, the current release | tag `chocolate-quake-2.1.0` (`8ee22175`); main is 14 commits ahead | a master commit; upstream has no release tags |
| **Licence** | GPL-2 | GPL-3 | GPL-2 game code with the Build engine under BUILDLIC, a non-commercial licence |
| **Build** | CMake, C | CMake, C99 | GNU Make, C and C++14 |
| **Audio and network** | `ENABLE_SDL2_MIXER` and `ENABLE_SDL2_NET` switch both off | SDL2_net, Vorbis, libmad and FLAC are all REQUIRED, with no switches | Make switches for Vorbis, FLAC, XMP, network, libvpx and mimalloc |
| **Data** | the staged Doom IWAD | the staged Quake data | the owner's retail `duke3d.grp`, 44 MB |

- **Chocolate Doom:**
  - **Audio and network:** with SDL2_mixer and SDL2_net off, upstream defines
    `DISABLE_SDL2MIXER` and `DISABLE_SDL2NET` and builds without sound,
    music or network play. Porting the two libraries isn't needed for this
    milestone.
  - **Timer:** it calls `SDL_Init(SDL_INIT_TIMER)` and ignores the result, so
    our failing timer subsystem is harmless.
  - **Presentation:** it expands the 8-bit frame into a streaming texture.
    With a hardware renderer it adds a two-stage scale: an integer upscale
    into a render target, then a linear pass to the window. Our only
    renderer is SDL's software one, so both stages run on the CPU (decision
    3).
- **Chocolate Quake:** a silent, local-only build needs a local patch:
  - leave out the music codecs (`snd_flac.c`, `snd_mp3*.c`, `snd_vorbis.c`) and
    their libraries;
  - drop the UDP driver, keeping loopback, which single player uses;
  - let sound initialization fail as it does with `-nosound`.
- **EDuke32:**
  - **Switches:** builds with `USE_OPENGL=0 POLYMER=0 USE_LIBVPX=0
    HAVE_VORBIS=0 HAVE_FLAC=0 HAVE_XMP=0 NETCODE=0 USE_MIMALLOC=0
    STARTUP_WINDOW=0 HAVE_GTK2=0`.
  - **Platform:** `Common.mak` detects the platform itself, so the recipe
    needs a Pyxis platform patch.
  - **Audio:** its audio library is always compiled; whether it disables
    itself cleanly when SDL audio fails, or needs a patch, is found in the
    task.
  - **Threads:** any use of SDL or C++ threads also surfaces in the task.
- **Licence:** the Build engine allows only free distribution over the
  internet and requires its credits and `BUILDLIC.TXT`. Packagers such as
  OpenBSD treat it as incompatible with the GPL. EDuke32 is therefore a
  personal-use build like [DevilutionX](../userland/devilutionx.md#personal-use-only),
  not only because of its data (decision 1).

## Tasks

Each task is one Pyxis PR with its ports PR, and stops for review. Recipes
follow the [ports conventions](../../ports/README.md): pinned sources with
mirrors, recorded patches and licences, and outputs derived from
`metadata.lua`.

- [ ] **Task 1, Chocolate Doom.**
  - **Recipe:** Chocolate Doom 3.1.1 through the SDK's CMake file, with
    SDL2_mixer and SDL2_net off. It is staged as `chocolate-doom`, reading the
    staged IWAD and keeping its configuration in `home://chocolate-doom/`.
    `chocolate-setup` and the other games built from the same tree (Heretic,
    Hexen, Strife) are left out.
  - **Native Doom timedemo:** native Doom refuses `-timedemo` today, so this
    task adds its timedemo report; it is the comparison's baseline.
  - **Stale text:** the ports READMEs for Doom and Quake still say their
    output is single-buffered and can tear, which the frame handoff changed.
- [ ] **Task 2, Chocolate Quake.** Chocolate Quake 2.1.0 with the local
  patch above, staged as `chocolate-quake`, reading the staged Quake data and
  saving to `home://chocolate-quake/`.
- [ ] **Task 3, EDuke32.** The classic renderer only, through the opt-in data
  handling in decision 1. It is checked by playing E1L1 in QEMU; there is no
  head-to-head. Its demos aren't a stable benchmark across versions.
- [ ] **Closing:** a reference page per game, native steps for the owner, and
  this document rewritten as implemented behaviour.

## Head-to-head comparisons

Both sides run silent and from the same staged data. Our native ports have
no sound; the SDL builds have no audio, and `-nosound` is passed wherever an
engine accepts it.

**Doom:**
- **Command:** `-timedemo demo1` with the shareware IWAD's demo, five runs per
  side.
- **Matched output:** Chocolate Doom set to integer scaling without aspect
  correction draws the same 960x600 picture as native Doom in a 1280x768
  content area.
- **Defaults:** a second set at each side's default scaling shows what a user
  gets.

**Quake:**
- **Command:** `timedemo demo1`, five runs per side.
- **Matched resolution:** Chocolate Quake set to native Quake's 320x240.
- **Defaults:** a second set at Chocolate Quake's defaults.

**Configuration:** QEMU first, on std VGA and VirtIO, in the nested-KVM
development VM: 4 CPUs, 1280x800. The record keeps every run. Native
ThinkPad steps follow for the owner.

**What it isolates:** the gap between each pair is the SDL path, including
8-bit expansion, SDL's software scaling and SUBMIT. The engines differ
somewhat as well, so the record says what the matched settings can't
equalize.

## Owner decisions

1. **How EDuke32 ships.**
   - **Default:** opt-in like DevilutionX.
     - `DUKE3D_DATA=DIR` builds EDuke32 and stages that directory's
       `duke3d.grp` alone, 44 MB, as `boot://share/duke3d/duke3d.grp`.
     - Images built that way are personal use only and never shared. CI,
       ordinary images and bundles never contain EDuke32.
     - The Megaton Edition's other files (high-resolution packs and the
       Polymost definitions) aren't used by the classic renderer and aren't
       staged.
     - Licences and `BUILDLIC.TXT` ship with a notice, as DevilutionX's do.
   - **Alternative:** build the engine into ordinary images without data.
     That relies on the informal understanding that the GPL game code may
     link the Build engine.
2. **Chocolate ports beside the native ones.**
   - **Default:** ordinary images carry `chocolate-doom` and
     `chocolate-quake` beside the native `doom` and `quake`, sharing the same
     staged data. Whether one later replaces the other is decided after the
     comparisons.
   - **Alternative:** opt-in builds only, keeping ordinary images unchanged.
3. **Chocolate Doom's default scaling.**
   - **Default:** upstream's 4:3 aspect correction, drawn with
     `force_software_renderer`: a single nearest-neighbour stretch from
     320x200 to 1024x768 in a 1280x768 area.
   - **Alternative:** upstream's two-stage scaling, which renders a 1280x800
     integer upscale and then a linear pass on the CPU every frame. It looks
     smoother and costs more; task 1 measures both before the default is
     final.
   - **Alternative:** integer scaling without aspect correction, matching
     native Doom's picture.

## Out of scope

- **Sound and music** for all three games, as accepted above. Music then
  needs a choice per game: built-in OPL synthesis for Chocolate Doom and
  EDuke32, CD tracks for Chocolate Quake, or none at first.
- **Network play.**
- **EDuke32's OpenGL renderers.**
- **The other Chocolate Doom games.**
- **Replacing the native ports.**

# OpenArena on Pyxis

The ioquake3 port runs OpenArena 0.8.8 offline through SDL2 and
[Mesa softpipe](../development/mesa.md). It statically links the opengl1 renderer,
uses interpreted QVMs from the unchanged base-game PK3s, and presents through
SDL's real double-buffered GL path. It has no JIT, external networking, native
shared game modules, OpenAL, HTTP downloads or voice chat.

The public-package integration is under qualification. Publication still needs
the preferred asset/QVM source distribution and complete codec notices; the
runtime ZIP alone is insufficient. See the [remaining milestone](../wip/quake3-llvmpipe.md)
and [QEMU record](../development/experiments/ioquake3-softpipe.md).

## Package and input

The package recipe is `packages/openarena.lua`, built explicitly with
`make game-packages GAMES=openarena REPOSITORY=/path/to/repository` from a clean
checkout. It combines the engine bundle, nine baseoa PK3s and notices, with no
foreign executable or missionpack. The bundle reads `pkg://openarena/baseoa`;
configuration, screenshots and saves use `home://ioquake3/baseoa`.

The bundle requires memory, threads, a precise sleeping clock, display and
keyboard grants. Missing required grants fail during launch. Pointer and audio
are optional: keyboard-only play or no sound remains possible. Audio uses the
existing SDL S16LE stereo 48 kHz mixer path; it adds no new device authority.

The logical canvas starts at 320×240. Ordinary `r_mode` settings select larger
canvases, aspect-fit into the native content area. The engine uses the back
buffer and swap; there is no front-buffer workaround. Relative mouse capture
requires the existing physical activation. Super+Esc releases capture, and a
fresh click reacquires it after returning to the space. In-game console engine
commands use a leading `/`, for example `/quit` or `/r_mode 3` followed by
`/vid_restart` for 640×480.

## Sources and limits

ioquake3 is pinned to `87a99d18206e878a20289f05ac3c16005dcae5c3`,
GPL-2.0-or-later, with its patched source and original notices included in the
bundle. Bundled Ogg/Vorbis, zlib and IJG JPEG retain their own licences; SDL,
Mesa and the SDK runtime notices accompany the static link. The data recipe
pins the release ZIP and every PK3 separately. All nine directory inventories
match Debian's reviewed release inventories by name, case, order and CRC.
Most assets permit GPL-2.0-or-later, but some require GPL-2.0-only; the combined
data distribution uses GPL version 2 and preserves inner notices.

Some shipped maps reference sounds or skins absent from the upstream release;
the engine reports those and uses its normal fallback. The port does not fill
in missing assets. Retail Quake 3/Team Arena data is private and unqualified;
no such data is shipped. QEMU software-rendering results do not establish
native performance or suitability for another game.

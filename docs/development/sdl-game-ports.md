# SDL game ports

Three games run through the [SDL2 port](sdl2.md), each compared with what
Pyxis already had. None has sound; the SDL2 port has no audio backend.

| Game | Image | Pin | Licence | Reference | Measurements |
| --- | --- | --- | --- | --- | --- |
| Chocolate Doom | ordinary, beside native `doom` | 3.1.1 | GPL-2 | [Chocolate Doom](../userland/chocolate-doom.md) | [against native Doom](experiments/chocolate-doom/README.md) |
| Chocolate Quake | ordinary, beside native `quake` | 2.1.0 (`8ee22175`) | GPL-3 | [Chocolate Quake](../userland/chocolate-quake.md) | [against native Quake](experiments/chocolate-quake/README.md) |
| EDuke32 | opt-in, `DUKE3D_DATA` | master `ec5824db` | GPL-2 and the Build licence | [EDuke32](../userland/eduke32.md) | [in its reference](../userland/eduke32.md#resources) |

Each recipe pins a mirrored source and records its patches and licences beside
it in `ports/`.

## Behaviour

- **Data:** the Chocolate ports read the data the image already stages for the
  native `doom` and `quake`. EDuke32 is built only when `DUKE3D_DATA` names a
  directory holding the owner's `duke3d.grp`, which is staged alone as
  `boot://share/duke3d/duke3d.grp`.
- **Personal use:** the Build engine's licence and the retail data make
  EDuke32 a personal build, handled like
  [DevilutionX](../userland/devilutionx.md#personal-use-only). Images
  containing it are not shared, and ordinary images, CI and bundles never
  contain it.
- **Picture:** Chocolate Doom keeps upstream's 4:3 aspect correction with one
  nearest-neighbour stretch on the CPU (`force_software_renderer`). Chocolate
  Quake stretches its 320x200 frame to 4:3. EDuke32 renders 8-bit frames at
  the full content area. All three submit whole frames.
- **Frames:** each game sleeps between frames where upstream spins on the
  clock: Chocolate Quake between its 72 Hz frames except in timedemos, and
  EDuke32 in its frame limiter, which paces at 60 Hz because Pyxis displays
  report no refresh rate.
- **Generated files** go under `home://APP/`, written in place as upstream
  does.
- **Memory:** upstream's allocations are kept and committed at startup:
  Chocolate Quake's 256 MiB heap (about 288 MiB in use) and EDuke32's 96 MiB
  cache (about 158 MiB in use).
- **Libc:** missing standard functions were added to libc, with math copied
  from the musl pin. Functions whose behaviour needs a design decision first
  are worked around in the port and listed in its README.
- **Threads:** EDuke32's threaded code (loguru, smmalloc's per-thread cache,
  the audio library's tasks, minicoro) is patched to run in the process's one
  thread.

## Cost of the SDL path

In QEMU (nested KVM, 4 CPUs, 1280x800):
- **Doom:** on the same demo at the same picture, Chocolate Doom ran at about 40% of native Doom's rate, 2.65 ms per
  frame against 1.06 ms. Copying the window surface and its SUBMIT accounted
  for about 0.46 ms.
- **Quake:** on the same demo, Chocolate Quake at native Quake's 320x240 ran at about 42% of its
  rate, about 1.2 ms more per frame.
- **Idle CPU:** with the frame sleep, the whole QEMU process used 686 host
  ticks per 20 s in Chocolate Quake's demo loop, against 2321 without it, and
  about 1,400 standing in EDuke32's E1L1, against about 2,700. The shell alone
  used about 300.

On the ThinkPad (1920x1080, a larger picture than QEMU's), the SDL path cost
about 2.0 ms per frame in Chocolate Quake and about 1.6 ms in Chocolate Doom.
All three games played; Chocolate Quake tore visibly more than native Quake.
The [native qualification](../technical-debt.md#sdl-game-ports-native-qualification)
lists what was observed and what is still unchecked.

## Limits

Remaining work is in technical debt:
- [SDL2 port limits](../technical-debt.md#sdl2-port-limits): no audio, so all
  three games are silent, and no refresh rate;
- [EDuke32 port limits](../technical-debt.md#eduke32-port-limits): the
  single-thread patches and the port-side libc gaps;
- [libc compatibility gaps](../technical-debt.md#libc-compatibility-gaps): short
  reads from files;
- [Quake port limits](../technical-debt.md#quake-port-limits).

Sound and music, network play, EDuke32's OpenGL renderers and whether a
Chocolate port replaces a native one are deferred in
[application ports](../wip/application-ports.md).

# SDL game ports

Three games run through the [SDL2 port](sdl2.md), each compared with what
Pyxis already had. Chocolate Quake and EDuke32 ship as program bundles with sound through the
SDL2 port's [audio output](sdl2.md#audio): Chocolate Quake's effects, and EDuke32's effects and
OPL-emulated music. Chocolate Doom uses SDL2_mixer for digital effects and its
own OPL music; the selected mixer has no SDL MIDI decoder.

| Game | Image | Pin | Licence | Reference | Measurements |
| --- | --- | --- | --- | --- | --- |
| Chocolate Doom | `doom` package, beside native `doom` | 3.1.1 | GPL-2 | [Chocolate Doom](../userland/chocolate-doom.md) | [against native Doom](experiments/chocolate-doom/README.md) |
| Chocolate Quake | `quake` package, beside native `quake` | 2.1.0 (`8ee22175`) | GPL-3 | [Chocolate Quake](../userland/chocolate-quake.md) | [against native Quake](experiments/chocolate-quake/README.md) |
| EDuke32 | personal `duke3d` package | master `ec5824db` | GPL-2 and the Build licence | [EDuke32](../userland/eduke32.md) | [in its reference](../userland/eduke32.md#resources) |

Each recipe pins a mirrored source and records its patches and licences beside
it in `ports/`.

## Behaviour

- **Data:** each game package carries both of its ports and the data, so the
  Chocolate ports read the same `doom` and `quake` packages as native Doom and
  Quake. EDuke32 reads the personal `duke3d` package. The image carries none of
  them; see [package inputs](../../packages/README.md).
- **Personal use:** the Build engine's licence and the retail data make
  EDuke32 a personal build, handled like
  [DevilutionX](../userland/devilutionx.md#personal-use-only). Images
  containing it are not shared, and ordinary images, CI and build bundles never
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
- **Threads:** SDL's audio callback thread mixes beside each game's thread.
  EDuke32 keeps upstream's thread-local state and locks; its patch leaves out
  only loguru's signal and stack-trace support and the audio library's async
  tasks, which no built decoder uses.

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
The [native qualification](https://git.internal/PyxisOS/pyxis-os/issues/1401)
lists what was observed and what is still unchecked.

## Limits

Remaining work is in technical debt:
- [SDL2 port limits](https://git.internal/PyxisOS/pyxis-os/issues/1396): no native refresh-rate query;
- [EDuke32 port limits](https://git.internal/PyxisOS/pyxis-os/issues/1400): the
  port-side libc gaps;
- [libc compatibility gaps](https://git.internal/PyxisOS/pyxis-os/issues/1368): short
  reads from files;
- [Quake port limits](https://git.internal/PyxisOS/pyxis-os/issues/1402).

Additional music formats, network play, EDuke32's OpenGL renderers and whether a
Chocolate port replaces a native one are deferred in
[application ports](../wip/application-ports.md).

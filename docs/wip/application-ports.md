# Application and library port candidates

Status: future candidates from discussion, not an approved sequence or an
implementation assignment. Select one bounded consumer, probe a pinned source
revision, and discuss missing contracts before starting each milestone. Keep
ports in userspace and adapt their platform interfaces without reshaping the
kernel around an individual application.
Every candidate follows the [ports and native commands](../development/ports.md#ports-and-native-commands)
boundary: ports are leaf tools, and nothing that defines Pyxis is ported.

See the [port workflow](../development/ports.md) and
[edit/build/run loop](../development/edit-build-run.md) for existing ports. The
next candidates should improve daily use or exercise a reusable OS facility.
[Verified HTTPS](../userland/https.md) already uses the packaged Mbed TLS and
TF-PSA-Crypto libraries; SSH remains deferred.

## Libraries and terminal tools

| Candidate | Intended use and initial investigation |
| --- | --- |
| POSIX regex | Implemented in [libc](../userland/libc-portability.md#regular-expressions-and-utf-8-conversion), with BRE/ERE, UTF-8 decoding and ASCII-only classes/folding. vi, less and grep use it; sed and awk remain later candidates. |
| fastfetch | [Implemented native port](../userland/fastfetch.md), packaged in the normal image with native system information, the Pyxis ASCII logo, text/JSON and explicit JSONC configuration. |
| zlib | [Implemented development library](../development/ports.md#zlib-development-library), first consumed by [screenshot PNG output](../userland/screenshot.md). Jar files and BusyBox `gzip`/`unzip` remain later consumers. |
| libpng | [Implemented development library](../development/ports.md#libpng-development-library), with explicit zlib input and screenshot RGB8 write qualification; target decoding, viewers and drawing tools remain later work. |
| SDL2 | A native Pyxis backend shared by graphical ports. Start with software rendering, presentation, keyboard/mouse input and timing; scope optional subsystems against a real consumer. [Implemented](../development/sdl2.md) on 2026-10-08, with DevilutionX as its first consumer. |
| vi | [Implemented BusyBox vi port](../userland/vi.md), packaged in the normal image as the first modal editor before Neovim, with libc `ftruncate` and BRE search/substitution. |
| Links | [Implemented Links 2.30 port](../userland/links.md), packaged in the normal image as a text web browser. Every page loads through libc, so local files, directory listings and the HTTP(S) providers work alike, with libc directory reading and a narrow `stat`. |
| less | [Implemented BusyBox pager](../userland/less.md) for files and pipelines, with BRE search/highlighting and libterm console input independent of stdin. |
| tar | [Implemented uncompressed BusyBox ustar subset](../userland/tar.md) for [in-Pyxis development](in-pyxis-development.md), with whole-archive validation and regular files/directories only. |
| PDCurses | Investigate a native libterm backend for terminal applications, using its documented platform hooks for drawing, input, cursor control and delays. |
| SQLite | Port the library and CLI through a native SQLite VFS adapter, then consider database scheme views. |
| awk | Text filtering, field processing and small scripts; select an implementation after a libc/dependency probe. |
| uniq | [Implemented sbase port](../userland/uniq.md), packaged in the normal image with libc `getline`/`isblank` and validated against host upstream output. |
| wc, tail, sort | [Implemented sbase ports](../userland/wc-tail-sort.md), packaged in the normal image and validated against host upstream output. `tail -f` is refused, as Pyxis cannot wait for a file to grow; see [technical debt](../technical-debt.md#sbase-tail-follow-and-sort-limits). |
| grep | [Implemented sbase port](../userland/grep.md) with `-r` over the native directory listing, packaged in the normal image and validated against host upstream and GNU output. |
| hexdump | Still undecided: port or implement natively. A pure byte filter can be a port; this is not a commitment to a complete Unix utility suite. |
| Everyday gaps | Small missing commands and options noticed in use are collected in [everyday gaps](everyday-gaps.md) until they are scheduled. Implemented native commands are covered in [everyday commands](../userland/everyday-commands.md). |
| jq | [Implemented jq 1.7.1 port](../userland/jq.md), packaged in the normal image, without Oniguruma (regular-expression builtins fail with jq's own message). It reads files, scheme paths and stdin, and needed libc math, `signal.h`, `dirname` and `strptime` additions. |
| pup | A Go command-line HTML parser and CSS-selector tool, with JSON output for pipelines. It is a small Go utility candidate; see the [Go runtime direction](toolchains-and-runtimes.md#go-cross-compiler-then-hosted-go-toolchain). |

Vi, Links and less (queued by the owner on 2026-10-04) are complete; they serve
offline development on Pyxis, described in
[developing inside Pyxis](in-pyxis-development.md). Each port still starts with
its own investigation and decisions, as vi did.

SDL2 means an upstream library port with a Pyxis platform backend, not a growing
collection of lookalike SDL functions. Begin with the subsystems a selected
application needs and report unsupported facilities explicitly. Audio, threads,
GPU APIs and desktop/window integration need their own requirements; a first
software-rendered application need not wait for the eventual compositor.

For SQLite, settle file identity, locking, journal lifecycle and durable sync
before promising concurrent persistent databases. An in-memory first slice or
an explicitly enforced restricted access model can precede broader support;
successful no-op locking/sync is not a substitute. WAL and shared-memory support
are separate decisions. SQLite's VFS is its userspace OS adapter, distinct from
Pyxis namespace routing or the proposed `sqlite://` provider.

Now that jq is ported, once pup is too, this illustrative command could select
HTML links and pass their JSON representation to jq:

```sh
cat http://example.com/ | pup 'a json{}' | jq '.[].href'
```

Shell pipelines, native head, cksum and restricted tee are implemented
([shell streams](../userland/shell-streams.md),
[libc portability](../userland/libc-portability.md)). The separate
[userspace scheme provider](capabilities-and-providers.md) proposal for
media-type aliases such as `json+http://` remains future work.

## Graphical applications and games

- **GrafX2:** the drawing application candidate, with SDL2 and image libraries
  as potential shared prerequisites. Probe the selected revision and optional
  dependencies. Editing and saving a picture inside Pyxis would complement the
  existing source-edit/build/run loop; see [graphics direction](desktop-graphics.md).
- **Quake:** [implemented](../userland/quake.md); audio and multiplayer remain
  separate slices.
- **DevilutionX:** [implemented](../userland/devilutionx.md) as an opt-in,
  personal-use bundle: single player with sound, without networking.
- **Chocolate Doom, Chocolate Quake and EDuke32:**
  [implemented](../development/sdl-game-ports.md); Chocolate Quake plays sound
  effects and EDuke32 effects and its default OPL-emulated music as bundles;
  Chocolate Doom is silent. EDuke32 is an opt-in personal build. Deferred:
  - **sound for Chocolate Doom (an SDL_mixer port), and music** for Chocolate
    Doom (built-in OPL synthesis) and Chocolate Quake (CD tracks), or none;
  - **network play;**
  - **EDuke32's OpenGL renderers** and the other Chocolate Doom games;
  - **whether a Chocolate port replaces its native port,** an owner decision
    once native comparisons are recorded.
- **Peanut-GB:** a single-header Game Boy emulator; parked (owner, 2026-10-07).
  A native display/keyboard backend can come first, converted to SDL2 later; its
  SDL2 example frontend shows what a backend needs.
- **Emulators:** four candidates recorded on 2026-10-13. The owner's purpose is
  that they "stress important systems like audio, graphics, timing". They are
  candidates only: no work is authorized, and each needs its own owner go under
  the planning freeze. The stress column is what the port is expected to
  exercise, not something measured; licences are as read from each repository's
  default branch on 2026-10-13 and must be re-read at the revision a probe pins.

  | Candidate | Expected to stress | Build and likely dependencies | Licence in its repository |
  | --- | --- | --- | --- |
  | [mGBA](https://github.com/mgba-emu/mgba) (Game Boy Advance) | Audio callback timing and latency (the GBA sample rate resampled to the output), frame pacing against the display, input latency, CPU | CMake. No hard dependencies; the SDL frontend needs SDL (SDL2 recommended); zlib and libpng are optional and bundled; the Qt frontend is not a candidate. Built-in BIOS | Mozilla Public License 2.0 (`LICENSE`) |
  | [LakeSnes](https://github.com/angelo-wf/lakesnes) (SNES) | Audio callback timing (SPC/DSP samples into an SDL audio buffer), frame pacing, CPU | Makefile. SDL2 for window, rendering, audio and input; the README names a bundled zip library (kuba--/zip, with Miniz) for zipped ROMs | MIT (`LICENSE.txt`) |
  | [SameBoy](https://github.com/LIJI32/SameBoy) (Game Boy, Game Boy Color) | Input latency, audio latency, frame pacing, CPU | Makefile, C. The SDL frontend needs SDL2, plus libpng off Windows and macOS; the boot ROMs need rgbds unless `BOOTROMS_DIR` supplies built ones. Ships open DMG/CGB boot ROMs | Expat (MIT) for the project. Its `iOS` and `HexFiend` directories are excluded, and `iOS` adds a condition on app-store distribution; neither would be used |
  | [NESEmu](https://github.com/johnmph/NESEmu) (NES) | CPU and the [C++ runtime in userspace](../development/cxx-userspace.md); frame pacing and audio through its sound-manager buffer | C++14, CMake. The core has no dependency; SDL2 is needed only by its `main.cpp` example frontend | MIT (`LICENSE`) |

  Peanut-GB above is the smaller Game Boy alternative. The SDL2 backend already
  has [audio output](../development/sdl2.md#audio) over the
  [PCM audio sessions](../interfaces/audio.md); whether its callback timing and
  latency suit these workloads has not been measured. ROMs and
  BIOS images stay local and are never committed to any repository; a public
  example uses homebrew or test ROMs only. Emulator bugs found in use are
  natural entries for the gallery's
  [glitches and failures](../gallery/README.md#glitches-and-failures) section.
- **AbyssEngine / Diablo II:** a motivating longer-term target. Investigate the
  current C implementation, not the archived Go OpenDiablo2 tree. First build and
  run it on the host to establish actual gameplay completeness separately from
  Pyxis portability. Its current build requires SDL2, zlib, libarchive and FFmpeg.
  FFmpeg is used for ordinary audio as well as video: skipping intros alone does
  not remove it. A silent, no-cinematics initial adaptation is a candidate to
  investigate, not a confirmed build option. Keep any missing engine gameplay
  separate from the OS port's completion criteria.
- **NetHack and Frotz:** terminal game candidates; audit the chosen frontends
  rather than assuming both require the same curses interface.
- **CHIP-8 interpreter:** a smaller graphical/input/timing candidate for fun.

Keep [retawq](later-os-directions.md#additional-ports) and Neovim on the list.
Neovim requires its own runtime/dependency milestone before it can host the
proposed [database worksheet experiment](capabilities-and-providers.md#database-worksheet-experiment).

One possible ordering is to alternate a practical usability milestone with an
enjoyable port. This is a suggestion awaiting selection, not permission to start
parallel tracks or to port every dependency speculatively.

## Distant candidates

**wasm3,** a WebAssembly interpreter in C, is an investigation candidate: what
running WebAssembly modules on Pyxis would need from libc and the runtime.

The [toolchain and runtime notes](toolchains-and-runtimes.md) record Go and C++
prerequisites, [Tailscale SSH for homelab administration](toolchains-and-runtimes.md#homelab-administration-over-tailscale), and
**wilder even later idea: Ladybird** as a graphical browser. Both need independent
probes and milestones; neither is an immediate port assignment.

## Upstream investigation references

These are leads for a pinned compile/runtime probe, not verified Pyxis support.

- [zlib](https://zlib.net/) and [libpng](https://www.libpng.org/pub/png/libpng.html).
- [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) and
  [GrafX2](https://gitlab.com/GrafX2/grafX2).
- [PDCurses platform interface](https://github.com/wmcbrine/PDCurses/blob/master/docs/IMPLEMNT.md).
- [Mbed TLS and its TF-PSA-Crypto dependency](https://github.com/Mbed-TLS/mbedtls).
- [SQLite OS interface](https://www.sqlite.org/vfs.html).
- [One True Awk](https://github.com/onetrueawk/awk) and [jq](https://github.com/jqlang/jq).
- [pup HTML command-line tool](https://github.com/ericchiang/pup).
- [Quake source](https://github.com/id-Software/Quake).
- [DevilutionX build options](https://github.com/diasurgical/DevilutionX/blob/master/docs/building.md).
- [Current C AbyssEngine](https://github.com/AbyssEngine/AbyssEngine),
  [dependencies](https://github.com/AbyssEngine/AbyssEngine/blob/main/CMakeLists.txt),
  [audio decoding](https://github.com/AbyssEngine/AbyssEngine/blob/main/src/audio/AudioStream.c)
  and [archived Go OpenDiablo2](https://github.com/OpenDiablo2/OpenDiablo2).

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
| fastfetch | [Implemented native port](../userland/fastfetch.md), delivered as a program package with native system information, the Pyxis ASCII logo, text/JSON and explicit JSONC configuration. |
| zlib | [Implemented development library](../development/ports.md#zlib-development-library), first consumed by [screenshot PNG output](../userland/screenshot.md). Jar files and BusyBox `gzip`/`unzip` remain later consumers. |
| libpng | [Implemented development library](../development/ports.md#libpng-development-library), with explicit zlib input and screenshot RGB8 write qualification; target decoding, viewers and drawing tools remain later work. |
| SDL2 | A native Pyxis backend shared by graphical ports. Start with software rendering, presentation, keyboard/mouse input and timing; scope optional subsystems against a real consumer. [Implemented](../development/sdl2.md) on 2026-10-08, with DevilutionX as its first consumer. |
| vi | [Implemented BusyBox vi port](../userland/vi.md), included in the core archive as the first modal editor before Neovim, with libc `ftruncate` and BRE search/substitution. |
| Links | [Implemented Links 2.30 port](../userland/links.md), delivered as a program package as a text web browser. Every page loads through libc, so local files, directory listings and the HTTP(S) providers work alike, with libc directory reading and a narrow `stat`. |
| less | [Implemented BusyBox pager](../userland/less.md) for files and pipelines, with BRE search/highlighting and libterm console input independent of stdin. |
| tar | [Implemented uncompressed BusyBox ustar subset](../userland/tar.md) for [in-Pyxis development](in-pyxis-development.md), with whole-archive validation and regular files/directories only. |
| ncursesw | [Accepted terminal UI milestone](terminal-ui.md): native raw access, `TERM=pyxis` terminfo and hexcurse first, followed by chronium’s candidate list. |
| SQLite | Port the library and CLI through a native SQLite VFS adapter, then consider database scheme views. |
| awk | Text filtering, field processing and small scripts. The [awk probe](#awk-probe) (2026-10-16) recommends onetrue-awk; three decisions are open. |
| uniq | [Implemented sbase port](../userland/uniq.md), delivered as a program package with libc `getline`/`isblank` and validated against host upstream output. |
| wc, tail, sort | [Implemented sbase ports](../userland/wc-tail-sort.md), delivered as a program package and validated against host upstream output. `tail -f` is refused, as Pyxis cannot wait for a file to grow; see [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1479). |
| grep | [Implemented sbase port](../userland/grep.md) with `-r` over the native directory listing, delivered as a program package and validated against host upstream and GNU output. |
| hexdump | Still undecided: port or implement natively. A pure byte filter can be a port; this is not a commitment to a complete Unix utility suite. |
| Everyday gaps | Small missing commands and options noticed in use are collected in [everyday gaps](everyday-gaps.md) until they are scheduled. Implemented native commands are covered in [everyday commands](../userland/everyday-commands.md). |
| jq | [Implemented jq 1.7.1 port](../userland/jq.md), delivered as a program package, without Oniguruma (regular-expression builtins fail with jq's own message). It reads files, scheme paths and stdin, and needed libc math, `signal.h`, `dirname` and `strptime` additions. |
| pup | A Go command-line HTML parser and CSS-selector tool, with JSON output for pipelines. It is a small Go utility candidate; see the [Go runtime direction](toolchains-and-runtimes.md#go-cross-compiler-then-hosted-go-toolchain). |
| vis | Low priority (chronium, 2026-10-22). <https://github.com/martanne/vis>: a vi-like editor with sam structural regular expressions and Lua scripting. To explore: its terminal UI layer (libtermkey/curses) on libterm, Lua (ports has 5.1 and 5.4), and its external-command filtering (`\|`, `<`, `>`), which assumes fork/exec and pipes. No commitment. |

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

### awk probe

Status: recommendation, 2026-10-16; no decision is agreed and no port is
authorized. This was a build-only probe: nothing ran on Pyxis.
It compiled each candidate against the SDK exported from Pyxis `a6510a15` in
builder `pyxis-llvm23.1.3-df87791`, using scratch declarations to get past
compile errors so that the link reported every missing symbol. Those
declarations are not support.

**Recommendation: [onetrue-awk](https://github.com/onetrueawk/awk)** (BWK awk,
the second-edition book's reference), probed at `5739fd7` (tag `20260426`). It
is standalone C (about 8,200 lines) under the permissive Lucent licence. It
uses its own regular-expression engine, handles UTF-8 in strings and regular
expressions, and supports `--csv`. Upstream is active, with 16 commits in 2025
and 4 so far in 2026. With the scratch declarations every object compiles; the
cross-compiled text is about 127 KiB at the SDK's `-O2`. The build needs two
host tools: yacc for `awkgram.y`, and a host-compiled `maketab` that generates
`proctab.c`. The builder container has a host `cc` but no bison or byacc.
The internal mirror is `https://git.internal/mirrors/awk`, a pull mirror of
upstream; its `master` and tag `20260426` are both at `5739fd79`.

**BusyBox awk** (the pinned `f96d33d`, `editors/awk.c`, about 4,100 lines,
GPL-2.0-only like the existing BusyBox applets) has no generated sources, and
the recipe and mirror already exist. But that recipe builds no Kconfig or libbb:
each applet brings its own shims. awk.c needs 27 libbb helpers that vi, less,
tar and printf don't provide, among them `xmalloc_read`, `xfopen_stdin`,
`xregcomp`, `endofname`, `fflush_all`, `itoa`/`utoa` and the `bb_*` error
helpers. It also needs `getopt`. Its matching uses libc regex
(ASCII-only classes), and its strings are bytes, so `length`, `substr` and
`index` do not count UTF-8 characters. Upstream fixes keep arriving (10 commits
to awk.c in 2026), so the shim layer would need refreshing on every pin move.
Both candidates need libm (`sin`, `cos`, `atan2`, `exp`, `log`, `pow`, `sqrt`,
`fmod`, `trunc`), `strtod`, `time`, `localtime`, `mktime` and `strftime`, and
printf `%e`, `%f`, `%g` and `%a`. libc already supplies all of these.

**Not gawk or mawk.** gawk is GPL-3.0-or-later, which would be a new licence in
the image. It is also an autoconf/gnulib build many times larger, and much of
its value is in extensions Pyxis would refuse or not use: `/inet` sockets,
dynamic extensions and i18n. mawk (GPL-2.0, autoconf) is fast and maintained,
but it offers Pyxis scripting nothing onetrue-awk lacks.

Gaps for onetrue-awk at the probed revision:

| Gap | Kind | Intended resolution |
| --- | --- | --- |
| `EDOM` in `errno.h`, `FOPEN_MAX` in `stdio.h`, `wctomb` | ISO C, absent from libc | Add to libc. `wctomb` mirrors the existing UTF-8 `mbtowc`; it serves printf `%c` |
| `random`/`srandom` | POSIX | Add to libc. The `rand` alternative has `RAND_MAX` 32767, so awk `rand()` would have only 15 bits |
| `freopen` | ISO C, [absent](https://git.internal/PyxisOS/pyxis-os/issues/1368) | Patch: used only to close a standard stream onto `/dev/null`; close it directly instead |
| `fcntl(F_SETFD, FD_CLOEXEC)` | POSIX | Patch it out: launch passes explicit descriptors, so nothing is inherited |
| `popen`, `pclose`, `system`, `<sys/wait.h>` | POSIX process execution | Refuse; decision 3 |
| `environ` | POSIX | Read-only adapter for `ENVIRON`, see below |

`ENVIRON` default: a recipe adapter fills it from libc's
`pyxis_environment_snapshot_init`, the exported TEXT values only, so libc gains
no `environ` alias. CAP entries are not strings and stay out. Upstream's
`setlocale(LC_CTYPE, "")` selects the only (C) locale; its UTF-8 support does
not depend on locale. `signal(SIGFPE)` gets `SIG_ERR`, which awk ignores; awk
reports division by zero itself. `srand()` with no argument seeds from `time()`,
so it needs the clock grant.

Decisions, with defaults:

1. **Implementation.** Default: onetrue-awk, for its UTF-8 handling,
   independence from libc regex limits and permissive licence, and because the
   port would be a small patch rather than a libbb shim layer. The alternative is
   BusyBox awk, to reuse the existing recipe and its licence.
2. **Parser generation.** Default: add bison to the builder container (an owner
   rebuild and a CI image pin bump), and generate `awkgram.tab.c` in the recipe
   as upstream's makefile does. The alternative is committing the generated
   parser beside the recipe, which avoids the rebuild but carries generated
   source.
3. **Commands.** Default: refuse `system()`, `print | cmd` and `cmd | getline`
   at parse time with a Pyxis message, until pipes with capabilities exist.
   Upstream's `-safe` mode already rejects these at parse time; the patch would
   apply that check always, but only to command execution, while file
   redirection, `ENVIRON` and `close()` keep working. libc gets no `popen` or
   `system`. A command string needs a shell interpreter and a native job
   design, not a libc stub. The alternative is designing libc `popen`/`system`
   over [native jobs](../userland/native-jobs.md) first.

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
  effects, Chocolate Doom effects and its own OPL music through SDL2_mixer,
  and EDuke32 effects and default OPL music. SDL2_mixer has no MIDI decoder;
  Doom's SDL MIDI/TiMidity backend is omitted. EDuke32 is an opt-in personal build. Deferred:
  - **music for Chocolate Quake** (CD tracks), or none;
  - **network play;**
  - **EDuke32's OpenGL renderers** and the other Chocolate Doom games;
  - **whether a Chocolate port replaces its native port,** an owner decision
    once native comparisons are recorded.
- **Peanut-GB:** a single-header Game Boy emulator; parked (owner, 2026-10-07).
  A native display/keyboard backend can come first, converted to SDL2 later; its
  SDL2 example frontend shows what a backend needs.
- **Emulators:** four candidates recorded on 2026-10-13. The owner's purpose is
  that they "stress important systems like audio, graphics, timing". They are
  candidates only: no work is authorized, and each needs its own owner go.
  The stress column is what the port is expected to
  exercise, not something measured; licences are as read from each repository's
  default branch on 2026-10-13 and must be re-read at the revision a probe pins.

  | Candidate | Expected to stress | Build and likely dependencies | Licence in its repository |
  | --- | --- | --- | --- |
  | [mGBA](https://github.com/mgba-emu/mgba) (Game Boy Advance) | Audio callback timing and latency (the GBA sample rate resampled to the output), frame pacing against the display, input latency, CPU | CMake. No hard dependencies; the SDL frontend needs SDL (SDL2 recommended); zlib and libpng are optional and bundled; the Qt frontend is not a candidate. Built-in BIOS | Mozilla Public License 2.0 (`LICENSE`) |
  | [LakeSnes](https://github.com/angelo-wf/lakesnes) (SNES) | Audio callback timing (SPC/DSP samples into an SDL audio buffer), frame pacing, CPU | Makefile. SDL2 for window, rendering, audio and input; the bundled ZIP code still needs `utime.h`/`utime` and `st_mtime`; optional 0.0.10 port deferred rather than disabling timestamp handling | MIT (`LICENSE.txt`) |
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

Keep [retawq](later-os-directions.md#additional-ports) on the list. Neovim is
delivered ([reference](../userland/neovim.md)), so its runtime milestone no longer
gates the proposed
[database worksheet experiment](capabilities-and-providers.md#database-worksheet-experiment).

One possible ordering is to alternate a practical usability milestone with an
enjoyable port. This is a suggestion awaiting selection, not permission to start
parallel tracks or to port every dependency speculatively.

### raylib games

[Minesweeper](../userland/minesweeper.md) is packaged as an opt-in public
program and QEMU-qualified, including gameplay and output resizing. It uses
the raylib/SDL integration and needs no runtime development package.

The twelve raylib-games classics are packaged together; install `raylib-games`
and launch their original command names, such as `tetris`, `snake` or `arkanoid`.
Build with `make game-packages GAMES=raylib-games REPOSITORY=DIR`. Other
candidates await selection; the implemented
[raylib profile](../development/raylib.md) defines the available APIs.
Candidates collected by chronium (2026-10-22). "To verify" marks a licence that
has not been checked yet.

| Candidate | Language | Licence | Source |
| --- | --- | --- | --- |
| [raylib-games classics](../../ports/raylib-games/README.md): twelve games, one `raylib-games` package | C, unchanged upstream sources | zlib | [pin and recipe](../../ports/raylib-games/metadata.lua) |
| Jump Prince (Jump King-inspired platformer) | C/C++ | zlib | <https://github.com/jakubtomsu/raylib-jump-prince> |
| AstroJumps (space platformer) | C | MIT | <https://github.com/braniik/astrojumps> |
| Tuxmino (moddable Tetris-style) | C + Lua | MIT | <https://github.com/masonarmand/tuxmino> |
| Celeste Classic (raylib adaptation) | C | to verify | <https://github.com/ayuusweetfish/ccleste-raylib> |
| DOOM (raylib port) | C | GPLv2 + others | <https://github.com/raydelto/raylib_DOOM> |
| Spillway (fluid physics puzzle) | C++20 | MIT | <https://github.com/zishaan1911/spillway> |
| Bloxorz (3D block-rolling puzzle) | C | MIT | <https://github.com/AchrafMez/bloxorz> |
| Midless (voxel sandbox) | C | MIT | <https://github.com/Sirvoid/Midless> |
| Gupta (chess-based roguelike) | C | to verify | <https://github.com/masonarmand/gupta> |
| Sandwich Factory (puzzle with level editor) | C | to verify | <https://github.com/tehtnaz/sandwichfactory2> |
| Astro Aegis (3D Asteroids) | C | zlib | <https://github.com/computermouth/astro-aegis> |
| rlVoxel (voxel sandbox/engine) | C | GPLv3 | <https://github.com/tacf/rlVoxel> |
| Snowball Moon (local multiplayer) | C | to verify | <https://github.com/wildmonkeydan/SnowballMoon> |
| Energy Swap (colour-sorting puzzle) | C++ | MIT | <https://github.com/juan-medina/energy-swap> |
| Puzzle Engine (PuzzleScript-compatible runtime) | C | MIT | <https://github.com/HParker/puzzleengine> |

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

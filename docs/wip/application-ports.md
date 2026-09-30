# Application and library port candidates

Status: future candidates from discussion, not an approved sequence or an
implementation assignment. Select one bounded consumer, probe a pinned source
revision, and discuss missing contracts before starting each milestone. Keep
ports in userspace and adapt their platform interfaces without reshaping the
kernel around an individual application.

Kilo, TCC, Lua and Doom already provide useful applications; see the
[port workflow](../development/ports.md), [edit/build/run loop](../development/edit-build-run.md),
[Lua reference](../userland/lua.md) and [Doom reference](../userland/doom.md). The next candidates
should improve daily use or exercise a reusable OS facility.
[Verified HTTPS](../userland/https.md) already uses the packaged Mbed TLS and
TF-PSA-Crypto libraries with native userland integration; SSH remains deferred.

## Libraries and terminal tools

| Candidate | Intended use and initial investigation |
| --- | --- |
| fastfetch | [Pinned SDK probe and bounded port proposal](fastfetch.md): native system information, the Pyxis ASCII logo and a small set of truthful modules. No working port yet; settle authority and memory-accounting labels before implementation. |
| zlib | Reusable compression/decompression, followed by a concrete consumer such as PNG loading. |
| libpng | PNG decoding/encoding for viewers, drawing tools and screenshots; depends on zlib. |
| SDL2 | A native Pyxis backend shared by graphical ports. Start with software rendering, presentation, keyboard/mouse input and timing; scope optional subsystems against a real consumer. |
| PDCurses | Investigate a native libterm backend for terminal applications, using its documented platform hooks for drawing, input, cursor control and delays. |
| SQLite | Port the library and CLI through a native SQLite VFS adapter, then consider database scheme views. |
| awk | Text filtering, field processing and small scripts; select an implementation after a libc/dependency probe. |
| grep, tail, wc, sort, uniq, hexdump | Small everyday commands that remain useful alongside awk. Decide which to port or implement separately; this is not a commitment to a complete Unix utility suite. |
| jq | JSON inspection and transformation, initially on local files. Audit libc/math and the selected regex configuration/dependency. |
| pup | A Go command-line HTML parser and CSS-selector tool, with JSON output for pipelines. It is a small Go utility candidate; see the [Go runtime direction](toolchains-and-runtimes.md#go-cross-compiler-then-hosted-go-toolchain). |

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

HTTP reads and shell pipelines are implemented. Once jq and pup are ported, this
illustrative command could select HTML links and pass their JSON representation
to jq:

```sh
cat http://example.com/ | pup 'a json{}' | jq '.[].href'
```

[Shell pipelines and native head](../userland/shell-streams.md) are implemented. The
[libc portability milestone](../userland/libc-portability.md) packages upstream cksum and
restricted tee through conventional descriptor APIs. jq and pup remain unported
candidates. The separate [userspace scheme provider](userspace-scheme-providers.md)
proposal for media-type aliases such as `json+http://` remains future work.

## Graphical applications and games

- **GrafX2:** the drawing application candidate, with SDL2 and image libraries
  as potential shared prerequisites. Probe the selected revision and optional
  dependencies. Editing and saving a picture inside Pyxis would complement the
  existing source-edit/build/run loop; see [graphics direction](desktop-graphics.md).
- **Quake:** the next proposed large game target. Investigate a software-rendered
  port with an initial single-player/demo scope rather than requiring a GPU.
  Select the source port and audit libc, input, timing and rendering requirements
  before committing; audio and multiplayer can be separate slices.
- **DevilutionX:** Diablo I, not Diablo II. A later candidate needing a C++
  userspace runtime, SDL and supporting libraries. Upstream offers a network-off
  configuration; assess a bounded first port with networking/audio deferred.
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
proposed [database worksheet experiment](userspace-scheme-providers.md#database-worksheet-experiment).

One possible ordering is to alternate a practical usability milestone with an
enjoyable port. This is a suggestion awaiting selection, not permission to start
parallel tracks or to port every dependency speculatively.

## Distant candidates

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

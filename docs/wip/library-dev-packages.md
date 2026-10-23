# Library development packages

Status: **decided (chronium, 2026-10-23); implementation assigned.**
Inventory the ports build and expose its reusable libraries through the existing
[`dev` kind and `sdk://` view](sdk-view.md). The original target is 0.0.9;
chronium allows the additive sweep in 0.0.8 if it finishes before that tag
(chronium, 2026-10-23). This changes no release gate or library profile.

## Inspected starting point

Parent `f26b1b32`, ports `cd89600c`, userland `5e4e7667`.
The [catalog](../../ports/ports.lua) contains 33 recipes;
[development staging](../../ports/develop.lua), recipe metadata, build scripts,
Makefiles, patches and checked-in licence notices identify **13 development
prefixes, 17 target static archives and 17 pkg-config modules**. Excluding
private wpa-rsn leaves 12 prefixes and 16 archives/modules for public packaging. This is source
inspection, not a fresh build or an audit of every upstream intermediate file.

[`raylib-dev` and `sdl2-dev`](../development/raylib.md#example-and-development-packages)
already ship through `packages/dev/`. The base `pyxis-dev` owns libc, libpyxis,
libterm, libfont, libnpfs-format, compiler builtins and startup files;
`libcxx-dev` owns libc++, libc++abi and libunwind. Those are existing SDK
packages, outside the ports-library additions. Native userland's libhttp,
libtls, libconfig and libpublisher are outside this sweep too.

## Reusable library inventory

Package boundaries below are **accepted**, with two packages already delivered.
Every package depends on `pyxis-dev`; the dependency column gives additional
named dependencies. Consumers are current link users, not promises that all
upstream APIs work. Lua's interpreter-only TLS dependency is distinguished
from its core archive in its [Makefile](../../ports/lua/Makefile).

| Package / version | Target archive(s); pkg-config module(s) | Licence carried | Additional dependencies | Current consumers |
| --- | --- | --- | --- | --- |
| `fmt-dev` 12.2.0 | `libfmt.a`; `fmt` | MIT | `libcxx-dev` | DevilutionX (personal build) |
| `libpng-dev` 1.6.59 | `libpng.a`; `libpng` | libpng-2.0 | `zlib-dev` | screenshot, screenshot-saver; DevilutionX |
| `libuv-dev` 1.52.1 | `libuv.a`; `libuv` | MIT, LICENSE-extra, MPL-2.0 native adapter | — | Lua 5.1/luv, Neovim; uv-relay example |
| `lua-dev` 5.5.1 | `liblua.a`; `lua` | MIT | — | Lua interpreter; boot-init, session, config, Wi-Fi profile reader, installer/publication code |
| `lua5.1-dev` 5.1.5 | `liblua5.1.a`; `lua5.1` | MIT | — | Lua 5.1 interpreter, Neovim, LPeg, luv |
| `lpeg-dev` 1.1.0 | `liblpeg.a`; `lpeg` | MIT | `lua5.1-dev` | Lua 5.1 interpreter, Neovim |
| `luv-dev` 1.52.1-0 | `libluv.a`; `luv` | Apache-2.0; bundled compat53 MIT | `lua5.1-dev`, `libuv-dev` | Lua 5.1 interpreter, Neovim |
| `mbedtls-dev` 4.1.1 / TF-PSA-Crypto 1.1.1 | `libmbedtls.a`, `libmbedx509.a`, `libtfpsacrypto.a`; `mbedtls`, `mbedx509`, `tfpsacrypto` | Apache-2.0 selected for both upstream projects | — | httpfs/native TLS, xfer, pkg, boot/install/update hashing; Lua interpreter's native module |
| `picohttpparser-dev` `f4d94b48` | `libpicohttpparser.a`; `picohttpparser` | MIT | — | libhttp/httpfs |
| `raylib-dev` 6.2 **existing** | `libraylib.a`; `raylib` | zlib; selected bundled MIT/MIT-0/public-domain notices; MPL-2.0 Pyxis source | `sdl2-dev` | raylib-shapes, Minesweeper |
| `sdl2-dev` 2.32.10 **existing** | `libSDL2.a`; `sdl2` | zlib; MPL-2.0 Pyxis backend/configuration | — | raylib, Chocolate Doom/Quake, EDuke32 and DevilutionX personal builds |
| `tree-sitter-dev` 0.26.13 | `libtree-sitter.a`; `tree-sitter` | MIT, bundled Unicode/endian notices | — | Neovim |
| `utf8proc-dev` 2.11.3 | `libutf8proc.a`; `libutf8proc` | MIT and Unicode data licence | — | Neovim |
| `zlib-dev` 1.3.2 | `libz.a`; `zlib` | zlib | — | libpng, screenshot/saver; DevilutionX |

Evidence: each recipe's `metadata.lua`, `build.lua` and `.pc` files;
[port runner dependency checks](../../ports/build.lua),
[userland link rules](../../userspace/Makefile), and the
[Lua 5.1](../../ports/lua51/PORT-NOTICE.md),
[Mbed TLS](../../ports/mbedtls/PORT-NOTICE),
[libuv](../../ports/libuv/PORT-NOTICE),
[wpa-rsn](../../ports/wpa-supplicant/PORT-NOTICE) and
[raylib bundled notices](../../ports/raylib/THIRD-PARTY-LICENSES).
Package dependencies express static link/header needs. Installing a compiled
program does not acquire its build-time `-dev` dependencies.

### Export and profile details

Most prefixes already have the required flat `include/`, `lib/` and
`lib/pkgconfig/` layout. Lua 5.5.1's headers are top-level; Lua 5.1 uses
`lua5.1/`, luv uses `luv/`. LPeg has an archive and module entry point but no
separate public header. The split gives each SDK file one owner. There is no
explicit cross-prefix collision in the staging rules; implementation must check
actual CMake/recursive installs too.

Mbed TLS combines its three archives and merged `mbedtls/`, `psa/` and
`tf-psa-crypto/` headers under one configuration. Keep that one package, retaining
both Pyxis config headers. Its pkg-config dependency chain remains
`mbedtls → mbedx509 → tfpsacrypto`. `liblua.a` excludes the interpreter's
native module/jobs and needs no `mbedtls-dev` dependency.

Mbed TLS also needs application-supplied entropy, time and allocation hooks
([existing boundary](../../ports/mbedtls/README.md)). Today native libtls supplies
them; that adapter is not in the ports archive. The accepted scope packages the
configured upstream library and documents those requirements, with no successful
fallback or implicit networking authority. A `-lmbedtls` client alone is not a
ready native TLS transport.

Keep the existing selected profiles: zlib omits gz-file helpers; fmt omits OS
helpers and follows the selected libc++ feature configuration; Lua 5.5.1 omits math/debug and the stock
all-libraries initializer; libuv is the native subset; tree-sitter has no Wasm
or dynamic parser loader. The wpa-rsn export remains Pyxis-internal glue with the supplicant; its
implementation-facing headers are not a public development package. Header
installation alone is not API qualification. Broader profiles
need separate consumer-led work.

All packages retain original licences, notices, provenance and required covered
Pyxis source/configuration. Several recipes keep those under `stage/share`,
outside `stage/dev`; copying only the latter is insufficient. Verify the
upstream Unicode/additional notices at the pinned sources during implementation;
do not replace them with guessed SPDX identifiers. CMake metadata and make
fragments remain under `pkg://NAME-dev/...`; their usability requires inspection.
Only the existing SDK-view paths enter `sdk://`.

## Embedded and private closure

These components have no independent development prefix today. The source
inventory accounts for them; extracting a reusable library is a different scope
from wrapping the existing public exports.

| Build owner | Private or embedded components / licence evidence | Proposed treatment |
| --- | --- | --- |
| wpa-supplicant | `libwpa-rsn.a`, RSN wrapper/config and upstream header selection; BSD-3-Clause/public-domain crypto, MPL-2.0 integration | Private supplicant glue, no `wpa-rsn-dev` |
| Lua 5.1 | lua-compat-5.3 (MIT), compiled into luv | Retain in `luv-dev`, no separate archive/package |
| raylib | rlsw, sinfl/sdefl, selected stb headers (MIT); rprand/rcamera/raymath (zlib), RNG public-domain dedications; miniaudio/dr_wav (MIT-0 selected) | Existing raylib archive/notices; public raymath/rlgl headers stay there |
| Neovim | Seven `libpyxis_parser_*` archives: C/Lua/Vim/Markdown/Markdown-inline MIT, Vimdoc/query Apache-2.0; xdiff LGPL-2.1-or-later; mpack, lua-bitop, lua-cjson/fpconv, klib, libtermkey/libvterm retain MIT notices | Editor-private closure; no parser/module `-dev` packages |
| DevilutionX | bzip2's own licence; libmpq GPL-2.0-or-later; libsmackerdec LGPL-2.1; SimpleIni MIT; SDL_image PNG subset zlib; SDL_audiolib LGPL-3.0-or-later with BSD resampler and public-domain/MIT-0 decoders; embedded BSD/CC0 helpers and PKWare code with no separately identified notice | No extraction; preserve the existing personal-build restriction and source-notice limits |
| Chocolate Doom/Quake | Game-support/module libraries under their upstream program terms; omitted SDL_mixer/net, samplerate/FluidSynth and music codecs are not new outputs | Private; enumerate actual intermediates if later extracting one |
| Fastfetch | `libfastfetch` is an OBJECT library; yyjson MIT | Program closure, no standalone archive export |
| jq | libjq source objects, decNumber ICU, Gay dtoa Lucent notice; jq MIT | Direct object linkage; no `libjq.a`; Oniguruma is absent |
| pkgconf | libpkgconf ISC source objects; separate getopt notices | Direct object linkage, no public library export |
| sbase / BusyBox | libutil/libutf and helpers with MIT/ISC/BSD-3-Clause/Unicode-3.0 notices; libbb/libarchive helpers under BusyBox's GPL-2.0-only closure | Selected command helpers, no public dev output |
| EDuke32 | Bundled ImGui/loguru/smmalloc/minicoro/LZ4 and other engine helpers retain source-file notices, not a verified reusable licence set | No extraction; existing GPL/Build personal-build restriction stays |

The private rows follow their checked port notices and selection code, not a
fresh enumeration of upstream CMake/GNU Make intermediates. In particular,
EDuke32's reached helper licence set and private game archive names need their
own source audit before any independent export. Other catalog entries are
program/data-only. Neovim's host Lua/LPeg and `host/nlua0.so`, tzdata's host
`zic`, configure probes and compiler tools are host products, outside target SDK
packages. No host library enters `sdk://`.

## Delivery tasks

Existing [package rules](../interfaces/packages.md) remain fixed: `kind = "dev"`,
release ABI, no commands, dependencies by package ID, flat upstream header/layout,
static `.pc` output with explicit `sdk://include` and `sdk://lib`, and conflict
refusal. Installing a library grants no network, file, display or Wi-Fi authority;
the compiled consumer still declares its native grants. No per-package roots,
shared libraries, compatibility layer or compiler-container rebuild.

1. [ ] **Foundation libraries:** package zlib/libpng, fmt, picohttpparser and the
   coherent Mbed TLS set; fill licence/provenance/source gaps and inspect
   static transitive `.pc` dependencies. After this task chronium can install
   those libraries and compile consumers against `sdk://` without search flags.
2. [ ] **Runtime and text libraries:** package libuv, utf8proc, tree-sitter and Lua
   5.5.1; split Lua 5.1/LPeg/luv ownership as decided below, retaining both Lua
   ABIs. After this task chronium can embed the selected runtimes and text APIs
   with dependencies installed by name.
3. [ ] **Release qualification:** check every recipe is discovered by `make development-packages`, licences
   and view ownership, clean install/coexistence/update/removal, and representative
   C/C++ compile/link/run through hosted Clang and pkg-config for every archive.
   Test Lua ABIs together, static dependency order and existing refusals; no
   credentials or radio access needed for the library packaging check. After
   this task chronium can publish the complete reusable-library set for the
   chosen release.

Each implementation uses focused ports changes first and a linked parent
packaging/pin PR; existing programs keep their selected static closures. No
userland/kernel changes are planned. Runtime/API changes or unresolved licences
stop for review. Measure package file/byte counts when built. This proposal
changes no execution path, so it needs document/link review, not a boot or timing
baseline; implementation qualification must not claim unrun upstream APIs.

## Decided (chronium, 2026-10-23)

1. **Package boundaries:** fourteen logical packages in the table, twelve new.
   Split `lua5.1-dev`, `lpeg-dev` and `luv-dev` from their one build prefix;
   keep Mbed TLS's three archives together with their shared headers/configuration.
2. **Private library scope:** package the reusable prefixes except wpa-rsn.
   Its only consumer is the native supplicant, and that glue stays with it.
   Embedded private libraries stay with their programs and existing notices.
3. **Mbed TLS adapter boundary:** ship the configured upstream archives/headers
   and documented callback requirements. Native libtls stays outside this sweep;
   consumers provide real hooks using their declared grants.

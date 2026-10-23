# Library development packages

Install a library's `-dev` package to expose its headers, static archives and
pkg-config metadata in the live [`sdk://` view](../interfaces/packages.md#reaching-package-files).
The hosted compiler searches that view by default. Named dependencies are
installed automatically; compiled applications retain their static libraries
and do not need the development packages at runtime.

```text
pkg install libpng-dev
pkg-config --cflags --libs libpng
clang home://image.c -lpng -lz -o home://image.pxe
```

`make development-packages REPOSITORY=DIR` builds and packs these recipes from
[`packages/dev/`](../../packages/README.md#development-packages). Packages have
`kind = "dev"`, the build's release/development ABI and no commands. They add
no file, network, display or credential authority to a program; consumers still
declare their grants. There are no package-named roots or shared objects.

## Libraries and dependencies

Every row depends on `pyxis-dev`, directly or through another named dependency;
`libcxx-dev` supplies C++ headers and runtime. Those two existing SDK packages
remain separate. Versions/profiles are pinned in each
[port recipe](../../ports/ports.lua); consumers below identify current uses,
not full upstream API qualification.

| Package / version | Target archive(s); pkg-config module(s) | Licence carried | Additional dependencies | Current consumers |
| --- | --- | --- | --- | --- |
| `fmt-dev` 12.2.0 | `libfmt.a`; `fmt` | MIT | `libcxx-dev` | DevilutionX (personal build) |
| `libpng-dev` 1.6.59 | `libpng.a`; `libpng` | libpng-2.0 | `zlib-dev` | screenshot, screenshot-saver; DevilutionX |
| `libuv-dev` 1.52.1 | `libuv.a`; `libuv` | MIT, BSD-2-Clause/ISC extras, MPL-2.0 native adapter | — | Lua 5.1/luv, Neovim; uv-relay example |
| `lua-dev` 5.5.1 | `liblua.a`; `lua` | MIT | — | Lua interpreter; boot-init, session, config, Wi-Fi profile reader, installer/publication code |
| `lua5.1-dev` 5.1.5 | `liblua5.1.a`; `lua5.1` | MIT | — | Lua 5.1 interpreter, Neovim, LPeg, luv |
| `lpeg-dev` 1.1.0 | `liblpeg.a`; `lpeg` | MIT | `lua5.1-dev` | Lua 5.1 interpreter, Neovim |
| `luv-dev` 1.52.1-0 | `libluv.a`; `luv` | Apache-2.0; bundled compat53 MIT | `lua5.1-dev`, `libuv-dev` | Lua 5.1 interpreter, Neovim |
| `mbedtls-dev` 4.1.1 / TF-PSA-Crypto 1.1.1 | `libmbedtls.a`, `libmbedx509.a`, `libtfpsacrypto.a`; `mbedtls`, `mbedx509`, `tfpsacrypto` | Apache-2.0 selected for both upstream projects | — | httpfs/native TLS, xfer, pkg, boot/install/update hashing; Lua interpreter's native module |
| `picohttpparser-dev` `f4d94b48` | `libpicohttpparser.a`; `picohttpparser` | MIT | — | libhttp/httpfs |
| `raylib-dev` 6.2 **existing** | `libraylib.a`; `raylib` | zlib; selected bundled MIT/MIT-0/public-domain notices; MPL-2.0 Pyxis source | `sdl2-dev` | raylib-shapes, Minesweeper |
| `sdl2-dev` 2.32.10 **existing** | `libSDL2.a`; `sdl2` | zlib, BSD-3-Clause, Sun fdlibm grant; MPL-2.0 Pyxis backend/configuration | — | raylib, Chocolate Doom/Quake, EDuke32 and DevilutionX personal builds |
| `tree-sitter-dev` 0.26.13 | `libtree-sitter.a`; `tree-sitter` | MIT, bundled Unicode/endian notices | — | Neovim |
| `utf8proc-dev` 2.11.3 | `libutf8proc.a`; `libutf8proc` | MIT and Unicode data licence | — | Neovim |
| `zlib-dev` 1.3.2 | `libz.a`; `zlib` | zlib | — | libpng, screenshot/saver; DevilutionX |

## File ownership and selected profiles

Each installed SDK file has one owner. Lua 5.5.1 owns top-level `lua.h` and
`liblua.a`; Lua 5.1 owns `lua5.1/` and `liblua5.1.a`. Select one Lua ABI per
program. LPeg and luv are separate packages depending on Lua 5.1; compat53 is
inside luv, and LPeg has no separate public C header. Lua's interpreter-only
Mbed TLS dependency does not belong to `lua-dev`.

Mbed TLS keeps its three archives and merged headers in one configured package.
`mbedtls.pc → mbedx509.pc → tfpsacrypto.pc` records the static link order. Clients
use the packaged configuration flags for every consuming translation unit and
supply real allocation, time and entropy callbacks using their declared grants
([callback boundary](../../ports/mbedtls/README.md)). Native libtls is not part
of this package; installing it alone does not provide a ready TLS transport.

The existing selected profiles stay in effect: zlib omits gz-file helpers;
fmt omits OS helpers and follows libc++'s configured features; Lua 5.5.1 omits
math/debug and the stock all-libraries initializer; libuv has its native subset;
tree-sitter has no Wasm/dynamic parser loader. A header can declare APIs outside
the compiled profile. Wider APIs need concrete consumer qualification.

Only `include/**`, archives/startup files directly in `lib/`, and
`lib/pkgconfig/**` enter the SDK view. Notices, source, provenance and CMake/make
metadata remain under `pkg://NAME-dev/...`. `.pc` files spell
`sdk://include`/`sdk://lib` explicitly and pkgconf defaults to static dependencies.
No extra include/library search path is needed for ordinary upstream includes;
module-specific defines/include subdirectories still apply, such as Lua 5.1's
header selection and Mbed TLS's configuration defines.

## Notices and private components

Every reusable development prefix carries upstream notices/provenance and the
covered original Pyxis source/configuration with its MPL terms. SDL's reached
yuv2rgb and fdlibm grants and libuv's tree/heap/queue header notices also travel
through the existing static consumers. Original notice texts remain authoritative.

wpa-rsn remains internal glue with the native supplicant, with no public dev
package. Native libhttp/libtls/libconfig/libpublisher and embedded editor/game
libraries stay with their existing consumers. Host generators, configure probes
and host libraries never enter the target SDK. These boundaries were accepted
(chronium, 2026-10-23); extracting another library requires a standalone profile,
source/licence closure and qualification.

The [qualification record](experiments/library-dev-packages.md) covers packing,
live view ownership, dependency/cancel/update/removal behavior and hosted
compilation. Runtime checks are QEMU evidence, with no new native or performance
claim. The sweep targets 0.0.9 and may ship in 0.0.8 if completed before its tag
(chronium, 2026-10-23).

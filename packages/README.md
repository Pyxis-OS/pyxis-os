# Package inputs

Run the host packer from the Pyxis checkout. Recipes are trusted literal Lua
input; paths resolve relative to the recipe file. Use a fresh version for changed
bytes: the packer never replaces an already published ID/version.

Fonts reuse the existing cache-only, SHA-256-checked data recipe and retain all
nineteen unchanged fonts (Caskaydia Cove, IBM Plex and Noto), original notices
and provenance:

```sh
make font-assets
python3 tools/pxpkg.py pack packages/fonts.lua /path/to/public-repository
python3 tools/pxpkg.py check /path/to/public-repository
```

## Development packages

`dev/pyxis-dev.lua` and `dev/libcxx-dev.lua` pack the C SDK and the C++ runtime
from this build's `build/sdk` for [`sdk://`](../docs/interfaces/packages.md#reaching-package-files).
`base` depends on `pyxis-dev`, so `make base-packages` packs it and
`make development-packages` discovers the other recipes in `dev/` and
`programs/`, including `libcxx-dev`, `sdl2-dev` and `raylib-dev`. Both targets
require a clean checkout.

The remaining reusable ports libraries also have recipes in `dev/`: fmt,
zlib/libpng, libuv, Lua 5.5.1, Lua 5.1/LPeg/luv, Mbed TLS, picohttpparser,
utf8proc and tree-sitter. Their named dependencies install automatically.
`lua5.1-dev`, `lpeg-dev` and `luv-dev` own separate view files and coexist with
`lua-dev`; the three Mbed TLS archives share one configured package. Each
retains its selected profile, notices, provenance and covered Pyxis source.
Mbed TLS consumers still supply its documented native callbacks; libtls and
supplicant-private wpa-rsn are outside this set. See the
[library reference](../docs/development/library-dev-packages.md).

## Game packages

`doom.lua` and `quake.lua` are program packages: both ports of each game, built
for one system, with the shareware data and its upstream notices.
`openarena.lua` combines the offline ioquake3 bundle with the nine unchanged
OpenArena 0.8.8 baseoa PK3s and notices. Its corresponding sources are separate
downloads under `sources/openarena/0.8.8/` in the repository. The game target
verifies/stages these and copies them before packing the binary; sources are
never installed with the game. Upload the source downloads before publishing
the binary/index. The [game reference](../docs/userland/openarena.md) records
the source layout and remaining runtime limits.
`minesweeper.lua` packages the Minesweeper bundle, its flag PNG and upstream,
raylib/rlsw and SDL2 notices. Pack games only as an explicit step of a release
or a deploy to a real machine, from a clean checkout:

```sh
make game-packages REPOSITORY=/path/to/public-repository
make game-packages GAMES=minesweeper REPOSITORY=/path/to/public-repository
```

The target builds the ports it needs, then runs
`tools/pxpkg.py pack --build build` for each of `GAMES` (default `doom quake`)
from `GAME_RECIPES` (default `packages`). It is never a dependency of the image,
CI or another target, and it refuses a checkout with local changes. The version
is `VARIANT+LABEL`, for example `shareware-1.9+0.0.8` at a tag or
`shareware-1.9+3f1c2a9b0d4e` otherwise. `pxpkg.py prune` removes the builds for
systems no longer in use.

Each bundle lists its own game package in its manifest's `packages` and reads it
read-only: `pkg://doom` for both Doom ports and `pkg://quake` for both Quake
ports. The image carries no games.
Minesweeper reads its bundled PNG through `app://` and needs no separate data
package. Its [usage guide](../docs/userland/minesweeper.md) lists controls and
required grants.

These packages are fetched through the configured network repository by the same
`pkg` client on live and installed systems. There is no boot-archive data fallback
or offline seed. A fresh live boot must configure a repository and install the
wanted packages again. Run `pkg install fonts` before `font-min`.

## Owner-local game packages

`DOOM_WAD`, `DOOM_DEMOS`, `QUAKE_DATA`, `DIABLO_DATA` and `DUKE3D_DATA` are no
longer image-build inputs. Put explicit file selections into local packer
recipes outside the checkout instead. Keep private recipes, archives and data in
the personal repository, separate from the public fonts/toolchain repository;
never commit them or paste their contents into build/review logs.

For example, a private Quake recipe keeps the ports from the build and selects
the owned files individually:

```lua
return {
  id = "quake",
  variant = "retail-1.06",
  kind = "program",
  depends = {},
  commands = {
    quake = { path = "quake.pxb", command = "quake" },
    ["chocolate-quake"] = { path = "chocolate-quake.pxb", command = "chocolate-quake" },
  },
  entries = {
    { build = "ports/quake/stage/bin/quake.pxb", at = "quake.pxb" },
    { build = "ports/chocolate-quake/stage/bin/chocolate-quake.pxb", at = "chocolate-quake.pxb" },
    { build = "ports/quake/stage/share/licenses/quake", at = "licenses/quake" },
    { build = "ports/chocolate-quake/stage/share/licenses/chocolate-quake", at = "licenses/chocolate-quake" },
    { file = "/private/id1/pak0.pak", at = "id1/pak0.pak" },
    { file = "/private/id1/pak1.pak", at = "id1/pak1.pak" },
  },
}
```

Put it in a private directory as `quake.lua` and run
`make game-packages REPOSITORY=/private/repository GAMES=quake GAME_RECIPES=/private`.
`duke3d` and `diablo` exist only as such private recipes, packed with
`GAMES="duke3d diablo"`; the target builds their ports (EDuke32, DevilutionX) for
that run. They are never packed into the public tree.
The packer copies only listed files and rejects links/special entries; it does
not scan an owner directory for arbitrary additions. Uppercase source names are
fine when the recipe selects the canonical lowercase destination below.

| Input | Package | Destination in `tree/` |
| --- | --- | --- |
| Input | Package | Destination in `tree/` |
| --- | --- | --- |
| Doom IWAD | `doom` | `DOOM.WAD` |
| Optional Doom demo recordings | `doom` | `e1m1sec.lmp`, `e1m2sec.lmp` |
| Quake PAKs | `quake` | `id1/pak0.pak`, optional `id1/pak1.pak` |
| Diablo data | `diablo` | `spawn.mpq` or `DIABDAT.MPQ` |
| Duke Nukem 3D | `duke3d` | `duke3d.grp` |

Each private recipe also packs its game's bundles from the build, as in the
Quake example: `ports/eduke32/stage/bin/eduke32.pxb` with
`ports/eduke32/stage/share/licenses/eduke32`, and
`ports/devilutionx/stage/bin/devilutionx.pxb` with
`ports/devilutionx/stage/share/licenses/devilutionx` and
`ports/devilutionx/stage/share/devilutionx/source.txt`, exporting the `eduke32`
and `devilutionx` commands. Add applicable data notices as explicit `licenses/`
files. Having a private package does not grant distribution permission, and the
EDuke32 and DevilutionX licence restrictions on sharing apply to the whole
package.

## raylib development and shapes

The release's development-package step discovers `packages/dev/raylib-dev.lua`,
`packages/dev/sdl2-dev.lua` and `packages/programs/raylib-shapes.lua` automatically:

```sh
make -j8 development-packages REPOSITORY=/path/to/public-repository
```

It builds and packs the development set from a clean checkout. These packages
are installed with `pkg`; none is added to the boot archive.

`raylib-dev` and `sdl2-dev` are separate ABI-bound `dev` packages. Raylib depends
on SDL2 by name, and SDL2 on `pyxis-dev`; neither embeds the other. Their flat headers, archives and
pkg-config metadata join `sdk://` when the package set publishes. Licences,
provenance, covered backend source and examples remain package-local under
`pkg://NAME/...`. `raylib-shapes` is a separate program package with no external
assets or audio authority. The [raylib reference](../docs/development/raylib.md)
shows hosted Clang use without search flags; the
[qualification record](../docs/development/experiments/raylib/README.md) keeps
revisions and limits.

For desktop OpenGL, `sdl2-osmesa-dev` selects `libSDL2-osmesa.a` instead of
`libSDL2.a`, with named `sdl2-dev`, `mesa-dev` and `libcxx-dev` dependencies.
Ordinary SDL/raylib consumers keep their C-only closure. Both new packages are
discovered by `make development-packages`; see the
[SDL GL profile](../docs/development/sdl2.md#opengl-through-osmesa).


## Git client

The optional first-party Git package provides public HTTPS clone, log and status:

```sh
make git-package GO_PREFIX=/absolute/pyxis-go REPOSITORY=/absolute/repository
```

It uses the pinned Go compiler and fixed module graph through the internal proxy;
the CLI is not added to the archive. See the [Git guide](../docs/userland/git.md)
for retained-directory authority, checkout modes and publication limits.

## Caddy server

Caddy is an explicit optional package, outside the base/development aggregate
and boot archive. From a clean checkout, with the pinned native Go cross-toolchain installed:

```sh
make caddy-package GO_PREFIX=/absolute/path/to/pyxis-go REPOSITORY=/absolute/path/to/repository
```

The usual `REPOSITORY_NAME` publication/signing controls apply.
See the [Caddy guide](../docs/userland/caddy.md) for the declared LISTEN authority,
foreground configuration, supplied TLS and feature limits.

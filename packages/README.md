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
`make development-packages` packs `libcxx-dev`, each from a clean checkout.

## Game packages

`doom.lua` and `quake.lua` are program packages: both ports of each game, built
for one system, with the shareware data and its upstream notices. Pack them only
as an explicit step of a release or a deploy to a real machine, from a clean
checkout:

```sh
make game-packages REPOSITORY=/path/to/public-repository
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

The ordinary ports build produces raylib/SDL2 development prefixes and the
small no-audio shapes bundle; neither becomes a default image program. Pack
these opt-in packages from a clean checkout after building that system:

```sh
make -j8 sdk
lua ports/build.lua sdl2 --sdk build/sdk --work build/ports/sdl2
lua ports/build.lua raylib --sdk build/sdk --work build/ports/raylib --sdl2 build/ports/sdl2/stage/dev
python3 tools/pxpkg.py pack --build build packages/raylib-dev.lua /path/to/public-repository
python3 tools/pxpkg.py pack --build build packages/raylib-shapes.lua /path/to/public-repository
```

`raylib-dev` is `toolchain` kind because its static archives are tied to that
system ABI. It contains `raylib/` and `sdl2/` development prefixes. The program
package exports `raylib-shapes`; it needs no external assets or audio authority.
The [raylib milestone](../docs/wip/raylib.md) tracks runtime qualification.

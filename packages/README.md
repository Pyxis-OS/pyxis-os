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

The public shareware recipes retain the upstream distribution notices:

```sh
python3 tools/pxpkg.py pack packages/doom-shareware.lua /path/to/public-repository
python3 tools/pxpkg.py pack packages/quake-shareware.lua /path/to/public-repository
```

These packages are fetched through the configured network repository by the same
`pkg` client on live and installed systems. There is no boot-archive data fallback
or offline seed. A fresh live boot must configure a repository and install the
wanted packages again. Run `pkg install fonts` before `font-min`.

## Owner-local game data

`DOOM_WAD`, `DOOM_DEMOS`, `QUAKE_DATA`, `DIABLO_DATA` and `DUKE3D_DATA` are no
longer image-build inputs. Put explicit file selections into local packer
recipes outside the checkout instead. Keep private recipes, archives and data in
the personal repository, separate from the public fonts/toolchain repository;
never commit them or paste their contents into build/review logs.

For example, a private Quake recipe selects the owned files individually:

```lua
return {
  id = "quake-data",
  version = "owner-1",
  kind = "data",
  depends = {},
  entries = {
    { file = "/private/id1/pak0.pak", at = "id1/pak0.pak" },
    { file = "/private/id1/pak1.pak", at = "id1/pak1.pak" },
  },
}
```

Run `python3 tools/pxpkg.py pack /private/quake.lua /private/repository`.
The packer copies only listed files and rejects links/special entries; it does
not scan an owner directory for arbitrary additions. Uppercase source names are
fine when the recipe selects the canonical lowercase destination below.

| Input | Package | Destination in `tree/` |
| --- | --- | --- |
| Doom IWAD | `doom-data` | `DOOM.WAD` |
| Optional Doom demo recordings | `doom-data` | `e1m1sec.lmp`, `e1m2sec.lmp` |
| Quake PAKs | `quake-data` | `id1/pak0.pak`, optional `id1/pak1.pak` |
| Diablo shareware | `diablo-data` | `spawn.mpq` |
| Duke Nukem 3D | `duke3d-data` | `duke3d.grp` |

Add applicable notices as explicit `licenses/` files. Having a private package
does not grant distribution permission. The optional personal-use programs are
built separately with `make image DEVILUTIONX=1` or `EDUKE32=1`, without opening
any game-data file. Their existing licence restrictions on sharing images and
build bundles still apply. Unsetting these flags removes the programs on the
next image assembly; installing data does not silently add an executable.

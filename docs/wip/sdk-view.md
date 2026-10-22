# Package namespace and the `sdk://` view

Part of step 2 of [package upgrades](pkg-upgrade.md), as task 2c. Decisions
30–43 continue that document's list; it links them here once #1119 merges.
Nothing is implemented yet.

## Decided

30. **No per-package `NAME://` roots.** Every installed package is reachable as
    `pkg://NAME/...` from one provider. A grant can be one package's subtree
    (`pkg://fonts`) or all of `pkg://`. Today's `fonts://` users switch to
    `pkg://fonts/...`. *(chronium, 2026-10-22)*
31. **Views only where tools need a flat namespace:** `bin://`, the existing
    command view, and a new `sdk://` development view. There is no `share://`
    until a second package needs lookup by name. *(chronium, 2026-10-22)*
32. **`sdk://` is the default development path, as on any other system.**
    `sdk://include` and `sdk://lib` combine the base SDK with every installed
    `-dev` package. There are no per-environment selection lists and no setup
    step. `pkg` refuses an install whose files conflict with the view. Both
    compilers, hosted Clang and TCC, search `sdk://include` and `sdk://lib` by
    default. `cc x.c -lraylib -lSDL2` with plain `#include <raylib.h>` and
    `#include <SDL2/SDL.h>`, in their upstream include layout, works with no
    `-I` or `-L` flags. pkg-config supplies library flags, and
    `PKG_CONFIG_PATH` adds libraries installed elsewhere. This amends
    decision 23: the base SDK is the view's base layer, not a root of its own.
    It also supersedes [raylib](raylib.md)'s "do not silently expand the base
    SDK" and its explicitly delegated development root. *(chronium, 2026-10-22)*

## Layout

This is the layout pkgconf and `.pc` authors work against.

```text
sdk://
  include/          headers, each package's upstream layout kept
                    (raylib.h, SDL2/SDL.h, lua5.1/lua.h)
  lib/              static archives (libNAME.a) and startup objects (crt0.o)
  lib/pkgconfig/    NAME.pc
```

- **A `-dev` package's tree** holds `include/`, `lib/` and `lib/pkgconfig/` at
  its root, the same three paths the view has. Only those enter the view. Other
  files, such as `share/` or licences, stay reachable at `pkg://NAME-dev/...`.
  Directories are shared; files are not.
- **Conflicts:** each relative path under `include/` and `lib/` belongs to at
  most one installed member, the base SDK included. `pkg` refuses an install
  that would add a second owner, and names both packages. Upgrading a package
  replaces its own files.
- **Libraries are static.** There are no shared objects. A library that needs
  another names it in its `.pc` file.

### `.pc` files

```text
includedir=sdk://include
libdir=sdk://lib

Name: SDL2
Description: Simple DirectMedia Layer
Version: 2.30.x
Libs: -lSDL2
Cflags: -I${includedir}/SDL2 -D_REENTRANT
```

- Spell `includedir` and `libdir` out in full. `${prefix}/include` with
  `prefix=sdk://` gives `sdk:///include`, which path resolution does not accept.
- Keep upstream `Cflags`. SDL2's own `-I${includedir}/SDL2` still supports code
  that writes `#include "SDL.h"`. Code that writes `<SDL2/SDL.h>` needs no flag.
- pkgconf on Pyxis defaults to:
  - `PKG_CONFIG_LIBDIR=sdk://lib/pkgconfig`;
  - system include path `sdk://include` and system library path `sdk://lib`,
    so it leaves out `-I` and `-L` flags for the view itself;
  - `PKG_CONFIG_PATH` searched first, for libraries elsewhere such as
    `pkg://NAME/lib/pkgconfig` or `home://`.

## How it is built

All *(chronium, 2026-10-22)*.

33. **Task 2c**, after 2b and before 3, covers `pkg://`, the `sdk://` view, the
    base SDK as a package and both compilers' defaults. Task 3's core archive
    then no longer carries `sdk/`.
34. **The base SDK is an ordinary `-dev` package**, `pyxis-dev`: libc,
    libpyxis, libterm, libfont, crt0 and compiler-rt builtins, with headers.
    The view has no special base layer, and `base` depends on `pyxis-dev`.
    `pkg` and `sdk` join the reserved package IDs.
35. **`-dev` is a package kind**, `dev`. It must name its release `abi`, as
    programs do, and has no commands. By convention its ID ends in `-dev`, but
    `pkg` checks the kind, not the name.
36. **The kernel serves the view from an index written by `pkg`.** At publish,
    `pkg` writes `sets/N.sdk`, mapping each view path to its package, beside
    `sets/N.commands`, and refuses conflicts there. The kernel serves `sdk://`
    as a read-only npfs view, as it serves the command view, and caches the
    index per set, since sets are immutable.
37. **The view is live**, like the command view: a `-dev` package is in
    `sdk://` as soon as its set publishes, with no reboot.
38. **Bundles inherit `sdk://`, not `pkg://`.** `sdk://` is the public
    development surface: every space holding the packages source has it, and
    bundles inherit it like `boot://`, so hosted Clang needs no flags.
    Spaces holding the packages source also get `pkg://`. Bundles do not
    inherit it; they keep declaring the package subtrees they read, which
    appear as `pkg://NAME` (for example `pkg://fonts`).
39. **`pkg://` covers every installed package of every kind**, not only data
    packages with files. The automatic `ID://` fallback in path resolution and
    the shell goes, and the 256-root limit becomes the set's package limit.
40. **TCC** searches `sdk://include` and `sdk://lib` instead of
    `{R}/usr/...`. Its private `lib/tcc`, with `libtcc1.a` and its headers,
    moves into the `tcc` package.
41. **Hosted Clang:**
    - On a Pyxis host, the Pyxis driver searches `sdk://include` and
      `sdk://lib`, with no `usr/`.
    - Its three bundled copies of the SDK sysroot go.
    - The host cross-compiler keeps `--sysroot` and `usr/`.
    - `clang++` takes its headers and libraries from a `libcxx-dev` package.
    - The owner rebuilds the hosted toolchain once; the compiler container is
      unaffected.
42. **pkgconf defaults to static output**, as with `--static`, since every
    library is static. `Libs.private` is then included, so `pkg-config --libs
    raylib` also yields `-lSDL2`.
43. **raylib:** `raylib-dev` and `sdl2-dev` are separate `dev` packages with a
    flat `include/` and `lib/`. They replace the nested `raylib/` and `sdl2/`
    prefixes on `port/raylib-shapes`. The shapes example builds with
    `cc shapes.c -lraylib -lSDL2` under both compilers.

## Revisit points

- **The view as a userspace provider.** Redo `sdk://` as a provider once
  providers can serve directory lookups; today they open files only, and
  Clang's header search looks up directories. *(chronium, 2026-10-22)*

## Future ideas

- **Bundles declare `sdk://`**, for example in Clang's manifest, instead of
  inheriting it. *(chronium, 2026-10-22)*

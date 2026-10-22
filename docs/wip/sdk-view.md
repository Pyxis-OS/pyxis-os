# Package namespace and the `sdk://` view

Part of step 2 of [package upgrades](pkg-upgrade.md). Decisions 30–32 continue
that document's list; it links them here once #1119 merges. Decided items are
marked with chronium's name; everything under [Proposed](#proposed) is a default
awaiting a decision.

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

## Proposed

Defaults for chronium's decision. None is implemented.

1. **Placement: task 2c**, after 2b and before 3. It covers `pkg://`, the
   `sdk://` view, the base SDK as a package and both compilers' defaults. Task
   3's core archive then no longer carries `sdk/`. *Alternative:* fold it into
   task 3, which already moves `sdk/` out of the archive (decision 23). That
   makes task 3 the largest step-2 task.
2. **The base SDK is an ordinary `-dev` package**, `pyxis-dev`: libc, libpyxis,
   libterm, libfont, crt0 and compiler-rt builtins, with headers. The view then
   has no special base layer, and `base` depends on `pyxis-dev`. `pkg` and `sdk`
   join the reserved IDs, since decision 23's package name `sdk` now names the
   view.
3. **`-dev` is a package kind**, `dev`. It must name its release `abi`, as
   programs do, and has no commands. By convention its ID ends in `-dev`, but
   `pkg` checks the kind, not the name. *Alternative:* the `-dev` suffix alone
   on a `data` package, which carries no ABI.
4. **Mechanism: an index written by `pkg`, served by the kernel.** At publish,
   `pkg` writes `sets/N.sdk`, mapping each view path to its package, beside
   `sets/N.commands`. It refuses conflicts there. The kernel serves `sdk://` as
   a read-only npfs view, as it serves the command view, and caches the index
   per set; sets are immutable. *Rejected:* a userspace provider, because
   provider roots open files only, and Clang's header search looks up
   directories.
5. **Live, like the command view.** A `-dev` package is in `sdk://` as soon as
   its set publishes, with no reboot.
6. **Who sees `pkg://` and `sdk://`:** every space that holds the packages
   source, as ordinary roots, and bundles inherit both like `boot://`. This is
   needed for `clang.pxb` to find `sdk://` with no flags. It reverses the
   2026-10-21 rule that a bundle reads only the packages its manifest grants,
   for these two roots. *Alternative:* a manifest grant kind for `sdk://`,
   which every compiler and build tool would then have to declare.
7. **`pkg://` covers every kind and every installed package**, not only data
   packages with files. The automatic `ID://` fallback in path resolution and
   the shell goes. The 256-root limit becomes the set's package limit.
8. **TCC:** its search paths become `sdk://include` and `sdk://lib` instead of
   `{R}/usr/...`. Its private `lib/tcc` (`libtcc1.a` and headers) moves into
   the `tcc` package.
9. **Hosted Clang:**
   - On a Pyxis host, the Pyxis driver searches `sdk://include` and
     `sdk://lib`, with no `usr/`.
   - Its three bundled copies of the SDK sysroot go.
   - The host cross-compiler keeps `--sysroot` and `usr/`.
   - `clang++` takes its headers and libraries from a `libcxx-dev` package.
   - This needs a hosted toolchain rebuild by the owner. The compiler
     container itself is unaffected.
10. **pkgconf defaults to static output**, as with `--static`, since every
    library is static. `Libs.private` is then included, so `pkg-config --libs
    raylib` also yields `-lSDL2`.
11. **raylib:** `raylib-dev` and `sdl2-dev` become separate `dev` packages with
    a flat `include/` and `lib/`. They replace the nested `raylib/` and `sdl2/`
    prefixes on `port/raylib-shapes`, and the in-Pyxis paragraph of
    [raylib's packaging and linking](raylib.md#packaging-and-linking) changes
    with them. The shapes example then builds with
    `cc shapes.c -lraylib -lSDL2` under both compilers.

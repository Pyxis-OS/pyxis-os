# Package namespace and the `sdk://` view

Part of step 2 of [package upgrades](pkg-upgrade.md), as task 2c. Decisions
30–43 continue that document's list.
The package namespace and SDK view are not implemented yet. The pkgconf port
and development metadata are qualified separately below.

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
    step. `pkg` refuses an install whose files conflict with the view. The
    compiler, hosted Clang, searches `sdk://include` and `sdk://lib` by
    default. `cc x.c -lraylib -lSDL2` with plain `#include <raylib.h>` and
    `#include <SDL2/SDL.h>`, in their upstream include layout, works with no
    `-I` or `-L` flags. pkg-config supplies library flags, and
    `PKG_CONFIG_PATH` adds libraries installed elsewhere. This amends
    decision 23: the base SDK joins the view like any `-dev` package
    (decision 34), not as a root of its own.
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
    base SDK as a package and hosted Clang's defaults. Task 3's core archive
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
38. **Bundles inherit neither `sdk://` nor `pkg://`.** A bundle's manifest
    declares what it reads: package subtrees as `pkg://NAME` (for example
    `pkg://fonts`), and `sdk://` itself; hosted Clang's manifest declares
    `sdk://`. The 2026-10-21 rule that a bundle reads only what its manifest
    grants stays intact. Plain programs see `sdk://` like any other root,
    through their space's root list: the installed configuration's
    initial space definitions include `sdk://` where development happens.
    Spaces holding the packages source get `pkg://`.
39. **`pkg://` covers every installed package of every kind**, not only data
    packages with files. The automatic `ID://` fallback in path resolution and
    the shell goes. The 256-root limit goes too (decision 56).
40. **TCC is retired.** It leaves `base` and the image build; its port stays
    frozen in the ports repository's history. 2c does not touch it, and
    decision 23's "`tcc` depends on `sdk`" is moot.
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
    `cc shapes.c -lraylib -lSDL2` under hosted Clang.

## Task 2c details

All *(chronium, 2026-10-23)*.

44. **The index** `sets/N.sdk` is a new compact format with its own header
    magic: file records only, sorted by view path, each naming its package and
    version. Directories are implied by path prefixes. The kernel validates
    the index on capture and caches it per set, capturing again when
    `current` names another set.
45. **What enters the view:** a `dev` package's `include/**`, the regular files
    directly in `lib/`, and `lib/pkgconfig/*`. Anything else, such as
    `lib/cmake/` or `share/`, stays reachable at `pkg://NAME`.
46. **Conflicts are refused by `pkg` at publish.** The kernel rejects only a
    malformed index.
47. **Spaces** get the views as roots in `boot.lua`: `sdk = { view = "sdk" }`
    and `pkg = { view = "pkg" }`, accepted only where the packages volume is
    defined.
48. **Bundles** declare `"sdk": true` for `sdk://`, and `"packages": [ ... ]`
    for a `pkg://` holding only those packages.
49. **LLD** receives `-L sdk://lib` from Clang, and `lld.pxb` declares
    `sdk://` itself.
50. **`pyxis-dev`** holds today's archive SDK plus `pyxis.ld` and `pyxis.pc`,
    without the `libfat32` archives.
51. **`libcxx-dev`** holds the `c++/v1` headers, libc++, libc++abi, libunwind
    and `libcxx.pc`.
52. **The `dev` kind names its `abi` but exports no commands.** `pkg` separates
    "names an ABI" from "may export commands", which today are one rule.
53. **Reserved package IDs** keep today's list and add `pkg` and `sdk`.
54. **Live boots** have no `sdk://` unless `pyxis-dev` is installed.
    `boot://sdk` leaves the archive in task 3 (decision 33).
55. **raylib:** `raylib-dev` and `sdl2-dev` switch from `toolchain` to `dev`.
56. **No fixed package limit per set.** The view sizes itself from the set and
    refuses cleanly past a memory bound, with headroom, instead of a fixed 256.
57. **The work is three tasks:**
    - [x] **2c-1. `pkg://`:** today's package-roots view becomes the `pkg`
      root over every package kind; the `ID://` fallback in path resolution
      and the shell goes; `fonts://` users and the ports' package roots move to
      the new forms. Implemented with the kernel's `ROOTS_SELECT` for a
      bundle's `pkg://`, the 1 MiB index bound, and the removal of the
      `package = "ID"` space roots
      ([reaching package files](../interfaces/packages.md#reaching-package-files)).
    - [ ] **2c-2. `sdk://`:** the `dev` kind, `pyxis-dev`, `libcxx-dev`,
      `sets/N.sdk` and the kernel view.
    - [ ] **2c-3. Hosted Clang:** driver defaults and dropping the bundled
      sysroots. It stops for the owner's hosted LLVM rebuild.

## pkgconf

pkgconf is a ports leaf tool, built and packaged like the other ports. It is
delta's task, beside 2c rather than part of it: it ports pkgconf with the
defaults under [`.pc` files](#pc-files) and adds `.pc` files to the `-dev`
packages. 2c does not depend on it, because the compiler needs no flags for
the view itself. *(chronium, 2026-10-22)*

- [x] Port pkgconf with the `pkg-config` alias and native/static defaults; stage
  `.pc` metadata in every existing development prefix and the base SDK/C++
  runtime export. [Qualification](../development/experiments/pkgconf.md) uses an
  explicitly granted SDK-layout fixture, not the unimplemented package view.
  Task 2c must include those files in its `pyxis-dev`, `libcxx-dev` and ports
  `-dev` package trees.

## Revisit points

- **The view as a userspace provider.** Redo `sdk://` as a provider once
  providers can serve directory lookups; today they open files only, and
  Clang's header search looks up directories. *(chronium, 2026-10-22)*

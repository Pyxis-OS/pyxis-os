# Dynamic linking

Accepted by chronium (2026-11-02): the round-1 decisions and the grouped defaults
below, then direct `.pxe` launches, declared library directories, the explicit
`libs` source, the IMPORT directory right, IMPORT on Development's home, and
bundles declaring their library directories like any program.
Release placement (0.0.11 or 0.0.12) is open. No task has started; each task
starts when the owner says so.

## Goal

Programs and ports stop statically linking libc and large libraries (SDL, Mesa,
LLVM). The wins are update and rebuild decoupling, smaller packages, one libc to
fix and `dlopen`-based ports. Saving memory is not a goal. The CI goal is that
pyxis-os no longer builds userland and ports: a kernel change rebuilds the kernel
and the runtime libraries, not every port.

## Starting point

- [P1F](../../include/pxe/p1f.h) images have fixed addresses and no relocations,
  symbols or imports. LLD in the `pyxis-llvm` fork writes them; its driver
  refuses `-shared`, PIC, PIE and every TLS model but local-exec
  ([toolchain](../development/llvm-toolchain.md)).
- The [kernel loader](../kernel/program-loading.md) copies segments into private
  pages with W^X. Executable user pages come only from it and from
  [JIT](../interfaces/jit.md) PROTECT; there is no executable file mapping.
- TLS is executable-only variant II, set up by libc
  ([threads](../interfaces/threads.md#tls-and-runtime-keys)).
- Libraries are static archives built `-fno-pic`, there is no `dlfcn.h`, and
  ports compile out their `dlopen` callers.
- The SDK is assembled from userland sources, every port depends on the whole
  SDK, userland depends on every port, and every package records the pyxis-os
  commit's ABI. A userspace pin or an ABI change therefore rebuilds everything:
  63% of PR runs, with ports about 100 s of a 145–170 s build (CI measurement,
  2026-10-29).

This replaces the outline in [packages](https://git.internal/PyxisOS/pyxis-os/src/commit/93bdcf03303655edd67874f8bca061f5418a23d7/docs/wip/packages.md#owner-direction-2026-10-18),
which it refines, and serves the program side of
[#1337](https://git.internal/PyxisOS/pyxis-os/issues/1337).

## Accepted contract

### Formats

`include/pxe/p1f.h` and `include/pxe/p1d.h` define the layouts; the kernel,
the loader and LLD's writer follow them.

- **Magics** name the format: `PXE1FLAT` (P1F), `PXE2FLAT` (P2F), `PXE1DYNM`
  (P1D) and `PXE2DYNM` (P2D). D means dynamic; 2 means with symbols. There is no
  separate version field; until a public release a change edits the layout in
  place.
- **P2F** is P1F followed by a symbol section that is never mapped: a count
  header, the full `Elf64_Sym` table (locals included) and its strings.
  Stripping truncates the file there and turns the magic's 2 into 1, which gives
  a valid P1F. Backtraces read it (#1337).
- **P1D** extends the P1F header with flags (library or executable) and the
  address and size of a dynamic section. That section lies in a read-only
  segment, so the loader reads the tables from the mapped image, never the file
  again. It points to the library name (`.pxl` only), needed library names, the
  program's declared library directories (executables only, see
  [library lookup](#library-lookup)), the symbols with a GNU hash table over the
  exports, RELR relative relocations, other relocations, init/fini arrays, the
  TLS template and the `eh_frame_hdr` location. **P2D** is P1D with the P2F
  symbol section.
- Table records use ELF's encodings (`Elf64_Sym`, GNU hash, RELR,
  `Elf64_Rela`); the container, the header and the rules are native. There are
  no ELF files, program headers, `.dynamic` tags or interpreter.
- RELRO is its own whole-page segment, flagged `P1D_RELRO`: mapped writable for
  relocation, then sealed read-only. A library's addresses are offsets from a
  base the kernel chooses.
- Relocation kinds are a closed list of x86-64 types: `RELATIVE`, `64`,
  `GLOB_DAT`, `JUMP_SLOT` (bound eagerly), `DTPMOD64`, `DTPOFF64` and
  `TPOFF64`. Copy, `IRELATIVE`, TLSDESC and text relocations are refused, by
  LLD and again by the loader.
- The kernel checks the container, W^X, RELRO segments and that the dynamic
  section lies in one read-only segment; it never reads the tables. The loader
  checks every table range, string and relocation target.
- **Symbols by default.** LLD writes P2F or P2D unless the link strips (`-s`).
  Until 0.x, release and package builds keep their symbols too, for on-device
  backtraces; nothing strips them.
- `.pxl` libraries are position-independent and placed at load. Dynamic `.pxe`
  executables stay fixed at `0x400000` with imports; there is no PIE and no ASLR.
- LLD writes P2F, P1D and P2D from its ELF dynamic-link layout and reads `.pxl`
  files as link inputs. The SDK ships `.pxl` files, not link stubs.

### Loading and authority

- **Kernel:** one operation maps a dynamic image into the calling process from a
  FILE handle, at a fixed address for an executable or a kernel-chosen base for a
  library. It reuses image capture, segment validation and W^X: text R/X, rodata
  R, data and GOT RW, no relocation. A second, one-way call seals the image's
  RELRO range from RW to R. There is no release operation.
- **Kernel interface (accepted by chronium, 2026-11-02):**
  - **Calls:** `MEMORY_MAP_IMAGE` and `MEMORY_SEAL_IMAGE` on the MEMORY object,
    under `MEMORY_RIGHT_MANAGE`, in the common caller-CPU VM transaction.
  - **Map result:** identity, base, entry and the dynamic section's address.
  - **Placement:** an executable maps at its fixed addresses and fails if any
    is taken. A library gets a base the kernel chooses, with its whole span
    reserved.
  - **Rights:** IMPORT travels as `DIRECTORY_RIGHT_IMPORT` and
    `FILE_RIGHT_IMPORT`, outside `DIRECTORY_RIGHTS` and `FILE_RIGHTS`, so it
    is never carried unless named. A launch grant can add the kernel-only
    `FILE_RIGHT_PROGRAM` to a file the caller can read. PROGRAM maps only an
    executable; IMPORT maps only a library.
  - **Memory:** image segments are private memory of their own kind, counted
    as committed. Nothing releases them before exit.
  - **Checks:** `kernel/image.c` validates the container and never reads the
    tables. The whole file is captured, symbol section included.
  - **Sources:** `pkg://` and `app://` always carry IMPORT. An init-config
    root can ask for it with `import = true`, and the live and installed
    Development homes do; `tmp`, `host` and Read-only's roots don't. IMPORT
    through manifest grants waits for task 5 or 6.
- **Authority: the IMPORT right** (name chosen by chronium, 2026-11-02).
  IMPORT is a directory right meaning that libraries reached through this
  directory may be imported into a process. Importing a library means the map
  operation places its code in a process that is already running. IMPORT is not
  a right to execute anything:
  - a program's own launch authorizes the program, as for any P1F today;
  - a `.pxl` cannot be run, because it has no entry point and the launcher
    refuses it.

  So IMPORT decides only which directories may supply code to a process that
  was launched for other reasons. Mapping a library needs its FILE handle and
  IMPORT on the directory source it was opened from. IMPORT attenuates like any
  directory right: a directory opened from a source with IMPORT keeps it only if
  the opener asks for it.
- **Which sources carry IMPORT (accepted, chronium 2026-11-02):** the package
  view, the library view and a bundle's `app://` always do. The Development
  space's `home://` root does by default, for programs and libraries under
  development. Hardened spaces (Read-only, and any space that does not set it)
  grant IMPORT only on the package view, the library view and `app://`. tmp and
  host never carry it by default. A manifest grant may name another directory
  with IMPORT. Without JIT, a process runs only its launched program and
  libraries imported from directories that carry IMPORT.
- **The executable itself** is authorized by its launch, as a P1F is today: the
  launcher hands the loader the program's FILE handle as a one-shot image
  capability. A dynamic program therefore runs wherever a static one would;
  only its libraries need IMPORT.
- **Loader:** `loader.pxe`, a static P1F at a fixed base away from `0x400000`,
  loaded by the kernel like any program. The launcher (libpyxis `program_open`)
  starts it for a P1D/P2D image, both for bundle programs and for a `.pxe` run
  directly by path, outside any bundle. It passes the program's image capability
  and the library sources from [library lookup](#library-lookup) as startup
  capabilities. The loader maps the program and its libraries, relocates eagerly,
  seals, builds the TLS block and enters the program. The file names no
  interpreter.
- **Binding:** eager, no lazy PLT, full RELRO. Symbols resolve in one global
  scope in load order. There is no symbol versioning.
- Library text is copied into each process, as program text is today. Sharing
  immutable frames between processes is a later kernel option needing no format
  change.

### TLS

The loader builds one static TLS block for the executable and every library
loaded at startup, so every model resolves to a fixed FS offset. Libraries use
general-dynamic, local-dynamic or initial-exec; executables keep local-exec for
their own variables and use initial-exec for a library's. TLSDESC waits until a
library needs it, as one more relocation type. libc's bootstrap takes the layout
from the loader; the FS and TCB rules are unchanged. `dlopen` of a library with
TLS is refused with a clear error until a consumer needs it.

### Names, packages and versions

- A library's name is its file name, `libNAME.pxl` or `libNAME.N.pxl`, stored
  in the `.pxl`, so `-lNAME` and build systems work unchanged. Incompatible
  versions are different names (`libSDL2.pxl`/`libSDL3.pxl`,
  `libfoo.1.pxl`/`libfoo.2.pxl`). There are no `.pxl.REVISION` files or links to
  stable names.
- Packages export libraries explicitly, as they export commands:
  `libraries = { ["libSDL2.pxl"] = { path = "lib/libSDL2.pxl" } }`. Set
  publication refuses duplicate names and writes `sets/N.libraries` beside
  `sets/N.commands`.
- The set's library view is taken at launch and kept for the process's life,
  `dlopen` included, so `pkg update` never mixes versions inside one process.
- `pxpkg pack` refuses a program whose needed names its declared dependencies
  do not export. `-dev` packages keep headers and `.pc` files; the `.pxl` belongs
  to the runtime package.
- **Two identities:** the kernel ABI (`include/abi`) is recorded only by the
  runtime package (libc, libpyxis, the loader, the static rescue set). The
  runtime ABI names libc, libpyxis and libc++ exports and public headers; it is
  computed by a textual comparison like the [kernel ABI's](../interfaces/abi.md)
  and changes only when they do. Program and library packages record the runtime
  ABI and their needed names, not the pyxis-os commit.

### Library lookup

Accepted (chronium, 2026-11-02). The loader searches only sources it was handed
at launch, in this order, and the first source holding the name wins:

1. **An explicit library source**, when the launch supplies one: a directory
   capability under the reserved startup name `libs`, given by the launcher's
   override helper (in the shell, a captured directory value passed to the
   launch). It serves layouts the program does not declare, such as testing a
   rebuilt library against an installed program. It is a capability, not a path
   in an environment variable.
2. **The program's declared library directories**, in their recorded order.
3. **The set's library view.**

Every source must carry IMPORT; a source without it is not searched. When a
name is not found, the loader's error names the library and every source it
skipped for lacking IMPORT, so a missing right is not mistaken for a missing
file. There is no environment search path and no fixed directory convention.

**Declared directories.** The executable records where its own libraries live,
as paths relative to its own directory: the role ELF `RUNPATH` with `$ORIGIN`
plays elsewhere. A game in development that vendors its libraries beside it
links with a `lib` entry; a program in `bin/` with libraries in a sibling `lib/`
records `../lib`.

- LLD writes them from `-rpath` entries of the form `$ORIGIN` or
  `$ORIGIN/PATH`, recorded as `.` and `PATH`; the familiar spelling keeps port
  build systems working. Any other `-rpath` entry is refused at link time. The
  Pyxis CMake platform file turns off CMake's automatic build and install rpaths,
  which would otherwise add absolute host paths.
- An entry is a relative native path: no scheme, no root, no empty component.
  `..` is allowed but never climbs above the root through which the program was
  reached; a bundle program never climbs above its `app/` directory.
- The launcher resolves the entries, relative to the directory it opened the
  executable from and within that root. It hands the loader exactly those
  directories, opened with LOOKUP, READ_FILES and IMPORT when the source
  carries IMPORT. The loader never receives the executable's directory itself
  or its root.
- **The executable's own directory is not searched unless declared** (an entry
  of `.`). Programs often share a directory, such as a command list or a home
  folder, with unrelated files; an implicit same-directory search would bind any
  `.pxl` dropped there. Making it an explicit entry keeps the choice with the
  program's author and visible in the binary.
- Libraries record no directories of their own. Their needed names resolve in
  the same process-wide sources as the executable's.

**Bundles (accepted, chronium 2026-11-02).** There is no fixed `app://lib/`
and no manifest `library_dirs` field: one mechanism serves both cases.
- A bundle program declares its library directories like any program,
  typically `../lib` from `app/bin/` or `lib` beside an entry at the top of
  `app/`.
- The declarations are resolved within `app/`, which carries IMPORT.
- The same binary works run directly during development and inside its bundle,
  without relinking.

### dlopen

libc provides `dlfcn.h`: `dlopen`, `dlsym`, `dlerror`, `dladdr` and a module
iterator, which libunwind uses instead of linker-script bounds. A name without
`/` or `:` resolves like a needed name, through the
[same sources](#library-lookup); a URI needs IMPORT on its source.
`RTLD_LOCAL` and `RTLD_GLOBAL` work, and `dlsym(RTLD_DEFAULT)` serves LLVM's
process symbols. `dlclose` drops a reference and never unmaps.

### Static programs

`loader.pxe`, boot-init and the `boot/rescue.list` set, `pkg` included, stay
static: they repair a broken library set. Go's linker keeps writing P1F, and
Rust stays static for now. Everything else becomes dynamic.

### Build split

Artifacts flow one way. pyxis-os publishes the kernel and its ABI headers.
pyxis-userland builds the runtime and its SDK part against those headers and
publishes a runtime snapshot and the SDK. pyxis-ports builds against a published
SDK and publishes packages. pyxis-os assembles images from the kernel and a
pinned runtime/core snapshot (exact version and SHA-256) from the owner's
artifact store, not from source submodules; the userspace and ports gitlinks
leave pyxis-os or remain provenance only. Each repository runs its own CI:

| Repository | Per-PR build |
| --- | --- |
| pyxis-os | kernel, ABI check, header artifact, image assembly from the pinned snapshot |
| pyxis-userland | runtime and programs against the published SDK and ports development snapshot |
| pyxis-ports | changed ports and their dependents only; Mesa only when it or something below it changes |

A kernel ABI change goes dependency-first: a userland PR publishes a runtime
built against the kernel PR's headers, then the pyxis-os PR moves its pin.

### Debugging

The loader keeps a module list at a known symbol for the debugger. Builds keep
producing ELF side files for gdb (`-Wl,--oformat=elf`). P2D symbol tables give
on-device backtraces.

## Tasks

Each task is a focused PR set (fork, pyxis-os, userland, ports as needed), with
merge order stated. Validation follows the usual rules: builds, interactive QEMU
and debugger inspection; native vela checks where named. No new tests or CI are
added except the build split itself.

- [ ] **0. Baselines.** Before task 1: launch time to `main` for a small command,
  Neovim and an SDL+Mesa game; the 1,024-launch session from the
  [program-capacity record](../development/experiments/program-capacity/README.md);
  package archive sizes (per package, base set) and ISO size; owned backing per
  process; CI build time per PR class with the 2026-10-29 phase method. QEMU and
  native vela, matched configurations. Recorded in an experiment record.
  Progress: the QEMU, CI and size rows are in the
  [baseline record](../development/experiments/dynamic-linking-baseline.md); the
  native vela rows wait for a scheduled run.
- [ ] **1. Formats and toolchain.** Format headers beside `p1f.h`; in the fork,
  LLD writes P2F/P1D/P2D and reads `.pxl`; the driver allows `-shared`, `-fPIC`,
  GD/LD/IE, `-z now -z relro` by default and no copy relocations; LLD records
  `$ORIGIN` `-rpath` entries as declared library directories and refuses other
  `-rpath` entries. Libraries are `libNAME.pxl`, their stored name is the file
  name, and `-lNAME` finds them. The pin update also teaches the kernel loader,
  userland, `pxpkg` and the toolchain scripts to accept P2F, since every
  unstripped program becomes P2F, and adds `-fdirect-access-external-data` to
  the kernel, which keeps its code unchanged. Validation: build a sample library
  and executable, inspect both on the host, confirm refused relocation kinds
  and rpath forms, the recorded directories, and stripped-P2F → P1F. **Owner
  action: build and publish the compiler container** (new `pyxis-llvm` pin and
  image tag). No further rebuild unless the format changes later.
  Progress: the headers and pyxis-llvm #23 are in review, validated on the host.
- [x] **2. Kernel map and seal.** The map operation, the one-way RELRO seal and
  the IMPORT directory right, carried by the package and app views, and set on
  the Development space's `home://` in the live and installed defaults.
  Validation in QEMU with the debugger: fixed and kernel-chosen placement, W^X on
  every page, refusal without IMPORT or of an invalid image, IMPORT present
  on Development's home and absent on Read-only's, seal irreversibility, failure
  unwind and process-exit reclamation.
  Done: pyxis-os #1718 (kernel and ABI) and pyxis-userland #469 (the `import`
  key, IMPORT on the packages volume and Development homes), validated in QEMU
  as recorded in #1718.
- [ ] **3. Loader and the C runtime.** `loader.pxe`; `libc.pxl` and
  `libpyxis.pxl`; the TLS block and loader-driven `libc_enter`; `dlfcn.h`; the
  module iterator for libunwind; the debugger module list; launcher selection in
  `program_open`, for bundles and for a `.pxe` run directly; resolution of
  declared library directories and the `libs` explicit source. CMake declares
  shared libraries supported and adds no automatic rpaths; this waits for
  `libc.pxl` because ports such as SDL build shared libraries whenever CMake
  allows them. One small userland command becomes dynamic. Validation in QEMU: the same program run
  directly from Development's home with vendored libraries in a declared
  directory, and inside a bundle; a `libs` source overriding a set library; the
  same launches from Read-only, where home libraries are skipped and the error
  names the sources lacking IMPORT; `..` stopping at the program's root and at
  `app/`; gdb with ELF side files; `dlopen`/`dlsym` from a sample; refused
  `dlopen` of a TLS library.
- [ ] **4. C++ runtime and userland.** `libc++`, `libc++abi` and `libunwind` as
  `.pxl`; userland programs outside the rescue set become dynamic; the rescue set
  stays static. Validation: QEMU boot and rescue boot, launches across spaces,
  C++ exceptions thrown across a library boundary, G8 launch numbers against
  task 0.
- [ ] **5. Packages.** `libraries` exports, `sets/N.libraries`, the per-process
  library view, the `pxpkg pack` check, the runtime ABI comparison and the
  runtime package; program packages record the runtime ABI. Validation in QEMU:
  install, update and remove with libraries; a running process keeps its view
  across `pkg update`; duplicate-name and missing-export refusals.
- [ ] **6. Ports.** Mesa (OSMesa) and SDL2/SDL3 as `.pxl` first, with their
  consumers relinked (Doom, Quake, raylib, ImGui, SoH, DevilutionX, the private
  game port); then zlib, libpng, Lua and mbedtls as their consumers move.
  Validation: QEMU and a native vela run of an SDL+Mesa game; launch and size
  numbers against task 0.
- [ ] **7. dlopen consumers.** LLVM ORC process symbols, Lua C modules; cmus when
  it is ported. Validation: each consumer's own path in QEMU.
- [ ] **8. Build split.** Runtime snapshot and SDK publication from userland CI,
  package publication from ports CI, image assembly in pyxis-os from the pinned
  snapshot, gitlinks removed or provenance only. Can start after task 5.
  Validation: CI build times per PR class against task 0. **Owner actions:
  choose the artifact store (location and naming) and enable CI in
  pyxis-userland and pyxis-ports.** No CI configuration changes before that go.

On completion, the contract moves into the subsystem references
([program loading](../kernel/program-loading.md), [threads](../interfaces/threads.md),
[packages](../interfaces/packages.md), [program bundles](../userland/program-bundles.md),
[SDK](../development/sdk.md), [toolchain](../development/llvm-toolchain.md) and
[SDK and repositories](../development/sdk-and-repositories.md)), and this
document is removed.

## Owner actions

| When | Action |
| --- | --- |
| Open | Release placement (0.0.11 or 0.0.12) |
| Task 1 | Build and publish the compiler container |
| Before task 8 | Choose the artifact store; enable CI in pyxis-userland and pyxis-ports |

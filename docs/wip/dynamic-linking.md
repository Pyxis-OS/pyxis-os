# Dynamic linking

Accepted by chronium (2026-11-02): the round-1 decisions and the grouped defaults
below. Release placement (0.0.11 or 0.0.12) is open. No task has started; each
task starts when the owner says so.

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

This replaces the outline in [packages](packages.md#owner-direction-2026-10-18),
which it refines, and serves the program side of
[#1337](https://git.internal/PyxisOS/pyxis-os/issues/1337).

## Accepted contract

### Formats

- **P2F** is P1F plus an optional symbol section. Stripping it yields a valid
  P1F. Backtraces use it (#1337).
- **P1D** is the dynamic form: P1F's segments plus a dynamic section with the
  library name (`.pxl` only), needed library names, exports with a hash table,
  imports, a RELRO range, the TLS template, init/fini arrays and the
  `eh_frame_hdr` location. **P2D** is P1D with the P2F symbol section.
  D means dynamic; 2 means with symbols.
- Relocation kinds are a closed list: relative, absolute/GOT, jump slot and the
  TLS module/offset kinds. Text relocations and copy relocations are refused.
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
- **Authority:** mapping needs the FILE handle and a new MAP_CODE right on the
  directory source it was opened from. The package view, the library view and a
  bundle's `app://` carry MAP_CODE; home, tmp and host do not, unless a manifest
  grant names a directory with code rights. Without JIT, a process runs only code
  its launcher chose or code from a source that carries MAP_CODE.
- **Loader:** `loader.pxe`, a static P1F at a fixed base away from `0x400000`,
  loaded by the kernel like any program. The launcher (libpyxis `program_open`)
  starts it for a P1D/P2D image, with the program's FILE handle and the library
  sources as startup capabilities. It maps the program and its libraries,
  relocates eagerly, seals, builds the TLS block and enters the program. The file
  names no interpreter.
- **Binding:** eager, no lazy PLT, full RELRO. Symbols resolve in one global
  scope in load order. There is no symbol versioning.
- Library text is copied into each process, as program text is today. Sharing
  immutable frames between processes is a later kernel option needing no format
  change.

### TLS

The loader builds one static TLS block for the executable and every library
loaded at startup, so every model resolves to a fixed FS offset. Libraries use
general-dynamic or TLSDESC; executables keep local-exec. libc's bootstrap takes
the layout from the loader; the FS and TCB rules are unchanged. `dlopen` of a
library with TLS is refused with a clear error until a consumer needs it.

### Names, packages and versions

- A library's name is `NAME` or `NAME.N`, stored in the `.pxl`. Incompatible
  versions are different names (`SDL2`/`SDL3`, `foo.1`/`foo.2`). There are no
  `.pxl.REVISION` files or links to stable names.
- Packages export libraries explicitly, as they export commands:
  `libraries = { SDL2 = { path = "lib/SDL2.pxl" } }`. Set publication refuses
  duplicate names and writes `sets/N.libraries` beside `sets/N.commands`.
- The loader resolves a needed name in `app://lib/` first, then in the set's
  library view. It takes the view at launch and keeps it for the process's life,
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

### dlopen

libc provides `dlfcn.h`: `dlopen`, `dlsym`, `dlerror`, `dladdr` and a module
iterator, which libunwind uses instead of linker-script bounds. A name without
`/` or `:` resolves like a needed name; a URI needs MAP_CODE on its source.
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
- [ ] **1. Formats and toolchain.** Format headers beside `p1f.h`; in the fork,
  LLD writes P2F/P1D/P2D and reads `.pxl`; the driver allows `-shared`, `-fPIC`,
  GD/LD/IE/TLSDESC, `-z now -z relro` by default and no copy relocations; CMake
  declares shared libraries supported. Validation: build a sample library and
  executable, inspect both on the host, confirm refused relocation kinds and
  stripped-P2F → P1F. **Owner action: build and publish the one compiler
  container** (new `pyxis-llvm` pin and image tag). No further rebuild unless the
  format changes later.
- [ ] **2. Kernel map and seal.** The map operation, the one-way RELRO seal and
  the MAP_CODE directory right, carried by the package and app views. Validation
  in QEMU with the debugger: fixed and kernel-chosen placement, W^X on every
  page, refusal without MAP_CODE or of an invalid image, seal irreversibility,
  failure unwind and process-exit reclamation.
- [ ] **3. Loader and the C runtime.** `loader.pxe`; `libc.pxl` and
  `libpyxis.pxl`; the TLS block and loader-driven `libc_enter`; `dlfcn.h`; the
  module iterator for libunwind; the debugger module list; launcher selection in
  `program_open`. One small userland command becomes dynamic. Validation: QEMU
  launch, gdb with ELF side files, `dlopen`/`dlsym` from a sample, refused
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

# Pyxis target toolchain

Build this compiler separately from normal kernel, SDK and application builds.
It targets `x86_64-unknown-pyxis` with Clang, LLD and compiler-rt builtins from
LLVM 23.1.3. `build.sh` builds the pinned commit of the
[`pyxis-llvm`](https://git.internal/PyxisOS/pyxis-llvm) fork: the release with
the Pyxis commits on the `pyxis-23.1.3` branch. The compiler has no dependency
on a particular checkout's runtime headers.

## Building

On a Linux host, install CMake, Ninja, Python 3, git and a host C/C++ compiler.
From the Pyxis checkout:

```sh
JOBS=8 toolchain/build.sh "$HOME/opt/pyxis-llvm" /tmp/pyxis-llvm-build
export PATH="$HOME/opt/pyxis-llvm/bin:$PATH"
make clean
make -j16 image
```

Use a fresh work directory; the script refuses to reuse its build directories.
It fetches only the pinned commit, shallowly, from `git.internal`.
`LLVM_SOURCE=/path/to/checkout` builds a local fork checkout instead, for work
on the fork itself. `CC`/`CXX`, `LLVM_USE_LINKER` and `LINK_JOBS` tune the host
build. The install prefix must be dedicated to this toolchain. The script builds
tools only; it does not install host packages or run tests.

A build directory belongs to one compiler. `build/toolchain` records it, and a
different compiler needs `make clean`.

`ci/Containerfile` runs the same build script. The owner builds and publishes
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83` before merging
consumers of that image; the tag names the release and the fork commit, and the
image checks that commit. Normal workflows consume the image without rebuilding
LLVM. The public image is pulled anonymously by CI; the Forgejo instance must
allow anonymous registry access. Publishing still requires login with an
account allowed to write packages in `PyxisOS`. Container paths use the
lowercase namespace `pyxisos`. Ordinary SDK changes do not require a container
rebuild; changing the installed cross-compiler pin does:

```sh
podman build -f ci/Containerfile \
  -t git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83 .
podman push git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83
```

## Native hosted payloads

`build-hosted.sh` is an opt-in Linux build of native Clang, standalone LLD and clangd.
It does not run from ordinary SDK, ports or image builds. Use the installed Pyxis
compiler, CMake, Ninja, Python 3 and a host C/C++ compiler, with a complete current
SDK and a persistent work directory:

```sh
JOBS=16 toolchain/build-hosted.sh /absolute/sdk /absolute/payloads /absolute/work
```

The script pins the hosted source independently, currently
`819c2b770271807f4436dcf76e435c0727c4a1ad`, and fetches only that commit from
the internal mirror. `LLVM_SOURCE` may select a clean checkout of that same
commit. Installed compiler provenance and builtins must still match the SDK.
`CROSS_COMPILE` selects the installed compiler prefix;
`HOSTCC`/`HOSTCXX` select Linux compilers for the two TableGen tools. Target
objects use persistent Ninja trees. SDK changes rerun target configuration checks
and rebuild affected dependencies/relink; a source update rebuilds affected files.
SDK content inventories invalidate affected Ninja objects even when bundled
headers retain fixed timestamps. An interrupted build records its attempted SDK
so reverting to the prior SDK still invalidates the affected objects.
The script locks WORKDIR against concurrent runs. Compiler/tool identities, paths,
CMake options and build flags are recorded; changes print the reason and clear
all three CMake trees. `--clean` explicitly does the same. An older unrecorded
work directory requires `--clean` once or a new directory. SDK changes and
hosted-only fork changes need no compiler-container rebuild; changes to the
installed cross compiler or its runtime sources require a new published image.
Keep the SDK, source checkout and installed resources unchanged through build
and export. Before/after inventories detect changes; they are not atomic
snapshots or protection against another builder modifying these inputs.

The Release X86 build uses real SDK C++ runtimes, with compiler project EH/RTTI
and optional host libraries disabled. Clang and LLD build with LLVM workers
disabled, in one tree that builds only the Clang, LLD and resource-header closure.
clangd requires LLVM threads, so it builds in a second tree with threads on and
no clang-tidy checks, static analyzer, ARCMT, remote index or XPC; the shared
libraries are compiled twice and Clang/LLD are unaffected. All three `.pxb`
bundles include the full installed
compiler resource directory and the SDK notices, with host symlinks expanded
into regular files. They carry no SDK: they declare `"sdk": true` and read
headers and libraries from `sdk://`, the installed dev packages
([the `sdk://` view](../docs/wip/sdk-view.md)); the `clang-hosted` package
depends on `pyxis-dev` and `libcxx-dev`. On Pyxis the driver's default sysroot
is `sdk://`, with `include/` and `lib/` at its root and no `usr/`. They request
an 8 MiB stack and required
`memory/manage`, `threads/manage`, `clock/read,precise,sleep`, `random/read` grants.
`app` and `clang_resources` are read-only roots. Clang passes its `sdk://` to
the LLD it launches. Clang embeds the same
leaf LLD bundle in a declared read-only `linker` root and requests optional
`launcher/launch`; LLD and clangd request no launcher. Source and output use separately
selected ordinary roots. With launch delegated, the single-source driver runs
integrated cc1 then LLD through the contained native job core. With launch
withheld, use Clang `-c` and invoke standalone LLD separately.

The existing builder image can run the whole sequence without rebuilding LLVM.
Install its missing host build tools in the disposable running container, then
build as your own UID. Keep the same checkout/SDK and hosted mount paths across
runs. Use a plain clone with initialized submodules (a worktree's `.git` lives
outside this mount):

```sh
podman run --rm --userns=keep-id --user 0 -e HOME=/tmp -e JOBS=16 \
  -e BUILD_UID="$(id -u)" -e BUILD_GID="$(id -g)" \
  -v "$PWD":/src:Z -v /absolute/hosted-build:/hosted:Z -w /src \
  git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-3d69e83 \
  sh -ec 'apt-get update -qq
    apt-get install -y --no-install-recommends build-essential ninja-build
    exec setpriv --reuid="$BUILD_UID" --regid="$BUILD_GID" --clear-groups \
      sh -ec "git config --global --add safe.directory /src
        make -j16 sdk
        toolchain/build-hosted.sh /src/build/sdk /hosted/out /hosted/work"'
```

Output is `OUTPUT/hosted-SHA256/payload/` plus `payload.tar` and its checksum.
The content identity covers files and directories before the identity inventory
itself is added. Metadata records per-file checksums, SDK inputs, exact source,
producer/configuration provenance and actual P1F segment, image, span and eager
backing sizes. Exported CMake cache metadata is sorted key/value configuration,
without comments or cache-editing types, with compiler/archive tool names resolved
to their actual paths. Raw caches stay in WORKDIR; the payload records tool hashes
separately. This keeps a no-op reconfigure from changing the payload identity.
The build refuses changed SDK/source inputs, images over the
existing 128 MiB ceiling, spans over 256 MiB and existing published revision
paths with different contents. Repeating identical output verifies and reuses its
immutable revision; incomplete private staging is recreated on the next run.
These sizes do not establish a native working-memory peak or budget.

`payload.tar` is deterministic, uncompressed USTAR containing `clang.pxb`,
`lld.pxb`, `clangd.pxb` and `metadata`, without a revision prefix. The
`clang-hosted` package uses these `payload/` contents at its tree's top level.
The producer also writes `clang-hosted.lua`, an ordinary package recipe whose
version is the full 64-digit SHA-256 from the immutable `hosted-SHA256` identity,
and `LICENSE`, the LLVM license, which the recipe installs at the package root
beside the per-bundle notices and `metadata/LLVM-LICENSE.TXT`. `LICENSE` is
outside `payload/`, so it is not part of the identity.
Publish it with the existing packer, from the clean Pyxis checkout that
produced the payload, still at the same revision. `--build` gives the package
the [ABI](../docs/interfaces/packages.md#repository) of that revision:

```sh
make build/kernel-build-revision.h
python3 tools/pxpkg.py pack --build build /absolute/payloads/hosted-SHA256/clang-hosted.lua /absolute/repository
```

The recipe explicitly exports `clang`, `clang++`, `clang-cpp`, `lld`, `ld.lld`
and `clangd` through the real bundle manifests. Clang selects C++, preprocessor
or C mode from its invoked name; `ld.lld` selects ELF, while generic `lld` still
requires `-flavor gnu`. These names share their tool's image, stack and resource
policy, with no wrapper process or alias manifest.

With a configured network repository, `pkg install clang-hosted` installs it.
Bare tool names then resolve through the command list's live package view; a new
launch sees the selected set without restarting the space, while running tools
retain their selected bundle and roots. Live installs last until reboot. Neovim's
clangd default uses the same bare command. Missing exports refuse without an
archive fallback. Follow the [hosted-tools guide](../docs/development/hosted-tools.md)
for package setup and invocation, or its explicit owner workflow for manual
immutable publication. Manual archive transfer still requires checksum
verification and an immutable published revision.

## Native LLVM development libraries

`build-llvm-development.sh SDK OUTPUT WORKDIR` builds a separate threaded static
X86 library/header prefix for Mesa, with a Linux-host `llvm-config` reporting
the target libraries. The development pin is separate; the compiler container and
hosted tool bundles are unchanged. See [the prefix and manual object-emission
check](../docs/development/llvm-development.md).

## Installation

- Clang and LLD for the X86 target, with `x86_64-unknown-pyxis` as the default
  target;
- the LLVM archive and object tools;
- compiler-rt builtins for Pyxis in Clang's resource directory.

`x86_64-unknown-pyxis-` names (`clang`, `clang++`, `ld.lld`, `ar`, `nm`, `ranlib`,
`objcopy`, `strip`, `objdump`, `readelf`, `size`, `addr2line`) point at
those tools. The SDK Make fragment asks Clang for `llvm-ar` by path, so another
`ar` earlier in `PATH` cannot supply it.

## Executables

LLD in the fork writes Pyxis P1F executables with `--oformat=p1f`. The Pyxis
driver passes it for every executable link, so `clang -o hello.pxe hello.c`
produces a runnable PXE, and an executable without `-o` is `a.pxe`.
- **ELF output:** a later `-Wl,--oformat=elf` gives an ELF executable with
  symbols at the same addresses. The kernel link uses it, because Limine loads
  ELF; use it to give a debugger an application's symbols.
- **Relocatable output:** `-c` and `-r` produce ordinary ELF relocatables.
- **Format owners:** `include/pxe/p1f.h` remains the format's definition. The
  kernel loader reads it and the fork's `lld/ELF/P1F.cpp` writes it. A format
  change must update both together.

## Target contract

The target is little-endian x86-64 LP64: 8-bit char, 16-bit short, 32-bit int,
64-bit long/pointers and the System V x86-64 calling convention. The Pyxis
Clang driver:
- predefines `__pyxis__`, `__ELF__` and `__SIZEOF_FLOAT128__`, and no Unix or
  POSIX platform macros;
- defaults to no red zone;
- rejects other ABIs, shared libraries and PIE.

Clang's `__INT_FAST*_TYPE__` predefines follow the least-width types. The
SDK's `stdint.h` keeps the Pyxis choice of 32-bit integers for the 8- and
16-bit fast types and is authoritative.

The compiler defaults to baseline x86-64 x87/SSE2 code. Caelum preserves
x87/SSE state across task switches; AVX remains unsupported. Do not select
`-march=native` or other options that require unsaved extended state. Kernel
builds explicitly retain `-mgeneral-regs-only`. The compiler-rt builtins include
the x87, quad, half-precision and complex helpers and are built without the red
zone; they are not a libc or libm implementation. The target has no 32-bit/x32
multilib, shared libraries or PIE. Executable-only `thread_local` and
`_Thread_local` use x86-64 local-exec TLS (variant II); other TLS models and
emulation are rejected. They require the matching gate 7 SDK linker template
and libc FS bootstrap. C11/pthread and C++ synchronization remain gate 8.

Applications select an external SDK with `--sysroot=/path/to/sdk/sysroot`.
The driver finds `crt0.o`, `pyxis.ld`, libc and libpyxis there, and the
compiler-rt builtins in its resource directory. For C++ it also searches
`usr/include/c++/v1` ahead of libc's headers and links libc++, libc++abi and
libunwind from the sysroot. The SDK builds that [C++ runtime](../docs/development/sdk.md#c)
from this toolchain's fork commit, so the compiler installs none. The SDK owns the linker script
and fixed load layout; objects and `.a` archives are ordinary ELF.

A normal C link supplies startup, the SDK script and a grouped compiler-rt,
libc and libpyxis sequence, and links with LLD. Every executable link passes
`--eh-frame-hdr`; C code has no unwind tables, so C executables gain nothing. Pass `-lterm` for terminal
helpers. `-nostdlib` suppresses the runtime and default SDK script for
kernel/custom links; `-T` overrides the default script. Existing SDK Make
settings name runtime inputs explicitly for dependency tracking. Changing the
compiler target defaults or compiler-rt requires rebuilding this compiler;
changing SDK headers/libraries/startup/linker script does not.

## Sources and licenses

[LLVM 23.1.3](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.3),
through the `mirrors/llvm-project` mirror and the `pyxis-llvm` fork, adds:
- Pyxis OS support in LLVM's triple, Clang's target information and its driver,
  including the SDK's C++ runtime and static local-exec TLS;
- P1F output in LLD;
- a compiler-rt helper that avoids the red zone;
- libc++ selections for Pyxis libc: `timespec_get` clocks, no terminal check
  and no `ELAST` limit.

The installation records the fork commit in `share/pyxis-toolchain/llvm-revision`.
LLVM, including Clang, LLD and compiler-rt, is Apache-2.0 WITH LLVM-exception.
The installation records `LICENSE.TXT` beside this document, and the SDK carries
both with the compiler-rt builtins.

## Go cross-toolchain

The optional [Go target](../docs/development/go-toolchain.md) has a separately
pinned host cross-toolchain and direct P1F linker. `build-go.sh` builds it without
changing LLVM or the ordinary builder container. Normal images need no Go tools.

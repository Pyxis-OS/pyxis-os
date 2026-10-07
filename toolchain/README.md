# Pyxis target toolchain

Build this compiler separately from normal kernel, SDK and application builds.
It targets `x86_64-unknown-pyxis` using GCC 16.2.0 and binutils 2.47. Source
archives are pinned by `SHA256SUMS`; patches apply directly to those releases.
The compiler has no dependency on a particular checkout's runtime headers.

## Building

On a Linux host, install a C/C++ compiler, GNU Make, Bison, Flex, Texinfo,
GMP/MPFR/MPC development libraries, ISL, zlib, curl, patch and xz. From the Pyxis
checkout:

```sh
JOBS=8 toolchain/build.sh "$HOME/opt/pyxis-cross" /tmp/pyxis-cross-build
export PATH="$HOME/opt/pyxis-cross/bin:$PATH"
make clean
make -j4 image
```

Use a fresh work directory. Verified archives can be placed there beforehand
for an offline build; the script refuses to reuse source/build directories.
Both archives come from the internal cache of ftp.gnu.org
(`https://repo.internal/repository/raw-gnu/gnu/`) and are checked against the
pinned SHA-256.
The install prefix must be dedicated to this toolchain. Keep work/prefix paths
free of spaces, as required by the upstream builds. The script downloads and
builds tools only; it does not install host packages or run tests.

`ci/Containerfile` runs the same build script. The owner builds and publishes
`git.internal/pyxisos/pyxis-builder:pyxis-gcc16.2-binutils2.47` before merging
consumers of that image. Normal workflows consume the image without rebuilding
GCC or binutils. The public image is pulled anonymously by CI; the Forgejo
instance must allow anonymous registry access. Publishing still requires login
with an account allowed to write packages in `PyxisOS`. Container paths use the
lowercase namespace `pyxisos`. Ordinary SDK changes do not require a container
rebuild:

```sh
podman build -f ci/Containerfile \
  -t git.internal/pyxisos/pyxis-builder:pyxis-gcc16.2-binutils2.47 .
podman push git.internal/pyxisos/pyxis-builder:pyxis-gcc16.2-binutils2.47
```

## Target contract

The target is little-endian x86-64 LP64: 8-bit char, 16-bit short, 32-bit int,
64-bit long/pointers and the System V x86-64 calling convention. The compiler
defines `__pyxis__`; it does not define Unix/POSIX
platform macros. Only the C frontend and a static target libgcc are built.

The compiler defaults to baseline x86-64 x87/SSE2 code and no red zone. Caelum
preserves x87/SSE state across task switches; AVX remains unsupported. Do not
select `-march=native` or other options that require unsaved extended state.
Kernel builds explicitly retain `-mgeneral-regs-only`. Static libgcc includes
the normal x86 floating-point arithmetic/conversion helpers and also avoids the
red zone; it is not a libc or libm implementation. The target has no 32-bit/x32
multilib, shared libraries, PIE, C++ runtime, thread runtime or exception-handling
contract.

Applications select an external SDK with `--sysroot=/path/to/sdk/sysroot`.
The driver finds `crt0.o`, `pyxis.ld`, libc and libpyxis there, and its own libgcc
in the compiler installation. The SDK owns the linker script and fixed load
layout; binutils continues to use ordinary ELF objects, executables and `.a`
archives. `elf2pxe` remains a separate SDK tool.

A normal C link supplies startup and a grouped libc/libpyxis/libgcc sequence.
Pass `-lterm` for terminal helpers. `-nostdlib` suppresses the runtime and default
SDK script for kernel/custom links; `-T` overrides the default script. Existing
SDK Make settings name runtime inputs explicitly for dependency tracking.
Changing the compiler target defaults or libgcc requires rebuilding this
compiler; changing SDK headers/libraries/startup/linker script does not.

## Sources and local changes

- [GNU binutils 2.47](https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz):
  recognize the Pyxis OS tuple in config.sub, BFD, GAS and ld. Use the existing
  x86-64 ELF backend/emulation, without adding a PXE backend.
- [GCC 16.2.0](https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.gz):
  recognize the tuple, add Pyxis driver/builtin defaults and GNU-stack metadata,
  and configure static libgcc, including its x86 floating-point helpers,
  without libc headers or fixed-header copies.
  GCC retains its builtin headers; SDK-owned integer/limit headers take
  precedence in SDK builds. This does not introduce a newlib dependency.

Upstream sources retain their licenses: GCC/binutils are primarily
GPL-3.0-or-later, with the GCC Runtime Library Exception 3.1 for covered runtime
components; individual files retain their own notices. Local patches follow
the licenses of the files they modify. The installation records the patches,
source hashes, this document, GPLv3 and the runtime exception under
`share/pyxis-toolchain`. Preserve corresponding source availability when
redistributing compiler binaries; the pinned URLs and patches identify it.

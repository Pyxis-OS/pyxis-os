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
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-41ab604` before merging
consumers of that image; the tag names the release and the fork commit, and the
image checks that commit. Normal workflows consume the image without rebuilding
LLVM. The public image is pulled anonymously by CI; the Forgejo instance must
allow anonymous registry access. Publishing still requires login with an
account allowed to write packages in `PyxisOS`. Container paths use the
lowercase namespace `pyxisos`. Ordinary SDK changes do not require a container
rebuild; a new fork commit does:

```sh
podman build -f ci/Containerfile \
  -t git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-41ab604 .
podman push git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-41ab604
```

## Installation

- Clang and LLD for the X86 target, with `x86_64-unknown-pyxis` as the default
  target;
- the LLVM archive and object tools;
- compiler-rt builtins for Pyxis in Clang's resource directory.

`x86_64-unknown-pyxis-` names (`clang`, `ld.lld`, `ar`, `nm`, `ranlib`,
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
  kernel loader reads it; the fork's `lld/ELF/P1F.cpp`, the TCC port and
  `elf2pxe` write it. A format change must update all four together.

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
multilib, shared libraries, PIE, C++ runtime, thread runtime or exception-handling
contract.

Applications select an external SDK with `--sysroot=/path/to/sdk/sysroot`.
The driver finds `crt0.o`, `pyxis.ld`, libc and libpyxis there, and the
compiler-rt builtins in its resource directory. The SDK owns the linker script
and fixed load layout; objects and `.a` archives are ordinary ELF.

A normal C link supplies startup, the SDK script and a grouped compiler-rt,
libc and libpyxis sequence, and links with LLD. Pass `-lterm` for terminal
helpers. `-nostdlib` suppresses the runtime and default SDK script for
kernel/custom links; `-T` overrides the default script. Existing SDK Make
settings name runtime inputs explicitly for dependency tracking. Changing the
compiler target defaults or compiler-rt requires rebuilding this compiler;
changing SDK headers/libraries/startup/linker script does not.

## Sources and licenses

[LLVM 23.1.3](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.3),
through the `mirrors/llvm-project` mirror and the `pyxis-llvm` fork, adds:
- Pyxis OS support in LLVM's triple, Clang's target information and its driver;
- P1F output in LLD;
- a compiler-rt helper that avoids the red zone.

The installation records the fork commit in `share/pyxis-toolchain/llvm-revision`.
LLVM, including Clang, LLD and compiler-rt, is Apache-2.0 WITH LLVM-exception.
The installation records `LICENSE.TXT` beside this document, and the SDK carries
both with the compiler-rt builtins.

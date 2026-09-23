# Userspace SDK

`make sdk` exports `build/sdk` using the prebuilt `x86_64-unknown-pyxis-`
[toolchain](../toolchain/README.md). `make userspace` builds that SDK first, then
applications against it;
`make image` continues through initrd and ISO assembly. No compiler is built by
these targets. Repository extraction remains a later
[milestone task](wip/sdk-and-repositories.md).

## Contents and ownership

| SDK path | Contents |
| --- | --- |
| `sysroot/usr/include` | libc, libpyxis and libterm headers, plus public `abi/` and `pxe/` headers |
| `sysroot/usr/lib` | `crt0.o`, `libc.a`, `libpyxis.a`, `libterm.a` and `pyxis.ld` |
| `bin/elf2pxe` | Host executable for converting the linked ELF to PXE |
| `share/pyxis.mk` | Relocatable compiler, compile/link flags and exported artifact paths |
| `share/licenses` | TLSF license and upstream/local-adaptation record |
| `manifest.txt` | Pyxis revision and dirty state, compiler/linker identities and host identity |

The compiler supplies its own builtin headers and libgcc. It stays outside the
SDK, as does the host C runtime needed by elf2pxe. Use a compatible host for that
executable and the compiler recorded in the manifest. The manifest records build
provenance, not an ABI version or compatibility guarantee. Modified checkouts
are marked as such; publish SDKs from committed source for an exact revision.

Only public header trees are exported. Kernel-private headers, TLSF's allocator
API and the example content-service protocol are not part of the target include
path. The example protocol lives beside the application helpers. Header exports
remove obsolete files; unchanged headers and installed artifacts keep their
timestamps so an unchanged build does not recompile consumers.

## Building applications

After `make sdk`, build individual applications with:

```sh
make -C userspace shell
make -C userspace hello client server
make -C userspace SDK=/path/to/sdk BUILD=/tmp/pyxis-apps all
```

The userspace Makefile now builds applications only. It consumes a complete SDK
and does not build the converter or runtime. `SDK` defaults to `../build/sdk`;
`BUILD` defaults to `../build/userspace`. The SDK can be copied to another path.
Application sources, their local helpers, the Makefile, compiler and SDK are
sufficient; kernel and runtime source directories are not needed.

`share/pyxis.mk` exposes `PYXIS_CPPFLAGS`, `PYXIS_CFLAGS`, `PYXIS_LDFLAGS`,
`PYXIS_START`, `PYXIS_LIBRARIES`, `PYXIS_LDLIBS` and `PYXIS_ELF2PXE`. It supplies
paths relative to its installed location, not the source checkout. Applications
link their objects with `PYXIS_START` and `PYXIS_LDLIBS`, then run the converter
with `--format p1f`. Keep the resulting ELF for debugging.

The fragment preserves the current GNU C23 freestanding/static build settings,
4 KiB segment alignment and fixed load address. It excludes implicit host or
cross-installation C headers, keeping compiler builtin headers and SDK headers.
Project headers use a normal include path so generated dependencies track them.
Startup and runtime archives are explicit link inputs; no host libc or startup
is linked. SDK selection and compiler flags are recorded in each build directory
to rebuild consumers when those inputs change, even with older SDK timestamps.

For a direct compiler invocation, the Pyxis driver supplies startup, libc,
libpyxis, libgcc and the SDK linker script:

```sh
x86_64-unknown-pyxis-gcc --sysroot=/path/to/sdk/sysroot program.c -o program.elf
/path/to/sdk/bin/elf2pxe --format p1f -o program.pxe program.elf
```

Add `-lterm` when using terminal helpers. The target defaults to general
registers and no red zone; FP/SIMD execution remains unsupported. This produces
an ELF for conversion, not a directly runnable PXE. The SDK Make fragment keeps
explicit startup/archive paths so Make can track them as dependencies.

## Runtime build phase

The root build first exports public headers and compiler settings, then invokes
`userspace/runtime.mk` to build startup and the three libraries under
`build/runtime`. It installs those outputs, linker support and the host converter
before invoking the separate application build. This avoids a dependency cycle
between SDK production and applications.

For focused runtime work, `make sdk-headers` followed by
`make -C userspace -f runtime.mk libc` builds just libc; `libpyxis` and `libterm`
are also targets. Run `make sdk` afterward to publish a complete SDK. Runtime
builds still consume the shared shebang source and pinned TLSF source from this
repository. Handling those shared sources and preserving their history/licenses
belongs to repository extraction; no vendored implementation is duplicated here.

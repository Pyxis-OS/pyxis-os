# Userspace SDK

`make sdk` exports `build/sdk` using the prebuilt `x86_64-unknown-pyxis-`
[toolchain](../../toolchain/README.md). `make userspace` builds that SDK first, then
the ports development files and applications against it; `make image` continues through initrd and ISO assembly.
GCC and binutils remain prebuilt; image builds also build the guest TCC port. Initialize the [userspace submodule](sdk-and-repositories.md)
with `git submodule update --init userspace` before building the SDK.

## Contents and ownership

| SDK path | Contents |
| --- | --- |
| `sysroot/usr/include` | libc, libpyxis and libterm headers, plus public `abi/` and `pxe/` headers |
| `sysroot/usr/lib` | `crt0.o`, `libc.a`, `libpyxis.a`, `libterm.a`, target `libgcc.a` and `pyxis.ld` |
| `bin/elf2pxe` | Host executable for converting the linked ELF to PXE |
| `share/pyxis.mk` | Relocatable compiler, compile/link flags and exported artifact paths |
| `share/pyxis/shebang.c` | Authoritative shared parser source, compiled into libpyxis |
| `share/licenses` | TLSF and musl licenses and adaptation records |
| `share/toolchain` | Installed toolchain source hashes, patches, GPLv3 and GCC Runtime Library Exception |
| `manifest.txt` | Pyxis and userland revisions/dirty states, compiler/linker identities, libgcc hash and host identity |

The compiler supplies its own builtin headers. SDK export copies its target
libgcc archive and installed `share/pyxis-toolchain` provenance into the SDK;
it does not copy GCC's private headers. The compiler stays outside the SDK,
as does the host C runtime needed by elf2pxe. Use a compatible host for that
executable and the compiler recorded in the manifest. The manifest records build
provenance, not an ABI version or compatibility guarantee. Modified checkouts
are marked as such; publish SDKs from committed source for an exact revision.

Only public header trees are exported. Kernel-private headers, TLSF's allocator
API and the example content-service protocol are not part of the target include
path. The example protocol lives beside the application helpers. Header exports
remove obsolete files; unchanged headers and installed artifacts keep their
timestamps so an unchanged build does not recompile consumers.

Userland owns `stdint.h` and `limits.h` in the SDK. They define the current
x86-64 LP64 model independently of GCC's private type macros: exact/least types
use 8/16/32/64 bits, fast types use 32 bits through `int_fast32_t` and 64 bits
for `int_fast64_t`, and pointer/max types use `long`/`unsigned long`. Limits and
constant suffixes preserve the existing Pyxis GCC choices. `MB_LEN_MAX` is 1
for the current single-byte libc; these headers add no character conversion,
signal or wide-character runtime facilities.

`inttypes.h` includes stdint.h and supplies fixed-width PRId/PRIi/PRIo/PRIu/PRIx/
PRIX output macros for 8/16/32/64-bit types. Other integer-type families,
scanning macros and conversion functions are not supplied. `stdio.h` defines
BUFSIZ as 8192, without enabling stream buffering. Descriptor I/O headers include
`fcntl.h`, `unistd.h` and the exported `sys/types.h` subdirectory; see
[the I/O contract](../userland/stdio.md#descriptor-io).

`stddef.h`, `stdarg.h`, `stdbool.h` and `float.h` remain compiler-provided.
SDK `-I` paths precede compiler `-isystem` paths, so both GCC and another
compiler use the same SDK integer definitions while retaining their own
compiler-sensitive headers. Do not add GCC's private include directory to a
TCC build. Public declarations use GNU noreturn attributes where needed and
include their own type dependencies; implementations remain GNU C23.

## Building applications

After `make sdk`, build individual applications with:

```sh
make -C userspace SDK=../build/sdk BUILD=../build/userspace shell
make -C userspace SDK=../build/sdk BUILD=../build/userspace hello client server
make -C /path/to/pyxis-userland SDK=/path/to/sdk \
  LUA_PREFIX=/path/to/ports-dev/lua \
  PICOHTTPPARSER_PREFIX=/path/to/ports-dev/picohttpparser \
  MBEDTLS_PREFIX=/path/to/ports-dev/mbedtls all
```

The `session` application also consumes Lua headers and `liblua.a` through
`LUA_PREFIX`. The `httpfs` application consumes picohttpparser headers and
`libpicohttpparser.a` through `PICOHTTPPARSER_PREFIX`. Its fetch library also
links the native `libtls.a` adapter and the configured Mbed TLS export through
`MBEDTLS_PREFIX`, even while the installed provider remains in HTTP mode.
These development prefixes are exported by the ports build. Other application targets can still build with
the SDK alone.

The userspace Makefile builds applications and their support archives. It consumes a complete SDK
and does not build the converter or runtime. `SDK` defaults to `build/sdk`
within the userland checkout; application `BUILD` defaults to `build/apps`. The integrated
Pyxis build passes both paths explicitly to keep its outputs in
`build/userspace`. The SDK can be copied to another path.
Application sources, their local helpers, the Makefile, compiler, SDK and any
required ports development files are sufficient; kernel and runtime source
directories are not needed.

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
libpyxis, libgcc and the SDK linker script. Use the same explicit header search
as the Make fragment: GCC otherwise searches its private integer headers first
and ignores an `-I` duplicate of the sysroot's default system directory.

```sh
x86_64-unknown-pyxis-gcc --sysroot=/path/to/sdk/sysroot \
  -nostdinc -isystem "$(x86_64-unknown-pyxis-gcc -print-file-name=include)" \
  -I/path/to/sdk/sysroot/usr/include program.c -o program.elf
/path/to/sdk/bin/elf2pxe --format p1f -o program.pxe program.elf
```

Add `-lterm` when using terminal helpers. The target defaults to x87/SSE2 and
no red zone; the SDK explicitly selects baseline `-march=x86-64`. AVX remains
unsupported. Use the rebuilt FP-capable Pyxis compiler: the earlier compiler
forced general registers and omitted floating-point libgcc helpers. This produces
an ELF for conversion, not a directly runnable PXE. The SDK Make fragment keeps
explicit startup/archive paths so Make can track them as dependencies.

Hardware `float`, `double` and x87 `long double` arithmetic and compiler libgcc
helpers are available. Libc provides floating-point parsing/formatting and a
small math subset; a full libm remains deferred. See the [userspace FP contract](../kernel/userspace.md#floating-point).

## Guest SDK

`make image` also packages the [TCC port](ports.md#tcc-and-the-guest-sdk) and a
target-only SDK at `app://sdk`. Shared headers remain in `usr/include`, startup
and runtime archives in `usr/lib`, and TCC-private headers/support in `lib/tcc`.
The guest payload includes library licenses, exact TCC patches and toolchain
source provenance. Its manifest adds the ports revision/dirty state to the
exported SDK record. There is no separate guest ABI version.

The guest receives no GCC/binutils executables, GCC private headers, host
`elf2pxe` or GNU linker script. TCC writes P1F directly using the same loader
contract. `app://sdk` is read-only; applications compile source and write output
in `home://` or other explicitly granted directories.

## Runtime build phase

The root build first exports public headers and compiler settings, then invokes
`userspace/runtime.mk` to build startup and the three libraries under
`build/runtime`. It installs those outputs, linker support and the host converter
before invoking the separate application build. This avoids a dependency cycle
between SDK production and applications.

For focused runtime work, run `make sdk-headers`, then:

```sh
make -C userspace -f runtime.mk SDK=../build/sdk BUILD=../build/runtime libc
```

`libpyxis` and `libterm` are also targets. Run `make sdk` afterward to publish a
complete SDK. Standalone runtime builds default to `build/runtime` within the
userland checkout. They take their own libc/libpyxis/libterm headers from that
checkout and ABI/format headers from the selected SDK. TLSF is vendored in
userland. The shared shebang implementation stays in Pyxis and is exported to
`share/pyxis/shebang.c` during the header stage, so runtime builds never reach
into parent source directories.

# Userspace SDK

`make sdk` exports `build/sdk` using the prebuilt `x86_64-unknown-pyxis-`
[toolchain](../../toolchain/README.md). `make userspace` builds that SDK first, then
the ports development files and applications against it; `make image` continues through initrd and ISO assembly.
The toolchain is the prebuilt Pyxis Clang and LLD. Image builds also build the guest TCC port. Initialize the [userspace submodule](sdk-and-repositories.md)
and filesystem submodules with `git submodule update --init userspace fs` before
building the SDK.

## Contents and ownership

Libc headers include `regex.h`, the minimal `wchar.h` type interface, and ASCII
`wctype.h`. The four POSIX regex functions and UTF-8 `mbtowc` live in `libc.a`;
there is no extra link library. See
[interfaces and limits](../userland/libc-portability.md#regular-expressions-and-utf-8-conversion).

| SDK path | Contents |
| --- | --- |
| `sysroot/usr/include` | libc, libpyxis and libterm headers, public `abi/`, `pxe/` and `pyxis_fs/npfs.h` headers, and libc++'s headers in `c++/v1` |
| `sysroot/usr/lib` | `crt0.o`, `libc.a`, `libpyxis.a`, `libterm.a`, target `libnpfs-format.a`, the compiler runtime `libclang_rt.builtins.a`, the [C++ runtime](#c) `libc++.a`, `libc++abi.a` and `libunwind.a`, and `pyxis.ld` |
| `share/pyxis.mk` | Relocatable compiler, compile/link flags and exported artifact paths |
| `share/pyxis/shebang.c` | Authoritative shared parser source, compiled into libpyxis |
| `share/pyxis/key_layout.c` | The US key layout shared with the kernel's terminal text, compiled into libpyxis as `key_layout_character` (`pxe/key_layout.h`) |
| `share/licenses` | TLSF and musl licenses/adaptation records, TRE's BSD notice and the npfs MPL-2.0 license |
| `share/toolchain` | Installed toolchain provenance: the LLVM fork revision, the toolchain README and the LLVM license |
| `manifest.txt` | Pyxis, userland and filesystem revisions/dirty states, compiler/linker identities, runtime hash and host identity |

The compiler supplies its own builtin headers. SDK export copies its target
runtime archive (Clang's compiler-rt builtins) and installed
`share/pyxis-toolchain` provenance into the SDK; it does not copy compiler
private headers. Consumers use the same compiler, because the runtime archive
belongs to it. The compiler stays outside the SDK; use the one recorded in the
manifest. The SDK contains no host executables. The manifest records build
provenance, not an ABI version or compatibility guarantee. Modified checkouts
are marked as such; publish SDKs from committed source for an exact revision.

Only public header trees are exported. Kernel-private headers, TLSF's allocator
API and the example content-service protocol are not part of the target include
path. The example protocol lives beside the application helpers. Header exports
remove obsolete files; unchanged headers and installed artifacts keep their
timestamps so an unchanged build does not recompile consumers.

Userland owns `stdint.h` and `limits.h` in the SDK. They define the current
x86-64 LP64 model independently of the compiler's predefined type macros, which
for Clang differ in the fast types: exact/least types
use 8/16/32/64 bits, fast types use 32 bits through `int_fast32_t` and 64 bits
for `int_fast64_t`, and pointer/max types use `long`/`unsigned long`. Limits and
constant suffixes preserve the Pyxis choices first made with GCC. `MB_LEN_MAX` is 1
for the current single-byte libc; these headers add no character conversion,
signal or wide-character runtime facilities.

`inttypes.h` includes stdint.h and supplies PRId/PRIi/PRIo/PRIu/PRIx/PRIX
output macros for the 8/16/32/64-bit, pointer and greatest-width types. Other
integer-type families, scanning macros and conversion functions are not supplied. `stdio.h` defines
BUFSIZ as 8192, without enabling stream buffering. Descriptor I/O headers include
`fcntl.h`, `unistd.h` and the exported `sys/types.h` subdirectory; see
[the I/O contract](../userland/stdio.md#descriptor-io).

Libc headers can be included from C++: declarations are wrapped in `extern "C"`,
and parameters use `__restrict`, which C and C++ both accept.

`stddef.h`, `stdarg.h`, `stdbool.h` and `float.h` remain compiler-provided.
SDK `-I` paths precede compiler `-isystem` paths, so Clang and TCC use the
same SDK integer definitions while retaining their own compiler-sensitive
headers. Do not add Clang's private include directory to a TCC build. Public declarations use GNU noreturn attributes where needed and
include their own type dependencies; implementations remain GNU C23.

## Building applications

After `make sdk`, build individual applications with:

```sh
make -C userspace SDK=../build/sdk BUILD=../build/userspace shell
make -C userspace SDK=../build/sdk BUILD=../build/userspace hello client server
make -C /path/to/pyxis-userland SDK=/path/to/sdk \
  LUA_PREFIX=/path/to/ports-dev/lua \
  PICOHTTPPARSER_PREFIX=/path/to/ports-dev/picohttpparser \
  MBEDTLS_PREFIX=/path/to/ports-dev/mbedtls \
  ZLIB_PREFIX=/path/to/ports-dev/zlib \
  LIBPNG_PREFIX=/path/to/ports-dev/libpng all
```

The `session` application also consumes Lua headers and `liblua.a` through
`LUA_PREFIX`. The `httpfs` application consumes picohttpparser headers and
`libpicohttpparser.a` through `PICOHTTPPARSER_PREFIX`. Its fetch library also
links the native `libtls.a` adapter and the configured Mbed TLS export through
`MBEDTLS_PREFIX`, even while the installed provider remains in HTTP mode.
The [screenshot command](../userland/screenshot.md) consumes libpng and zlib
through `LIBPNG_PREFIX` and `ZLIB_PREFIX`, linking its objects, libpng, zlib
and the normal runtime in that order. These development prefixes are exported
by the ports build. Other application targets can still build with the SDK alone.

The userspace Makefile builds applications and their support archives. It consumes a complete SDK
and does not build the runtime. `SDK` defaults to `build/sdk`
within the userland checkout; application `BUILD` defaults to `build/apps`. The integrated
Pyxis build passes both paths explicitly to keep its outputs in
`build/userspace`. The SDK can be copied to another path.
Application sources, their local helpers, the Makefile, compiler, SDK and any
required ports development files are sufficient; kernel and runtime source
directories are not needed.

`share/pyxis.mk` exposes `PYXIS_CPPFLAGS`, `PYXIS_CFLAGS`, `PYXIS_LDFLAGS`,
`PYXIS_START`, `PYXIS_LIBRARIES`, `PYXIS_LDLIBS` and `PYXIS_RUNTIME_LIBRARY`
(the runtime archive's `-l` name, for tools such as TCC). It supplies paths
relative to its installed location, not the source checkout. Applications link
their objects with `PYXIS_START` and `PYXIS_LDLIBS` straight to a `.pxe`
executable: the Pyxis driver has LLD write P1F. For a debugger, relink with
`-Wl,--oformat=elf`; the ELF has symbols at the same addresses.

The fragment preserves the current GNU C23 freestanding/static build settings,
4 KiB segment alignment and fixed load address. It excludes implicit host or
cross-installation C headers, keeping compiler builtin headers and SDK headers.
Project headers use a normal include path so generated dependencies track them.
Startup and runtime archives are explicit link inputs; no host libc or startup
is linked. SDK selection and compiler flags are recorded in each build directory
to rebuild consumers when those inputs change, even with older SDK timestamps.

For a direct compiler invocation, the Pyxis driver supplies startup, libc,
libpyxis, compiler-rt builtins and the SDK linker script, and links with LLD,
which writes the P1F executable directly. Use the same explicit header search
as the Make fragment, so the SDK's integer headers come before the compiler's:

```sh
x86_64-unknown-pyxis-clang --sysroot=/path/to/sdk/sysroot \
  -nostdinc -isystem "$(x86_64-unknown-pyxis-clang -print-file-name=include)" \
  -I/path/to/sdk/sysroot/usr/include program.c -o program.pxe
```

Without `-o` the executable is `a.pxe`. Add `-Wl,--oformat=elf` for an ELF
executable, for example to keep symbols for a debugger.

Add `-lterm` when using terminal helpers. Format-library consumers explicitly
link `libnpfs-format.a` and supply `npfs_memory_copy` and `npfs_memory_zero`;
this archive supplies encoding only, without allocation or disk operations. The target defaults to x87/SSE2 and
no red zone; the SDK explicitly selects baseline `-march=x86-64`. AVX remains
unsupported. The SDK Make fragment keeps
explicit startup/archive paths so Make can track them as dependencies.

Hardware `float`, `double` and x87 `long double` arithmetic and compiler-rt
helpers are available. Libc provides floating-point parsing/formatting and a
small math subset; a full libm remains deferred. See the [userspace FP contract](../kernel/userspace.md#floating-point).

## Startup, exit and layout

`crt0.o` calls libc, which binds startup resources, initializes stdio and the
heap, runs `.init_array` (C constructors and C++ static initializers), then
calls `main`. Returning from `main` calls `exit`, which runs `atexit` and
`__cxa_atexit` handlers in reverse order of registration, then `.fini_array`
in reverse, then closes stdio. `_Exit`, `abort` and faults skip all three.
The first 32 handlers use static storage; later ones are allocated from the
heap, and registration reports failure if allocation fails.

`pyxis.ld` places `.eh_frame_hdr`, `.eh_frame` and `.gcc_except_table` in the
read-only segment with `__eh_frame_hdr_start`/`_end` and
`__eh_frame_start`/`_end` bounds for a static unwinder, and the constructor and
destructor arrays in the data segment. C code is compiled without unwind tables,
so C executables carry none. TCC links define empty constructor arrays.

## C++

The SDK carries libc++, libc++abi and libunwind as static archives, built from
the same [`pyxis-llvm`](llvm-toolchain.md) commit as the compiler. The driver
finds them: `x86_64-unknown-pyxis-clang++ --sysroot=SDK/sysroot app.cpp -o
app.pxe` searches `usr/include/c++/v1` ahead of libc's headers and links the
three archives with libc. Make consumers use `CXX`, `PYXIS_CXX_CPPFLAGS`,
`PYXIS_CXXFLAGS` (GNU C++23, without `-ffreestanding`) and `PYXIS_CXX_LDLIBS`
from `share/pyxis.mk`. Like the C settings, they keep SDK headers on a normal
include path so `-MMD` tracks them.

| Supported | Not provided |
| --- | --- |
| Exceptions, RTTI, static constructors and destructors, local statics | Threads, `thread_local`, `<thread>`, `<mutex>`, non-lock-free atomics |
| Containers, algorithms, strings, `<format>`, `<print>`, `<charconv>` | `<iostream>`, `<locale>`, `<regex>` and wide characters |
| `system_clock` and `steady_clock` | `<filesystem>`, `random_device`, time zones |
| Aligned `new` up to 4096 bytes | Most of `<cmath>`, which follows libc's [math subset](../kernel/userspace.md#foundational-libc) |

The headers of absent features still include, but their names do not exist, so
a program using one fails to compile. `thread_local` and `_Thread_local` are
rejected by the compiler. `-fno-exceptions` and `-fno-rtti` code links against
the same archives.

An uncaught exception prints `libc++abi: terminating due to uncaught
exception of type …` with the mangled type name, to keep the demangler out of
every program, and exits with status 1 through `abort`. C code has no unwind
tables, so an exception thrown through a C frame, such as a `qsort`
comparator, also terminates. A C++ program printing a `vector<string>` is about
108 KB; `echo` in C is 56 KB.

## Guest SDK

`make image` also packages the [TCC port](ports.md#tcc-and-the-guest-sdk) and a
target-only SDK at `boot://sdk`. Shared headers remain in `usr/include`, startup
and runtime archives in `usr/lib`, and TCC-private headers/support in `lib/tcc`.
The guest payload includes library licenses, exact TCC patches and toolchain
source provenance. Its manifest adds the ports revision/dirty state to the
exported SDK record. There is no separate guest ABI version.

The guest receives no compiler or linker executables, Clang private headers,
SDK linker script or C++ runtime and headers. TCC writes P1F directly using the same loader
contract. `boot://sdk` is read-only; applications compile source and write output
in `tmp://` or other explicitly granted directories.

## Runtime build phase

The root build first exports public headers and compiler settings, then invokes
`userspace/runtime.mk` to build startup and the three libraries under
`build/runtime`. It installs those outputs and linker support
alongside the target npfs codecs built by `scripts/npfs-sdk.mk`, before invoking
the separate application build. This avoids a dependency cycle
between SDK production and applications.

`scripts/cxx-runtime.sh` then builds the C++ runtime against the exported
headers, with CMake. It reads the fork commit from the installed toolchain's
`share/pyxis-toolchain/llvm-revision` and fetches only that commit's runtime
sources, shallow, blobless and sparse (about 40 MiB), from `git.internal`. The
checkout and build live in `build/cxx-runtime`, keyed by the commit, so repeated
SDK builds reuse them; a new toolchain commit or `make clean` fetches again, and
a changed script configures from scratch. CI runs the same fetch in each job.
The header export keeps libc++'s headers in place, so unchanged builds keep
their timestamps.

For focused runtime work, run `make sdk-headers`, then:

```sh
make -C userspace -f runtime.mk SDK=../build/sdk BUILD=../build/runtime libc
```

`libpyxis` and `libterm` are also targets. Run `make sdk` afterward to publish a
complete SDK. Standalone runtime builds default to `build/runtime` within the
userland checkout. They take their own libc/libpyxis/libterm headers from that
checkout and ABI/format headers from the selected SDK. TLSF is vendored in
userland. The shared shebang parser and US key layout stay in Pyxis and are
exported to `share/pyxis/shebang.c` and `share/pyxis/key_layout.c` during the
header stage, so runtime builds never reach into parent source directories.

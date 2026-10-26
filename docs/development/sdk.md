# Userspace SDK

`make sdk` exports `build/sdk` using the prebuilt `x86_64-unknown-pyxis-`
[toolchain](../../toolchain/README.md). `make userspace` builds that SDK first, then
the ports development files and applications against it; `make image` continues through initrd and ISO assembly.
The toolchain is the prebuilt Pyxis Clang and LLD. Initialize the [userspace submodule](sdk-and-repositories.md)
and filesystem submodules with `git submodule update --init userspace fs` before
building the SDK.

## Contents and ownership

Libc headers include `regex.h`, the minimal `wchar.h` type interface, and ASCII
`wctype.h`. The four POSIX regex functions and UTF-8 `mbtowc` live in `libc.a`;
there is no extra link library. See
[interfaces and limits](../userland/libc-portability.md#regular-expressions-and-utf-8-conversion).

| SDK path | Contents |
| --- | --- |
| `sysroot/usr/include` | libc, libpyxis, libterm, libfont and libsha256 headers, public `abi/`, `pxe/`, `terminal/` and `pyxis_fs/npfs.h` headers, and libc++'s headers in `c++/v1` |
| `sysroot/usr/lib` | `crt0.o`, `libc.a`, `libpyxis.a`, `libterm.a`, `libfont.a`, `libsha256.a`, target `libnpfs-format.a`, the compiler runtime `libclang_rt.builtins.a`, the [C++ runtime](#c) `libc++.a`, `libc++abi.a` and `libunwind.a`, and `pyxis.ld` |
| `share/pyxis.mk` | Relocatable compiler, compile/link flags and exported artifact paths |
| `share/pyxis.cmake` | Relocatable [CMake toolchain file](#cmake) |
| `share/cmake/Platform/Pyxis.cmake` | The CMake platform description `pyxis.cmake` loads |
| `share/pyxis/machine_settings` | The shared userspace hostname validator/header for host image assembly, also compiled into libpyxis |
| `share/pyxis/shebang.c` | Authoritative shared parser source, compiled into libpyxis |
| `share/pyxis/key_layout.c` | The US key layout shared with the kernel's terminal text, compiled into libpyxis as `key_layout_character` (`pxe/key_layout.h`) |
| `share/pyxis/terminal_style.c` | Shared bounded SGR interpretation and palette resolution (`terminal/style.h`), compiled into libpyxis, the kernel and host remote client |
| `share/licenses` | TLSF, musl, stb_truetype and libsha256 licenses/adaptation records, TRE's BSD notice and the npfs MPL-2.0 license |
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
constant suffixes preserve the Pyxis choices first made with GCC. `MB_LEN_MAX` is 4
for libc's UTF-8 conversion and bounded `iconv`; the limits headers add no
runtime facilities. Signals remain absent. The C-only locale supplies
`setlocale` and `localeconv`; [wide FILE I/O](../userland/stdio.md#wide-file-io)
and [restartable conversion](../userland/libc-portability.md#regular-expressions-and-utf-8-conversion)
are bounded native facilities.

`inttypes.h` includes stdint.h and supplies PRId/PRIi/PRIo/PRIu/PRIx/PRIX
output macros for the 8/16/32/64-bit, pointer and greatest-width types and
declares `strtoimax`, the only conversion function. Other integer-type families
and the SCN scanning macros are not supplied. `stdio.h` defines BUFSIZ as 8192,
the default size `setvbuf` and `setbuf` use when they enable stream buffering;
output is unbuffered otherwise. Descriptor I/O headers include
`fcntl.h`, `unistd.h` and the exported `sys/types.h` subdirectory; see
[the I/O contract](../userland/stdio.md#descriptor-io).

Libc headers can be included from C++: declarations are wrapped in `extern "C"`,
and parameters use `__restrict`, which C and C++ both accept.

`stddef.h`, `stdarg.h`, `stdbool.h` and `float.h` remain compiler-provided.
SDK `-I` paths precede compiler `-isystem` paths, so compilers use the SDK's
integer definitions while retaining their own compiler-sensitive headers.
Public declarations use GNU noreturn attributes where needed and
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
(the runtime archive's `-l` name). It supplies paths
relative to its installed location, not the source checkout. Applications link
their objects with `PYXIS_START` and `PYXIS_LDLIBS` straight to a `.pxe`
executable: the Pyxis driver has LLD write P1F. For a debugger, relink with
`-Wl,--oformat=elf`; the ELF has symbols at the same addresses.

The fragment selects hosted GNU C23 for programs and reusable libraries, static
linking, 4 KiB segment alignment and fixed load address. Libc, its musl/TLSF closure,
crt0 and pre-TLS startup remain freestanding in `runtime.mk`, including the
native environment/handle/memory/thread helper units used before FS is installed.
Explicit no-builtin and rounding controls are preserved. The no-red-zone
convention is unchanged.
The fragment excludes implicit host or
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
calls `main`. A runtime startup failure publishes
[the typed process result](../interfaces/processes.md#implemented-process-completion),
so the parent can name the failing step before child stdio exists. Ordinary
application `_Exit`/`abort` remains an ordinary exit; startup failure is explicit,
not inferred from status 1. Returning from `main` calls `exit`, which runs `atexit` and
`__cxa_atexit` handlers in reverse order of registration, then `.fini_array`
in reverse, then closes stdio. `_Exit`, `abort` and faults skip all three.
The first 32 handlers use static storage; later ones are allocated from the
heap, and registration reports failure if allocation fails.

`pyxis.ld` places `.eh_frame_hdr`, `.eh_frame` and `.gcc_except_table` in the
read-only segment with `__eh_frame_hdr_start`/`_end` and
`__eh_frame_start`/`_end` bounds for a static unwinder, and the constructor and
destructor arrays in the data segment. C code is compiled without unwind tables,
so C executables carry none.

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
| Exceptions, RTTI, static constructors/destructors, local statics, executable-only `thread_local`, joinable and detached threads (`std::thread::detach`), `std::async` workers on detached siblings and synchronization; exiting with live workers skips orderly finalization ([threads](../userland/threads.md#creation-and-collection)) | Non-lock-free compiler atomic helpers |
| Containers, algorithms, strings, `<format>`, `<print>`, `<charconv>`, streams, C-only locale, wide facilities | Non-C locales |
| `system_clock`, `steady_clock`, native `random_device` | `<filesystem>`, time zones |
| Aligned `new` up to 4096 bytes | Most of `<cmath>`, which follows libc's [math subset](../kernel/userspace.md#foundational-libc) |

The matching gate 7 compiler adopts the hosted-Clang
[task 2 closure](experiments/hosted-clang-cxx.md) and static local-exec TLS. The
SDK builds from the installed compiler's exact runtime source. It enables the
native `__cxa_thread_atexit_impl` bridge and enables the audited external thread
profile. The [libc adapter](../userland/threads.md) supplies real parking-backed
mutexes/conditions/once and checked unwinder rwlocks; no host pthread library
or POSIX-shaped kernel surface is introduced. No new compiler build is needed.

The headers of absent features still include, but their names do not exist, so
a program using one fails to compile. `-fno-exceptions` and `-fno-rtti` code links
against the same archives.

An uncaught exception prints `libc++abi: terminating due to uncaught
exception of type …` with the mangled type name, to keep the demangler out of
every program, and exits with status 1 through `abort`. C code has no unwind
tables, so an exception thrown through a C frame, such as a `qsort`
comparator, also terminates. A C++ program printing a `vector<string>` is about
108 KB; `echo` in C is 56 KB.

## Static TLS and runtime keys

Executable-only `_Thread_local`/`thread_local` use x86-64 local-exec variant II.
`pyxis.ld` exposes `__tls_template_start`, `__tls_template_init_end`,
`__tls_template_end` and absolute `__tls_alignment`. The zero-tail extent can
share ELF virtual addresses with ordinary data; bootstrap copies only initialized
bytes, then zeros the private instance. TLS block padding follows its alignment;
TCB alignment is separate. Other TLS models, emulation and dynamic modules reject.
The final P1F contains ordinary load segments, without a TLS header.

Before errno, stdio, malloc or constructors, TLS-free startup validates native
bindings, retains MEMORY MANAGE, allocates guarded backing and binds the caller's
FS once using the distinct startup `thread_fs` grant. That grant supplies SET_FS
only and is closed after binding. Missing authority, allocation or bind failure
completes as `PROCESS_START_FAILED` with the step and native error, without
using stderr, errno or allocation to report it; there is no global errno fallback. FS:0 points to libc's TCB;
`errno` is an accessor to its field. Rebuild SDK, applications and ports together;
old runtime archives expecting a global errno are not compatible.

`<pyxis/thread_runtime.h>` prepares contexts for native CREATE, drains ordinary
thread exit, and collects context backing after the matching native completion.
This low-level bridge remains separate from the managed
[C11/pthread creation and collection profile](../userland/threads.md). Distinct guarded
stacks and prepared FS contexts remain caller-owned; native pins prevent RELEASE
until retirement. Raw native entries that omit libc still start with FS=0.

Keys are process-local and generational, initially 128 slots as tunable policy.
Deletion invokes no callback; stale generations reject. Values belong to each TCB.
Ordinary exit drains language destructors in reverse registration order, then up
to four key passes with values cleared before callbacks. Callbacks run outside
registry exclusion. Required language-destructor registration allocation failure
terminates the process honestly. Main-thread FS remains alive through atexit,
.fini_array and stdio shutdown. Forced process stop/fault runs no user destructors.
See [gate 7 qualification](experiments/threads-task7/README.md) and the remaining
[native threads](../interfaces/threads.md#delivery).

## CMake

CMake projects select the SDK with its toolchain file:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/sdk/share/pyxis.cmake
```

Like `pyxis.mk`, the file resolves paths from its own location. It sets:

- **The platform.** `CMAKE_SYSTEM_NAME` is `Pyxis`, so a project can test
  `CMAKE_SYSTEM_NAME STREQUAL "Pyxis"`. Neither `UNIX` nor `WIN32` is set.
  Executables are named `.pxe`, and only static libraries exist.
- **The compilers.** C, C++ and assembly use the `x86_64-unknown-pyxis-` tools
  on `PATH`; `-DPYXIS_CROSS_COMPILE=PREFIX` or a `CROSS_COMPILE` environment
  variable selects another prefix. `CMAKE_SYSROOT` is the SDK's sysroot, and
  the driver supplies startup, libraries, the linker script and P1F output.
- **The language modes.** C and C++ are hosted. C keeps the SDK's headers ahead
  of Clang's builtin ones, as in `pyxis.mk`, so `stdint.h` is the SDK's. C++
  keeps the driver's own header order. Runtime implementation exceptions are
  selected by their build, not inherited by applications.
- **Searching.** Libraries, headers and packages are searched only in the
  sysroot and in other `CMAKE_FIND_ROOT_PATH` entries, never programs. Each
  entry is searched at `/usr` and at its top. Development prefixes such as
  `build/ports-dev/sdl2` can therefore be added, for example
  `-DCMAKE_FIND_ROOT_PATH=/path/to/pyxis/build/ports-dev/sdl2`.

CMake caches its C flags: use a fresh build directory when adopting changed
SDK language defaults.

It leaves the language standard, optimization and warnings to the project and
`CMAKE_BUILD_TYPE`. Configure checks link real executables against the
complete SDK, so a check for a missing function fails. Some things to know:

- Stripping after the link does not work, because `llvm-strip` cannot read
  P1F executables.
- Object files are named `.obj`, because CMake picks `.o` only for `UNIX`
  platforms. Builds are unaffected unless a project names `.o` files itself.

The fastfetch, fmt and mbedtls recipes use this file.
[The C++ runtime build](#runtime-build-phase) configures CMake itself, because
it runs before the SDK is complete.

## Guest SDK

The `pyxis-dev` package exposes the target SDK through the installed
[`sdk://` view](../interfaces/packages.md#reaching-package-files): public headers,
startup/runtime archives, `pyxis.ld`, `pyxis.pc`, licences and toolchain source
provenance. `libfont.a` is not a default library; link it with `-lfont`.
There is no separate guest ABI version.

The default core archive carries no `boot://sdk`. Full development images retain
a target-only archive copy for development; normal hosted Clang uses `sdk://`.
Compiler/linker executables and private Clang resources come from the separate
`clang-hosted` package, while C++ headers/runtime come from `libcxx-dev`; see
[library development packages](library-dev-packages.md).

## Runtime build phase

The root build first exports public headers and compiler settings, then invokes
`userspace/runtime.mk` to build startup and the four libraries under
`build/runtime`. It installs those outputs and linker support
alongside the target npfs codecs built by `scripts/npfs-sdk.mk`, before invoking
the separate application build. This avoids a dependency cycle
between SDK production and applications. `libfont` consumers include `font.h`
and link explicitly with `-lfont`.

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

`libpyxis`, `libterm` and `libfont` are also targets. Run `make sdk` afterward to publish a
complete SDK. Standalone runtime builds default to `build/runtime` within the
userland checkout. They take their own libc/libpyxis/libterm/libfont headers from that
checkout and ABI/format headers from the selected SDK. TLSF is vendored in
userland. The shared shebang parser and US key layout stay in Pyxis and are
exported to `share/pyxis/shebang.c` and `share/pyxis/key_layout.c` during the
header stage, so runtime builds never reach into parent source directories.

`sha256.h` and the standalone `libsha256.a` provide caller-owned incremental
SHA-256 without an entropy, clock or allocator dependency. Link with
`-lsha256` before the normal runtime libraries; `libsha256.pc` also supplies
static libc linkage metadata. See [the SHA-256 contract](../userland/sha256.md).

## Static image bounds

The SDK linker script exports `__pyxis_image_start` and `__pyxis_image_end` as
addresses bounding the statically linked image, including its BSS. They allocate
no storage and convey no memory authority. Native consumers that must distinguish
image addresses from tagged data can use these symbols without a dynamic loader
or a fixed architecture-specific load address. Heap and sibling stacks are
outside this interval; this is not a general pointer-validation API.

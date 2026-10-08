# C++ in userspace

Status: **decisions accepted; tasks 1–3 done, task 4 in review, 2026-10-08.**
The owner selected this milestone for Claude on 2026-10-07 and accepted the three
[decisions](#decisions) on 2026-10-08. The rest of the [proposal](#proposal)
guides the tasks; each task starts when the owner says so. It is the second of
the three
[LLVM milestones](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
narrowed to userspace. Clang running on Pyxis, and threads, come later.

## Goal

- **Cross-compiled C++ programs run on Pyxis.** The host Clang builds them
  against the SDK, linking a C++ runtime built from the `pyxis-llvm` fork.
- **The supported subset is explicit.** Features the runtime leaves out fail
  visibly when compiling or linking, and the SDK documentation lists them, so a
  port knows what is missing before it runs.
- **One real consumer** uses the runtime. Ports such as DevilutionX come
  afterwards, in their own milestones.

This milestone does not change the kernel's language. It does not revive the
kernel C++ experiment, and it does not provide a C++ compiler inside Pyxis.

## Today

- **The compiler side mostly exists.** The Pyxis driver already selects libc++ and
  compiler-rt, and Clang compiles C++ for the Pyxis target. However, the driver
  adds no C++ header directory and links no C++ libraries. The toolchain also
  installs no `x86_64-unknown-pyxis-clang++` name, which CMake and Make would
  look for.
- **Startup runs no constructors and exit runs no handlers.**
  - `libc_enter` initializes stdio and the heap, then calls `main`.
  - `exit` closes stdio. libc has no `atexit`.
  - `pyxis.ld` has no `.init_array`.
- **No unwind information survives a link.** `pyxis.ld` discards `.eh_frame`.
  C is compiled without unwind tables, so none of the 190 C objects or
  SDK archives contains `.eh_frame` today.
- **There is no thread-local storage.** P1F rejects TLS segments, and processes
  have one thread.
- **libc headers are C-only.** They have no `extern "C"`, they use `restrict`,
  and `stdlib.h` names a parameter `template`.
- **libc is a small subset.** Its math is a short list taken from the vendored
  musl sources. It has no locale and almost no wide-character support.

## Probe results

Measured on 2026-10-08 in Claude's Fedora VM: nested KVM inside the owner's
desktop, 8 vCPUs. The build used:

- Pyxis main `a87e524` and userland `2430567`;
- the pinned toolchain `41ab6043cc4f`;
- libunwind, libc++abi and libc++ from that same fork commit.

QEMU ran with 4 CPUs under KVM, and test programs were loaded from `host://`.
Nothing from the probe is committed. The probe scaffolding was:

- a copy of the SDK headers with C++ fixes;
- a forced-include header supplying missing declarations;
- a startup shim that runs constructors and exit handlers around `main`;
- a linker script that keeps unwind tables and `.init_array`;
- a copy of `malloc.c` with `aligned_alloc` and `ceilf` added.

### What the runtime needs from libc

Building the three libraries with threads, localization, wide characters,
filesystem and `random_device` off failed until the following existed:

- **C++-safe headers:**
  - `extern "C"` guards;
  - no `restrict` keyword in C++;
  - no parameter named `template`.

  Without these, every libc function was mangled as a C++ name and failed to
  link.
- **Missing declarations:**
  - `div_t`, `ldiv_t`, `lldiv_t` and their functions, plus `labs` and `llabs`;
  - `mbstate_t`;
  - the `FP_*` classification macros;
  - `PRIxPTR` and the other pointer format macros;
  - `fenv.h`, whose macros libc++'s float parsing includes.
- **Platform selection inside libc++:**
  - `chrono.cpp` falls back to `gettimeofday` unless it is told that
    `timespec_get` exists. libc already provides `timespec_get` for UTC.
  - `print.cpp` calls `fileno` and `isatty` whenever `unistd.h` exists.
- **Link-time symbols:** a program using containers, `std::format`, aligned
  `new` and exceptions needed only `aligned_alloc` and `ceilf` beyond today's
  libc. libc's TLSF allocator already provides aligned allocation.

Most of the C library that libc++ declares is still absent. libc++ declares
these with `using ... _LIBCPP_USING_IF_EXISTS`, so a missing function becomes a
compile error at its first use, not a silent stub.

| libc++ header | Names it imports | Missing from libc |
| --- | --- | --- |
| `<cmath>` | 186 | 161, such as `exp`, `log`, `fmin` and most `float`/`long double` forms |
| `<cwchar>` | 62 | 59 |
| `<cfenv>`, `<clocale>`, `<cinttypes>` | 23 | all |
| `<cstdlib>`, `<cstdio>`, `<cstring>`, `<ctime>` | 124 | 21, such as `atol`, `strtok` and `strncat` |

### Behaviour in QEMU

With a probe startup shim that runs `.init_array` after heap setup, and
`__cxa_atexit` handlers before stdio closes, one program showed the following
working:

- a global constructor and destructor;
- `vector`, `map`, `unordered_map`, `string`, `unique_ptr`, `variant`,
  `optional` and `function`;
- `std::format` with a float;
- virtual calls, `dynamic_cast` and `typeid`;
- a lambda-initialized local static;
- 64-byte aligned `new`;
- `system_clock::now`;
- catching `std::runtime_error`, an `int` and a derived class by base
  reference;
- `std::out_of_range` thrown from inside libc++;
- `std::bad_alloc` from an impossible `new[]`.

Unwinding used libunwind's static mode. A linker script kept `.eh_frame` and
`.eh_frame_hdr` in the read-only segment and exported their bounds. P1F and the
loader needed no change.

Edge cases:

- **An uncaught exception** prints
  `libc++abi: terminating due to uncaught exception of type std::logic_error: …`
  and exits with status 1, through libc's `abort`.
- **An exception thrown from a `qsort` comparator** terminates the program the
  same way. The libc `qsort` frame has no unwind table, so the exception cannot
  cross it.
- **`thread_local`** fails at link time: `STT_TLS symbol but … no PT_TLS
  segment`.

### Cost

- **Building the runtime:**
  - configuring takes 2.1 s and building 3.9 s (16.4 s CPU), for 60
    compiled sources;
  - the static archives are libc++ 604 KiB, libc++abi 712 KiB and libunwind
    80 KiB.
- **Fetching its source:** a shallow, blobless fetch of the fork commit, sparse
  to `runtimes`, `libcxx`, `libcxxabi`, `libunwind`, `cmake`, `llvm/cmake` and
  `libc`, takes 41 MiB and 4.4 s from `git.internal`. The runtime builds from
  that checkout unchanged.
- **Executable size:**
  - a C++ program printing a `vector<string>` is 293,079 bytes; the C `echo`
    is 55,659;
  - with `-fno-exceptions -fno-rtti` it is 288,235 bytes;
  - most of that is libc++abi's demangler, which the default terminate handler
    pulls in to name the uncaught exception's type;
  - with libc++abi's non-demangling terminate handler, the same program is
    108,739 bytes and the full probe program falls from 642,895 to 458,579.
    The uncaught-exception message then reads `… of type St11logic_error: …`.
- **C programs:** their objects carry no `.eh_frame`, so keeping that section in
  the shared linker script should leave C executables unchanged. Task 2 checks
  this byte for byte.

## Proposal

### Runtime

libunwind, libc++abi and libc++, built as static archives from the fork and
installed in the SDK sysroot:

- C++ headers in `usr/include/c++/v1`;
- archives in `usr/lib`;
- the LLVM license under `share/licenses`.

The guest SDK gets none of it, because TCC cannot compile C++.

| Feature | Setting | Reason |
| --- | --- | --- |
| Exceptions and RTTI | on | the language default; libc++ itself throws; `-fno-exceptions` code still links |
| Unwinder | libunwind, static mode | locates `.eh_frame_hdr` from linker-script symbols; no loader change |
| Threads, `thread_local` | off | no threads or TLS yet; Clang hosting brings them |
| Local statics | single-threaded guards | follows from threads off |
| Localization: `<iostream>`, `<locale>`, `<regex>` | off | needs locale and wide-character libc |
| Wide characters, `<filesystem>`, `random_device`, time zones | off | no libc or native support yet |
| `steady_clock` | on | through C23 `timespec_get(TIME_MONOTONIC)`, a small libc addition |
| Terminate handler | non-demangling | saves about 184 KB per program; uncaught type names appear mangled |
| Hardening | libc++ default, none | a program can select a mode with libc++'s macro |

The headers of disabled features still include cleanly, but their names are
absent: `std::thread`, `std::mutex`, `std::cout << 1`, `random_device` and
`steady_clock` (while off) each fail when compiling.

### Startup, exit and layout (userland)

- **Startup.** `libc_enter` runs `.init_array` after stdio and the heap are
  ready, then calls `main`. The array bounds are weak symbols, so TCC's links,
  which have no arrays, still work.
- **Exit.** libc gains C's `atexit` and the Itanium ABI's `__cxa_atexit` and
  `__dso_handle`. `exit` runs registered handlers in reverse order, then
  `.fini_array`, then closes stdio. `_Exit` and `abort` skip them, as today.
- **Layout.** `pyxis.ld` stops discarding `.eh_frame`. It keeps `.eh_frame_hdr`,
  `.eh_frame` and `.gcc_except_table` in the read-only segment and
  `.init_array`/`.fini_array` in the data segment, with the bounds libunwind
  and startup need.
- **One effect on C programs.** compiler-rt's `cpu_model` constructor starts
  running in the programs that use `__builtin_cpu_supports`. Today the
  [LLVM toolchain limits](../development/llvm-toolchain.md#limits) say it never
  runs.

### libc additions (userland)

- Make every libc header C++-safe.
- Add the declarations and functions libc++ requires:
  - `aligned_alloc`;
  - the `div_t` family, `labs` and `llabs`;
  - `mbstate_t`;
  - the `FP_*` macros;
  - the pointer format macros;
  - `fenv.h`;
  - `TIME_MONOTONIC`.
- Add `ceilf`, which libc++'s unordered containers use, and whatever further
  standard functions the first consumer needs. These are real implementations,
  taken from the vendored musl where possible.

A full libm stays outside the milestone. The SDK reference lists which C and
C++ library parts are absent.

### Fork and toolchain (owner rebuilds the container once)

- **The driver:**
  - adds `usr/include/c++/v1` ahead of libc's headers for C++;
  - links `-lc++ -lc++abi -lunwind` when invoked as `clang++`;
  - always passes `--eh-frame-hdr`.
- **No TLS.** Pyxis's target information marks thread-local storage as
  unsupported, so `thread_local` and `_Thread_local` fail when compiling, not at
  link time.
- **libc++ platform selection:**
  - `timespec_get` drives `system_clock` and `steady_clock`;
  - `std::print` does not ask whether the output is a terminal. Pyxis libc has
    no `isatty`, and stdout is unbuffered anyway.
- **The toolchain** installs `x86_64-unknown-pyxis-clang++`.
- **The SDK fragment.** `share/pyxis.mk` gains `CXX`, `PYXIS_CXX_CPPFLAGS`,
  `PYXIS_CXXFLAGS` and `PYXIS_CXX_LDLIBS`, with the libc++ header directory
  ahead of libc's. libc++'s `<stdlib.h>` must be found before libc's.

## Decisions

Accepted by the owner on 2026-10-08, as proposed after the probe:

1. **Where the runtime is built: in the SDK build.** It compiles from the
   fork commit recorded by the installed toolchain
   (`share/pyxis-toolchain/llvm-revision`), using the sparse fetch measured
   above.
   - One authority keeps the libc++ headers matched to the compiler.
   - A libc change never needs a container rebuild.
   - A runtime-only fork change does need one, because it moves the pin.

   Considered instead:
   - building it in the toolchain container, which bakes libc's headers into
     the image;
   - a recipe in `pyxis-ports`, which adds a second pin and puts the runtime
     outside the SDK that the driver searches.
2. **The first library configuration, as in the [table](#runtime).**
   - Exceptions and RTTI are on, `steady_clock` is on, and the terminate
     handler does not demangle.
   - Everything else listed there is off.
   - libc grows only where the runtime or the consumer needs it.

   `<iostream>` and locales were considered and deferred. They first need a
   "C"-locale libc: `locale.h` and the missing wide-character functions (59
   names in `<cwchar>` alone).
3. **The first consumer: [{fmt}](https://github.com/fmtlib/fmt).** It is a C++
   library that DevilutionX requires, and it uses templates, exceptions and
   floating-point formatting heavily.
   - The owner mirrored it as `mirrors/fmt` on 2026-10-08. Task 4 pins release
     `12.2.0` (commit `1be298e1bd68`) unless the owner chooses another.
   - It is checked in QEMU with a small program that is not committed, so no
     C++ program ships in the image yet.
   - fmt's locale support has to be disabled at build time; the probe did not
     check this.

   Porting a small C++ program chosen by the owner was the alternative.

## Tasks

Each task starts when the owner says so.

1. **Probe and proposal** (this document).
2. **libc and SDK layout** (userland #153, Pyxis #505). Done 2026-10-08:
   - C++-safe headers;
   - constructors, exit handlers and `aligned_alloc`;
   - the other [libc additions](#libc-additions-userland) the runtime build
     needs, so task 3 changes no libc;
   - the linker-script sections.

   See [task 2 results](#task-2-results).
3. **Fork, toolchain and runtime build** (pyxis-llvm #3, userland #156,
   Pyxis #510). Done 2026-10-08:
   - the driver and libc++ commits in `pyxis-llvm`;
   - the toolchain pin, the `clang++` name and the new image tag;
   - the SDK runtime build and `pyxis.mk`'s C++ settings.

   CI passed on the owner-built image. See [task 3 results](#task-3-results).
4. **The {fmt} port**, from `mirrors/fmt`. In review, 2026-10-08; it needed no
   libc additions. See [task 4 results](#task-4-results).
5. **Close.** Turn this document into a reference under `docs/development`,
   listing the supported subset and its gaps.

## Task 2 results

Measured on 2026-10-08 in the same VM as the probe, against main `2a9ddd6`
and userland `38886c7`.

- **C executables.** All 81 executables in the build still have the same three
  segments with the same flags, and no read-only data changes, so none gained
  unwind tables. Each grows by the new startup and exit code:
  - text grows 624 to 848 bytes;
  - initialized data grows 16 bytes (`__dso_handle` and the handler list
    pointer);
  - the data segment's memory size grows 784 or 800 bytes, mostly the 32
    static handler slots.

  In 9 executables the text crosses a page boundary, so the later segments
  move up one page. The build's 399 warnings are unchanged, all in ports'
  upstream code.
- **The kernel.** It differs from the earlier build only in the 12 bytes of its
  embedded revision string. Its own linker script still discards `.eh_frame`.
- **In QEMU** (4 CPUs, KVM), a throwaway C program showed:
  - a constructor using `malloc` and stdio before `main`;
  - atexit handlers in reverse order, including more than 32 and one
    registered during exit;
  - `.fini_array` running last, and `_Exit` skipping all of them;
  - `aligned_alloc` at 64 and 4096 bytes, and `NULL` for 3 and 8192;
  - the `div` family, the new format macros and `ceilf`;
  - `timespec_get(TIME_MONOTONIC)` advancing;
  - fenv flags from SSE and x87 division, both rounding directions, and
    `feholdexcept`/`feupdateenv`.
- **TCC.** A program compiled and linked by TCC inside Pyxis ran its `atexit`
  handler and used `aligned_alloc`.
- **The C++ probe.** The probe program rebuilt against these headers with no
  forced declarations except `fileno`/`isatty`, now with `steady_clock` on and
  the non-demangling terminate handler. Linked with the SDK's `crt0.o` and
  `pyxis.ld` (no probe shim), it behaved as in the probe, and `steady_clock`
  advanced.

For task 3: libc++'s `system_error` warns that `ELAST` is not defined for
Pyxis.

## Task 3 results

Measured on 2026-10-08 in the same VM, against main `c81b9a7`, with the
toolchain built from fork commit `49e2c1a1518b` (19 minutes on 8 vCPUs).

- **The runtime build.** A clean `make sdk` fetches and builds the runtime in
  about 16 s in all; an unchanged rerun takes about 1 s, and a touched libc
  header rebuilds nothing. The archives record no dependent libraries.
- **C executables and the kernel.** Main built in the same directory with the
  old and new toolchains gives byte-identical output, apart from:
  - the build timestamps in Quake and fastfetch;
  - the compiler identification in the kernel's debug information.

  The kernel's loaded sections are identical, so `--eh-frame-hdr` adds nothing
  to C links. The image builds with the same 399 warnings.
- **Fixed during the task:**
  - libc++abi's bare-metal mode silenced the uncaught-exception message, so
    the SDK builds libc++abi hosted;
  - CMake kept that setting cached after it was removed, so the script now
    configures from scratch whenever it changes.
- **In QEMU** (4 CPUs, KVM), programs built with plain `clang++
  --sysroot=… -o app.pxe`, and one built through `pyxis.mk`, ran correctly:
  - the probe program, with `steady_clock`;
  - `std::println`;
  - `unordered_map`;
  - `error_code` messages, including an unknown value;
  - the uncaught-exception and `qsort` messages.

  `thread_local` and `_Thread_local` fail when compiling. The guest SDK has no
  `c++` headers, and TCC in the guest still compiles and runs C programs.

## Task 4 results

Measured on 2026-10-08 in the same VM, against main `cffc733`.

- **The port.** fmt 12.2.0 (`1be298e1bd68`) builds with the SDK's C++ settings
  and no warnings in about 3 s. It needed no libc additions. It did need two
  patches, both keyed to libc++'s feature macros:
  - locale support off, because libc++ has no localization;
  - one Windows helper returning `std::wstring` left out, because libc++ has
    no wide characters.

  `FMT_OS` is off.
- **Headers.** Of fmt's headers, `chrono.h`, `ostream.h`, `std.h` and `xchar.h`
  need `std::locale`, `printf.h` needs `std::wstring`, and `os.h` needs `FMT_OS`;
  each fails to compile. The rest are usable. With locales off, upstream's `L`
  specifier groups floating-point values with `,` but not integers.
- **In QEMU** (4 CPUs, KVM), a throwaway consumer ran correctly, both to the
  terminal and through a pipe. It used:
  - alignment, width, precision, hexadecimal and scientific formats;
  - shortest float output;
  - vectors, maps and `join` from `ranges.h`;
  - a custom formatter;
  - `color.h` escapes;
  - `fmt::format_error` from a runtime format string;
  - `memory_buffer`, and `print` to stderr.

  It is 258,899 bytes.

## Not in this milestone

- Threads, `thread_local`, `<thread>`, `<mutex>` and non-lock-free atomics.
- Shared libraries.
- Exceptions crossing C frames (`qsort` callbacks): C code is built without
  unwind tables, so such an exception terminates the program.
- A full libm.
- Clang or any other C++ compiler running on Pyxis.

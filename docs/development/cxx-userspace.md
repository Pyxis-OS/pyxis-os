# C++ in userspace

Pyxis runs cross-compiled C++ programs. The host Clang builds them against the
SDK, which carries libc++, libc++abi and libunwind built from the `pyxis-llvm`
fork. The milestone completed on 2026-10-08. It is the userspace part of the
second [LLVM milestone](../wip/toolchains-and-runtimes.md#llvmclang-transition-and-hosting);
Clang running on Pyxis, with threads, comes later.

How to build a C++ program, and what the runtime does and does not provide, is
in the [SDK's C++ section](sdk.md#c). The accepted
[limits](../technical-debt.md#c-runtime-subset) are in technical debt.

## The pieces

- **The compiler** ([toolchain README](../../toolchain/README.md),
  [fork](llvm-toolchain.md#the-fork)):
  - for C++, the Pyxis driver searches the sysroot's `usr/include/c++/v1`
    ahead of libc's headers and links libc++, libc++abi and libunwind;
  - every executable link passes `--eh-frame-hdr`;
  - `thread_local` and `_Thread_local` fail when compiling;
  - libc++ uses `timespec_get` for its clocks, skips the terminal check in
    `print` and needs no `ELAST` limit;
  - the toolchain installs `x86_64-unknown-pyxis-clang++`.
- **The runtime build** ([SDK runtime build phase](sdk.md#runtime-build-phase)).
  During `make sdk`, `scripts/cxx-runtime.sh`:
  - reads the fork commit from the installed toolchain;
  - fetches only that commit's runtime sources (sparse, shallow and blobless);
  - builds the three archives with CMake against the exported libc headers.

  The checkout and build are kept in `build/cxx-runtime`, keyed by the commit.
  The guest SDK leaves the C++ headers and archives out, because TCC compiles
  C only.
- **libc** ([startup and exit](sdk.md#startup-exit-and-layout)):
  - startup runs `.init_array`, and `exit` runs `atexit`/`__cxa_atexit`
    handlers and then `.fini_array`;
  - `pyxis.ld` keeps the exception tables and their bounds;
  - the headers can be included from C++;
  - libc gained the declarations and functions libc++ needs: `aligned_alloc`,
    `fenv.h`, the `div` family, `mbstate_t`, `FP_*`, `PRI*PTR`/`PRI*MAX`,
    `ceilf` and `timespec_get(TIME_MONOTONIC)`.
- **The first consumer:** the [fmt port](ports.md#fmt-development-library), a
  development library under `build/ports-dev/fmt` and a DevilutionX
  prerequisite. No program in the image uses C++ yet.

## Decisions

Accepted by the owner on 2026-10-08, after the probe:

1. **Where the runtime is built: in the SDK build.** It uses the fork commit
   the installed toolchain records, so the libc++ headers always match the
   compiler.
   - A libc change never needs a container rebuild.
   - A runtime-only fork change does, because it moves the pin.
2. **The configuration:**
   - on: exceptions, RTTI and `steady_clock`, with a terminate handler that
     does not demangle;
   - off: threads, localization (`<iostream>`, `<locale>`, `<regex>`), wide
     characters, `<filesystem>`, `random_device` and time zones.

   libc grows only where the runtime or a consumer needs it.
3. **The first consumer: fmt.** It is checked in QEMU with a program that is
   not committed, so no C++ program ships yet.

Choices made while implementing:
- **libunwind runs in bare-metal mode.** It finds the tables through the
  linker script instead of `dl_iterate_phdr`.
- **libc++abi stays hosted.** On x86-64, its bare-metal mode only silences the
  uncaught-exception message.
- **Configure checks for pthread, rt, dl and atomic are forced off.** The
  configure step only compiles its test programs, so those checks would pass
  spuriously.
- **fmt follows libc++'s feature macros.** Two patches turn off its locale
  support and leave out one `std::wstring` helper.

## Measurements

Taken on 2026-10-08 in Claude's Fedora VM (nested KVM, 8 vCPUs; QEMU with 4
CPUs).

- **The runtime build:** fetching and building take about 16 s in a clean
  `make sdk`, and an unchanged rerun about 1 s. The archives are libc++
  606 KB, libc++abi 587 KB and libunwind 76 KB.
- **Executable sizes:** a C++ program printing a `vector<string>` is 107,807
  bytes; `echo` in C is 56,299. A demangling terminate handler would add about
  184 KB to every C++ program.
- **C output:** the new toolchain leaves C executables and the kernel's loaded
  sections byte-identical. The startup and exit code adds 624–848 bytes of
  text to each C executable.
- **Behaviour in QEMU** (programs built with plain `clang++ --sysroot` and
  through `pyxis.mk`):
  - constructors and destructors, containers, `std::format`/`std::println`;
  - RTTI and exceptions, including ones thrown inside libc++;
  - aligned `new`, both clocks and `error_code` messages;
  - an uncaught exception's message;
  - fmt formatting.

  No C++ program was run natively on the ThinkPad.

## Maintaining it

- **Moving the LLVM pin** also moves the runtime: change the commit as in
  [changing the pin](llvm-toolchain.md#changing-the-pin); the next SDK build
  fetches and builds the matching runtime. The owner rebuilds the container.
- **Changing the runtime configuration** is a `scripts/cxx-runtime.sh` edit.
  The script reconfigures from scratch whenever it changes, because CMake keeps
  cached options that a later configure no longer passes.
- **A missing C or C++ library function** surfaces as a compile error at its
  first use. Add the standard function to libc, from the vendored musl where
  possible, rather than a port-local stub.

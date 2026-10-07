# LLVM toolchain on the host

Status: **milestone, agreed 2026-10-07.** This is the first of the three LLVM
milestones in [hosted toolchains](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
whose direction was chosen on 2026-09-29. Claude implements it; each task starts
when the owner says so. It runs alongside the [display drivers](display-drivers.md)
milestone; completed [remote debugging](../development/remote-debugging.md)
provides native bring-up support.

## Goal

- **Clang builds Pyxis.** Clang, LLD and the LLVM archive and object tools,
  running on the host, build:
  - the kernel;
  - the SDK;
  - the userland;
  - every port.

  The result boots and behaves as it does with GCC.
- **One toolchain afterwards.** Once that holds, LLVM becomes the default, and
  GCC and binutils are retired.
- **A base for hosting.** This prepares the later milestones: a native C++
  runtime and system support, then Clang running on Pyxis. Those are not part of
  this milestone.

## Today

- **The cross toolchain is GCC 16.2.0 with binutils 2.47.** Both are patched to
  recognize the `x86_64-unknown-pyxis` tuple
  ([toolchain](../../toolchain/README.md)). Only the C frontend and a static
  libgcc are built.
- **The target contract:**
  - the compiler defines `__pyxis__` and no Unix macros;
  - code is baseline x86-64 with SSE2 and no red zone;
  - the kernel adds `-mgeneral-regs-only` and `-mcmodel=kernel`;
  - there are no shared libraries, no PIE, and no C++, thread or exception
    runtime.
- **The driver and SDK:** with `--sysroot`, the driver finds `crt0.o`,
  `pyxis.ld`, libc and libpyxis in the SDK, and libgcc in the compiler
  installation. `elf2pxe` turns the ELF output into PXE executables.
- **The build environment:** the owner builds the toolchain into the builder
  container (`ci/Containerfile`). Ordinary builds and CI consume that container
  and never rebuild GCC.
- **Port recipes** build with Make, CMake or their own scripts, using the cross
  tools by their `x86_64-unknown-pyxis-` prefix.
- **Upstream code** is fetched only from the owner's internal mirrors and caches.
- **Patch precedent:** GCC and binutils changes are patch files on pinned release
  archives. lwIP's local changes live in a separate repository, `pyxis-lwip`.

## Decisions

Agreed with the direction on 2026-09-29:

- The toolchain is Clang, LLD and the LLVM archive and object tools.
  compiler-rt builtins are investigated as libgcc's replacement.
- The existing ABI, the kernel's register restrictions and the userspace
  CPU-state assumptions stay as they are.
- ELF objects, static archives and `elf2pxe` stay.
- The owner builds the toolchain container; ordinary CI does not rebuild LLVM.
- There is no permanent second default toolchain.

Accepted by the owner on 2026-10-07:

1. **Next for Claude.** This milestone follows the completed ACPI milestone.
2. **Switching over.**
   - During the milestone, GCC stays the default and LLVM is chosen by a build
     setting.
   - Once LLVM builds and boots everything (task 4), the default flips and GCC
     and binutils are removed in the same task.

Proposed, for the owner:

3. **Where Pyxis's LLVM changes live: a fork on Forgejo.** Proposed default: a
   `pyxis-llvm` repository, created by the owner.
   - It holds a branch per pinned LLVM release, with Pyxis commits on top of the
     release tag.
   - Builds pin a commit, and moving to a new release is a rebase.
   - Upstream stays reachable through the usual internal mirror.
   - The fork is created only when the first change is needed. If task 1 finds
     that milestone 1 needs no LLVM source change (see task 1), there is nothing
     to fork yet.

   Reasons:
   - The later milestones need large changes: a Pyxis target in the triple and
     driver, and a native backend for LLVM's Support library. Those are easier
     to review, bisect and rebase as commits than as patch files.
   - The `pyxis-lwip` precedent already works this way.

   The cost is the size of the LLVM history on the server. Builds can clone
   shallowly by commit, as the port recipes already do.

## Tasks

- [ ] **1. Probe and proposal.** A docs PR that updates this document with:
  - **Target approach**, one of:
    - upstream Clang with a configuration file for Pyxis (target, sysroot,
      defaults), plus the SDK's startup files and linker script, possibly
      with no LLVM source change in this milestone;
    - a `pyxis` OS in LLVM's triple and Clang's driver, as GCC has today.

    The proposal names what each choice means for milestone 3, where Clang has
    to know it runs on Pyxis.
  - **The pinned release** and where it comes from. That means a distribution
    package, an owner-built container or a fork build, and the exact mirror
    entries the owner must create.
  - **Runtime helpers:** compiler-rt builtins in place of libgcc, including the
    x87 and SSE floating-point helpers and the no-red-zone requirement.
  - **Kernel build equivalence:**
    - flags such as `-mgeneral-regs-only` and `-mcmodel=kernel`;
    - inline assembly and the `.S` files under Clang's integrated assembler;
    - the linker script under LLD;
    - the warnings Clang reports.
  - **SDK and ports:**
    - how the SDK's Make settings, `ports/build.lua`, CMake and the other port
      build systems select the LLVM tools;
    - what the hosted TCC and the Lua runtime need.
  - **Measurements to compare:**
    - kernel and executable sizes;
    - boot time;
    - existing benchmarks such as `iobench`, the memory benchmarks and Quake
      `timedemo`;
    - build time.
  - **Open decisions:** at most about three per round, each with a proposed
    default.

- [ ] **2. Kernel and SDK with LLVM.**
  - The kernel and SDK build with the LLVM setting, with no new warnings left
    unexplained, and the image boots in QEMU.
  - Real problems Clang finds in Pyxis code are fixed in their own commits, and
    they stay correct under GCC too.
  - **Finish when:**
    - one and four CPUs boot to the shell;
    - the power, display and network paths behave as with GCC;
    - the matched measurements are recorded against GCC.

- [ ] **3. Userland and ports with LLVM.**
  - Every userland program and port builds with the LLVM setting.
  - **Finish when:** every port's documented check passes in QEMU, including
    Doom, Quake, Lua, TCC compiling a program, Links, BusyBox and Fastfetch.
    Any port that needs a recipe change records it in its README.

- [ ] **4. Switch the default and retire GCC.**
  - The owner builds and publishes the LLVM builder container.
  - LLVM becomes the default, and the GCC and binutils patches and build script
    are removed. The toolchain and SDK docs describe the LLVM contract.
  - The switch lands at a quiet point between Codex tasks.
  - **Finish when:**
    - the owner's ThinkPad boots and behaves as before;
    - an installed system updates to the LLVM build;
    - the matched measurements are recorded.

## Working rules

- **No disruption to other work.** Until task 4, GCC stays the default and every
  existing build keeps working unchanged. Kernel code in the display and remote
  debugging areas changes only for real compiler findings, in small separate
  commits.
- **No blanket warning suppression.** Fix the code, or record why a warning is
  wrong. Upstream port warnings stay upstream.
- **Mirrors only.** New sources (LLVM, compiler-rt) come from internal mirrors
  or caches. Name each upstream URL and commit or file for the owner before
  relying on it.
- **Diagnostics:** output needed only to check something goes to `ktrace` or is
  removed.
- **Measurements:** use existing tools, label nested-VM figures as such, and
  give the revisions and configuration.

## After the milestone

- **Milestone 2: native C++ and system prerequisites.** libc++, libc++abi and
  unwinding as Clang actually needs them. A native Pyxis backend for LLVM's
  Support library (files, process launch, memory, signals), written in the
  way LLVM's Windows backend is, with no POSIX layer in the kernel
  ([ports boundary](../development/ports.md)).
- **Milestone 3: Clang on Pyxis.** Cross-built, and complete when it compiles,
  links and runs a small C program entirely inside Pyxis.
- **Self-hosting,** where Pyxis rebuilds LLVM itself, comes after that, with its
  own build tools and resource needs.

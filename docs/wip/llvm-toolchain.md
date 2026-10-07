# LLVM toolchain on the host

Status: **milestone, agreed 2026-10-07; task 1 probe and decisions 3–5
recorded the same day.** This is the first of the three LLVM
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

Accepted by the owner on 2026-10-07, after the task 1 probe:

3. **Target: a `pyxis` OS in LLVM, in the `pyxis-llvm` fork.** This settles the
   earlier fork question. The [proposal](#proposal) compares it with a
   configuration file on upstream Clang.
   - The owner created `pyxis-llvm` on Forgejo. It holds one branch per
     pinned release, with Pyxis commits on top of the release tag.
   - The first commit adds the Pyxis OS to LLVM's triple and Clang's driver,
     doing what the GCC patch does today.
   - Builds pin a commit, and moving to a new release is a rebase.
   - Reasons for a fork rather than patch files:
     - the later milestones' Support backend is large, and commits are easier
       to review, bisect and rebase;
     - the `pyxis-lwip` repository already works this way.
   - The cost is LLVM's history on the server. Builds fetch shallowly by
     commit, as the port recipes do.
4. **P1F output from LLD.** This was suggested by the owner. It revisits the
   2026-09-29 decision to keep `elf2pxe`.
   - LLD gains `--oformat=p1f` in task 3, and the Pyxis driver uses it for
     executables, as TCC already writes P1F.
   - `elf2pxe` stays in the SDK alongside it as a fallback, including under
     the LLVM setting.
   - `elf2pxe` becomes a candidate for removal once no build uses GCC. It is
     not removed automatically in task 4.
5. **Pin and sources.**
   - LLVM 23.1.3.
   - The owner started the pull mirror `mirrors/llvm-project` and created the
     empty `pyxis-llvm` repository. Task 2 starts the `pyxis-23.1.3` branch at
     `llvmorg-23.1.3` once the mirror has the tag.
   - One build script serves the owner's container and local builds.

   See [pin, sources and container](#pin-sources-and-container).

## Probe results

Measured on 2026-10-07 in Claude's Fedora 44 VM (nested KVM inside the owner's
desktop, 8 vCPUs, `make -j16`), at main `87b9571`, using Fedora's Clang, LLD and
LLVM tools 22.1.8.

- **The probe compiler is not the chosen pin.** Fedora 22.1.8 is only a
  stand-in.
- **A throwaway wrapper stood in for a Pyxis driver.** It was not committed.
  - It compiled with `clang --target=x86_64-unknown-pyxis -D__pyxis__ -mno-red-zone`.
  - It linked with `ld.lld` and archived with `llvm-ar`.
  - It supplied Fedora's Linux compiler-rt builtins in place of libgcc. Those
    builtins are built with the red zone, so the probe image shows only that
    everything builds and boots, not a valid runtime contract.

### Builds and boot

- **`make -j16 image` succeeded with no GCC or binutils program run.** It built
  the kernel, SDK, every userland program and every port.
- **The kernel** compiles with its existing flags and **no warnings**. Those
  flags include:
  - `-mgeneral-regs-only` and `-mcmodel=kernel`;
  - `-mno-red-zone`;
  - `-Wshadow`, `-Wstrict-prototypes` and `-Wmissing-prototypes`.

  Inline assembly compiles, and every `.S` file assembles under the integrated
  assembler, with no diagnostics.
- **The SDK and userland** produce one warning. `userspace/runtime.mk` passes
  the GCC-only `-Wno-maybe-uninitialized`, which Clang does not know.
- **Ports.** Upstream warnings are similar: 402 warning lines with GCC and 373
  with Clang, mostly in Quake and Doom. Three recipes need changes:
  - **fastfetch:** the Pyxis patch's `main` ends without `return`, and the
    recipe builds with `-Werror=return-type`. With `-ffreestanding`, GCC still
    exempts `main` from that warning, but Clang does not.
  - **mbedtls:** CMake adds `-MD` beside the SDK's `-MMD -MP`. Clang reports
    the unused argument, and mbedtls's `-Werror` makes that fatal.
  - **TCC:** libtcc1 defines `__fixxfdi`, `__floatundidf`, `__floatundisf`
    and `__floatundixf`, which compiler-rt builtins also define. TCC's patch
    already removes its libgcc overlaps the same way.
- **Boot:** the Clang/LLD image reached all three started spaces with 1 and 4
  CPUs. This was headless KVM for 25 seconds with no interaction. It is a smoke
  check, not task 2's finish criteria.

### Driver

- **Clang 22 treats `x86_64-unknown-pyxis` as an unknown OS.** It:
  - predefines no Unix macros and no `__pyxis__`;
  - keeps the red zone;
  - links by running `x86_64-unknown-pyxis-gcc`.

  Every x86-64 triple without a known OS does the same in 22, so a
  configuration file alone cannot give a GCC-free link there. This was run.
- **Clang 23 changes this,** from inspecting the 23.1.3 source; it was not run.
  - Clang 23 extends its bare-metal toolchain to x86 triples whose environment
    is `elf`, such as `x86_64-unknown-none-elf`. It links with `ld.lld`
    directly and adds `crt0.o`, compiler-rt and `-lc`.
  - Configuration files can append link-only options with a `$` prefix.
- **GCC driver queries.** Clang has no `-dumpfullversion`; `-dumpversion`
  works. `-dumpmachine` and `-print-file-name=include` behave as in GCC.

### Linker

- **LLD rejects symbols in `PHDRS` `FLAGS()`** (`symbol not found: PF_READ`).
  Both the kernel and userland linker scripts use them.
- **The fix:** numeric flags with comments, which GNU ld also accepts.
- **The layout:** LLD then produces the same three `PT_LOAD` segments with
  the same permissions. Nothing reads `caelum.map`, whose format differs
  under LLD.

### Runtime helpers

- **Current libgcc use.** Final executables use only `__udivti3` from libgcc,
  in httpfs, lua and xfer.
- **Hosted TCC** links the guest SDK's libgcc. Its libtcc1 relies on libgcc's
  `__fixunssfdi`, `__fixunsdfdi` and `__fixunsxfdi`.
- **compiler-rt builtins for x86-64 cover these.** They also have the
  helpers libgcc provides for:
  - x87 `long double` (`xf`) and quad (`tf`) arithmetic;
  - half precision;
  - complex arithmetic.
- **They must be built for Pyxis with `-mno-red-zone`.** Distribution builds
  are Linux builds.
- **Leave out `cpu_model`.** Its CPU-feature constructor would never run,
  because Pyxis startup runs no constructors. `__builtin_cpu_supports` stays
  unsupported.
- **Lua** needs nothing beyond `__udivti3`. The host Lua that runs the
  recipes is unaffected.

### Sizes and build time

Sizes are text plus data at `-O2`, comparing GCC 16.2.0 with binutils 2.47
against Clang and LLD 22.1.8.

| Output | GCC (bytes) | Clang (bytes) | Change |
| --- | ---: | ---: | ---: |
| Kernel | 803,337 | 795,314 | −1.0% |
| All 49 userland and port executables | 9,067,905 | 9,126,165 | +0.6% |
| Doom | 440,728 | 452,723 | +2.7% |
| Quake | 424,271 | 438,795 | +3.4% |
| Lua | 592,375 | 599,789 | +1.3% |
| Guest TCC | 379,511 | 497,603 | +31.1% |

- Most small programs shrink by 2–4%.
- The guest TCC's growth is unexplained so far. Its text grew from 321,751
  to 433,971 bytes; task 3 checks it.

**Clean kernel build** (`make -j16 build/caelum.elf`), three alternating
samples:

| Toolchain | Samples (s) |
| --- | --- |
| GCC | 3.18, 3.05, 4.19 |
| Clang | 1.84, 1.81, 3.72 |

One full image build each took 33.1 s with GCC and 22.7 s with Clang. Both
include downloading the port sources, so they are not a matched measurement.

**Measured later.** These are left for tasks 2 and 3, against the pinned
compiler and Pyxis-built builtins:
- boot time;
- `iobench`, the memory benchmarks and Quake `timedemo`.

The probe compiler and the Linux builtins would make them unrepresentative.

### Sources

- **Release archives.** The download cache serves LLVM release archives at
  `https://repo.internal/repository/raw-github/llvm/llvm-project/releases/download/llvmorg-<version>/llvm-project-<version>.src.tar.xz`.
- **The newest is 23.1.3.** It was the newest the cache served on 2026-10-07;
  23.1.4 returned 404.
- **Its SHA-256 as served:**
  `c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34`. This is
  not yet checked against the release signature.
- **There is no git mirror of `llvm-project` yet.**

## Proposal

### Target approach

Both approaches can build milestone 1 from Clang 23 on.

- **A configuration file on upstream Clang 23**, using the bare-metal
  toolchain with options such as `-D__pyxis__`, `-mno-red-zone`, `-lpyxis` and
  the SDK paths.
  - **Benefits:** no LLVM source change in this milestone, and any LLVM 23
    build works.
  - **Costs:** startup files, library order, multilib and GCC detection are
    whatever the bare-metal driver does in each release. That is a contract
    Pyxis does not own. The driver cannot reject `-m32`, `-shared` or `-pie`
    as the GCC patch does.
  - **For milestone 3:** Clang on Pyxis would run with a triple that names no
    OS. The Support backend can still select itself with `__pyxis__`. But
    native defaults have nowhere to live in the driver: the `boot://sdk`
    sysroot, P1F output and an `a.pxe` default name. The fork would arrive
    then anyway.
- **A `pyxis` OS in LLVM's triple and Clang's driver**, as GCC has today.
  - **The changes:**
    - LLVM's triple gains the Pyxis OS;
    - Clang's target information predefines `__pyxis__` and no other OS
      macros;
    - a Pyxis toolchain class mirrors the GCC patch. It defaults to no red
      zone, accepts only the LP64 ABI, and rejects shared and PIE output. It
      takes `crt0.o`, `pyxis.ld`, libc and libpyxis from the sysroot and
      compiler-rt builtins from Clang's resource directory. It runs `ld.lld`
      directly with `--build-id=none -z max-page-size=0x1000`.
  - **Size:** about 300–400 lines.
  - **Precedent:** Managarm's recent upstream addition, whose driver toolchain
    is 217 lines, and Serenity and Haiku.
  - **For milestone 3:** LLVM's host triple is Pyxis itself. Native driver
    defaults have one place to live, and compiler-rt and later libc++ can be
    configured for the OS by name.

**Chosen: the `pyxis` OS** (decision 3). Pyxis then owns the same contract it
owns in the GCC patch.

### P1F output from LLD

- **The linker option.** LLD's ELF linker gains `--oformat=p1f`, beside its
  existing `binary` output.
  - Addresses are laid out as for ELF. Each loadable segment's bytes then
    follow the header and segment descriptors contiguously.
  - The header comes from the `PT_LOAD` list, and LLD's section writer is
    reused.
  - Checks match `elf2pxe`:
    - segments are readable, page-aligned and ordered, and none is both
      writable and executable;
    - there is no TLS, interpreter or dynamic segment;
    - the entry lies in an executable segment.
  - Estimated at 150–250 lines in `lld/ELF` (configuration, driver and
    writer).
- **The driver.** The Pyxis driver passes `--oformat=p1f` for executables.
  - `clang -o hello.pxe hello.c` writes P1F, and an executable with no `-o`
    is named `a.pxe`, as with TCC.
  - `-Wl,--oformat=elf` still produces an ELF with symbols when one is
    wanted. P1F carries no symbols, and today's userland `.elf` files have no
    documented consumer.
- **The SDK.** Its Make settings link straight to `.pxe` under the LLVM
  setting. `elf2pxe` stays in the SDK as a fallback until no build uses GCC
  ([decision 4](#decisions)).
- **The format's owners.** P1F would be written in three places: the kernel
  loader, the TCC patch and LLD. `include/pxe/p1f.h` stays the authoritative
  definition.
- **Hosting.** This makes hosted Clang write PXE directly. It does not need
  a relocatable format or loader changes.

### Pin, sources and container

- **Pin:** LLVM 23.1.3, tag `llvmorg-23.1.3`.
- **Repositories (decision 5):**
  1. The pull mirror `https://git.internal/mirrors/llvm-project` of
     `https://github.com/llvm/llvm-project`. The owner started it on
     2026-10-07; the history is several gigabytes.
  2. The `pyxis-llvm` repository. The owner created it; task 2 adds the branch
     `pyxis-23.1.3`, starting at `llvmorg-23.1.3`.
- **The build script.** Pyxis gains a build script for LLVM beside the GCC
  one. It:
  - fetches the pinned fork commit shallowly;
  - builds Clang, LLD and the LLVM tools for the X86 target only;
  - builds compiler-rt builtins for `x86_64-unknown-pyxis` with
    `-mno-red-zone` and without `cpu_model`;
  - installs `x86_64-unknown-pyxis-` names for `clang`, `ar`, `nm`, `ranlib`,
    `objcopy` and `ld.lld`.

  The owner's container and local builds use the same script. Ports keep
  selecting tools by prefix.
- **The container.** CI keeps GCC until task 4, so the LLVM container is
  needed only then. Tasks 2 and 3 use a local build.
- **The build setting.** Proposed name: `TOOLCHAIN=llvm`, with `gcc` the
  default until task 4. It reaches the SDK's `pyxis.mk`, `ports/build.lua`
  and `export-sdk.sh`. The SDK then:
  - exports `libclang_rt.builtins.a` and its Apache-2.0 with LLVM exception
    license in place of libgcc;
  - records `-dumpversion`.

### Work found for later tasks

- **Task 2:**
  - numeric `PHDRS` flags in both linker scripts;
  - a GCC-only guard for `-Wno-maybe-uninitialized`;
  - `-dumpversion` in the SDK identity and manifest.
- **Task 3:**
  - fastfetch's missing `return`;
  - the mbedtls `-MD` duplication;
  - the libtcc1 overlap with compiler-rt;
  - the hosted TCC's library name;
  - the TCC size growth.


## Tasks

- [x] **1. Probe and proposal.** Recorded in [probe results](#probe-results)
  and the [proposal](#proposal). The owner accepted decisions 3–5.

- [ ] **2. Kernel and SDK with LLVM.**
  - The kernel and SDK build with the LLVM setting, with no new warnings left
    unexplained, and the image boots in QEMU.
  - Real problems Clang finds in Pyxis code are fixed in their own commits, and
    they stay correct under GCC too.
  - This task also adds:
    - the fork's triple and driver commit;
    - the LLVM build script, including compiler-rt builtins.
  - **Finish when:**
    - one and four CPUs boot to the shell;
    - the power, display and network paths behave as with GCC;
    - the matched measurements are recorded against GCC.

- [ ] **3. Userland and ports with LLVM.**
  - Every userland program and port builds with the LLVM setting.
  - For decision 4:
    - LLD gains `--oformat=p1f`;
    - the Pyxis driver uses it for executables;
    - the SDK's LLVM setting links straight to PXE.
  - **Finish when:** every port's documented check passes in QEMU, including
    Doom, Quake, Lua, TCC compiling a program, Links, BusyBox and Fastfetch.
    Any port that needs a recipe change records it in its README.

- [ ] **4. Switch the default and retire GCC.**
  - The owner builds and publishes the LLVM builder container.
  - LLVM becomes the default, and the GCC and binutils patches and build script
    are removed. `elf2pxe` then becomes a removal candidate; the owner
    decides whether to remove it. The toolchain and SDK docs describe the LLVM contract.
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

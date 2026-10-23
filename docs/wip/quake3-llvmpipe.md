# Quake 3, OpenArena, Mesa llvmpipe and a JIT grant

Status: direction and first-round decisions made by chronium, 2026-10-23. The
track targets [0.0.9](../releases.md#009-direction); its sources are cached and
verified (see [source cache](#source-cache)). Nothing here authorizes code: a
task starts when it is assigned. Direction: run ioquake3 with OpenGL through Mesa's llvmpipe
software rasterizer, and give Pyxis JIT support through an explicit grant. The
design below is a proposal except where marked decided.

## Decided by chronium (2026-10-23)

- **Targets.** ioquake3 (C, SDL2) running Quake 3 Arena and Team Arena, and
  OpenArena as a public package. One engine port serves both.
  - **Quake 3 Arena and Team Arena.** chronium owns both on Steam. Their game
    data is a private package, handled like the other
    [personal-use games](../userland/eduke32.md#personal-use-only): never
    committed, packed into a public repository, an image or CI.
  - **OpenArena.** Free assets, shipped as a public package in the public
    repository, as the shareware [Doom and Quake](../../packages/README.md#game-packages)
    packages are.
- **OpenGL through Mesa llvmpipe.** Pyxis has no GPU acceleration. llvmpipe is
  a multithreaded software rasterizer that generates its shaders with the LLVM
  JIT.
- **JIT through an explicit declared grant, never ambient.** A manifest-declared
  resource, designed as a general runtime-code-generation right rather than for
  llvmpipe. The long-term consumers are .NET, Java, JavaScript runtimes, LuaJIT
  and emulators.

## Decided by chronium, round 1 (2026-10-23)

1. **Shape of the JIT grant.** A separate `jit` resource with one `manage` right.
   A space receives it only with `jit = true` (off by default), and a bundle must
   also declare it. Code regions are explicitly writable or executable, with W^X
   transitions between the states. A dual-view region (a read/write view and a
   read+execute view of one backing) is a later, additive operation on the same
   resource. See [the contract](#the-jit-contract).
2. **How Mesa reaches SDL2.** Pin Mesa 26.2.5 and carry a port-local patch that
   restores the OSMesa frontend from Mesa 25.0.7, the last series that has it.
   The game renders into a buffer SDL already owns, so no window-system platform
   is needed. See [the window-system path](#the-window-system-path).
3. **When the kernel JIT task lands.** After the 0.0.9 milestone for
   [removing BSP-owned allocation and VM mutation](boot-sdk-ports.md#planned-next-kernel-milestone),
   as a user of its VM mutation interface. The Mesa, SDL2 glue and ioquake3
   tasks on softpipe need no JIT and can precede it.

Smaller choices that I would apply unless chronium objects are listed under
[Routine choices](#routine-choices).

## The JIT contract

Decision 1 fixes the resource, its right, the `jit = true` space gate, the
bundle declaration, the writable/executable region states and the later
dual-view operation. The operations, postconditions, budgets and mechanism below
remain a proposal for the kernel design PR.

### What exists today

- **Executable mappings come from one place.** The kernel loader maps a P1F
  segment executable only if the segment says so (`kernel/image.c:101`), and
  both loader and LLD refuse a segment that is writable and executable
  (`kernel/image.c:44-46`; [P1F output](../development/llvm-toolchain.md#p1f-output)).
  The `memory` service returns "readable/writable and non-executable" backing
  (`include/abi/memory.h:60`); nothing a program holds can make a page
  executable.
- **W^X is already a mapping invariant.** The generic VM and the x86-64 page
  code each reject write plus execute (`kernel/mm/vm.c:77-78`,
  `arch/x86_64/paging.c:163-164`), so an ABI that can request both is
  unreachable by construction.
- **No permission change for programs.** "There is no resize, fixed-address
  request, permission change, shared memory or per-region capability object"
  ([memory](../kernel/memory.md), end of the operations section). The kernel has
  a single-page `vm_protect` (`include/kernel/mm/vm.h:135`) used by the loader
  and by kernel mappings; programs cannot reach it.
- **Authority.** `memory` is a kernel object whose only right is `manage`
  (`kernel/object/object.c:321`); `memory_call` checks it per call
  (`kernel/object/memory.c:142`), then hands the work to the BSP
  (`kernel/object/memory.c:217`). A copied or transferred grant manages only
  the receiver's own regions ([memory](../kernel/memory.md#authority-and-lifetime)).
- **Launch.** A bundle manifest names resources and rights
  ([manifest grants](../userland/program-bundles.md#app-views-grants-and-lifetime));
  under the temporary grant-all policy every requested grant the launcher can
  delegate is delivered ([debt](../technical-debt.md#temporary-bundle-grant-policy)).
  Spaces gate services with fields such as `launch = true` and `jobs = true`
  ([space configuration](../development/hosted-tools.md#pool-and-authority),
  [private job contexts](../userland/program-bundles.md#private-job-contexts)).
- **The LLVM seam already exists.** The hosted LLVM fork has a
  `llvm/lib/Support/Pyxis/Memory.inc` (pyxis-llvm `819c2b7`, the hosted pin in
  `toolchain/build-hosted.sh`). It allocates read/write memory through the
  `memory` capability, but `protectMappedMemory` returns not-supported and
  `InvalidateInstructionCache` calls `report_fatal_error("Pyxis does not support
  generated executable memory")`, and a `NearBlock` is rejected. Those three
  functions are where a JIT grant plugs into LLVM.
- **How the consumers use memory.** Checked in their sources:
  - LLVM's `SectionMemoryManager` (used by Mesa's MCJIT path) allocates
    read/write sections with the previous block as a placement hint, then in
    `finalizeMemory` protects code read+execute, read-only data read-only, and
    invalidates the instruction cache
    (`llvm/lib/ExecutionEngine/SectionMemoryManager.cpp:194-196, 239-267`).
  - ioquake3's x86 QVM compiler maps `PROT_WRITE`, copies the code, then
    `mprotect`s it read+execute (`code/qcommon/vm_x86.c`, `VM_CompileX86`,
    under `VM_X86_MMAP`).
  - LuaJIT keeps its machine-code area read+execute and flips it to read/write
    only while assembling, and uses address hints to place the area
    (`src/lj_mcode.c`, `LUAJIT_SECURITY_MCODE`).
  - The brief says V8, .NET and the JVMs can use dual mapping. I did not check
    those runtimes in this round; the design PR for the kernel task should.

All four use the same shape: obtain writable pages, write, make them executable,
later release or rewrite. A transition-based region serves all of them. A
dual-view region is an optimization for runtimes that change code often.

### Proposed contract

A `jit` kernel object, with its own right mask beside `OBJECT_MEMORY`'s
(`kernel/object/object.c:321`). Operations, as request/reply messages in a new
`include/abi/jit.h`, following `include/abi/memory.h`:

- **ALLOCATE** takes a nonzero size and an optional placement hint. It returns
  an opaque identity and a page-aligned range, zero-filled, in the *writable*
  state (read/write, non-executable). The hint is advisory, never a fixed
  address; LLVM's near block and LuaJIT's retry loop both tolerate a different
  answer. Identities are never reused, so a stale identity is refused even if
  the address is reused, as for `memory_reservation`
  ([memory](../kernel/memory.md#operations)).
- **PROTECT** takes an identity, a page-aligned contained range and a target
  state, *writable* or *executable* (read+execute, never writable). The request
  names a state rather than permission bits, so write plus execute cannot be
  expressed. This is the single publication point. On return:
  - no CPU can still write through, or execute through, the old permissions of
    those pages;
  - code written before the call is visible to execution on every CPU;
  - architecture instruction-cache maintenance is done. x86-64 needs none for
    the writing CPU, and the cross-CPU serialization requirement is met by the
    quiescence or shootdown below. Putting the cache step here keeps a future
    architecture from needing a second call. I have not re-read the Intel
    cross-modifying-code rules (SDM volume 3, "Handling Self- and
    Cross-Modifying Code") for this proposal; the kernel design PR must.
- **RELEASE** takes an identity and removes the whole region. Backing and
  address range are reused only after no CPU can hold a translation.
- **Not provided:** executable file mappings, executable `memory` regions,
  permission changes on image segments or on `memory` backing, and any state
  with write plus execute.

**Lifetime.** Regions belong to the process, not the handle, exactly as for
`memory`: closing the service unmaps nothing, and final process teardown
reclaims what is left ([memory](../kernel/memory.md#authority-and-lifetime)).
A copied or transferred `jit` grant manages only the receiver's regions.

**Budgets.** The kernel bounds per-process executable bytes and region count;
values are tuning, not ABI, and exhaustion returns the ordinary limit or
no-memory status before any effect. A transition that cannot quiesce within the
mutation budget returns BUSY before effects, as MEMORY does
([memory](../kernel/memory.md#execution)). A `jit` region counts in the existing memory
profile (the `profile` resource's `memory` right), with separate totals.

**Which right is checked where.**

1. *Manifest.* `jit` becomes a recognized resource class with the right `manage`;
   unknown classes and rights still reject, optional or not.
2. *Space.* The launcher can only delegate what it holds. A space receives a
   `jit` source grant only when its configuration sets `jit = true`, so the
   grant-all policy cannot hand it to an arbitrary bundle; the default is off
   and the Development space is the first candidate to enable it. A new gate is
   needed because `memory` and `jit` are different authorities: holding
   `memory/manage` must not imply code generation.
3. *Call.* The kernel checks `JIT_RIGHT_MANAGE` on every operation, the same
   place and shape as `kernel/object/memory.c:142`. This is the enforcement
   point; steps 1 and 2 decide who can ever hold the handle.
4. *Mapping.* Only the `jit` operations create executable user pages after the
   loader. The VM-level invariant (`kernel/mm/vm.c:77-78`) stays as a second
   line.

### TLB and instruction-cache maintenance on SMP

The contract above is stated in terms of what is true on return, so it does not
depend on the mechanism.

- **Today.** A process mapping change runs on the BSP, closes admission, drains
  active roots and borrowers, mutates, and the process resumes with a CR3
  reload (PCID and global pages are off) ([memory](../kernel/memory.md#execution),
  [threads](../interfaces/threads.md#shared-capability-and-vm-ownership),
  `include/kernel/process_vm.h`). Sibling threads are not running during the
  change, so no stale translation or stale permission survives, and a
  transition would be correct on this path. The cost is that every transition
  stops all of the process's threads and queues behind the BSP, and a
  one-second budget can return BUSY.
- **After the BSP-removal milestone.** The direction is "per-address-space
  mutation on the caller's CPU, per-CPU frame allocation, IPI shootdown for
  shared mappings" ([milestone](boot-sdk-ports.md#planned-next-kernel-milestone)).
  The `jit` operations keep their contract and become: change the page tables on
  the caller's CPU, then shoot down the CPUs currently running the space.
- **Why direction matters.** A page made executable must have no CPU still
  writing through a stale writable translation; a page made writable must have
  none still executing. I propose flushing in both directions and not relying
  on the CPU to refault on a stale restrictive entry. Whether the
  read/write-to-execute direction could skip a flush is a possible later
  optimization, not assumed here.
- **Cost drivers to measure, not assume.** One transition per finalized LLVM
  module (llvmpipe, one per newly seen shader state); two per trace for LuaJIT;
  one per QVM load for ioquake3. The llvmpipe worker threads run rasterization
  in sibling threads, so a drain-based transition stalls rendering. The
  [measurement plan](#measurement) records this per frame.

### Fit with the BSP-removal milestone

The `jit` operations are process VM mutations. They must enter through the same
transaction as MEMORY and DISPLAY (`process_vm_mutation_forward`,
`include/kernel/process_vm.h:56`) and must not add a new BSP-only service with
its own page-table code. The kernel needs a range-level "set page state" helper
under `vm_protect`, whose caller then moves with the rest of the mutations to the
caller's CPU. Dependencies:

- the milestone's proposal should name `jit` as a consumer, so its mutation
  interface covers a permission change with remote invalidation, which MEMORY's
  allocate/release do not exercise today;
- until it lands, the BSP transaction is the only correct implementation, which
  is why the kernel task waits for the milestone (decision 3).

### Dual-view regions (later, additive)

One backing mapped twice, a permanent read/write view that is never executable
and a permanent read+execute view that is never writable. After creation nothing
changes, so a code update needs no page-table change, no shootdown and no
quiescence; only allocate and release mutate. The price is a writable alias of
executable code, a weaker form of W^X than a transition (which has its own
window while another thread can write during the writable state). It needs one
backing owned by two ranges, which the VM's per-range ownership and per-page
wait identity do not provide today ([memory](../kernel/memory.md#address-wait-mapping-identity)).
LLVM's `SectionMemoryManager` cannot use it without a patched memory manager, so
it serves the other runtimes, not llvmpipe. It is additive (a flag on ALLOCATE
or a second operation), so deferring it costs nothing in the ABI.

### Userland surface

- The LLVM fork's `Pyxis/Memory.inc` calls native libpyxis functions directly
  already; its three refusing functions change to `jit` calls
  ([`Memory.inc`](https://git.internal/PyxisOS/pyxis-llvm/src/commit/819c2b770271807f4436dcf76e435c0727c4a1ad/llvm/lib/Support/Pyxis/Memory.inc)).
- libpyxis gains thin `jit_*` wrappers beside `memory_allocate`, returning
  native statuses.
- Libc gains no `mmap` or `mprotect` for this. They are not ISO C, libc has
  neither today (`userspace/libc`), and the one real user so far (LLVM) is
  native. ioquake3's compiled QVM and LuaJIT are patched to the native calls
  when they are ported. Revisit if a second POSIX-shaped consumer makes a
  narrow anonymous `mmap`/`mprotect` subset worth proposing under the
  [libc rules](../userland/libc-portability.md#which-standard-functions-belong-in-libc).

Lands in: pyxis-os (`include/abi/jit.h`, `kernel/object/`, launch and space
configuration, docs), pyxis-userland (libpyxis wrappers, bundle manifest
parsing, space gate), pyxis-llvm (`Pyxis/Memory.inc`).

## The Mesa llvmpipe port

### Version and licence

- **Pin Mesa 26.2.5**, tagged 2026-10-14, the latest 26.2 release. The 26.3
  branch point is 2026-10-15 and has no release yet. Source: the release
  archive `mesa-26.2.5.tar.xz` ([source cache](#source-cache)).
- Mesa's `meson.build` declares `license : 'MIT'`, and the llvmpipe build file is
  MIT. A per-file licence audit of the reached closure belongs to the recipe
  task, as it did for [raylib](../development/raylib.md#source-and-selected-profile).

### Build

- **Meson, cross-built** against the exported SDK from the host. `meson.build`
  requires Meson 1.4 or newer, Python 3 with Mako and PyYAML, and flex and bison
  for the OpenGL frontend. The SDK has a CMake toolchain file but, as far as
  [the SDK reference](../development/sdk.md) shows, no Meson cross file; the
  recipe generates one.
- **Configuration**, as options to confirm in the first Mesa task: Gallium
  drivers `llvmpipe` and `softpipe` only, no Vulkan drivers, no GLX, EGL or GBM,
  no GLES, static libraries, `-Dllvm=enabled -Dshared-llvm=disabled`, and
  `-Dcpp_rtti=false`.
- **RTTI.** `meson.build` (lines 2036-2049) fails the configure if Mesa's RTTI
  setting differs from LLVM's, and its project defaults do not set `cpp_rtti`.
  The hosted LLVM build sets `LLVM_ENABLE_RTTI=OFF` (and `LLVM_ENABLE_EH=OFF`,
  `toolchain/build-hosted.sh`), so the Mesa recipe passes `-Dcpp_rtti=false`.

### LLVM

What llvmpipe asks of LLVM (`meson.build` in 26.2.5, lines 1962-2003):

| Need | Value |
| --- | --- |
| Modules | `bitwriter`, `engine`, `mcdisassembler`, `core`, `executionengine`, `scalaropts`, `transformutils`, `instcombine`, plus `native` for the draw module and `mcjit` |
| JIT | MCJIT by default on x86-64; ORCJIT only with `-Dllvm-orcjit=true` |
| Version | `>= 8.0.0` for MCJIT, `>= 15.0.0` for ORCJIT; the optional `abi` module suggests it tracks LLVM 23 |
| Discovery | `dependency('llvm', method : 'config-tool')` on non-Windows, so an `llvm-config` is required |

Can the hosted LLVM build supply it? The pin is LLVM 23.1.3, and the source is
fine; the build as it stands is not:

- `toolchain/build-hosted.sh` builds the `clang`, `lld` and `clangd` trees and
  stages only tools; it sets `LLVM_BUILD_TOOLS=OFF`, so there is no
  `llvm-config`, and no LLVM libraries or headers are installed for linking.
- Clang and LLD are built with `LLVM_ENABLE_THREADS=OFF`; only the clangd tree
  has threads. With threads off, LLVM's own locking is disabled
  (`llvm_is_multithreaded()` is `LLVM_ENABLE_THREADS`,
  `llvm/include/llvm/Support/Threading.h:52`), which is unsafe for llvmpipe: it
  compiles on application threads while its rasterizer threads run. The Mesa
  libraries must be built with threads on.
- The target list is already X86 only, which is what llvmpipe needs.

So the work is a fourth LLVM output: static libraries and headers for the module
list above, threads on, RTTI and EH off, X86 only, installed beside the SDK as a
development prefix, with a host-runnable `llvm-config` (or an equivalent file
Meson accepts) describing it. It reuses the pinned source and flags and, like
the hosted rebuild, is built by luna and pushed by chronium; the compiler
container is not affected.

### Threads, CPU count and CPU features

- **Threads.** llvmpipe creates its rasterizer threads with Mesa's
  `u_thread_create` (`src/gallium/drivers/llvmpipe/lp_rast.c:1252`), which Mesa
  builds on C11 threads where `thrd_create` is present (`meson.build:1677-1682`).
  Pyxis offers them through its joinable [thread profile](../userland/threads.md). A bundle needs `threads/manage`; the
  per-process native thread bound is 256, enough for a rasterizer pool
  ([threads](../interfaces/threads.md#placement-and-limits)). Siblings inherit
  the space's CPU set and are placed by least load, so llvmpipe's workers do run
  on other CPUs; there is no per-thread affinity.
- **How many threads.** `lp_screen.c:1072-1076` uses the detected CPU count (zero
  threads, meaning single threaded, if it is one) and `LP_NUM_THREADS`
  overrides it. Mesa counts CPUs with `sched_getaffinity` or
  `sysconf(_SC_NPROCESSORS_ONLN)` only when `DETECT_OS_POSIX` is set
  (`src/util/u_cpu_detect.c:782-815`, `src/util/detect_os.h`). Libc has no
  `sysconf` (`userspace/libc`), and Mesa's `detect_os.h` does not know Pyxis.
  Mapping `sysconf(_SC_NPROCESSORS_ONLN)` onto the
  [system information](../interfaces/system-information.md) CPU count is a
  natural libc extension on a native object; until then, set `LP_NUM_THREADS`
  explicitly from the program.
- **AVX is off.** Userspace state is FXSAVE only: the kernel clears `CR4.OSXSAVE`
  (`arch/x86_64/user.c:50-51`). Mesa enables `+avx` only when CPUID, OSXSAVE and
  `xgetbv` agree (`src/util/u_cpu_detect.c:894-896`), so llvmpipe on Pyxis would
  generate SSE code, with four-wide vectors where AVX2 gives eight. Both target
  CPUs report AVX2 in their recorded inventories
  ([ThinkPad](../targets/t14-gen1-amd/thinkpad-inventory-docked.txt),
  [vela](../targets/ideapad-slim5-16iph11/ideapad-inventory.txt)). Enabling
  XSAVE is a separate kernel change to context-switch state and is not part of
  this proposal; the measurement should report llvmpipe's chosen CPU features so
  the cost is visible before anyone decides.
- **Code model.** LLVM's x86-64 JIT defaults to the large code model
  (`llvm/lib/Target/X86/X86TargetMachine.cpp:180-181`), so generated code can
  call libc and Mesa without being placed near them.

### The window-system path

**Decided (chronium, 2026-10-23, decision 2): restore OSMesa.** OSMesa is gone
from current Mesa: `docs/relnotes/25.1.0.rst` lists "Remove osmesa", and 26.2.5's
`meson.build` has no OSMesa option. EGL surfaceless is not an alternative here:
in 26.2.5 EGL requires DRI (or Haiku, Windows, Android), DRI requires
`system_has_kms_drm` (`meson.build` lines 363-370, 560-562), an unknown OS has
neither, and Mesa's EGL loads its driver with `dlopen`.

The port carries `src/gallium/frontends/osmesa/osmesa.c` and its target from
`mesa-25.0.7` as a port-local patch. The frontend renders into a buffer the
caller supplies, which is what SDL's software path already owns. The risk is the
frontend compiling against Mesa 26.2.5's Gallium interfaces; I did not compile
it, and the first Mesa task proves or disproves it. If it does not compile, the
patch must be adapted, or the path goes back to chronium.

With OSMesa the SDL glue is concrete. The SDL2 port currently builds without
OpenGL (`docs/development/sdl2.md`, "OpenGL and Vulkan are not built"). Add a
`SDL_pyxisopengl.c` beside `ports/sdl2/pyxis/SDL_pyxisvideo.c`:

- `SDL_GL_LoadLibrary` binds to the statically linked Mesa (there are no shared
  objects), and `SDL_GL_GetProcAddress` resolves through `OSMesaGetProcAddress`
  (ioquake3 loads all its GL entry points that way,
  `code/sdl/sdl_glimp.c:269`).
- `SDL_GL_CreateContext` creates an OSMesa context for the window's SDL
  surface, `SDL_GL_MakeCurrent` attaches that surface's pixels, and
  `SDL_GL_SwapWindow` runs the existing present path (copy into the held
  display slot, submit). OSMesa's default row order is bottom-up, so the glue
  sets its Y-up storage option or flips on present, and picks a BGRA format to
  match the slot.
- Context attributes (`SDL_GL_SetAttribute`) that OSMesa cannot honour fail
  honestly (multisample, stereo, ES profile, core profile above what llvmpipe
  offers), instead of reporting success.
- Output resolution follows the raylib rule: an application-selected logical
  canvas, aspect-fit scaled into the display content area, because the window is
  always the whole content area and a software rasterizer's cost scales with
  pixels ([raylib canvas](../development/raylib.md#canvas-and-input)).

### Libc and POSIX gaps

Known from Mesa's sources, to be confirmed by compiling:

- `sysconf`/CPU count (above); `sched_getaffinity` and `getpid` (guarded by
  `HAS_SCHED_GETAFFINITY`, so skipped if absent).
- C11 threads exist; `pthread_setaffinity_np` and thread naming are optional
  paths in `u_thread.h`.
- `aligned_alloc` exists in libc (`userspace/libc/malloc.c:125`); Mesa's
  `posix_memalign` path is a configure probe.
- No file-system or window-system dependency in the OSMesa path.

Missing semantics are recorded and refused, never stubbed successfully. Anything
that needs a new behavioural contract stops and returns to chronium.

### Softpipe as the first step

`softpipe` is Mesa's non-JIT software rasterizer. Building Mesa with
`-Dllvm=disabled -Dgallium-drivers=softpipe` proves the Meson cross build, the
OSMesa patch and the SDL glue without LLVM or the JIT grant, and gives
ioquake3 a slow but correct OpenGL for engine bring-up. The softpipe build is a
different configuration from the llvmpipe one, so it validates the front half,
not the JIT path.

## ioquake3 and OpenArena

### Pins and licences

- **ioquake3**: GPL-2.0 (`COPYING.txt`), source at
  `https://github.com/ioquake/ioq3`. The repository lists no tags or releases
  through its API on 2026-10-23, so the pin is a commit: **`87a99d1820`**, main
  as of 2026-10-12 (full hash below). It builds with CMake 3.25 or newer
  (`CMakeLists.txt`); the old Makefile is no longer in the tree. Its bundled
  libraries are versioned in `misc/lib-versions.sh`: SDL 2.32.8, zlib 1.3.1,
  libjpeg 9f, libogg 1.3.6, libvorbis 1.3.7, opus 1.5.2, opusfile 0.12. The
  Pyxis SDL2 port is 2.32.10.
- **OpenArena data**: release **0.8.8** (2012), from the OpenArena download
  server. The archive holds `baseoa/pak0.pk3` through `pak6-*.pk3`,
  `missionpack/mp-pak0.pk3`, documentation, and prebuilt engines for other
  systems (ignored). Debian reviewed the assets and packages them as GPL-2+
  (`openarena-data` 0.8.5split-17 `debian/copyright`), with id Software and many
  individual authors credited. Debian's source packages drop "non-free files"
  from the upstream archives, and its `README.source` says some non-GPL files
  "have been excised from releases but are still in svn": the release archive
  itself is the cleaned form. The package must ship `COPYING` and `CREDITS`, and
  the recipe task confirms the licence of every shipped pk3 against Debian's
  review, not on this note alone.
- **The OpenArena engine** (`https://github.com/OpenArena/engine`, GPL-2.0, active
  through 2026-10-14) is an ioquake3 fork with its own renderer. Not used by
  default (see below).
- **Quake 3 Arena and Team Arena**: chronium's Steam copy. I did not read it. The
  retail `baseq3` and `missionpack` are expected to hold QVM game modules as
  OpenArena's do; the packaging task confirms that against the real files.

### Engine decisions

- **One engine, ioquake3.** The expectation is that ioquake3 runs OpenArena's
  `baseoa` directory as a game directory (`fs_game baseoa`). I have not checked
  that against 0.8.8's protocol and QVMs; the first ioquake3 task does. If it
  fails, the OpenArena engine fork becomes a second recipe with the same Pyxis
  patches, which is why the patches stay small and engine-side.
- **No shared objects.** `USE_RENDERER_DLOPEN=OFF` links one renderer into the
  client; `cmake/client.cmake` adds both renderers' sources "never
  simultaneously populated", so exactly one of `BUILD_RENDERER_GL1` or
  `BUILD_RENDERER_GL2` is on. `BUILD_GAME_LIBRARIES=OFF` and
  `BUILD_GAME_QVMS=ON`: there is no `dlopen`, so game, cgame and UI run as QVM
  bytecode from the pk3s.
- **Renderer.** ioquake3's default is opengl2, which needs GLSL; opengl1 is the
  classic fixed-function path. For a CPU rasterizer, start with **opengl1**
  (OpenArena's site describes the game as using "the OpenGL 1.x graphics API"). opengl2 is a later
  comparison, because llvmpipe compiles every GLSL variant it meets through the
  JIT.
- **QVM execution.** The interpreter first; no JIT is needed. ioquake3's
  `vm_x86.c` compiler then becomes a second consumer of the `jit` grant (it
  already maps writable, then `mprotect`s executable). Its fallback when
  `VM_X86_MMAP` is absent is `malloc`, which is not executable here, so the
  compiled path stays off until the grant exists.
- **Audio and input.** `USE_OPENAL=OFF`, so the engine uses its SDL sound
  (`code/sdl/sdl_snd.c`, `code/client/snd_dma.c`) through the SDL2 port's
  [audio](../development/sdl2.md#audio), which needs `audio` and
  `threads/manage`. Keyboard and relative mouse use the SDL backend; latency
  limits are the existing [SDL audio latency](../technical-debt.md#sdl-audio-latency)
  debt. `USE_HTTP`, `USE_VOIP`, `USE_MUMBLE` and `USE_CODEC_OPUS` off. Vorbis
  stays on for OpenArena's music, from ioquake3's bundled libogg and libvorbis.
  zlib and libjpeg come from the tree or from the existing zlib port, to settle
  in the recipe.
- **System layer.** `code/sys/sys_unix.c` uses `fork`/`execvp` in one helper,
  `signal` for crash handlers and a TTY console; Pyxis has no `fork` or POSIX
  signals ([libc portability](../userland/libc-portability.md#which-standard-functions-belong-in-libc)).
  The port adds its own platform file in place of the Unix one rather than
  stubbing them. Networking: single player uses ioquake3's loopback; the BSD
  socket layer (`code/qcommon/net_ip.c`, controlled by `net_enabled`) is
  disabled for the first tasks, and a networked game is a later, separately
  decided step.
- **Paths.** The pk3s are read-only package data (`pkg://`); configuration and
  saves go to `home://` through SDL's preference path, as in
  [Chocolate Quake](../userland/chocolate-quake.md).

### Packaging

- **Engine**: `ports/ioquake3` in **pyxis-ports**, one recipe, the program bundle
  `ioquake3.pxb` with its manifest (display, keyboard, clock, memory, threads,
  optional pointer and audio; `jit` once the grant exists).
- **OpenArena**: public package `openarena` (`packages/openarena.lua` in
  **pyxis-os**), the 0.8.8 `baseoa` pk3s, `missionpack/mp-pak0.pk3` if it is
  wanted, and the notices. Read as `pkg://openarena`.
- **Quake 3 Arena and Team Arena**: a private package, packed with a private
  recipe and repository as `duke3d` is
  ([EDuke32](../userland/eduke32.md#packing-it)); the engine port is public, the
  data never is. Its recipe is not committed.

## Order of work

Small tasks, each with a first check. Repositories in parentheses.

1. [x] **LLVM development prefix for Mesa** (pyxis-os `toolchain/`; container
   unaffected). Build the library set above with threads on and a usable
   `llvm-config`, on the existing pin. *Check:* a host-built Meson probe finds
   it, and a small C++ program linked on Pyxis builds an LLVM module and emits an
   object for x86-64 without executing generated code.
2. **Mesa with softpipe through OSMesa** (pyxis-ports `ports/mesa`, recipe,
   metadata, notices, patches). Cross build, the OSMesa restoration, a static
   `libOSMesa.a`, CPU count and libc gaps recorded. *Check:* a manual program
   under `ports/mesa/manual` renders a triangle into a buffer on Pyxis in QEMU,
   written out for inspection, with `glGetString(GL_RENDERER)` naming softpipe.
3. **SDL2 OpenGL on OSMesa** (pyxis-ports `ports/sdl2`). Add the OpenGL glue and
   the logical-canvas present. *Check:* a GL gears-style program (the manual
   program pattern of `ports/sdl2/manual`) animates in a window with input,
   focus loss and resize.
4. **ioquake3 on softpipe with OpenArena** (pyxis-ports `ports/ioquake3`, pyxis-os
   `packages/openarena.lua`). Pin and patches, platform file, opengl1, interpreted
   QVM, the public package. *Check:* OpenArena reaches its menu and plays a map
   at a small canvas, with the missing-authority errors from the manifest
   checks, in QEMU. Mouse look, sound and quit are part of the check.
5. **The `jit` grant** (pyxis-os kernel and docs, pyxis-userland, pyxis-llvm).
   Starts after the BSP-removal milestone (decision 3). *Check:* a manual
   program obtains the grant through a
   `jit = true` space, writes a function, transitions it, calls it, and shows
   refusal without the grant, a writable-plus-executable request, a stale
   identity, and use after release; QEMU with several CPUs and a debugger look
   at the sibling-thread case.
6. **Mesa with llvmpipe** (pyxis-ports `ports/mesa`, pyxis-llvm). Switch LLVM on.
   *Check:* the task 2 program reports llvmpipe and renders the same image, with
   `LP_NUM_THREADS=0` first, then with workers.
7. **ioquake3 on llvmpipe** (pyxis-ports). Same game, the real rasterizer.
   *Check:* the task 4 map plays; time demo recorded as in
   [Measurement](#measurement).
8. **Quake 3 Arena and Team Arena** (private recipe outside the repository).
   *Check:* baseq3 and missionpack launch from the private package on a real
   machine; the public repository and images contain no game data.
9. **Optional follow-ups, each separately decided:** ioquake3's compiled QVM on
   the grant, opengl2, AVX/XSAVE in the kernel, a dual-view region, a networked
   game, `sysconf` in libc if not done earlier.

### Task 1: LLVM development prefix

Implemented [producer/interfaces/limits](../development/llvm-development.md)
and [qualification](../development/experiments/llvm-development.md). The
development-only source pin is separate from hosted Clang and the installed
compiler; no container rebuild is needed. Luna builds published output,
chronium publishes it.

Accepted (chronium, 2026-10-23): narrow LLVM-fork portability changes on that
separate pin, explicitly refusing unsupported FD-based ORC remote execution
and shared-library discovery, without fabricated metadata or successful fake
operations. LLVM [#17](https://git.internal/PyxisOS/pyxis-llvm/pulls/17) precedes
parent [#1169](https://git.internal/PyxisOS/pyxis-os/pulls/1169). The same closure
also needs the standard `size_t` include in the execution-engine C header.
Generated code remains data; the JIT task and its BSP-removal gate are unchanged.

### Task 2: softpipe/OSMesa scope (assigned 2026-10-23)

The selected profile is Mesa 26.2.5, static OpenGL and softpipe, no LLVM/JIT,
window-system frontend, disk shader cache or shared objects. Restore the 25.0.7
OSMesa context/render interface and build one `libOSMesa.a`; a manual triangle
program writes an image and reports the renderer. SDL2 glue remains task 3.
The cross configure and compile inventory have run; rendering is not qualified.
No existing Mesa program is available as a baseline. Any shared-libc change
also gets an interleaved existing `iobench` FILE comparison before delivery.

Native integration uses libc's C11 threads and monotonic clock/sleep, without
identifying Pyxis as POSIX. Missing clock authority fails explicitly. Physical
memory/available-worker queries remain unknown rather than substituting global
allocator capacity or CPU counts; softpipe's memory-info capability is omitted.
Unrelated Unix/DRM/shared-file helpers are unavailable. Reached standard libc
work is `open_memstream`, the compiler stack-allocation header and additional
math functions from the existing musl 1.2.5 pin. These are implementation work,
not a new kernel interface; build/link and QEMU qualification remain required.

**Decided (chronium, 2026-10-23):** omit the optional legacy
`OSMesaPostprocess` API. There is no declaration or exported symbol, and
`OSMesaGetProcAddress("OSMesaPostprocess")` returns NULL. No successful no-op.
The selected profile restores the context/render API without its removed
Gallium filter stack. Standard math additions use the existing musl 1.2.5
commit `0784374d561435f7c787a555aeab8ede699ed298`, copied with upstream
formatting/notices and listed in userland's `third_party/musl/UPSTREAM.md`.

## Measurement

AGENTS requires baselines before implementation and matched repeats after.

- **Baselines first.** The software renderers already shipped give reference
  points on the same machines: Chocolate Quake `+timedemo demo1` and
  Doom `-timedemo demo1` ([Chocolate Quake](../userland/chocolate-quake.md),
  [Doom](../userland/doom.md)), and the recorded native Quake figure
  ([LLVM toolchain](../development/llvm-toolchain.md#behaviour-and-performance)).
- **Workload.** A time demo of a shipped demo in each game (OpenArena's and,
  privately, Quake 3's), at fixed logical canvases, with the engine's frame time
  recorded, not only the average: the shader-compile stalls of a JIT show in the
  tail.
- **Matrix.** `LP_NUM_THREADS` (0 and each count up to the CPU count), QEMU
  `-smp` counts, interpreted versus compiled QVM, opengl1 versus opengl2,
  softpipe versus llvmpipe, and with transitions counted per frame once the grant
  exists.
- **Machines.** QEMU, reported with accelerator and host, and kept apart from
  native results. Native: the ThinkPad T14 Gen 1 (Ryzen 5 PRO 4650U, six cores,
  twelve threads) and vela (Core Ultra 7 356H, sixteen logical CPUs in a 4+8+4
  arrangement that Pyxis does not enumerate by type,
  [vela](../targets/ideapad-slim5-16iph11/notes.md#cpu-topology)). State AC or
  battery, and compare on AC.
- **Record** revisions, configuration, commands, repeated samples and ranges
  in the PR and a short experiment record, with the CPU features llvmpipe
  chose. Scaling with workers on vela mixes performance and efficiency cores;
  report it as measured, not as a scaling law.

## Routine choices

Applied unless chronium objects; none changes an architectural contract.

- opengl1 before opengl2; one renderer linked in.
- Interpreted QVM first; compiled QVM only after the grant, as a separate task.
- `USE_OPENAL=OFF`, SDL audio; networking, VoIP, HTTP and Mumble off.
- libpyxis `jit_*` wrappers, not libc `mmap`/`mprotect`.
- `LP_NUM_THREADS` set by the program until libc has `sysconf`.
- A logical render canvas with aspect-fit scaling, as for raylib.
- The OpenArena engine fork only if ioquake3 cannot run the 0.8.8 data.

## Source cache

Sources are fetched only through the internal mirrors. chronium added the entries
(2026-10-23); on the same day I downloaded the three cached archives from
`repo.internal` and their SHA-256 values equal the table, and the ioquake3
mirror has the pinned commit.

| Input | Upstream | Cache | SHA-256 or commit |
| --- | --- | --- | --- |
| Mesa 26.2.5 | `https://archive.mesa3d.org/mesa-26.2.5.tar.xz` | `https://repo.internal/repository/raw-mesa/mesa-26.2.5.tar.xz` | `794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5` |
| Mesa 25.0.7 (source of the OSMesa frontend) | `https://archive.mesa3d.org/mesa-25.0.7.tar.xz` | `https://repo.internal/repository/raw-mesa/mesa-25.0.7.tar.xz` | `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6` |
| OpenArena 0.8.8 data | `https://download.tuxfamily.org/openarena/rel/088/openarena-0.8.8.zip` | `https://repo.internal/repository/raw-tuxfamily/openarena/rel/088/openarena-0.8.8.zip` | `5a8faf7f5b51f351b0a1618c06b6b98a5f1a6758f1d39818de2c87df2a0bac4a` |
| ioquake3 (git) | `https://github.com/ioquake/ioq3.git` | `https://git.internal/mirrors/ioq3` | commit `87a99d18206e878a20289f05ac3c16005dcae5c3` |

The ioquake3 entry is a git mirror like `mirrors/SDL`, pinned by commit. Mesa and
OpenArena are archives checked by SHA-256, as for zlib. No new compiler container
is required for the ports; the LLVM development prefix needs the hosted rebuild
that luna builds and chronium pushes.

### Verifying them

**Mesa signs its tarballs.** Each archive has a detached signature next to it
(`mesa-26.2.5.tar.xz.sig`, `mesa-25.0.7.tar.xz.sig`). Both are made by one key:
RSA 2048, fingerprint `5755 1DE1 5B96 8F63 41C2 48F6 8D8E 31AF C324 28A6`, user
ID Eric Engestrom. On 2026-10-23 both signatures verified as good with `gpg
--verify` against that key.

- **Where to get the key.** Mesa publishes its release maintainers' keys in its
  own repository, `docs/release-maintainers-keys.asc` at the release tag, and its
  release process requires the signer's key to be there
  (`docs/releasing.rst`). The same fingerprint is in Debian's packaging as
  `debian/upstream/signing-key.asc` for `mesa`, and on `keys.openpgp.org`.
- **How to verify.** Import the key from the tagged Mesa repository, check that
  its fingerprint matches the one above, then run `gpg --verify
  mesa-X.Y.Z.tar.xz.sig mesa-X.Y.Z.tar.xz`. gpg will warn that the key is not
  certified, because nothing here signs it; the fingerprint appearing in Mesa's
  repository and in Debian's packaging is the trust evidence, not a web of trust.
- **Checksums.** Mesa's release notes on `main` list SHA-256 and SHA-512 for each
  tarball; both of ours match the table (`docs/relnotes/26.2.5.rst`,
  `docs/relnotes/25.0.7.rst`). The 26.2.5 notes inside the release tag still say
  `TBD.`; the notes on `main` are the ones with values.
- **Recipe.** Pin the SHA-256 in the recipe, as for zlib; the signature is the
  check chronium makes once when adding the cache entry.

**OpenArena publishes an MD5, no signature.** The download directory has
`openarena-0.8.8.md5sum.txt` (`9f353d96d7889c377349d692c3905e5b`), and the
project's download page for the unified zip gives the same MD5. The archive I
downloaded matches it. There is no GPG signature or SHA-2 checksum, and both MD5
values come from the project's own sites, so they detect a damaged download but
not a deliberate substitution. Treat the SHA-256 above as trust on first use,
recorded when chronium adds the cache entry. For an independent check, Debian
lists every file of the 0.8.x pk3s with its CRC-32 in its `openarena-data`
packaging (`debian/*.pk3.contents`); comparing the pk3 contents against those
lists is possible but is not done here.

**ioquake3** has no tags or signatures to verify; the commit hash in the table is
the pin, and a git mirror preserves it.

## Limits and open questions

- Nothing here is compiled. The OSMesa frontend against Mesa 26.2.5, the Meson
  cross build, and OpenArena 0.8.8 under upstream ioquake3 are the claims most
  likely to need revision, and tasks 2 and 4 are where they are settled.
- The kernel design for the `jit` transitions waits for the BSP-removal
  proposal; the contract above is stated in terms of its postconditions so it
  survives that change.
- llvmpipe without AVX is slower than the same machine allows; whether to enable
  XSAVE is a measured follow-up, not assumed.
- Privately, the Steam data is unread, and the retail QVM claim is unchecked.

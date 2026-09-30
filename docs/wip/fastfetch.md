# Fastfetch on Pyxis: bounded port investigation

Status: source audit and SDK object-compilation probe, 2026-09-30. No fastfetch
port, native system-information ABI or guest execution is implemented. The
native information contract below is agreed for the next implementation task.
The remaining port scope and tasks are proposals; this document does not start
their implementation.

## Proposed result

Run upstream fastfetch with the Pyxis ASCII logo, OS/kernel identity, guest CPU
brand and online count, allocator memory, uptime, terminal dimensions and ANSI
colors. Keep upstream formatting and JSON output. This is a substantially
smaller platform port than [Go's runtime](go-runtime.md), but it is not currently
a configure-and-build port.

Start single-threaded, without external graphics/hardware libraries, image
logos, scripting, network modules or process enumeration. Do not fabricate
users, hostnames, package counts, CPU frequency or installed-memory totals.
Missing authority or information must remain unavailable. The selected modules
should work through local and remote terminals, and redirected output must use
stdout without reading terminal input or contaminating machine-readable output.

## Pinned evidence and probe limits

- Pyxis source: `a1957b3` on main; userland:
  `5cbfbbd5189c2159e6059f4281bceb43af5d77e8`.
- [Fastfetch 2.69.0](https://github.com/fastfetch-cli/fastfetch/releases/tag/2.69.0),
  commit `0c3b852bf7bad2837a814c7a31bf332092048a2b`.
- Compiler: `x86_64-unknown-pyxis-gcc` 16.2.0; CMake 3.31.8.
- Used the existing SDK with target-only headers, freestanding x86-64 flags
  and compiler builtin headers. All 76 exported headers were byte-compared
  with the current parent and pinned userland sources, with no differences or
  extra headers. The SDK manifest itself records older parent `14c6798` and a
  modified source tree; this probe does not relabel it as a freshly built SDK.
- No kernel/userland/ports changes, dependency-pin updates, executable link,
  P1F conversion or boot were performed. This is a lower-bound compilation
  inventory, not proof of a complete dependency closure or runtime correctness.

The unmodified CMake build rejects `CMAKE_SYSTEM_NAME=Pyxis` with
`Unsupported platform: Pyxis`. In a disposable clone only, that fatal diagnostic
was replaced with a status message to generate common-source compile commands.
No OS backend was selected and no fake headers, function stubs or Linux target
macros were supplied.

All optional `ENABLE_*` features were disabled, including threading and system
yyjson; bundled yyjson remained selected. All modules except `os`, `kernel`,
`uptime`, `cpu`, `memory`, `terminalsize`, `colors`, `break` and `separator` were
disabled. Upstream still generated **147 translation units**: its module-disable
macros disable registration, not compilation. Four compiled: bundled yyjson,
`FFlist.c`, the disabled Lua wrapper and the `memrchr` fallback. The other 143
failed, predominantly on the same shared-header errors. That count does not
mean 143 independent missing facilities.

CMake try-compiles used static libraries because the native executable link
requires the SDK startup/linker setup. Function-presence probes therefore cannot
prove linkability: `HAVE_MEMRCHR`, `HAVE_PIPE2`, `HAVE_MALLOC_USABLE_SIZE` and
`HAVE_MALLOC_SIZE` were explicitly forced off. Otherwise static archive creation
can falsely claim allocator-size functions exist. No result from those checks
is treated as evidence of libc support.

### Reproduce the compilation inventory

Clone the pinned revision into a disposable directory. Use a toolchain file
with the following settings, substituting the absolute SDK sysroot:

```cmake
set(CMAKE_SYSTEM_NAME Pyxis)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-unknown-pyxis-gcc)
set(CMAKE_AR x86_64-unknown-pyxis-ar)
set(CMAKE_RANLIB x86_64-unknown-pyxis-ranlib)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_SYSROOT /absolute/path/to/build/sdk/sysroot)
execute_process(COMMAND x86_64-unknown-pyxis-gcc -print-file-name=include
  OUTPUT_VARIABLE builtin_headers OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_C_FLAGS_INIT "-nostdinc -isystem ${builtin_headers} -I${CMAKE_SYSROOT}/usr/include -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -march=x86-64")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

First run ordinary CMake configuration to observe the unsupported-platform
error. Then apply only the diagnostic bypass above and configure with:

```text
-DCMAKE_TOOLCHAIN_FILE=<toolchain file>
-DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
-DBUILD_TESTS=OFF -DBUILD_FLASHFETCH=OFF -DSET_TWEAK=OFF
-DBINARY_LINK_TYPE=static
-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE
-DCMAKE_DISABLE_FIND_PACKAGE_Threads=TRUE
-DHAVE_MEMRCHR=0 -DHAVE_PIPE2=0
-DHAVE_MALLOC_USABLE_SIZE=0 -DHAVE_MALLOC_SIZE=0
```

Also pass OFF for every `ENABLE_*` option declared in the pinned CMake file,
and ON for each `FF_MODULE_DISABLE_<UPPERCASE_NAME>` except the nine names above.
Execute each generated `compile_commands.json` command in its recorded working
directory, retaining separate diagnostics; remove any `-flto*` flags to obtain
machine-code objects rather than LTO intermediates. Do not link this deliberately
backend-less source selection or interpret configuration success as a port.

## What the errors mean

| Area | Evidence | Proposed treatment |
| --- | --- | --- |
| Build and source selection | Explicit unsupported-platform gate; module macros leave disabled sources in the build. | Add a real Pyxis platform branch and select a bounded source set. Keep unsupported modules unregistered and exclude their dependencies deliberately. |
| String helpers | Shared `FFstrbuf.h`/`strutil.h` use `strcasecmp`, `strncasecmp` and `strcasestr`. | The first two already exist in libc's `strings.h`; fix direct includes. `strcasestr` is absent and is a candidate reusable libc addition. The resulting `nullptr` comparison errors are cascades, not a C23 compiler gap. |
| Formatting | `FFstrbuf.c` needs `vasprintf` and `round`; neither is currently exported. | Add correct reusable library support if retained by the selected source set, with allocation failure, overflow and FP behavior defined. Existing floating-point printf and allocation are available. |
| Broader source set | Diagnostics include `sscanf`, `sqrt`, `isblank`, `sprintf`, `strftime`, `mktime`, and missing `fnmatch.h`/`net/if.h`. | Several come from unwanted font/display/media/network/calendar helpers. Recheck the real minimal source closure before turning this list into libc tasks. Do not implement whole subsystems just to compile disabled modules. |
| Common initialization | `init.c` includes `locale.h`, then assumes Unix signal handlers and stdio buffering controls. | A native one-shot path must respect existing locale/stdio behavior and avoid unsupported signal machinery. No fake successful `sigaction`/`setlocale`/`setvbuf` implementations. Dynamic display modes remain excluded. |
| Time | Common headers use `clock_gettime` and `nanosleep`. | Use native clock adapters where fastfetch abstracts the platform. Decide failure propagation and units; do not manufacture timestamps when a clock grant is absent. |
| Files, paths and platform state | Shared `io.h` pulls in `dirent.h`; Unix backends assume `stat`, `termios`, `uname`, passwd records, `/proc`, and colon-separated PATH. | Supply native platform/detection adapters and URI-aware path handling. Preserve ordinary libc file reads; do not route them through ad hoc port-only replacements. No synthetic procfs or passwd database. |
| Terminal behavior | Upstream detects stdout's terminal status and can query terminal escape responses. | Detect the actual stdout binding, separately from retained terminal authority. Query native dimensions without consuming keyboard input. A delegated console does not imply redirected stdout is a terminal. |
| JSON | Bundled `yyjson.c` compiled unchanged with these target headers. | Retain its license and validate linkage/output in the actual port. Object compilation alone is not a runtime or full-link result. |

The primary source references are the pinned
[CMake selection](https://github.com/fastfetch-cli/fastfetch/blob/0c3b852bf7bad2837a814c7a31bf332092048a2b/CMakeLists.txt),
[module registry](https://github.com/fastfetch-cli/fastfetch/blob/0c3b852bf7bad2837a814c7a31bf332092048a2b/src/modules/modules.c),
[string implementation](https://github.com/fastfetch-cli/fastfetch/blob/0c3b852bf7bad2837a814c7a31bf332092048a2b/src/common/impl/FFstrbuf.c),
[initialization](https://github.com/fastfetch-cli/fastfetch/blob/0c3b852bf7bad2837a814c7a31bf332092048a2b/src/common/impl/init.c)
and [Unix platform backend](https://github.com/fastfetch-cli/fastfetch/blob/0c3b852bf7bad2837a814c7a31bf332092048a2b/src/common/impl/FFPlatform_unix.c).
Fastfetch's root LICENSE is MIT; bundled yyjson carries its own MIT notice in
its source/header. The eventual recipe must preserve both and inventory any
other bundled sources actually included.

## Agreed native information contract

Expose one reusable, explicitly delegated `system_info` capability with one
READ right. Three tagged synchronous queries return identity, CPU information
and allocator memory through the existing call mechanism. Keep time on the
existing clock capability and dimensions on the relevant console capability.
This contract is agreed but not implemented; it does not add an ambient syscall.

| Field | Existing source and meaning | Agreed first result |
| --- | --- | --- |
| OS/kernel | Launch environment contains `OS_NAME=Pyxis OS`; private `defs.h` names Caelum. No public running-kernel/build query. | Immutable names `Pyxis OS`, `Caelum` and `x86_64`, plus the running kernel's short Git commit SHA, embedded at build time. The SDK manifest or a userspace build revision is not running-kernel identity. No separate release version is needed. |
| CPU | Private CPUID helpers and `arch_cpu_count()`. Successful SMP boot checks AP online acknowledgements before userspace. | Guest-visible brand plus online logical CPU count after successful boot, under today's no-hotplug contract. Do not confuse that count with physical cores or CPUs available to a pinned process. |
| Memory | `pmm_get_stats()` reports total/free/allocated frames. Total excludes permanent reservations and is allocator capacity, not installed RAM. | Coherently sampled total/allocated/free bytes, clearly labeled **Memory (allocator)**. Do not call free frames Linux-style available memory or process RSS. |
| Uptime | Public `clock_now()`, requiring clock READ. | Existing monotonic epoch starts at HPET initialization during boot; earlier boot time is omitted. Do not promise wall-clock elapsed time across VM pauses/suspend. |
| Terminal dimensions | Public `console_size()`, requiring READ or WRITE. | Columns/rows of the application's console, excluding navigation, with existing fixed-size semantics. Remote terminals report their own dimensions; this is not the physical framebuffer size. |

Authority, ownership and error behavior:

- **Authority:** one READ right covers these system-wide identity/CPU/memory
  observations, supplied to trusted init by kernel bootstrap and explicitly
  delegated through init, sessions and launchers to local and remote shells and
  their ordinary children. Restricted launches may omit it. Global memory
  visibility for a holder is an accepted part of this right.
- **CPU identity:** cache the BSP brand during boot, explicitly identifying it
  as the sampled guest CPU. Missing brand information does not invalidate an
  available online logical CPU count. Do not claim a heterogeneous machine
  inventory, physical-host identity, physical-core count or CPU frequency.
- **Memory ownership:** every PMM call is BSP-only with interrupts disabled.
  Sample there through the existing BSP request mechanism; no AP counter reads,
  new allocator locks or memory mutation. Keep the total/free snapshot internally
  coherent, with `total_bytes = allocated_bytes + free_bytes`. Immutable identity
  and CPU replies need no BSP handoff once published. Separate clock/console
  calls do not form a globally atomic snapshot.
- **Replies:** bounded typed records, fixed-size NUL-terminated strings,
  initialized padding and explicit unavailable information. No kernel pointers
  or physical maps. Follow existing call errors for malformed requests, bad
  buffers, handles and insufficient rights; missing authority never produces
  synthetic values. Libpyxis wrappers leave the caller's result unchanged on
  failure. Exact field layouts and names follow existing ABI conventions during
  implementation; do not introduce schema versions merely for a new interface.
- **Presentation:** label allocator accounting in text and explain upstream JSON
  memory-field semantics in the port documentation. No silently substituted
  installed-RAM metric; the agreed first text label is **Memory (allocator)**.

The build identifier names the source commit used to build the running kernel,
not whatever HEAD is present when userspace runs. A proposed follow-up detail,
not yet agreed, is appending `-dirty` for modified tracked kernel/build inputs
without treating documentation or unrelated submodule edits as kernel changes.
Do not silently implement that filtering policy as an accepted requirement.
If build provenance cannot be established, report it as unavailable rather than
inventing a revision.

### Next-task handoff

Implement only this information contract: public ABI and SDK export, kernel
object and BSP memory sampling, libpyxis wrappers, and explicit grant forwarding
through local and remote launch paths. Deliver focused Pyxis and userland PRs,
publishing dependency changes before updating the parent pin. Fastfetch recipes,
libc additions and a permanent diagnostic application remain outside this task.

Validate with an ordinary build and boot, using temporary native calls or
debugger inspection to check identity/build values, CPU reporting, coherent
memory counters, local/remote delegation and omitted or insufficient authority.
No new tests or boot automation. The task remains unchecked until implementation
and validation are complete; these decisions should let another agent begin
without reopening the agreed policy.

For the later port, a packaged minimal default and existing CLI/JSON formatting
remain proposed, with explicit native URI config paths if needed. Automatic XDG
discovery, executable search and cache writes need not be prerequisites.

Relevant invariants are in [SMP](../kernel/smp.md), [memory](../kernel/memory.md),
[clock ABI](../../include/abi/clock.h), [console ABI](../../include/abi/console.h),
[PMM interface](../../include/kernel/mm/pmm.h) and
[terminal sessions](../userland/terminal-sessions.md). The allocation profiler
measures caller events over an interval; it is not a substitute for a current
system-memory snapshot.

## Focused tasks

- [x] **Investigation:** pin upstream, probe against SDK headers, identify native
  information sources and record evidence without claiming a working port.
- [ ] **Expose native system information:** implement the agreed contract and
  next-task handoff above, including the running kernel's short commit SHA.
  Exercise authorized and omitted grants manually before relying on it from
  fastfetch.
- [ ] **Bound the port and fill its reusable libc gaps:** select the actual
  minimal source closure in a temporary port build, then make focused userland
  additions for the standard/library functions it still needs. Record any newly
  exposed contract decisions before implementing them. No fake Unix services or
  changes to how ordinary application file reads work.
- [ ] **Native fastfetch port:** pinned ports recipe and patches, Pyxis platform
  and detector backends, existing ASCII logo, one-shot text and JSON output.
  Exclude unsupported facilities explicitly. Build with the SDK's normal static
  link/startup and ELF-to-P1F conversion; preserve licenses and patch provenance.
- [ ] **Integration and validation:** package it, publish dependency PRs before
  updating pins, and boot normally. Check local and remote terminal output,
  narrow dimensions, stdout redirection/pipelines, JSON, unavailable grants and
  repeated runs. Compare memory/cpu/time observations with native sources and
  report limitations. Close this WIP into a concise implemented port reference.

Pyxis owns ABI/kernel/SDK changes, userland owns libc/wrappers/delegation, and
ports owns fastfetch. See [repository ownership](../development/sdk-and-repositories.md).
No compiler-container rebuild is indicated by this probe. Threads, a compositor,
GPU drivers, a package database and general POSIX process APIs are not proposed
prerequisites. Newly discovered dependencies must be assessed rather than hidden
behind successful stubs or an ever-growing first-port scope.

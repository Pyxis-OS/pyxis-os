# Fastfetch on Pyxis: native port

Status: standalone native recipe implemented and exercised in the guest,
2026-09-30. Default-image packaging and broader integration acceptance remain
the next task; the milestone is not yet complete.

## Agreed first-port scope

Run upstream fastfetch with the Pyxis ASCII logo, OS/kernel identity, guest CPU
brand and online count, allocator memory, uptime, terminal dimensions and ANSI
colors. Keep upstream formatting and JSON output. This is a substantially
smaller platform port than [Go's runtime](go-runtime.md). The standalone recipe
now builds through the normal SDK startup, static link and P1F conversion.

Start single-threaded, without external graphics/hardware libraries, image
logos, scripting, network modules or process enumeration. Do not fabricate
users, hostnames, package counts, CPU frequency or installed-memory totals.
Missing authority or information must remain unavailable. The selected modules
should work through local and remote terminals, and redirected output must use
stdout without reading terminal input or contaminating machine-readable output.

The selected modules are `os`, `kernel`, `cpu`, `memory`, `uptime`, `terminalsize`,
`colors`, `break` and `separator`. Preserve upstream text/JSON formatting and CLI
overrides, with explicit native-URI JSON/JSONC config reads through ordinary
libc file I/O. These configs select module order, keys, formats, colors, spacing
and the ASCII logo; they do not configure the OS or grant authority. A typical
invocation is `fastfetch --config home://fastfetch.jsonc`. Retain bundled yyjson;
Pyxis session Lua configuration stays separate. Automatic config discovery,
cache/config writes, dynamic refresh, image logos, Lua execution and
executable/network helpers are excluded. Retain upstream diagnostics and fallback
behavior rather than introducing a port-wide option validation framework.

Uptime means duration since HPET initialization, as exposed by the existing
monotonic clock. JSON keeps the numeric `uptime` and reports `bootTime: null`.
Duration formatting remains available; calendar placeholders `boot-time`,
`years`, `days-of-year` and `years-fraction`, including their positional forms,
are unset and render empty through the upstream formatter. No boot timestamp or
calendar age is inferred.

## Initial compilation inventory (historical)

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
and ON for each `MODULE_DISABLE_<UPPERCASE_NAME>` except the nine names above.
These CMake options define the corresponding `FF_MODULE_DISABLE_*` C macros.
Execute each generated `compile_commands.json` command in its recorded working
directory, retaining separate diagnostics; remove any `-flto*` flags to obtain
machine-code objects rather than LTO intermediates. Do not link this deliberately
backend-less source selection or interpret configuration success as a port.

## What the initial errors meant

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

## Implemented native information prerequisite

The [system-information reference](../interfaces/system-information.md) describes
the implemented `system_info` READ capability, typed identity/CPU/allocator
queries, SDK/libpyxis interface, explicit local/remote delegation, BSP ownership
and errors. Identity includes the running kernel's short source commit SHA;
CPU reports a cached guest-visible BSP brand and online logical CPU count.
Memory is labeled **Memory (allocator)**, not installed RAM or available memory.
Time and dimensions remain on the existing clock and console capabilities.

The revision identifies the source commit and does not attest to a clean tree.
Appending `-dirty` only for tracked kernel/build inputs remains an unagreed
follow-up proposal. No filtering policy has been implemented. See the accepted
[observation limits](../technical-debt.md#system-information-observation-limits).

The bounded source-closure investigation below supersedes the initial missing-
function inventory. Fastfetch recipes and a permanent diagnostic application
were not part of either prerequisite task.

### Native information validation

An ordinary SDK, ports, userland and kernel image build passed with the existing
GCC 16.2.0 compiler and CMake 3.31.8. Userland is pinned to
`17945c6c7e22580d1308096d599ef7cb3d0be9d9`
([userland PR #88](https://git.internal/PyxisOS/pyxis-userland/pulls/88)).
Temporary native calls used the exported SDK; no diagnostic application, test,
fault injection or boot automation was added to either repository.

Four-CPU interactive validation used nested KVM, QEMU 10.2.2 with the documented
AHCI fix, CPU `max`, 256 MiB, Fedora OVMF, entropy, virtio-net and a private
virtiofsd export. Local Development and remote shell children returned
`Pyxis OS`, `Caelum`, `x86_64`, embedded kernel revision `ba1391996bd8`, online
count 4 and the guest-visible BSP brand. The three allocator observations around
a temporary 1 MiB allocation had a constant total of 201,007,104 bytes:
allocated bytes were 41,422,848, 42,471,424 and 41,422,848 after release, each with
an exactly complementary free count. GDB independently observed that first reply
in the executor on CPU 0 with IF=0 before completion.

A one-CPU boot of `0c741adbd66a` used the same configuration without virtio-net.
It reported online count 1 and the updated embedded revision. Allocator total was
203,821,056 bytes; allocated bytes were 13,688,832 before, 14,737,408 during and
13,688,832 after the 1 MiB allocation. All three replies were coherent. This also
exercised a BSP userspace caller through the same executor path. The rebuild
reused the verified matching SDK/userland/ports bundles; only the kernel revision
and documentation had changed. All validation QEMU, debugger and daemon jobs
were stopped afterward.

All three zero-rights queries returned DENIED and left wrapper outputs unchanged.
A local session launched a child with only stdout and memory authority: no
`system_info` binding was present, every query returned BAD_HANDLE and outputs
remained unchanged. An ordinary child has no launcher authority, so this explicit
omission exercise used a session successor. Malformed payload, short reply,
bad user buffer, wrong operation and wrong protocol returned their documented
errors. Missing CPUID brand and absent Git provenance remain code-inspection
cases; no CPU inventory, installed-memory value or dirty-tree attestation is
claimed. These observations are functional checks in a nested VM, not owner-host
performance measurements.

## Bounded source closure and libc prerequisites

This second probe uses the same upstream pin, Pyxis `a36315bfe06bb5d6dddfa1ca9ffa97731b81c16d`
and published userland `c9ed311f8ab368528b71200a49ea4254b9f46ca8`
([userland PR #89](https://git.internal/PyxisOS/pyxis-userland/pulls/89)). The SDK
was freshly built with that userland revision and GCC 16.2.0. Its manifest
correctly marks the parent modified because of the pending gitlink update.

A disposable clone selected **34 translation units** for the portable core:

- `src/fastfetch.c`; `src/modules/modules.c`; `src/detection/version/version.c`.
- `common/impl/{commandoption,duration,format,frequency,jsonconfig,lua,option,parsing,percent,printing,size,temps,FFlist,FFstrbuf,strutil,memrchr}.c`.
- `options/{general,logo,display}.c`; `logo/{logo,builtin}.c`.
- The nine selected `modules/<name>/<name>.c` frontends.
- Unchanged `3rdparty/yyjson/yyjson.c`.

Paths in the last four bullets are relative to `src/`. The Lua wrapper compiles
with Lua disabled; it adds no interpreter. The existing upstream `memrchr`
fallback is retained rather than adding another libc export. Generated headers
come from the original pinned CMake selection, with all optional features off.

Scratch edits add the missing direct `strings.h` includes and replace Unix
I/O/time header implementations with declarations of Fastfetch's own native
boundary functions. Common Unix initialization and detectors are omitted.
Automatic discovery, generation/writes, repeated refresh/sleep and executable
logo paths are removed or replaced by fatal unsupported branches. The one-shot
normal exit explicitly destroys Fastfetch state instead of requiring `atexit`;
process teardown reclaims memory on fatal paths. The uptime frontend removes
calendar helpers, uses JSON null and retains unavailable named/positional slots
with explicit errors when evaluated. These are dependency-probe edits, not
shipped or runtime-validated port behavior.

All 34 units compile against target-only SDK headers. With LTO removed and
`-ffunction-sections -fdata-sections`, a relocatable link using
`ld -r --gc-sections -u main` follows the reachable core. Comparing its undefined
symbols with the built libc archive leaves only this native integration boundary:

```text
instance
ffInitInstance ffDestroyInstance ffStart ffFinish ffListFeatures
ffDetectOS ffDetectCPU ffDetectMemory ffDetectUptime ffDetectTerminalSize
ffIsTerminal ffTimeGetTick ffPathExists ffPathExpandEnv
ffAppendFDBuffer ffAppendFileBuffer ffWriteFDBuffer
```

These symbols are deliberately unresolved; no successful platform stubs, fake
Unix services or host libc were linked. This establishes the portable core's
library needs, not a complete executable link or the final adapter source list.
The native port must implement the boundary, replace Unix path assumptions,
select its packaged defaults and validate unsupported-option handling. Its
adapters can still expose further concrete dependencies.

The retained code needs `strcasestr`, `vasprintf`, `round` and `isascii`.
Userland adds those and the small paired `asprintf` wrapper:

| Interface | Implemented contract |
| --- | --- |
| `strcasestr` | First matching substring with ASCII-only case folding; empty needle returns the input; bytes outside ASCII are unchanged; no allocation or errno changes. |
| `isascii` | Accepts any int, true exactly for 0 through 127. |
| `asprintf` / `vasprintf` | Owned malloc storage including an empty result; character count excludes NUL; failure returns -1, sets errno and leaves the output NULL. Uses existing format/count limits, preserves the input argument list, and frees an unpublished second-pass failure. |
| `round` | Unmodified musl 1.2.5 implementation from the existing pin; nearest integral double, ties away from zero independently of rounding mode. Preserves signed zero, infinities and quiet NaNs; leaves errno unchanged and may raise FP inexact. Existing no-fenv/signaling-NaN limits remain. |

No `sscanf`, `sprintf`, calendar conversion, directory traversal, locale, signal,
threading or POSIX process facilities were added. The
[stdio contract](../userland/stdio.md#standard-streams-formatting-and-exit)
describes allocating formatting. Source headers and musl's provenance record
carry the other library contracts. There is no separate libm or compiler rebuild.

The port preserves upstream allocation and assertion behavior. In particular,
release builds disable assertions and do not promise graceful allocation failure.
The earlier proposed process-local allocation wrappers and strict formatting
policy were dropped by explicit agreement during port review. Standard libc
error returns remain unchanged.

### Prerequisite validation

The ordinary `make -j16 image` build passed, including the matching SDK, existing
ports, userland and kernel image. The musl `round.c` import matches its pinned
upstream source byte for byte, and archive inspection finds all five exports.

A disposable native observation program linked unchanged bundled yyjson with
the SDK's normal startup/static libraries and converted through ELF-to-P1F.
An interactive four-CPU boot used nested KVM, QEMU 10.2.2 with the documented
AHCI fix, CPU `max`, 256 MiB, Fedora OVMF, entropy, virtio-net and a private
virtiofsd export. Through the remote shell it observed:

- Correct ordinary, empty and 300-character allocating-format results, including
  ownership and NUL termination; two `vasprintf` calls reused one va_list and
  both produced `shared:123`.
- Unsupported `%n` returned -1/EINVAL with NULL output and an untouched count;
  a result exceeding INT_MAX returned -1/EOVERFLOW with NULL output.
- Case-insensitive first match, empty needle, absent match and unchanged high
  bytes; `isascii` on -1, 0, 127 and 128 yielded `0, 1, 1, 0`.
- Halfway rounding away from zero; preserved signed zeros, infinities and a
  quiet-NaN payload; subnormals rounded to signed zero; errno stayed unchanged.
  Both `round(2.5)` and `round(-2.5)` returned 3 and -3 under all four SSE rounding
  modes, restoring the original mode afterward.
- `yyjson_read_file` read `host://config.jsonc` with comments and a trailing comma,
  found the nine selected module entries and serialized them through stdout.
  Malformed JSON and an absent URI returned parser/read errors. Ordinary libc
  file reads were unchanged.

GDB stopped in the actual userspace allocating-format observation, inspected
its owned `shared:123` result at CPL3, and stopped in the imported `round`
implementation called by that program. The guest command completed with status
zero. Allocation exhaustion and second-pass failure unwinding were reviewed in
code, not fault-injected. No tests, self-tests, CI changes, permanent diagnostic
application or boot automation were added. These are functional nested-VM
observations, not owner-host performance results or fastfetch execution.
All validation processes were stopped afterward.

## Implemented standalone native port

[Ports PR #26](https://git.internal/PyxisOS/pyxis-ports/pulls/26) contains the
standalone recipe at `e7a1d92035278fde33c9a6efd3b1935a6d25e9f0`. The
[recipe reference](../../ports/fastfetch/README.md) records build commands,
observations, supported features, licenses and validation. Three ordered patches
separate SDK build integration, native adapters and the owner's ASCII logo.

The port was reconstructed from pinned upstream after review. Shared formatting,
string/list containers, dispatch, diagnostics and yyjson are unchanged. Upstream
initialization is retained with narrow guards for unavailable locale/signals and
buffering controls. Native adapters use existing SDK authority; the kernel and
libc need no further changes. The selected source set links statically and
converts to P1F without unresolved platform symbols or a compiler-container rebuild.

Native query errors use upstream module diagnostics; optional observations use
empty/unset format values and JSON null. Module errors retain upstream exit-status
behavior rather than an aggregate failure policy. There are no allocation
wrappers, assertion replacements or separate format validators. Native timing
failure reports an error instead of inventing a timestamp. The six data modules
run by default; Colors, Break and Separator remain explicitly selectable.

The owner-supplied compass rose at `/shared/pyxis-logo/logo` is embedded byte for
byte. MIT covers upstream and adaptations of existing files, including unchanged
yyjson; original native/build/logo material is MPL-2.0. The recipe stages notices.

The complete reconstructed fetch/apply/build/stage recipe and ordinary Pyxis image
build passed with GCC 16.2.0, CMake 3.31.8 and SDK userland `c9ed311`. Interactive
four-CPU nested KVM used the documented fixed QEMU, 256 MiB, Fedora OVMF,
virtio-net and a private virtio-fs export. The remote shell ran native text/logo,
JSON and explicit JSONC configurations; unknown format fields were empty and
conditionals omitted unknown frequency. Redirected JSON parsed on the host with
no escape bytes and still reported the delegated 100x30 console. A cat pipeline
showed plain upstream ASCII output. These are functional checks, not performance
measurements. Missing-grant paths were reviewed in code; no OOM injection or new
tests were added. Earlier guest results for the superseded patch do not establish
behavior of this reconstruction.

Default build/staging/install selection and broader local/narrow-console acceptance
remain the next task. Complete that task before closing this milestone.

## Focused tasks

- [x] **Investigation:** pin upstream, probe against SDK headers, identify native
  information sources and record evidence without claiming a working port.
- [x] **Expose native system information:** implemented the agreed contract,
  including the running kernel's short commit SHA, SDK/libpyxis wrappers and
  explicit launch forwarding. Authorized, omitted and insufficient grants were
  exercised manually as recorded above.
- [x] **Bound the port and fill its reusable libc gaps:** selected the portable
  core in a temporary build, added its reusable libc functions and recorded the
  agreed config/uptime boundaries and validation above. Native platform functions
  remain deliberately unresolved; ordinary application file reads are unchanged.
- [x] **Native fastfetch port:** pinned recipe/patch with native adapters, the
  owner-supplied ASCII logo and one-shot text/JSON output. Normal SDK static
  link/startup, P1F conversion, native observations and retained licenses
  were validated as recorded above.
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

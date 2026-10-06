# Neovim and libuv requirements for native Pyxis applications

Status: bounded source and SDK investigation, 2026-09-29. This report records findings
and proposes future work; it does not authorize a port. The separate
[block-storage foundation](../devices/block-storage.md) is complete. This report
completes the initial investigation in the [planning
agenda](storage-and-terminal-agenda.md#2-investigate-neovim-and-libuv-requirements). No
kernel, userland or port implementation is included.

## Executive summary

Pyxis already has capability-based program launch, standard streams and bounded native
pipes, explicit-offset file I/O, directory enumeration, atomic file replacement,
separate file/directory synchronization, console byte input and monotonic deadlines.
Those are useful foundations. They do not yet provide a libuv execution model.

The first major gap is waiting for several kinds of activity while keeping input and
output responsive. Neovim 0.12.5's [normal terminal startup][N-main] launches a second
`nvim --embed` process and communicates over pipes; basic editing therefore already
needs child launch and simultaneous terminal, pipe and timer processing. Plugin jobs are
an additional use of that machinery, not its first use. A second major gap is the
single-task process model: luv initializes mutex, once and thread-local-key state during
startup without creating workers. Libuv starts its pool lazily when work is submitted;
worker callbacks and luv thread APIs require shared-address-space threads. This does not
establish that every basic startup needs running worker threads. A third is truthful
filesystem metadata/identity and the native policy behind save protection and
replacement. Terminal rendering/session behavior and the pinned dependency closure are
further substantial work.

A native libuv backend and ordinary Pyxis platform adapters are appropriate. Importing
Linux's fork/epoll/signal implementation wholesale, declaring headers without semantics,
or running blocking calls on the event-loop thread as an asynchronous substitute would
conceal the missing contracts. Neovim is currently a requirements consumer, not a
demonstrated build or runtime port.

## Exact baseline and source pins

The investigation used Pyxis **93031d038743eaf33148b62627e3b556439cd467**, the main
revision when it began. Findings describe this baseline, not later in-progress storage
work. Its gitlinks are:

| Repository | Selected commit |
| --- | --- |
| Pyxis OS / Caelum | `93031d038743eaf33148b62627e3b556439cd467` |
| pyxis-userland | `66b7f117556237b8e307b9bf3428132ed2903363` |
| pyxis-ports | `90678ff95dfefd3952edbef9d9c58611beece4a7` |
| pyxis-lwip | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |

| Upstream source examined | Release/pin | Peeled commit |
| --- | --- | --- |
| [Neovim][N-build] | `v0.12.5` | `5885a30e1e1225349079e7a1c4a3848aa8e43e42` |
| [libuv][U-design] | `v1.52.1` | `1cfa32ff59c076ffb6ed735bbc8c18361558661f` |
| [luv][L-work] | `1.52.1-0` | `f65fe9d7616f9fbcd20227fc7a73b38d1c5c180d` |
| [LuaJIT][J-mcode] | Neovim's exact bundled source | `fbb36bb6bfa88716a47c58bcf9ce9f2ef752abac` |

Neovim and libuv release tags were independently resolved with `git ls-remote`; shallow
detached checkouts were used. These are explicit selected versions, without claiming
that every selected release is the latest. Crucially, **libuv 1.52.1 is the exact
version in this Neovim release's dependency manifest**, not an arbitrary separately
selected API version.

### Dependency inventory from the selected Neovim source

[cmake.deps/deps.txt][N-deps] fixes download URLs and SHA-256 values; [src/nvim/CMakeLists.txt][N-cmake] identifies the linked dependencies and supported options. The [dependency build defaults][N-deps-config] distinguish source pins from bundling policy. The table records source pins, not a promise that every dependency is bundled by default. Bundled PUC Lua defaults off; gettext/libiconv bundling defaults on only for MSVC. Dependency availability must be established for the selected target rather than inferred from distro packages or Pyxis's existing Lua.

| Target dependency | Manifest source pin | Scope and observations |
| --- | --- | --- |
| libuv | `1.52.1` | Core event, stream, process, filesystem, timer and synchronization APIs. |
| luv | `1.52.1-0` | The `vim.uv` Lua binding; Neovim's minimum CMake version check is 1.43.0, while its bundled pin is newer. |
| LuaJIT | commit `fbb36bb6bfa88716a47c58bcf9ce9f2ef752abac`, 2.1 source line | Default Lua 5.1-compatible runtime. Its memory allocator and executable-code placement/protection need native support if selected. |
| PUC Lua | **5.1.5**, supported alternative | `PREFER_LUA=ON` requires the Lua 5.1 API with `find_package(Lua 5.1 EXACT REQUIRED)`; 5.1.5 is the bundled source pin. This upstream-supported choice can defer JIT executable-memory requirements. It does not make Lua 5.5 ABI-compatible. |
| LPeg | `1.1.0` | Core Lua pattern/generator dependency. |
| lua-compat-5.3 | `v0.13` | BuildLuv downloads it for compatibility support. |
| tree-sitter library | `v0.26.13` | Required linked library; CMake minimum is 0.25.0. Parser loading remains a separate runtime feature. |
| Bundled tree-sitter parsers | C `v0.24.1`; Lua `v0.5.0`; Vim `v0.8.1`; Vimdoc `v4.1.0`; Query `v0.8.0`; Markdown `v0.5.3` | Built as **MODULE** libraries and loaded with `uv_dlopen`/`uv_dlsym`; packaging parser source alone cannot make them usable with Pyxis's static PXE loader. |
| utf8proc | `v2.11.3` | Unicode classification/width work inside the editor. It does not add Unicode decoding, shaping, fonts or cell behavior to the terminal renderer. |
| unibilium | `v2.1.2` | Default enabled terminfo dependency. `ENABLE_UNIBILIUM=OFF` is a real upstream option with built-in fallback terminfo; the terminal still must support the capabilities it advertises. |
| libiconv | `1.17` | Required Iconv API/header and implementation, supplied by libc or a separate library. [Linking a separate library][N-iconv-find] is conditional; the [bundled libiconv build][N-iconv-build] supports MSVC only. A Pyxis build needs its own real implementation/recipe; conversion scope remains a decision. |
| gettext/libintl | `0.20.1` | Default enabled; `ENABLE_LIBINTL=OFF` is an upstream-supported way to defer translated messages. |
| Wasmtime | `v36.0.14` | Optional tree-sitter WASM runtime; **disabled by default**, deferrable. |
| In-tree sources | xdiff, mpack, cjson, klib; `src/nvim/vterm`; `src/nvim/tui/termkey` | Included in the Neovim source pin. They are not additional external libvterm/msgpack/libtermkey package requirements for this selected build. |

Pyxis's [Lua
recipe](https://git.internal/PyxisOS/pyxis-ports/src/commit/90678ff95dfefd3952edbef9d9c58611beece4a7/lua/metadata.lua#L1)
pins Lua **5.5.1**, commit `7579fc9d7ed90240487251dfb69168f8e64e9294`. Its [development
export](https://git.internal/PyxisOS/pyxis-ports/src/commit/90678ff95dfefd3952edbef9d9c58611beece4a7/lua/README.md#L14)
contains selected libraries and omits `luaL_openlibs`, package/io/os/debug and the full
math library. Neovim calls `luaL_openlibs` and uses package loading
([executor.c][N-lua], lines 687, 891, 937). Reuse of the current archive therefore fails
both version and library-closure requirements.

Host tools and target libraries are separate: upstream uses CMake >=3.16, a host build
tool such as Ninja/Make, a host Lua interpreter/code generators, and host loadable
`nlua0` support. The selected source has an `NLUA0_HOST_PRG` cross-compilation path
([CMakeLists][N-cmake], line 533); documentation targets also invoke built Neovim. A
future recipe must deliberately keep these host generators native, produce static target
dependencies against the exported SDK, and convert the final target image through
Pyxis's normal PXE pipeline. No host libc linkage or full compiler rebuild is implied by
this investigation.

## Feature scopes and concrete gap map

**Basic terminal open/edit/save** means the normal TUI plus its editor server, a
readable/writable native file, runtime configuration, timers and responsive pipe/input
handling. Full default backup/swap behavior adds metadata, identity, exclusive creation
and ownership/recovery decisions. Choosing documented options such as `-n` can
explicitly defer swap recovery for a first slice, but should not be represented as
implementing it. Pure Lua/Vimscript configuration is a separate, smaller plugin scope
than arbitrary native modules and subprocess plugins.

**Jobs/subprocesses** add several concurrent input/output/error streams, asynchronous
child completion, cancellation/termination policy and environment/cwd/authority
delegation. **Embedded terminals** additionally need terminal sessions with duplex
input/output, resize and session-control behavior, as well as Neovim's in-tree terminal
emulator. **Language servers** normally reuse spawned stdio jobs and message framing;
network LSP is an optional transport. Each server executable is another port with its
own dependencies. Working Neovim's LSP client would not demonstrate that clangd or
another server can run.

In this table, **adapter** means behavior can substantially be built on existing native
operations; **contract** means the OS or native runtime has a missing observable
behavior. Rows containing both should not be reduced to merely installing a POSIX
header.

| Requirement and scope | Concrete upstream evidence | Current native behavior | Gap, boundary and proposed direction |
| --- | --- | --- | --- |
| Event-loop multiplexing: basic TUI, RPC, timers; all advanced scopes | [loop.c][N-loop] uses `uv_run`, timers and `uv_async_send`; [rstream.c][N-read] starts/stops `uv_read_start`; [libuv design][U-design] describes one loop owning callbacks | [Console](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/console.h#L26) can poll or wait with a deadline; [pipes](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/pipe.h#L55) block when empty/full; process WAIT and endpoint RECEIVE block individually | **Contract:** atomically observe/wait for several owned resources and a monotonic deadline, without lost wakeups. Pair readiness with genuinely nonblocking try-read/try-write, or select a submitted-operation/completion contract. Polling one console and then blocking on a pipe freezes the loop. No epoll syscall is prescribed. |
| Cross-thread loop wake and operation completion, cancellation and lifetime | [libuv async][U-async] coalesces wake calls; [requests][U-request] cancel only eligible pending request types and still deliver a completion callback; [stream docs][U-stream] keep write buffers valid until callback; [wstream.c][N-write] owns queued write buffers | [SEND](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/docs/endpoints.md#L58) confirms bounded queue admission only; CALL blocks the submitting task; deadlines report delivery separately | **Contract + adapter:** reliable wake/completion observation, stable operation tokens, exactly one terminal outcome, queue/backpressure limits, short-progress reporting and close/cancel races. Define whether input/output is copied on submission or borrowed until terminal completion; never reclaim memory merely because cancel was requested. Existing SEND/RECEIVE is not itself async file/pipe I/O. |
| Per-process threads and synchronization: real luv/libuv backend, work APIs | [luv startup/work][L-work] creates a mutex and TLS key even during initialization, stores VM state in TLS; work submission uses `uv_queue_work`; [luv threads][L-thread] uses `uv_thread_create[_ex]`; [libuv pool][U-pool-code] uses mutex/condition/semaphore/once and lazily creates four workers by default on first work submission | [process.h](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/kernel/process.h#L13) owns one user task/address space/handle table; SMP runs different processes/tasks, not multiple user threads in one process | **Contract:** create/join/exit for shared-address-space tasks, synchronization with wake/park ordering, thread-local storage and process-wide lifetime/fault policy. Arbitrary worker callbacks share pointers and execute application code, so separate worker processes cannot transparently replace threads. Affinity, priority and platform-specific thread naming can be deferred. |
| Shared VM/handle safety, libc thread safety and TLS | [libuv thread types][U-unix] and [luv work][L-work] require independent per-thread state; worker callbacks invoke allocation and file operations | [memory loan](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/docs/memory.md#L55) transfers exclusive inactive-address-space ownership to BSP; [capability growth](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/docs/smp.md#L80) lends the caller table. [errno](https://git.internal/PyxisOS/pyxis-userland/src/commit/66b7f117556237b8e307b9bf3428132ed2903363/libc/include/errno.h#L4), allocator, descriptor table and timezone cache assume one thread. [elf2pxe](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/tools/elf2pxe.c#L121) rejects PT_TLS | **Contract + runtime:** redesign loans/reclamation for sibling tasks, safe shared table mutation, stack ownership and last-thread/process exit; synchronize malloc/stdio/descriptors/environment/caches and supply real per-thread errno/TLS. Initially pinning a process's threads to one CPU still requires coordinating siblings during BSP loans. Compiler TLS vs runtime-key access is a decision, not proof that a compiler container rebuild is already required. |
| Paths, current directory, runtime/config/cache and temporary files: basic startup | [os/fs.c][N-fs] uses uv_chdir/uv_cwd/realpath; [path.c][N-path] has Unix absolute-path rules; [stdpaths.c][N-stdpaths] defaults to HOME/XDG and /etc,/usr paths | [path.h](https://git.internal/PyxisOS/pyxis-userland/src/commit/66b7f117556237b8e307b9bf3428132ed2903363/include/path.h#L61) resolves explicit scheme roots or a retained cwd chain, rejects leading / and bounds .. by capabilities. Current libc opens borrow the immutable startup cwd | **Userland adapter + policy:** one coherent mutable cwd/environment context used by libuv, libc and Lua; runtime/config/cache/temp locations under explicitly delegated roots; Pyxis scheme-aware path handling. No kernel global pathname namespace is required. Merely setting XDG variables does not resolve all path-normalization/realpath assumptions, and a descriptive path is neither authority nor file identity. Specify process-wide vs thread-local cwd behavior. |
| Monotonic time, wall time and timers: basic editing | [loop.c][N-loop] uses uv_timer callbacks; [os/time.c][N-time] uses uv_hrtime; Neovim evaluates formatted wall time | [clock.h](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/clock.h#L6) provides monotonic NOW, SLEEP_UNTIL and UTC wall readings; libc already has time/localtime but lacks strftime | **Mostly adapter/library:** map monotonic units and wall-time limits honestly, manage uv timers in userspace and pass the next deadline to composite wait. Implement needed formatting/calendar library functions without inventing a new clock subsystem. Blocking SLEEP cannot substitute for processing timers while streams are active. |
| File bytes and descriptor flags: basic read/save, swap | [os/fs.c][N-fs] wraps opens and directly uses read/write/readv/dup/fcntl; [memfile.c][N-memfile] opens swap with `O_RDWR\|O_CREAT\|O_EXCL`; [bufwrite.c][N-bufwrite] uses exclusive/no-follow backup creation | [FILE](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/file.h#L7) has READ/WRITE/RESIZE, explicit offsets and short-transfer semantics. Directory CREATE is exclusive. [public open](https://git.internal/PyxisOS/pyxis-userland/src/commit/66b7f117556237b8e307b9bf3428132ed2903363/libc/include/fcntl.h#L6) exposes only read-only/write-only/create/truncate, mode 0666; public unistd is read/write/close | **Mostly adapter:** read-write descriptor opening, seek/pread-style use of explicit offsets, exclusive-create without lookup/create races, vector loops, descriptor duplication and synchronized shared open-state where required. Define duplication/offset/inheritance behavior rather than inventing successful fcntl operations. Nonblocking stream semantics are the event-contract gap above. Atomic append, symbolic-link handling and actual mode changes are separate native policies. |
| Metadata, identity and modification detection: basic default file handling and Lua module caches | [os/fs.c][N-fs] lines 758 and 1232 call stat/fstat and compare `st_dev/st_ino`; [fileio.c][N-fileio] stores nanosecond mtime/size; [loader.lua][N-loader] uses `uv.fs_stat` | Public FILE reports size; directory enumeration reports name/type. [Identity debt](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/docs/technical-debt.md#L314) explicitly says two handles cannot be compared as the same object | **Contract:** native type/size and comparable object identity with scope/lifetime across RAM, host, future persistent storage and replacement; an honest modification indication/timestamp for caches and overwrite checks. A normalized path or fabricated inode cannot establish identity. Mapping results to `uv_stat_t` is adapter work only after genuine fields and unavailable-field behavior exist. |
| Save replacement, durability and access/ownership policy | [bufwrite.c][N-bufwrite] preserves owner/group/mode, makes backups and calls `os_fsync`; [memfile.c][N-memfile] syncs swap | [directory rename](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/directory.h#L61) already supports atomic file replacement and retained old handles. [file sync](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/file.h#L62) and [directory sync](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/directory.h#L81) are distinct; RAM success means no persistence, host durability depends on its service | **Existing mechanism + policy + adapter:** use native replacement/sync and preserve unknown-outcome errors. Settle native ownership/access attributes before mapping Unix chmod/chown/umask assumptions; no fake uid/gid or successful chmod. Strong crash-safe save/recovery additionally depends on the storage durability contract. Neovim's existing file fsync path is not by itself proof of parent-directory crash durability. |
| Locking, swap ownership and filesystem notifications | [memfile.c][N-memfile] uses exclusive swap creation; [memline.c][N-memline] stores creator PID and checks `os_proc_running`; [fileio.c][N-fileio] has optional `HAVE_DIRFD_AND_FLOCK` tempdir locking; [watch.lua][N-watch] uses `uv.new_fs_event` | Exclusive native CREATE and directory generation invalidation exist; no public file leases/locks, process-ID/liveness query, change subscription or reliable host notification | **Policy/contract, partly deferrable:** determine edit/lease/recovery behavior and process-incarnation visibility without turning a PID from untrusted file bytes into control authority. General Unix flock is not a basic-editor prerequisite merely because an optional tempdir feature uses it. Poll-based change detection still needs truthful metadata and event timers; watches can follow later. |
| Spawn, stdio transport and child completion: already needed by basic TUI | [main.c][N-main] line 354 calls [ui_client_start_server][N-ui]; it starts `--embed` via channel jobs. [libuv_proc.c][N-proc] creates pipes, delegates child streams, cwd/env and calls `uv_spawn` | [launcher](https://git.internal/PyxisOS/pyxis-userland/src/commit/66b7f117556237b8e307b9bf3428132ed2903363/include/launcher.h#L12) launches PXE with explicit grants/startup metadata. Pipes have EOF/EPIPE and retained-copy lifetime; [PROCESS_WAIT](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/process.h#L14) is immutable/repeatable after teardown | **Adapter + event extension:** build uv_spawn on native launch, not fork/exec. Provide asynchronous process completion and both pipe directions. Standard streams only cover slots 0–2: Neovim's redirected-stdin path may pass an additional fd 3, which requires a generic explicit extra-stream/resource delegation mapping. The normal shell delegates launcher/pipe creation to sessions, not ordinary commands; editor/subprocess authority must be intentionally granted and attenuated. |
| Stopping jobs and lifetime after parent exit | [proc.c][N-proc-control] escalates termination and kills process trees; [system.lua][N-system] supports timeouts/kill | Process control only authorizes WAIT; closing the handle does not stop the child | **Contract/policy:** cooperative stop vs forced termination, which child/descendant group is controlled, authority, cancellation of blocked requests and reclamation. Use held capabilities or explicitly authorized groups, not unrestricted numeric-PID killing. A timeout diagnostic alone does not stop a native child. |
| Terminal input/output, session state, resize and lifecycle: basic TUI | [stream.c][N-stream] calls tty raw/normal modes; [tui.c][N-tui] sets terminal state, uses windowsize and resize notifications, alternate screen, scrolling and output buffering | Console READ is already byte-oriented/no-echo; SIZE works, but [console ABI](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/console.h#L83) has no resize notification. [terminal.md](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/docs/terminal.md#L64) defines a small VT subset and shared parser/state, requiring exclusive use | **Session contract + renderer/adapter:** explicit input/output ownership, mode/state restoration, attachment/closure/focus/resize events and truthful terminal capabilities. Existing raw bytes need no invented canonical Unix line discipline. Neovim uses its own TUI/terminfo/input parser; PDCurses is not a dependency or compatibility proof. Current TTY lacks alternate-screen and scroll-region behavior expected by richer terminal descriptions. |
| Unicode and rendering: non-ASCII basic editing | [utf8proc pin][N-deps] and Neovim's TUI produce UTF-8 cells; in-tree vterm interprets embedded terminal streams | [tty.c](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/kernel/fb/tty.c#L32) indexes glyphs by unsigned byte and advances one cell per printable byte | **Terminal renderer gap:** UTF-8 decoding/input, width agreement, combining/wide cells and font coverage. Most of this can belong to the terminal renderer/session implementation, not to a POSIX libc layer or libuv. Grapheme/emoji/shaping completeness is deferrable after a stated Unicode subset; byte drawing cannot be claimed as UTF-8 support. |
| Embedded terminals (`:terminal`, PTY jobs) | [pty_proc_unix.c][N-pty] calls forkpty, termios, sessions, nonblocking master fd, resize; Neovim's in-tree vterm renders the child output | Ordinary native pipes and application spaces exist; no virtual duplex terminal-session endpoints or PTY session-control contract | **Deferrable native contract + platform adapter:** terminal-session creation, child attachment, raw/cooked responsibilities, resize, hangup/exit/control, output/input flow control. Implement a Pyxis PTY adapter over those semantics; native launch can replace forkpty's launch purpose without requiring fork or Unix session IDs. |
| Language servers, external providers and networking | [LSP rpc.lua][N-lsp] starts stdio transports; [system.lua][N-system] uses uv_spawn; [socket.c][N-socket] offers TCP/local named stream services | Native outgoing IPv4 TCP, UDP and DNS-related userspace code exist, mostly synchronous; no generic libuv socket layer/listen API or named byte-stream socket service | **Optional adapter/contracts:** stdio LSP needs jobs/composite I/O before networking. Remote RPC/TCP LSP needs native asynchronous networking plus listen/accept if hosting services. Namespace endpoints provide discovery/authority transfer, not arbitrary uv_pipe byte-stream semantics. Neovim's autogenerated local listener may fail non-fatally ([server.c][N-server], line 65), so it need not define the first editor scope. |
| Additional libc/library closure: normal Lua and editor functions | [executor.c][N-lua] opens standard Lua libraries; [time.c][N-time] calls strftime; eval/funcs.c contains additional numeric/time functions | [math.h](https://git.internal/PyxisOS/pyxis-userland/src/commit/66b7f117556237b8e307b9bf3428132ed2903363/libc/include/math.h#L32) is a small subset; [time.h](https://git.internal/PyxisOS/pyxis-userland/src/commit/66b7f117556237b8e307b9bf3428132ed2903363/libc/include/time.h#L45) excludes strftime/mktime; stdio lacks fileno/fdopen/scanning/pushback and locale.h/iconv.h are absent | **Library/adapter:** grow the required real math, time, locale/encoding and stdio closure with supported semantics. These are not all kernel gaps. The header probe did not establish every unresolved function in Lua/Neovim or an exhaustive target link closure. |
| Dynamically loaded parsers/C plugins; JIT/FFI | [treesitter.c][N-ts] uses uv_dlopen/uv_dlsym; [parser CMake][N-parser-cmake] produces MODULE objects; [LuaJIT mcode][J-mcode] manages writable then executable pages | Current PXE tool rejects dynamic linking and TLS; [MEMORY](https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/include/abi/memory.h#L29) supplies eager non-executable RW pages only | **Deferrable contract/tooling:** relocatable code-module loading, symbol resolution, permissions, authority/lifetime/unload; executable memory transitions if LuaJIT selected. Lua 5.1 interpreter plus legacy syntax and pure Lua plugins can defer these features honestly. Wasmtime is optional, not a replacement established by this probe. |

Several Neovim filesystem wrappers deliberately pass **NULL callbacks**, so those
selected APIs are synchronous by their upstream contract ([os/fs.c][N-fs], [libuv POST
dispatch][U-fs-code], line 139). This is distinct from falsely implementing
callback-based fs APIs with blocking event-loop calls. A native completion backend could
avoid workers for some I/O; arbitrary `uv_queue_work` callbacks and luv threads still
require real shared-address-space execution. Startup synchronization/TLS initialization
is a separate requirement from actually starting the worker pool.

## SDK provenance and small probes actually run

The existing SDK is
`https://git.internal/PyxisOS/pyxis-os/src/commit/93031d038743eaf33148b62627e3b556439cd467/build/sdk`
with sysroot under `sdk/sysroot`. Its manifest reports Pyxis
`9f40fd2e13dd91484631389336255922beea7412`, **source_state=modified**, userland
`66b7f117556237b8e307b9bf3428132ed2903363` clean, target `x86_64-unknown-pyxis`, GCC
**16.2.0**, Binutils **2.47.20260726**. The existing integrated ports manifest reports
ports `90678ff95dfefd3952edbef9d9c58611beece4a7` clean. Thus this is not an exact
committed-main binary provenance claim.

All **68 examined ABI, libpyxis/libterm and libc header exports** were compared with the
selected source pins; **68/68 were byte-identical**. Since these probes only inspect
headers, this establishes the inputs used while leaving archive/runtime provenance
unverified. This is not evidence that the SDK binaries match the selected main revision.

The original compiler was the existing `x86_64-unknown-pyxis-gcc` listed above. The
following reproduces the header-only probes with paths supplied for an SDK, a checkout
of the pinned libuv source and a scratch directory. Verify header provenance again
before using a different SDK. Only compiler-provided and exported SDK headers are
visible; these snippets are documentation, not a new test harness.

`probe-native.c`:

```c
#include <file.h>
#include <directory.h>
#include <pipe.h>
#include <console.h>
#include <clock.h>
#include <process.h>
#include <launcher.h>
```

`probe-uv-header.c`:

```c
#include <uv.h>
```

```sh
PYXIS_CC=x86_64-unknown-pyxis-gcc
PYXIS_SDK=/path/to/build/sdk
LIBUV_SOURCE=/path/to/pinned/libuv
PROBE_DIR=/path/to/scratch
"$PYXIS_CC" -std=gnu23 -ffreestanding --sysroot="$PYXIS_SDK/sysroot" -nostdinc \
  -isystem "$("$PYXIS_CC" -print-file-name=include)" \
  -I"$PYXIS_SDK/sysroot/usr/include" -fsyntax-only "$PROBE_DIR/probe-native.c"
"$PYXIS_CC" -std=gnu23 -ffreestanding --sysroot="$PYXIS_SDK/sysroot" -nostdinc \
  -isystem "$("$PYXIS_CC" -print-file-name=include)" \
  -I"$PYXIS_SDK/sysroot/usr/include" -I"$LIBUV_SOURCE/include" \
  -fsyntax-only "$PROBE_DIR/probe-uv-header.c"
```

| Probe | Observed result | Meaning and limit |
| --- | --- | --- |
| Include native file/directory/pipe/console/clock/process/launcher headers | Exit 0 | Header compilation only; no link or runtime claim. |
| Include pinned upstream `uv.h`, with default platform selection | Exit 1: `uv/unix.h:26: fatal error: sys/stat.h: No such file or directory` | The upstream Unix public header does not compile against this SDK. This is a native-backend/header-shape task, not evidence that Pyxis should implement every Unix include transitively used by that backend. |
| Header file inspection | pthread.h, sys/stat.h, dirent.h, termios.h, poll.h, dlfcn.h, sys/socket.h, sys/mman.h, locale.h and iconv.h absent | Demonstrated SDK gaps; no artificial declarations were installed. Individual later compiler errors were not chased through replacement headers. |

The observed failure was `uv/unix.h:26:10: fatal error: sys/stat.h: No such file or
directory`. No replacement headers were supplied to continue past it. The pinned sources
and snippets above preserve the probe inputs without depending on temporary
investigation files.

No Neovim/libuv configure, full build, target link, QEMU boot or runtime behavior probe
was run. No test suite, new test infrastructure, OS rebuild, package installation,
toolchain rebuild, port patch, CI change or fake operation was used. The gap map is
source inspection plus explicitly identified design inference; it is not measured
performance or proof of a completed port.

## Proposed bounded native milestones

These are ordered proposals with independently useful consumers. Block storage remains
the active implementation track. These proposals do not start parallel implementation;
filesystem metadata work should follow the implemented
[native filesystem](../devices/filesystem-native-adapter.md) and the deferred
[authority direction](users-and-authority.md). Each step should end with ordinary
focused builds, manual runtime use and debugger inspection appropriate to its actual
change.

1. **Wait on native stream activity and completion.** Choose readiness-plus-try-I/O or
   explicit submission/completion for a first console/pipe/process/deadline slice.
   Specify registration lifetime, wake-before-park, close, EOF/error, timeout races and
   bounded queues. Keep network/fs watches outside the first slice. Useful consumers: a
   responsive pipe relay, child-output viewer, existing shell completion handling and
   Kilo input/status timing. This produces a reusable event-loop basis before Neovim.
2. **Multiple user threads in one process and the matching runtime safety.** Start with
   an explicit CPU-placement policy, stack limits, create/join/exit, TLS keys/errno,
   mutex/condition/semaphore/once and reliable loop wake. Resolve sibling VM
   loans/handle growth and process-wide fault/last-thread cleanup before implementation;
   do not add locks to the old ownership model and hope it holds. Make libc shared state
   safe. Useful consumers: libuv worker callbacks and Lua background work; eager vs
   guarded/lazy stacks and cross-CPU execution can be separately bounded decisions.
   The [runtime SMP milestone](../kernel/smp.md) separated spaces from CPUs,
   migrates existing single-task processes and made private memory operations
   local. It does not establish shared-process safety; user threads remain
   [separate work](scheduling-and-threads.md#multiple-user-threads). Sibling coordination is required even
   if the first threaded processes stay on one CPU.
3. **Truthful native file metadata, identity and editor conflict information.** Define
   identity comparison scope/lifetime, file kind/size, modification indication and
   actual access/ownership information across RAM/host and the selected
   native-filesystem direction. Add conventional wrappers only for supported behavior;
   carry forward existing exclusive creation, rename and file/directory sync. Useful
   consumers: alias-safe copy, TCC include identity, navigators and editor overwrite
   checks. General file watches, Unix permissions and broad locking APIs stay deferred
   until concrete policy is agreed.
4. **A native terminal-session first slice.** Independent session input/output,
   dimensions, exclusive/attached use, close/focus/resize events and terminal-state
   ownership; publish an accurate rendering/input capability set and implement the VT
   subset needed by consumers. Use raw byte input already available. Useful consumers:
   shell/Kilo sessions and the first multiplexer pane; a full multiplexer is another
   task. State an ASCII-first limitation if selected, then add UTF-8/wide/combining/font
   behavior as focused follow-ups before claiming general Unicode editing. PTY child
   terminals remain a separate extension.
5. **A small native libuv backend and controlled child streams.** Map loops/timers/async
   wake, native pipes/console, synchronous fs operations, worker-completion delivery and
   launcher/child completion into real libuv behavior. Deliberately expose only
   supported features. Settle delegated child-control authority and cooperative/forced
   stop semantics before claiming uv_process_kill or timeout termination; do not
   implement a global PID kill oracle. Useful consumers: a luv event/job utility and
   generic stdio RPC. TCP listeners, filesystem watches, PTYs and dynamically loaded
   code can remain explicit unsupported features.
6. **Pinned dependency closure and a basic Neovim integration slice.** Choose Lua 5.1
   interpreter vs LuaJIT, build the needed Lua/LPeg/luv/libuv/utf8proc/iconv/tree-sitter
   closure and keep host generators separate. Provide consistent capability-root paths,
   runtime/config/cache/temp locations, and mutable cwd/environment across all adapters.
   Use the new platform adapters, give TUI/server explicit launch/pipe/file authority,
   and validate open/edit/save with declared backup/swap, encoding and rendering limits.
   Useful consumer: Neovim with pure Lua/Vimscript configuration and ordinary native
   files. Native-code plugins, dynamic tree-sitter parsers, language-server executables
   and `:terminal` are subsequent consumer-specific tasks, not implicit promises in this
   slice.

## Decisions to discuss before implementation

- **Event semantics:** readiness or completion, buffer copying/borrowing, how completion
  survives close/timeout/cancel, event coalescing, short progress and queue quotas.
  Libuv cancellation need not forcibly interrupt already executing file work; its
  pending cancellation is not permission to drop the eventual callback.
- **Thread authority and lifetime:** same-process shared handles vs any restricted
  thread authority, CPU placement, stack ownership/limits, process-wide faults and exit,
  TLS ABI/access method, cancellation of blocked operations and the redesigned
  BSP/private-VM loan rules. Four libuv workers request 8 MiB stacks each; a naive eager
  implementation would provision 32 MiB just for those requested stacks. This arithmetic
  is not a measured Neovim memory footprint.
- **Filesystem policy:** identity across mounts/replacement, modification/timestamp
  accuracy, native users/ownership and access representation, edit leases/swap
  ownership, durability order and external host changes. Coordinate with the existing
  persistent-pool and users/authority discussions; Neovim's Unix st_mode/uid/gid
  assumptions do not decide them.
- **Process/session policy:** who can launch/stop children and descendants, extra stream
  delegation, terminal/session ownership and restoration, attach/detach, focus/resize,
  cooperative stop vs forced teardown. Numeric IDs needed for diagnostics must not
  become unchecked control authority.
- **First consumer boundary:** default swap/backup/recovery or explicitly deferred
  options, ASCII/Unicode subset, legacy syntax vs dynamic tree-sitter parsers, pure Lua
  plugins vs native modules/jobs, stdio RPC vs public/local/network listeners. Lua 5.1
  is an upstream-supported runtime alternative; extending the Lua 5.5 export cannot
  erase this version choice.

The investigation leaves actual dependency builds/linker closure, end-to-end application
performance, full Unicode rendering choices, native module ABI and exact process/thread
cancellation design unproven. The pinned source references and probe inputs above are
the handoff; no temporary source directory is required to understand the findings.

[N-build]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/BUILD.md
[N-deps]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/cmake.deps/deps.txt
[N-cmake]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/CMakeLists.txt
[N-main]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/main.c#L354
[N-ui]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/ui_client.c#L48
[N-loop]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/event/loop.c
[N-read]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/event/rstream.c
[N-write]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/event/wstream.c#L70
[N-fs]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/os/fs.c
[N-fileio]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/fileio.c
[N-bufwrite]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/bufwrite.c
[N-memfile]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/memfile.c
[N-memline]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/memline.c
[N-loader]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/runtime/lua/vim/loader.lua
[N-proc]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/event/libuv_proc.c
[N-proc-control]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/event/proc.c
[N-stream]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/event/stream.c
[N-tui]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/tui/tui.c
[N-pty]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/os/pty_proc_unix.c
[N-lsp]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/runtime/lua/vim/lsp/rpc.lua#L655
[N-system]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/runtime/lua/vim/_core/system.lua
[N-watch]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/runtime/lua/vim/_watch.lua
[N-socket]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/event/socket.c
[N-server]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/msgpack_rpc/server.c#L65
[N-ts]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/lua/treesitter.c#L134
[N-parser-cmake]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/cmake.deps/cmake/TreesitterParserCMakeLists.txt
[N-lua]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/lua/executor.c
[U-design]: https://github.com/libuv/libuv/blob/1cfa32ff59c076ffb6ed735bbc8c18361558661f/docs/src/design.rst
[U-async]: https://github.com/libuv/libuv/blob/1cfa32ff59c076ffb6ed735bbc8c18361558661f/docs/src/async.rst
[U-request]: https://github.com/libuv/libuv/blob/1cfa32ff59c076ffb6ed735bbc8c18361558661f/docs/src/request.rst
[U-stream]: https://github.com/libuv/libuv/blob/1cfa32ff59c076ffb6ed735bbc8c18361558661f/docs/src/stream.rst
[U-pool-code]: https://github.com/libuv/libuv/blob/1cfa32ff59c076ffb6ed735bbc8c18361558661f/src/threadpool.c
[U-unix]: https://github.com/libuv/libuv/blob/1cfa32ff59c076ffb6ed735bbc8c18361558661f/include/uv/unix.h
[U-fs-code]: https://github.com/libuv/libuv/blob/1cfa32ff59c076ffb6ed735bbc8c18361558661f/src/unix/fs.c#L139
[L-work]: https://github.com/luvit/luv/blob/f65fe9d7616f9fbcd20227fc7a73b38d1c5c180d/src/work.c#L284
[L-thread]: https://github.com/luvit/luv/blob/f65fe9d7616f9fbcd20227fc7a73b38d1c5c180d/src/thread.c
[J-mcode]: https://github.com/LuaJIT/LuaJIT/blob/fbb36bb6bfa88716a47c58bcf9ce9f2ef752abac/src/lj_mcode.c#L124

[N-path]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/path.c#L2422
[N-stdpaths]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/os/stdpaths.c#L41
[N-time]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/src/nvim/os/time.c

[N-deps-config]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/cmake.deps/CMakeLists.txt
[N-iconv-find]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/cmake/FindIconv.cmake
[N-iconv-build]: https://github.com/neovim/neovim/blob/5885a30e1e1225349079e7a1c4a3848aa8e43e42/cmake.deps/cmake/BuildLibiconv.cmake

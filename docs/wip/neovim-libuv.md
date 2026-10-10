# Neovim on Pyxis

Status: **milestone decisions accepted 2026-10-10; tasks 1–4 merged in #651, #656, #657 and #664.**
Task 4 is delivered; its [native contract](#task-4-preflight) and
[qualification](../development/experiments/libuv-native/README.md) describe the bounded slice.
Task 5 is implemented for review, with its [decisions](#task-5-decisions) and
[qualification](../development/experiments/lua51-luv/README.md).
Later tasks start only on the owner's go. The owner
wants Neovim as the development editor (vi bindings now, clangd later) instead
of patching BusyBox vi. The initial re-check used code and document inspection
of `origin/main` at `dc91a4c6`; the task 1 update and linked qualification
record distinguish implemented behavior and measured costs.

## Re-check against current main

The 2026-09-29 investigation proposed six milestones. Their state now:

| # | Original milestone | State | What exists and what is missing |
| --- | --- | --- | --- |
| 1 | Event waits | **Partly done** | [`wait_many`](../../include/abi/wait.h) waits on up to 32 interests and an absolute deadline (at most 30 s; zero polls), level-triggered: console and terminal input and output, interrupt and resize, TCP with [try operations](../devices/tcp.md#readiness-and-transfer-attempts), process and group completion, keyboard, pointer, display. **Task 1 adds pipe readiness and native try operations** ([pipes](../interfaces/pipes.md#readiness)); libc streams remain blocking, file and provider opens cannot be waited on, and there are no completion tokens. |
| 2 | User threads | **Unchanged for this purpose** | [Task 1 and later gates](threads.md#small-next-task-and-delivery-gates) (#612) split process lifetime from task retirement; a process still has exactly one task. Native create/join, TLS, the pthread profile and thread-safe libc remain later unassigned gates. |
| 3 | File metadata and identity | **Identity/time added by task 2** | Libc now has `sys/stat.h` (type/size and independently valid identity/time), `dirent.h`, `mkdir`, `O_RDWR`/`O_EXCL`/`O_APPEND`, `pread`/`pwrite`/`lseek`/`ftruncate`/`fsync`, `mkstemp`, `rename`, `strftime`, and atomic saves are in use ([vi](../userland/vi.md), [Quake](../userland/quake.md#saves-and-configuration), [Links](../userland/links.md)). Remaining metadata limits are [scoped identity](../technical-debt.md#file-identity-across-capability-paths) and ownership; also missing: `fcntl`, `mktime`, `iconv`. Proved `realpath`, `fdopen` and `dup` are delivered by task 6 slice 2. Shared [working path and environment](../userland/process-state.md) are delivered by task 6's first slice. |
| 4 | Terminal sessions | **Mostly done; rendering gaps remain** | [Independent sessions](../userland/terminal-sessions.md) have duplex queues, resize generations with `WAIT_RESIZED`, hangup and interrupt passthrough, and the [multiplexer](../userland/multiplexer.md) runs a shell per pane. Missing: non-ASCII input and drawing (the alternate screen, scroll regions and saved cursor came with task 3), and a PTY-style session for child terminals. |
| 5 | libuv backend | **Native task 4 slice** | Pinned libuv 1.52.1 has a Pyxis platform layer for loop/timers/async, console/pipe streams, explicit child launch/completion and synchronous libc filesystem operations. Excluded workers, async fs, sockets, watches, signals and module loading fail honestly; the [adapter limits](../../ports/libuv/README.md) remain gates for luv/Neovim. |
| 6 | Dependency closure | **Lua side added by task 5** | `ports` has Lua 5.5.1 with selected libraries and no `luaL_openlibs`; libuv 1.52.1 is added by task 4, and Lua 5.1.5, LPeg, luv and lua-compat-5.3 by task 5 ([`lua51`](../../ports/lua51/README.md)); utf8proc, tree-sitter and iconv remain absent. The pins listed [below](#exact-baseline-and-source-pins) are still Neovim 0.12.5's manifest. |

Two findings change the plan. A pipe pair that `wait_many` can watch is also the
cross-thread wake that libuv's `uv_async` needs, so no new wake object is
required. And the first slice can avoid threads: libuv creates its pool only
when work is submitted, and Neovim's own file calls mostly pass no callback and
are synchronous, so the
backend refuses pool and thread requests with an error. The
[task 4 audit](#early-neovim-pool-audit) found no mandatory pool/thread submission
or callback-style file call in bounded startup, opening and `:w`. Excluded Lua
configuration can invoke those APIs; this is not a native Neovim runtime result.

## Milestone proposal

**Scope of the first slice.** Neovim 0.12.5 in its normal terminal mode: the
client launches an `--embed` server of the same program over pipes, so the
space needs `launch = true`. It edits and saves native files in a framebuffer
tab or a multiplexer pane with Vimscript and Lua configuration under `home://`.
Not in it: swap and backup recovery, jobs and `system()`, `:terminal`, language
servers, tree-sitter parsers, threads, Unicode beyond ASCII. BusyBox vi stays
until the owner accepts the slice, and receives no further editor work.

### Tasks

Each task is a focused PR after the owner's go. The first two are useful
without Neovim.

1. [x] **Pipe readiness and try operations.** `wait_many` gains pipe READABLE,
   WRITABLE and closure, and pipes gain try read and try write, following the
   TCP model. The owner can pipe a slow producer into a viewer that shows output
   as it arrives and keeps answering keys, such as `less` following its input;
   today [`less`](../userland/less.md) blocks on a pipe read and has no live
   refresh. Task 1 supplies the native operations and helpers, without changing
   consumers. [Qualification and matched costs](../development/experiments/pipe-readiness/README.md)
   cover one and four CPUs, a responsive scratch child-output viewer, and mixed
   pipe/console/process/TCP waits.
2. [x] **File identity and modification stamp.** Native FILE/DIRECTORY queries
   and libc validity fields report backend identity/time. The owner can see
   the shell refuse output onto an aliased FILE stdin, and TCC honour
   `#pragma once`. See the [accepted contract](#task-2-contract),
   [metadata reference](../interfaces/file-metadata.md) and
   [qualification and matched costs](../development/experiments/file-identity/README.md).
3. [x] **Terminal profile for a full-screen editor.** Alternate screen, scroll
   region and saved cursor in the framebuffer TTY and the multiplexer, and the
   `TERM` name that advertises exactly what is supported. ASCII only. The owner
   can run a full-screen program that finds the shell screen intact on exit, in
   a tab and in a pane. Implemented as the [`pyxis` profile](../userland/terminal.md#tty-output-controls),
   with vi, less, Kilo and Links moved onto the alternate screen;
   [qualification and matched costs](../development/experiments/terminal-profile/README.md).
4. [x] **libuv backend.** libuv 1.52.1 with a Pyxis platform layer: loop, timers,
   async wake over a pipe pair, console and pipe streams, synchronous file calls,
   and child launch through the launcher. Pool, threads, sockets, file watches,
   `dlopen` and signals return an unsupported error; mutex, once and key
   primitives are correct for one thread only. The task also checks the
   thread-free bet early, so task 6 does not discover it: a host build of
   Neovim 0.12.5 against a libuv whose pool, thread and async file entry points
   log and refuse the way the backend will, run through startup, opening a file
   and `:w`, or failing that a source audit of the calls those paths reach. The
   result lists every thread, pool or callback-style file call Neovim makes. If
   startup needs the pool, this task reports it and the plan changes here. The
   owner can run a small libuv program that relays a child's output with a
   timer.
5. [x] **Lua 5.1.5 with luv.** Lua 5.1.5 with its standard libraries, LPeg, luv and
   lua-compat-5.3 as one recipe set. The owner can run Lua scripts with timers
   and child processes through luv. Implemented as the [`lua51` recipe](../../ports/lua51/README.md)
   and the `boot://share/lua51/lua5.1.pxb` bundle;
   [qualification](../development/experiments/lua51-luv/README.md).
6. [ ] **Neovim recipe and first slice.** Neovim 0.12.5 and its closure (utf8proc,
   tree-sitter library, iconv) with host generators kept native, plus the libc
   functions its build finds missing. The owner can run `nvim file`, edit with vi
   keys, save, and quit in a tab and a pane. The task is split around the
   accepted shared-libc contract and remaining recipe/qualification work. The [task 6 groundwork](neovim-groundwork.md)
   records the current-SDK build inventory and shared-libc contract accepted
   by the owner on 2026-10-10;
   the groundwork probe is recorded, editor implementation and qualification remain open.
   - [x] Current-SDK probe inventory and native-design proposal.
   - [x] Shared libc working path and explicit environment, with consumers and child snapshots.
   - [x] Bounded proved realpath and fdopen/shared descriptor association (`dup`).
   - [ ] Stream rebinding/buffering, calendar/encoding and remaining closure after assignment.
   - [ ] Recipe and full editor qualification after assignment.

Later, each with its own proposal:
- swap and backup recovery (needs task 2 and a lease policy);
- jobs and `system()` (extra stream delegation and group stop);
- `:terminal`, UTF-8 rendering, user threads and the pool, tree-sitter parsers and LuaJIT;
- clangd with the [hosted Clang direction](hosted-clang.md);
- Universal Ctags for tags (parked by the owner 2026-10-10);
- compiler diagnostics as Neovim diagnostics before clangd (the owner prefers red underlines to jump-to-definition).

### Task 2 contract

**Accepted 2026-10-10; implemented in this review.** The
[metadata reference](../interfaces/file-metadata.md) is authoritative.

1. Boot-scoped backend domain/object IDs compare while an original reference is
   held. Rename preserves identity; replacement distinguishes. HOST same-mount
   hardlinks and repeated roots match; bind aliases and after-full-close
   comparison are excluded.
2. Native FILE/DIRECTORY queries preserve independent field validity in libc
   stat. Real backend modification times only; npfs needs no format change.
   Unknown metadata leaves type/size stat usable.
3. Before truncation, the shell proves explicit outputs distinct from every
   stage's FILE stdin. Different known domains suffice without object IDs;
   same-domain unavailable identity, unknown domains and query failure refuse.
   TCC retains one stream per once object until normal/error translation-unit
   cleanup; borrowed stdin remains caller-owned. Argument files are outside
   shell protection; identity supplies no mutation lease.

### Task 4 preflight

**Defaults accepted 2026-10-10; implemented for review.**
The source audit inspected main `6f318e0c`; task work starts from `7766dae0`.
The first task remains the native loop/child-output relay with a timer. Pool,
worker creation/join, asynchronous filesystem submissions, sockets, watches,
loading modules and signals return unsupported errors.

The requested mirror is the existing manifest pin: libuv **v1.52.1**, commit
`1cfa32ff59c076ffb6ed735bbc8c18361558661f`, archive
`https://github.com/libuv/libuv/archive/v1.52.1.tar.gz`, SHA-256
`478baf2599bfbc882c355288c9cb6f92e0e7dda435fa04031fa5b607cf3f414c`.
The owner mirror is
`https://repo.internal/repository/raw-github/libuv/libuv/archive/v1.52.1.tar.gz`.
Its downloaded archive matches that SHA-256, independently checked before code.
The checksum also matches Neovim's pinned `cmake.deps/deps.txt`. Preserve upstream `LICENSE` (MIT) and
`LICENSE-extra` (including BSD-2-Clause tree.h and ISC inet routines where used).
Inspection copies are not build sources; recipes use the verified owner mirror.

**Accepted contract:**

1. **Native console try-write and output readiness.** `CONSOLE_TRY_WRITE`
   returns bounded short progress or WOULD_BLOCK; terminal record headers count
   toward capacity. WRITABLE/WRITE_CLOSED use WRITE authority and the existing
   wake-before-park machinery. Framebuffer output remains synchronous under its
   shared output lock. Blocking WRITE keeps whole-record admission.
2. **Explicit bundle pipe authority.** `pipe/create` is a recognized manifest
   request, supplied only to requesting foreground bundles when the shell holds
   it; missing required authority rejects admission. Plain-program delegation is
   unchanged. The relay bundle requests memory, clock, launcher and pipe. Its
   plain children receive explicit stdio and selected memory/clock/launcher
   grants, inherited directories excluding app, and attenuated namespace lookup.
   Pipe creation and session/system controls are not forwarded.
3. **Native result fidelity.** `uv_stat_t.stat_valid` preserves libc's optional
   domain/object/mtime bits; type/size are known and other Unix fields remain
   unknown. `uv_process_t.exit_reason` distinguishes EXITED/FAULTED/TERMINATED.
   Normal signed exit status is preserved; fault/termination report -1 with
   term_signal zero. PID fields/accessors return negative UV_ENOSYS. Closing a
   process handle releases observation and does not terminate it. Later luv and
   Neovim adapters must consume these native fields and unsupported errors.

Implementation follows existing ownership and bounds: libc owns `uv_file`
integer descriptors. The native libc bridge adopts handles atomically
and provides descriptor-aware PIPE/CONSOLE try I/O. FILE try I/O is ENOTSUP;
ordinary file calls remain synchronous. There is no second fd table or bypass of
read-ahead. Stream-open transfers descriptor responsibility. Unsupported
extra stdio slots, duplex pipes, shared FILE cursor inheritance and unsupported
spawn options reject before launch. Submitted writes borrow buffers until one
terminal callback; close cancels remaining writes before its close callback.

The loop admits at most the native 32 interests, reserving one for its coalesced
async wake reader: reject excess admission with UV_ENOSPC before activating I/O
or publishing a child, rather than splitting an atomic wait into polling batches.
Timers use no interest; waits longer than 30 seconds are capped and recomputed.
`uv_async_send` supports this one-thread process only, without claiming thread
or signal safety. Mutexes include real recursive depth; once/key state and
`uv_thread_self`/equality work for the sole thread. Creation/join/pool work fail.

#### Early Neovim pool audit

Inspected exact Neovim/luv/libuv pins from the table below. Core startup, ordinary
native file opening and `:w`, with swap/backup off and excluded APIs absent from
configuration, reached **no pool submission, worker creation/join or callback-style
filesystem request**. This is a source audit, not an instrumented host run or a
claim that unmodified Neovim already starts on this backend.

- [loop_init][N-loop] initializes mutex, async wake and timers; runtime search
  uses a mutex and logging needs a recursive mutex. [Lua initialization][N-lua]
  calls luaopen_luv and uv_thread_self. Luv [work initialization][L-work]
  creates mutex/once/key state and allocates Lua-state slots, without submitting
  work; refusing those initializers aborts startup.
- [C filesystem wrappers][N-fs], including open/close/stat/fstat/fsync and
  [regular-file streams][N-read]/[N-write], pass NULL callbacks. File read/save
  also use libc read/write. Luv src/fs.c selects a NULL callback when no Lua
  callback is supplied; libuv's [fs dispatch][U-fs-code] runs those synchronously.
- Lua package initialization requires vim._init_packages and core modules;
  default callbacks use timers without work submissions. The optional Lua
  loader's file calls are synchronous. vim._watch.watchdirs uses callback-style
  fs_stat when watching; explicit luv work/thread APIs submit work/create threads.
  Watches, those APIs and arbitrary configuration invoking them are excluded.
- Signal initialization is unconditional in upstream loop_init; unsupported
  signals still need a Neovim platform adaptation in task 6. The audit establishes
  the pool conclusion, not closure of every native startup dependency.

**Endpoint receivers need no new readiness in this slice.** Child/editor RPC
uses byte pipes. Synchronous FILE client calls may invoke a provider and block
(as their API permits); callback-style fs is rejected. Serving provider requests
inside a libuv loop would need a receiver adapter, which remains outside this
slice. Native endpoint receiver readiness is now available from #663.

The pinned [ports adapter](../../ports/libuv/README.md) documents the implemented
API subset, transfer ownership and errors. Every opened stream/observed process,
including inactive ones, reserves admission. Child spawn supports explicit
native images and up to three directional streams; cwd selection, child bundle
paths, FILE cursor inheritance and unsupported flags reject before launch. A
failed spawn leaves a closable inactive process handle. A supplied filesystem
callback rejects before effects; ordinary libc file/directory operations run
synchronously. Unsupported filesystem operations and value-only peripheral
APIs remain gaps for the later consumer tasks.

Owner command in the Development shell: `boot://share/libuv/uv-relay.pxb`.
The sample relays six child chunks while a timer and console input stay active.
See the [qualification record](../development/experiments/libuv-native/README.md)
for matched costs, capacity/cleanup and native result inspection. Tasks 5 and 6
still require separate owner assignments.

### Mirrors the owner must provide

Builds download only from the owner's mirrors. Each source is needed before the
task that uses it. Hashes are from the Neovim manifest at the pinned commit; the
Neovim source is a Git pin.

| Task | Source | Upstream | Pin |
| --- | --- | --- | --- |
| 4 | libuv 1.52.1 | `https://github.com/libuv/libuv/archive/v1.52.1.tar.gz` | SHA-256 `478baf2599bfbc882c355288c9cb6f92e0e7dda435fa04031fa5b607cf3f414c` |
| 5 | Lua 5.1.5 | `https://www.lua.org/ftp/lua-5.1.5.tar.gz` | SHA-256 `2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333` |
| 5 | luv 1.52.1-0 | `https://github.com/luvit/luv/archive/1.52.1-0.tar.gz` | SHA-256 `e8b8774b31d24be4fcf2b021b90599ecccc8e476c61efcc59c3c10cab813a885` |
| 5 | LPeg 1.1.0 | `https://github.com/neovim/deps/raw/d495ee6f79e7962a53ad79670cb92488abe0b9b4/opt/lpeg-1.1.0.tar.gz` | SHA-256 `4b155d67d2246c1ffa7ad7bc466c1ea899bbc40fef0257cc9c03cecbaed4352a` |
| 5 | lua-compat-5.3 0.13 | `https://github.com/lunarmodules/lua-compat-5.3/archive/v0.13.tar.gz` | SHA-256 `f5dc30e7b1fda856ee4d392be457642c1f0c259264a9b9bfbcb680302ce88fc2` |
| 6 | Neovim 0.12.5 | `https://github.com/neovim/neovim.git` | tag `v0.12.5`, commit `5885a30e1e1225349079e7a1c4a3848aa8e43e42` |
| 6 | utf8proc 2.11.3 | `https://github.com/juliastrings/utf8proc/archive/v2.11.3.tar.gz` | SHA-256 `abfed50b6d4da51345713661370290f4f4747263ee73dc90356299dfc7990c78` |
| 6 | tree-sitter 0.26.13 | `https://github.com/tree-sitter/tree-sitter/archive/v0.26.13.tar.gz` | SHA-256 `ece24c3c5e2a76384075e830c7139b59fce8fb01e4ef8436fab08bbe10444c89` |
| 6, if libc supplies none | libiconv 1.17 | `https://github.com/neovim/deps/raw/b9bf36eb31f27e8136d907da38fa23518927737e/opt/libiconv-1.17.tar.gz` | SHA-256 `8f74213b56238c85a50a5329f77e06198771e70dd9a739779f4c02f65d971313` |

Unibilium, gettext, the bundled parsers and Wasmtime are not needed: the first
slice builds with `ENABLE_UNIBILIUM=OFF` and `ENABLE_LIBINTL=OFF`. Task 6 also
needs a native host Lua 5.1 or LuaJIT for Neovim's generators, which the build
host provides, not the image.

### Task 5 decisions

**Accepted 2026-10-10; implemented in this review.** The four task 5 archives
are fetched from owner mirrors: `raw-lua` for lua.org (added by the owner for
this task) and `raw-github` for the others; each matched the SHA-256 above
before patching.

1. **Delivery and authority.** The interpreter is the development bundle
   `boot://share/lua51/lua5.1.pxb`, requesting memory, clock (read and sleep),
   launcher and pipe creation, with random optional. libuv's loop needs pipe
   creation, which plain programs do not receive. It is not in the default
   command catalog; the Lua 5.5 `lua` is unchanged.
2. **Standard library profile.** What libc cannot do is left out or reported:
   no `io.popen`, `os.execute`, `os.clock`, `os.setlocale`, `file:setvbuf` or
   C modules; `os.time(table)` is rejected; strings compare bytewise;
   `os.tmpname` uses `mkstemp`. The math library is complete through six musl
   functions added to libc (`asin`, `acos`, `sinh`, `cosh`, `tanh`, `exp`).
3. **Native process results in luv.** No PIDs (`nil`, or ENOSYS from the
   accessors); the exit callback gets `reason` (`"exited"`, `"faulted"`,
   `"terminated"`) as a third argument; unsupported families return ENOSYS.

Implementation notes: the recipe is one `lua51` set whose extra sources may now
carry their own patches (a change to the ports rule that limited patches to the
main source), and it removes four duplicate `uv_timer_*` ENOSYS stubs from the
libuv port, which made any program pulling the libuv port's unsupported
entry points fail to link.

### Accepted decisions

1. **Lua runtime.** PUC Lua 5.1.5. It needs no executable memory or JIT support;
   it is slower, and plugins that need LuaJIT's `jit` or FFI will not run.
   LuaJIT waits for a native executable-memory transition.
2. **Event model.** Readiness plus try operations, extending `wait_many` as for
   console, terminals and TCP. There is no second submission and completion I/O
   model.
3. **First-slice scope.** The narrow slice above: ASCII only, swap and backup off
   in the packaged configuration, and no threads, jobs or `:terminal`. BusyBox
   vi stays until the owner accepts the slice. Swap and backup need the identity
   contract and a lease and recovery policy first.

Accepted 2026-10-10 for task 3, before implementation:

4. **Multiplexer history.** The alternate screen keeps no history; wheel
   browsing and selection stay on the live screen while it is up, and the
   shell's screen and history return unchanged.
5. **`TERM=pyxis`** in the packaged session configuration, with one documented
   sequence table implemented by both terminals, including insert/delete line
   and reverse index, and charset designations consumed.
6. **Existing programs** move onto the alternate screen through small ports
   patches: BusyBox vi and less, Links and Kilo.

The thread-free bet is checked early, in task 4, not task 6.

## Investigation of 2026-09-29

The rest of this document is the original investigation at Pyxis `93031d03`. The
[re-check](#re-check-against-current-main) supersedes it where they differ,
and the [milestone](#milestone-proposal) replaces its milestone list and open
questions.

### Executive summary

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

# Rust standard library: first native profile

Decisions accepted (chronium, 2026-10-25); implementation authorized.
[Stage 1](../development/rust-toolchain.md) is merged in
[#1221](https://git.internal/PyxisOS/pyxis-os/pulls/1221) and
[#1224](https://git.internal/PyxisOS/pyxis-os/pulls/1224). It qualified core+alloc,
libc startup, initial-thread TLS and unchanged tiny-skia, not std or Rust workers.
This milestone supplies a cross-built native std and a first command-line port;
hosted rustc/Cargo and general crate compatibility remain later work.

## Inspected baseline

Pyxis `c284db8d` pins userland `b468f20c` and ports `99b6fe88`.
Rust is `48f4b05538b8eca412137042d9647a61e692c6b3`, nightly-2026-10-24,
with LLVM 23.1.4; the Pyxis linker is LLVM 23.1.3. The
[qualified target JSON](../../toolchain/rust/x86_64-unknown-pyxis.json) and
[SDK link adapter](../../toolchain/rust/pyxis-link.py) remain the settings and
link-contract sources of truth. Gate 1 is qualified against main `4c5ed5f4`,
userland `67aa6220` and ports `a05ca2c3`; see the
[compiler qualification record](../development/experiments/rust-std-toolchain.md).
Pyxis std remains unimplemented.

Chronium has created [PyxisOS/pyxis-rust](https://git.internal/PyxisOS/pyxis-rust)
from `mirrors/rust`; its `main` contains the pinned commit. Branch `pyxis`
starts **at that commit**, rather than the mirror's newer tip.
Carry the JSON's target ABI, data layout, SSE2, small/static/non-PIE code model,
no red zone, inline probes and local-exec TLS unchanged into the built-in
`x86_64-unknown-pyxis` target. It remains `target_os="pyxis"` without Unix or
Windows family. Built-in std support metadata changes only when supplied.

In this Rust revision, [platform selection](https://github.com/rust-lang/rust/blob/48f4b05538b8eca412137042d9647a61e692c6b3/library/std/src/sys/mod.rs)
is split across `library/std/src/sys/`
modules (`alloc`, `args`, `env`, `fs`, `thread`, etc.) and `pal`; adding one PAL
file is insufficient. The fork must select Pyxis in each supported module and
in std's build configuration. Ordinary compiler-generated Rust main enters
`lang_start` through libc's existing C-main startup; libc keeps `_start`, FS,
heap bootstrap and final native process exit.

## Accepted decisions (chronium, 2026-10-25)

1. **Toolchain and forks:** build the pinned Rust fork on horse using
   Rust's matching CI-built LLVM through a new approved, hash-pinned cache route.
   Build our compiler, not a downloaded CI rustc: the new built-in target changes
   compiler source. Use the revision's pinned stage0 bootstrap inputs, build and
   qualify a stage-2 Linux-host compiler plus Pyxis std in a separate prefix,
   and retain the stage-1 prefix. Keep Pyxis Clang/LLD as the final target linker,
   with LTO and embedded cross-toolchain bitcode off. A separate LLVM source
   build is the alternative if CI artifacts cannot be retained; do not silently
   switch to it or to the Pyxis LLVM tree. Fork the libc crate from std's
   `0.2.191` dependency line, using pinned `[patch.crates-io]` entries in both the
   Rust library build and consumer workspaces. The repository now exists at
   `PyxisOS/pyxis-libc`, branch `libc-0.2`, baseline
   `ccc2ff4aaab962f7fde7b8edda7379c045b1ca12`; native bindings and overlays
   remain gate 2 work. Exact source/lock revisions and notices precede builds.
2. **First std profile:** alloc/startup/args, TEXT environment, real
   stdio, entropy/time, managed threads/TLS/synchronization, native byte paths
   and the filesystem subset below. Retain panic-abort. Fallible APIs preserve
   real errors; an infallible API that cannot obtain a required fact or authority
   reports the operation and failure, then terminates, rather than fabricating
   success, absence, permissions, time, randomness or a PID. Environment
   enumeration includes this process's private and exported TEXT, never CAP;
   child export policy remains independent. Network and child creation are
   explicitly unsupported in this profile. No kernel mechanism is proposed.
3. **First port:** hexyl `v0.17.0`, with pinned, reviewable native crate
   patches and the explicit `--character-table=ascii` qualification profile.
   Qualify plain and coloured output, file/pipe input and honest failures before
   packaging it as an optional port. Do not enable `cfg(unix)` or claim its whole
   dependency ecosystem works. General network/process std, unwind/backtraces,
   default hexyl glyph additions and later ports remain separate assignments.

The fork and native-std direction was accepted during stage 1; these three
accepted choices settle stage 2's build, supported surface and first consumer.
Implementation starts with the toolchain gate; delivery remains split into
the bounded gates below, without claiming later gates from an earlier build.

## Toolchain and dependency boundary

Rust's pinned bootstrap selects a beta dated 2026-09-29 through `src/stage0`.
Use those checked component hashes through the existing `raw-rust/<date>/<file>`
cache and the closed Cargo proxy. The stage-1 nightly is not substituted as an
unrecorded bootstrap compiler. No user-supplied `RUSTC_BOOTSTRAP` workaround.

Upstream [bootstrap configuration](https://github.com/rust-lang/rust/blob/48f4b05538b8eca412137042d9647a61e692c6b3/bootstrap.example.toml)
supports `llvm.download-ci-llvm`; its
[downloader](https://github.com/rust-lang/rust/blob/48f4b05538b8eca412137042d9647a61e692c6b3/src/bootstrap/src/core/download.rs)
uses `https://ci-artifacts.rust-lang.org/rustc-builds/<revision>/rust-dev-<version>-x86_64-unknown-linux-gnu.tar.xz`.
That host artifact supplies Rust's LLVM, not a Pyxis program. Bootstrap derives
its revision from LLVM/version invalidation history, so preserve sufficient Git
history and record the actual URL, LLVM source revision, options and SHA-256
before requesting a cache entry. The resolved upstream artifact and all three
stage0 archive hashes are pinned in
[fork-toolchain.json](../../toolchain/rust/fork-toolchain.json).
All four bootstrap artifacts and the pinned backtrace source archive verify
through the internal caches. The Linux-host stage-2 compiler and Pyxis
core+alloc build with networking disabled; the separate exported prefix repeats
the existing samples without a JSON target or `build-std` flag. Missing inputs
stop the build; there is no direct-Internet build fallback.

CI LLVM avoids maintaining a second LLVM build when no Rust LLVM changes are
needed. A source build provides independence from expiring CI artifacts, but
adds another pinned compiler build and its dependencies. Preserve the approved
artifact in the cache so ordinary rebuilds do not rely on upstream retention.
Use separate host and target outputs and retain bootstrap configuration,
compiler identity, SDK input hashes and third-party licences. This is a host
toolchain build on horse, not a rebuild of the ordinary Pyxis compiler container
or a Rust build added to normal image CI.

The libc fork contains only truthful declarations/layouts/constants matching
the SDK and native target selection, including std's `rustc-dep-of-std` needs.
Do not import a Unix module or define unsupported functions just to compile.
Native bindings not belonging to libc stay in the Pyxis backend. The overlay
uses exact published commits; crates.io remains the unmodified closed proxy.
SDK changes require rebuilding affected Rust std/programs, with no development
ABI compatibility shim. Any new libc helper is published in a userland PR before
the parent pin. Rust/compiler changes belong in `pyxis-rust`, third-party ports
and patches in `pyxis-ports`, SDK/package assembly in Pyxis; see
[repository ownership](../development/sdk-and-repositories.md).

## First-profile contract

| Area | Accepted behavior and boundary; implementation still required |
| --- | --- |
| Allocation and startup | `System` preserves stage 1's checked malloc-backed over-alignment, zeroing and realloc semantics. Copy argv into owned byte strings after libc startup; no second allocator/TLS bootstrap. Keep direct P1F output and debug ELF/map from the same native objects. |
| Environment | Owned TEXT lookup and owned all-TEXT snapshots prevent borrowed `getenv` races. Absence differs from failed capture. Native name/value limits and errors apply; mutation uses the native table, following libc's exported-TEXT `setenv` behavior. A CAP is never converted into an environment string. |
| Stdio | Rust buffering and locks over the supplied synchronized libc descriptors, preserving short progress, EOF and errors. `IsTerminal` reflects the actual descriptor. No ambient console, `/dev/*` access or reinterpretation of descriptor numbers as native handles. |
| Time and entropy | Real monotonic `Instant`, wall `SystemTime`, sleep and entropy for randomized collections under declared grants. No weak random fallback or invented clock. Fallible helpers report errors; infallible `now`, sleep or hash-seed acquisition follows decision 2's failure rule. |
| Threads and sync | Managed libc create/join/detach, mutex/condition/once and native parking support Rust threads, channels and park/unpark. Preserve wake-before-park and absolute timeout semantics. `available_parallelism` reports effective eligibility or an honest error, never merely online CPU count. |
| TLS and exit | Executable local-exec TLS; libc remains FS owner. Register language destructors with libc, and order Rust per-thread `rt::thread_cleanup` after its language/key drainage. Panic in any thread aborts the process; join does not recover it and catch_unwind supplies no recovery. Join workers before orderly main return; existing forced-stop/live-worker exit limitations remain. |
| OsStr and Path | Byte-preserving OsStr and native scheme-aware paths. `scheme://` is a root, not a Windows drive or a Unix relative component. Leading `/` has no native root; `..` cannot escape delegated authority. Byte preservation applies to OsStr, Path storage and filesystem I/O, not component round-trips. Components are conventional lexical components plus a Pyxis scheme root; repeated separators and interior `.` normalize on iteration/collection. Equality/hash compare components. Filesystem calls pass original bytes and resolve them in order without normalization. |
| Files | Real open/read/write/seek/clone, resize and sync; file type/size and valid backend metadata; ordinary directory enumeration and existing create/remove/rename operations where granted. Missing timestamps return Unsupported. Permission-mode mutation, unreported permission facts, symlinks and file locking remain unsupported. No synthetic Unix identities, modes or timestamps. |
| Net and processes | Socket creation, DNS, Command spawn/output and Unix process extensions remain unsupported. Real process exit/abort is required by std startup and is not deferred with child creation. No fake PID or signals. |

Supported operations still depend on each backend and the received rights.
Decode native errno values explicitly, preserving raw codes: ESPIPE maps to
NotSeekable, ENOTSUP to Unsupported and EAGAIN to WouldBlock. Linux's numeric
constants do not apply. Backtrace capture reports its unsupported status.
Provider streams may be forward-only and have no SIZE; seek/metadata must fail
honestly, with no implicit materialization. Request memory/manage, clock read/
precise/sleep and random/read for the complete qualification bundle; request
threads/manage only for the threaded exercise. Root and stream grants are
explicit. A general std program does not silently acquire the full exercise's
authority merely by linking std.

Rust thread stack defaults follow libc's 1 MiB eager guarded stack; explicit
requests round up to a page and at least 1 MiB, never below the requested size.
Reject overflow or requests above 8 MiB, with no clamping. Creation and collection
retain libc's actual capacity, grant and allocation failures. No Rust-owned FS,
dynamic TLS, asynchronous cancellation or language unwind is added.

Two inspected adapter gaps need work inside this scope:

- The current libc environment snapshot exports TEXT only; `vars_os` needs an
  owned all-TEXT capture, while native INCLUDE_PRIVATE/TEXT_ONLY already exists.
  Do not widen the helper used to choose a child's exported environment.
- Libc drains language destructors, bounded key passes, then language destructors
  first registered by keys. Rust's generic key guard assumes its cleanup runs
  after TLS destructors. Add a narrowly scoped userland end-of-drain hook for
  per-thread `rt::thread_cleanup` if
  needed; do not declare TLS complete by reusing that guard unchanged. Qualify
  the initial thread, normal worker return and keys that initialize Rust TLS.
  Rust's separate process cleanup inside `lang_start` remains distinct from
  this hook and from the later libc `exit` drainage.

Native paths also require changes above syscall dispatch: this Rust revision's
[fallback parser](https://github.com/rust-lang/rust/blob/48f4b05538b8eca412137042d9647a61e692c6b3/library/std/src/sys/path/unix.rs)
is Unix-shaped and cannot recognize a Pyxis absolute scheme.
Review scheme-root representation throughout Path/PathBuf components and joining;
do not disguise it as an existing Windows Prefix. The selected first profile
supports native schemes rather than shipping a std where direct File::open works
but its Path operations silently change the spelling.

### Gate 2 path components — accepted (chronium, 2026-10-25)

Keep conventional lexical components plus one Pyxis-only
`Component::SchemeRoot(&OsStr)` for the complete `scheme://`. No Windows prefix
is reused; leading `/` is not a native root. Repeated separators and interior
`.` normalize on iteration/collection as on Unix. `Path` equality/hash compare
components as on other platforms. Original `Path`/`OsStr` bytes remain unchanged
in storage and are passed unchanged to providers for every filesystem call;
there is no normalization on I/O. Byte preservation therefore does not promise
component round-trips. There is no separate separator component or raw-component
API. Parent/file-name inspection remains lexical, not provider discovery.

Gate 2's baseline precedes adapter edits; see the
[record](../development/experiments/rust-std-main.md).

The current libc DIR abstraction retains enumeration authority without a public
held-directory lookup helper. First-profile `DirEntry::metadata` is Unsupported
until it can operate against a retained authorized directory; reopening a saved
relative path after cwd changes is not an implementation. Enumeration errors
such as concurrent-change EAGAIN are errors, not EOF. Infallible permission
queries without a truthful native meaning follow decision 2, rather than invent
a writable/readonly bit. These are explicit first-profile limits.

References: [process environment](../interfaces/environment.md),
[cwd and TEXT](../userland/process-state.md), [paths](../userland/paths.md),
[native threads/TLS](../interfaces/threads.md), [libc threads](../userland/threads.md),
[libc profile](../userland/libc-portability.md), [loading](../kernel/program-loading.md).

## Hexyl admission

Accepted pin: [v0.17.0](https://github.com/sharkdp/hexyl/tree/v0.17.0), commit
`8eb6d4771ce1ec7af65d06bd335457783b77d557`, MIT OR Apache-2.0, Rust 2021/MSRV 1.88.
Cache archive `https://repo.internal/repository/raw-github/sharkdp/hexyl/archive/refs/tags/v0.17.0.tar.gz`,
SHA-256 `72fa17397ad187eec6b295d02c7caabbb209a6e0d5706187b8a599bd5df8615e`.
Source inspection, not a successful Pyxis build: it needs File open/metadata/seek,
stdin/stdout locks, buffered I/O, TEXT environment and LazyLock. It does not need
networking or child creation for ordinary viewing. Override its release LTO=true
to false and retain the locked, licensed dependency graph through the Cargo proxy.

Expected small native patches: replace its sole libc ESPIPE dependency with the
standard NotSeekable error kind; select a Pyxis terminal-size backend in
terminal_size and restrict that crate's rustix dependency to real Unix targets;
propagate input read failures instead of the printer's `if let Ok(n)` loops
silently discarding them. Publish these as separate, upstreamable patches.
supports-color already uses std IsTerminal/environment; no Unix impersonation.

The terminal lacks hexyl's default diamond and bullet glyphs. An explicit ASCII
character table avoids promising those glyphs; coloured output and ordinary
box borders still need actual terminal qualification. Use `--color=auto` for
TTY-versus-pipe checks: upstream defaults to Always. Exercise NO_COLOR separately.
Do not expand this std milestone into a terminal font change.

## Ordered delivery gates

Implementation is authorized (chronium, 2026-10-25), in the order below.
The first task is compiler/target plumbing, not an attempt at every std adapter.
Gate 1 is delivered by
[pyxis-rust #1](https://git.internal/PyxisOS/pyxis-rust/pulls/1) and
[Pyxis #1230](https://git.internal/PyxisOS/pyxis-os/pulls/1230). The built-in
target retains `std=false`; compiler qualification does not imply std support.

1. [x] **Fork and built-in target.** Create pyxis at the pinned source, resolve
   bootstrap/CI-LLVM cache receipts, build the Linux-host compiler and target
   core+alloc, then repeat both stage-1 samples with unchanged settings/checksum.
   **After this task chronium can:** cross-build the existing Rust samples with
   the owner-maintained compiler and built-in target, without a JSON target flag.
2. [ ] **First std main.** Publish the libc bindings fork; add std selection,
   allocator, startup/args, TEXT, stdio, entropy/time and native path support.
   Define any target-specific path representation and infallible failure handling
   before using it in filesystem adapters. Qualify missing grants and teardown.
   **After this task chronium can:** run an ordinary Rust main using strings,
   collections, arguments, environment, output and clocks under explicit grants.
3. [ ] **Threads/TLS.** Implement synchronization, spawn/join/detach, parking,
   scoped threads and destructor/per-thread cleanup ordering, publishing required
   libc helpers first. Qualify exhaustion and no lost wakeups, including TLS
   initialized during key cleanup. Record panic-abort and live-worker exit limits.
   **After this task chronium can:** run managed Rust workers with real thread-local
   state, channels, synchronization and deadlines.
4. [ ] **Native files.** Add the selected fs subset and retained ownership over
   supplied roots/streams; preserve metadata validity and Unsupported boundaries.
   **After this task chronium can:** read, write, seek, sync and enumerate granted
   files from std without an ambient filesystem or process launch mechanism.
5. [ ] **Hexyl and close-out.** Pin/patch/build/package hexyl, compare host output
   and errors, qualify the complete profile and publish concise interfaces/limits.
   **After this task chronium can:** install and use a std-based hex viewer on
   files or pipelines in Pyxis.

## Qualification and stop rules

Record exact fork/libc/port/SDK pins, bootstrap inputs and output provenance.
Capture matched stage-1 launch/session backing controls before runtime changes;
repeat with the new compiler/profile, keeping configuration and CPU count fixed.
Do not equate host compiler build success with supported standard operations.

Use interactive QEMU with one and four CPUs and GDB: normal Rust main, inherited
args/TEXT, mutation versus snapshot lifetime, stdio FILE/pipe/console and missing
streams; heap over-alignment/OOM; native clocks/random and missing grants;
worker join/detach, TLS isolation/drainage and park/unpark/timeouts. Inspect FS,
stack guards/probes and final completion without a second TLS owner. Qualify
panic-abort in a worker as process failure, not a successful join result.

Use RAM plus disposable HOST/npfs fixtures for file ownership, retained handles,
relative cwd and scheme operations, read-only/denied roots, conditional metadata,
concurrent enumeration errors and cleanup. Network/Command calls must report
Unsupported. No native disk, PXE, ABI extension, new CI or host-tool publication
is implied by these checks.

Compare hexyl with the pinned Linux build on identical binary input, explicit
ASCII/colour options, empty and partial final rows, seek/skip/count, stdin pipe,
file redirection and absent/denied inputs. Verify read/write errors are returned;
colour Auto/NO_COLOR and terminal width work on supplied streams. Qualify ordinary
package install/verify and subsequent launches. Full default-glyph rendering,
general crates, native hardware and performance require their own evidence.

Any new authority or kernel contract, cross-language unwind requirement or path/
cleanup behavior not covered above stops for a proposal. No compatibility shim,
successful unsupported stub or Unix-family shortcut closes such a gap.

# Rust standard library: first native profile

Decisions accepted (chronium, 2026-10-25); implementation authorized.
[Stage 1](../development/rust-toolchain.md) is merged in
[#1221](https://git.internal/PyxisOS/pyxis-os/pulls/1221) and
[#1224](https://git.internal/PyxisOS/pyxis-os/pulls/1224). It qualified core+alloc,
libc startup, initial-thread TLS and unchanged tiny-skia, not std or Rust workers.
This milestone supplies a cross-built native std and a first command-line port;
hosted rustc/Cargo and general crate compatibility remain later work.

## Qualified delivery

Gate 1 qualified the fork compiler/core+alloc; see the
[compiler record](../development/experiments/rust-std-toolchain.md).
Gate 2 uses Pyxis source `16b1abc6`, userland `14be162e`, ports `fa9998d5`
and Rust fork `5e85f86de5f7dc45a485fa8a3eb376487923bc0b`, based on
nightly-2026-10-24 `48f4b055` with LLVM 23.1.4. Final links use SDK Clang/LLD
23.1.3. The [std main record](../development/experiments/rust-std-main.md)
separates build, QEMU/debugger and native-program control results.
The stacked hexyl step uses Rust `a5c9a181`, libc crate `417acae0` and
ports `deff7c1b`, with the SDK refreshed on userland `6cd1cf57`. Its
[qualification](../development/experiments/rust-hexyl.md) covers the read-only
file subset and optional package, not workers or the full filesystem gate.
The stack is rebased and requalified after the 0.0.9 release; the original
release excluded Rust.
The [stage-1 target JSON](../../toolchain/rust/x86_64-unknown-pyxis.json) and
[SDK link adapter](../../toolchain/rust/pyxis-link.py) remain the original
code-generation and link-contract sources of truth; the built-in target retains
those settings, adds tier metadata and now reports installed std support.

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
   are published in [libc #1](https://git.internal/PyxisOS/pyxis-libc/pulls/1)
   at `a6aac31c`. Exact source/lock revisions and notices accompany builds.
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

| Area | Accepted behavior and boundary; delivery is tracked below |
| --- | --- |
| Allocation and startup | `System` preserves stage 1's checked malloc-backed over-alignment, zeroing and realloc semantics. Copy argv into owned byte strings after libc startup; no second allocator/TLS bootstrap. Keep direct P1F output and debug ELF/map from the same native objects. |
| Environment | Owned TEXT lookup and owned all-TEXT snapshots prevent borrowed `getenv` races. Absence differs from failed capture. Native name/value limits and errors apply; mutation uses the native table, following libc's exported-TEXT `setenv` behavior. A CAP is never converted into an environment string. |
| Stdio | Rust buffering and locks over the supplied synchronized libc descriptors, preserving short progress, EOF and errors. `IsTerminal` reflects the actual descriptor. No ambient console, `/dev/*` access or reinterpretation of descriptor numbers as native handles. |
| Time and entropy | Real monotonic `Instant`, wall `SystemTime`, sleep and entropy for randomized collections under declared grants. No weak random fallback or invented clock. Fallible helpers report errors; infallible `now`, sleep or hash-seed acquisition follows decision 2's failure rule. |
| Threads and sync | Managed libc create/join/detach, mutex/condition/once and native parking support Rust threads, channels and park/unpark. Preserve wake-before-park and absolute timeout semantics. `available_parallelism` reports effective eligibility or an honest error, never merely online CPU count. |
| TLS and exit | Executable local-exec TLS; libc remains FS owner. Register language destructors with libc, and order Rust per-thread `rt::thread_cleanup` after its language/key drainage. Panic in any thread aborts the process; join does not recover it and catch_unwind supplies no recovery. Join workers before orderly main return; existing forced-stop/live-worker exit limitations remain. |
| OsStr and Path | Byte-preserving OsStr and native scheme-aware paths. `scheme://` is a root, not a Windows drive or a Unix relative component. Leading `/` has no native root; `..` cannot escape delegated authority. Byte preservation applies to OsStr, Path storage and filesystem I/O, not component round-trips. Components are conventional lexical components plus a Pyxis scheme root; repeated separators and interior `.` normalize on iteration/collection. Equality/hash compare components. Filesystem calls pass original bytes and resolve them in order without normalization. |
| Files | Real open/read/write/seek/clone, resize and sync; file type/size and valid backend metadata; ordinary directory enumeration and existing create/remove/rename operations where granted. Missing timestamps return Unsupported. Permission-mode mutation, unreported permission facts, symlink creation/following and file locking remain unsupported; recursive removal can unlink HOST symlinks. No synthetic Unix identities, modes or timestamps. |
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

Gate 2's userland prerequisites are published in
[userland #384](https://git.internal/PyxisOS/pyxis-userland/pulls/384):
`pyxis_environment_text_snapshot_init` owns private and exported TEXT, while
child launch retains its exported-only capture. The narrow per-thread
`libc_thread_cleanup_register` hook runs Rust's `rt::thread_cleanup` after
language/key/new-language drainage. Actual TLS destructors register with libc;
libc remains FS owner. Rust's separate process `rt::cleanup` remains inside
`lang_start`. Initial-thread cleanup is qualified in gate 2; worker/key
exhaustion cases remain gate 3. The hook's post-cleanup registration refusal applies only to
threads with a registered runtime cleanup. Explicit GOT placement outside TLS
also fixes an SDK linker orphan that otherwise corrupts TLS relocations.

Gate 2 supplies the native address-wait bridge used by std's internal
mutex/once/stdio machinery. Managed worker creation and contended synchronization
are not delivered by this selection; gate 3 owns their lifecycle and qualification.
Sleep uses checked native `clock_sleep_for`, refusing duration/deadline overflow
instead of adopting libc nanosleep's saturation.

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

### Directories — accepted (chronium, 2026-10-26)

ReadDir and its entries share ownership of one retained native directory. Copy
name bytes and known types before advancing; names are not forced to UTF-8.
Entry metadata resolves through that original parent, never a reconstructed
path or a later cwd/environment lookup. It may observe removal or replacement
at the same name. The original path remains display spelling. Entries can
outlive the iterator and keep a mount reference until their last owner closes;
retain the existing checked-close failure policy.

Admission requires ENUMERATE only, preserving already delegated rights for later
entry queries. Ordinary C opendir remains unchanged. Iteration is live: return
one actual error, then finish; no sorting, snapshot or automatic restart. A
known cached type requires no lookup. An unknown type uses zero-right native
typed lookup through the retained parent, requiring its existing LOOKUP; never
guess, follow links or demand file content rights merely to identify a type.

Create/remove/rename are native-only libc routes, refusing provider invocation
before effects. Preserve traversal/CREATE/REMOVE authority, backend type and
same-store limits; no copy/delete rename fallback, implicit sync or automatic
mutation retry. A failed mutation may already have effects. Directory mode bits
are not persistent native permissions.

Checked create_dir_all captures its native root/cwd once, retaining the chain
and resolving components in original order. Do not cancel `..` or escape the
boundary. Validate real scheme roots and existing directories fallibly; reject
leading slash, preserve the documented empty-path no-op and concurrent creators.
Existing final `.`/`..` require actual resolution. Failure may leave created
parents; there is no transaction/rollback promise.

### Recursive removal and copy — accepted (chronium, 2026-10-26)

Implementation and qualification are delivered in
[userland #394](https://git.internal/PyxisOS/pyxis-userland/pulls/394),
[libc #6](https://git.internal/PyxisOS/pyxis-libc/pulls/6),
[Rust #7](https://git.internal/PyxisOS/pyxis-rust/pulls/7) and
[Pyxis #1268](https://git.internal/PyxisOS/pyxis-os/pulls/1268); see the
[record](../development/experiments/rust-copy-remove.md). These
operations use narrow native libc helpers, not the shared path-reopening or
permission-setting fallbacks.

- Removal unlinks HOST symlinks without following them, including a symlink
  operand. The native REMOVE gains exact SYMLINK support for HOST only; no link
  opening, traversal or creation is added. Other special entries remain
  Unsupported. A regular-file operand fails as not a directory.
- Resolve through retained native parents with one captured cwd for relative
  paths and one capture of each explicit root. Reject scheme roots and final
  dot/dotdot, but add no named-cwd/ancestor ban or one-filesystem promise. HOST
  bind-mounted contents can be emptied before final BUSY. A moved-away held
  directory can still be emptied and a replacement empty directory removed by
  the final name operation; this is not an identity-conditioned deletion.
- The iterative traversal grows fallibly with actual memory/handle/name limits,
  without a fixed depth or total-work cap. Collect bounded batches and reset
  observation after own confirmed removals. Allow eight consecutive CHANGED
  observation restarts without confirmed removal; the ninth returns EAGAIN.
  Progress resets the count, so creators can prolong the operation. Initial
  missing root returns NotFound; disappearance after admission is tolerated.
  Final nonempty/busy errors return immediately.
- Copy overwrites an existing retained FILE in place, preserving its identity
  and effects on other hardlinks/held handles. Source READ and target WRITE
  suffice for existing objects; absence uses exclusive CREATE. Reject known
  aliases before resize with InvalidInput, unprovable distinctness with
  Unsupported. Fresh exclusive creation proves a new object. Do not guess
  from paths or metadata zeroes, reopen a competing creator or stage replacement.
  The existing HOST bind-alias/cross-session identity exclusions remain.
- Copy contents and length only: no grant readonly, mode, ownership, time or
  other metadata propagation. Qualify Rust's public permission-copy sentence
  for Pyxis. Completed regular provider FILE sources remain allowed through
  read-only routes and real metadata/identity checks; source OPEN may already
  do network I/O when a later target fails. Native-only targets refuse writer
  OPEN/STAGE before effects. Streams without file metadata, symlink sources
  and special objects fail; no hidden materializer or symlink following.
- Copy the source's initial length with bounded scratch, ignoring later growth
  and reporting UnexpectedEof on premature EOF. Holes can become allocated
  zero bytes. Existing off_t/backend limits apply, without another size cap.
  Concurrent changes can mix data; no snapshot or hole/offload promise.
- First substantive failure stops new work, preserving partial effects. Never
  replay an uncertain mutation, roll back or unlink a partial copy by pathname.
  All helper-owned closes are checked: return a close failure after otherwise
  successful work, preserve an earlier failure during remaining cleanup.
  Public File/Dir destructor fatal-close policy remains unchanged. Neither
  operation implicitly syncs or promises crash durability.

These are proper native operations for Cargo, tempfile cleanup and installers,
not a permanently disabled profile. The entire std profile still needs its
separate close-out qualification; this task does not authorize later ports.

References: [process environment](../interfaces/environment.md),
[cwd and TEXT](../userland/process-state.md), [paths](../userland/paths.md),
[native threads/TLS](../interfaces/threads.md), [libc threads](../userland/threads.md),
[libc profile](../userland/libc-portability.md), [loading](../kernel/program-loading.md).

### Native-directory delivery

[Userland #392](https://git.internal/PyxisOS/pyxis-userland/pulls/392),
[libc #5](https://git.internal/PyxisOS/pyxis-libc/pulls/5),
[Rust #6](https://git.internal/PyxisOS/pyxis-rust/pulls/6) and
[Pyxis #1262](https://git.internal/PyxisOS/pyxis-os/pulls/1262) deliver the
accepted directory contract. ReadDir/DirEntry retain one native parent, including
across iterator drop, cwd change and root rebinding. Entry metadata uses that
parent and the saved name; path() remains display spelling and can resolve
elsewhere later. Entries may keep a mount release BUSY until dropped.

Live enumeration yields a real error once, then ends; earlier entries stay
usable. Known types need no child lookup; unknown types use native typed lookup
without asking for file content rights. Native create/remove/rename and checked
create_dir_all preserve backend limits and partial effects. Provider directory
operations refuse before invocation. The [record](../development/experiments/rust-directories.md)
separates one/four-CPU RAM/HOST/npfs and GDB checks, matched costs and limits.
No native hardware run, new kernel interface or new grant is claimed. That directory stage left recursive removal and copy to the separate delivery above.

### Worker yield — accepted (chronium, 2026-10-26)

For this native profile, `std::thread::yield_now` is a weak processor hint under
preemptive scheduling, matching the C++ adapter. It does not force a reschedule
or promise fairness. Channels may use it during bounded contention backoff;
blocking synchronization still uses native address waits. This adds no scheduler
operation or clock-grant requirement. Worker delivery retains the existing
managed stacks, FS ownership, TLS/runtime cleanup order and panic-abort contract.
`available_parallelism` remains Unsupported until effective caller eligibility
can be reported truthfully; online CPU count is not substituted.

### Native writable files — accepted (chronium, 2026-10-26)

`File::metadata().permissions().readonly()` reports the actual held grant's
FILE write right. `fs::metadata(path)` reports authority obtainable through that
captured path: FILE write for files, CREATE or REMOVE for directories. A
directory's WRITE_FILES right alone does not permit namespace mutation. Resolve
once and retain the native object while reading its metadata and rights; do not
combine a separate access probe with stat. The two queries can legitimately
differ when a read-only File was opened through a writable directory. Unknown
provider authority returns Unsupported from path metadata, without a writable
OPEN probe. `Permissions::set_readonly` changes the local value only;
`set_permissions` remains Unsupported and never grants authority.

Writable File opens use native FILE routes only. Refuse provider/transaction
writers before OPEN, STAGE, create or truncate; keep existing provider reads.
Native files are unbuffered at this layer, so `File::flush` has no pending data;
`sync_all` and `sync_data` use the backend's real sync operation. Preserve short
progress, checked close failures and actual metadata validity. Directory
enumeration and namespace mutation are delivered separately above.

The [native append milestone](atomic-append.md) replaces the Rust option guard
with libc `O_APPEND`. Append implies write access; read plus append is read/write.
Exclusive creation takes precedence, and append plus truncate remains invalid
unless exclusive creation is selected. Native APPEND serializes EOF selection
and mutation per operation up to 4,080 bytes. Larger writes can interleave;
unsupported backings refuse before create/truncate. No seek-to-end emulation or
provider transaction change is introduced.

The writable slice is delivered by [userland #388](https://git.internal/PyxisOS/pyxis-userland/pulls/388),
[libc #4](https://git.internal/PyxisOS/pyxis-libc/pulls/4),
[Rust #5](https://git.internal/PyxisOS/pyxis-rust/pulls/5) and
[Pyxis #1257](https://git.internal/PyxisOS/pyxis-os/pulls/1257).
The [record](../development/experiments/rust-native-files.md) separates functional
qualification, backing and noisy nested timing; no native run is claimed.

### Managed-worker delivery

[Libc #3](https://git.internal/PyxisOS/pyxis-libc/pulls/3) at `bdaad80e`
adds the existing managed lifecycle declarations. [Rust #4](https://git.internal/PyxisOS/pyxis-rust/pulls/4)
at `f08e6f816` implements the worker adapter; [Pyxis #1250](https://git.internal/PyxisOS/pyxis-os/pulls/1250)
selects its libc overlay and records the [qualification](../development/experiments/rust-std-workers.md).
Userland remains `6cd1cf57`: no new C helper, kernel operation, ABI or grant.
Spawn transfers startup state once; join consumes the managed observer, and
JoinHandle drop detaches through libc. Names and ThreadId are Rust metadata;
no native OS identity or scheduler control is invented. Contended synchronization,
channels and TLS/key/runtime cleanup are qualified on one and four QEMU CPUs.
The later native-file stages qualify writable files and directory mutation separately.

## Hexyl admission

Accepted pin: [v0.17.0](https://github.com/sharkdp/hexyl/tree/v0.17.0), commit
`8eb6d4771ce1ec7af65d06bd335457783b77d557`, MIT OR Apache-2.0, Rust 2021/MSRV 1.88.
Cache archive `https://repo.internal/repository/raw-github/sharkdp/hexyl/archive/refs/tags/v0.17.0.tar.gz`,
SHA-256 `72fa17397ad187eec6b295d02c7caabbb209a6e0d5706187b8a599bd5df8615e`.
The port builds against the supplied fork std and SDK; its C runtime and kernel
are unchanged from gate 2. It needs read-only File open/read/metadata/seek/clone,
stdio, TEXT and LazyLock, without workers, networking or child creation.
The Rust backend preserves original path bytes on lookup, owned descriptors and
shared cloned cursors. Regular files report their actual length; pipes and
consoles do not acquire a synthetic seekable length. Only reported mtimes are
exposed. Hexyl originally qualified the read-only backend; the native writable-file
task extends the shared std backend under the authority contract above.
The later native-file stages add directory iteration and namespace mutation.
File locks remain outside the profile. The later copy/removal delivery adds real
native convenience operations without changing the original hexyl port patches.

Two platform patches replace the direct libc ESPIPE dependency with
NotSeekable and add a native supplied-console backend to terminal_size. Its rustix dependency applies
only to actual Unix targets. The patched terminal_size source is a separate
local dependency; registry vendor checksums stay intact. supports-color uses
std IsTerminal and TEXT without a patch or Unix impersonation. Preserve
upstream BrokenPipe exit-success behavior. The locked closure, patch sources,
MIT/Apache notices and embedded CP437/CP1047 notices accompany the package;
the CP1047 table's EPL-1.0 text is retained in full.

General upstream I/O behavior is retained: a persistent read error can loop,
and include-mode final buffer-drop errors can go unreported. These limits are
recorded in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1405);
no generic error-handling patch is carried in the port.

The explicit qualified character profile is `--character-table=ascii`;
box borders remain UTF-8. Colour Always/Auto/Force, NO_COLOR, custom RGB,
file redirection and supplied terminal width are qualified in QEMU. This is
not qualification of every upstream byte glyph or CP437 character.
See [build and package usage](../development/rust-toolchain.md#optional-hexyl-package).

## Ordered delivery gates

Implementation is authorized (chronium, 2026-10-25). The next-step assignment
advanced the read-only hexyl consumer before managed workers and the remaining
file operations; it does not close those gates.
The first task is compiler/target plumbing, not an attempt at every std adapter.
Gate 1 is delivered by
[pyxis-rust #1](https://git.internal/PyxisOS/pyxis-rust/pulls/1) and
[Pyxis #1230](https://git.internal/PyxisOS/pyxis-os/pulls/1230). Gate 1 delivered
core+alloc; gate 2 adds the bounded native std backend.
Installed std metadata does not promise every std API or crate works.

1. [x] **Fork and built-in target.** Create pyxis at the pinned source, resolve
   bootstrap/CI-LLVM cache receipts, build the Linux-host compiler and target
   core+alloc, then repeat both stage-1 samples with unchanged settings/checksum.
   **After this task chronium can:** cross-build the existing Rust samples with
   the owner-maintained compiler and built-in target, without a JSON target flag.
2. [x] **First std main.** Publish the libc bindings fork; add std selection,
   allocator, startup/args, TEXT, stdio, entropy/time and native path support.
   Define any target-specific path representation and infallible failure handling
   before using it in filesystem adapters. Qualify missing grants and teardown.
   **After this task chronium can:** run an ordinary Rust main using strings,
   collections, arguments, environment, output and clocks under explicit grants.
3. [x] **Threads/TLS.** Implement synchronization, spawn/join/detach, parking,
   scoped threads and destructor/per-thread cleanup ordering, publishing required
   libc helpers first. Qualify exhaustion and no lost wakeups, including TLS
   initialized during key cleanup. Record panic-abort and live-worker exit limits.
   **After this task chronium can:** run managed Rust workers with real thread-local
   state, channels, synchronization and deadlines.
4. [x] **Native files.** Add the selected fs subset and retained ownership over
   supplied roots/streams; preserve metadata validity and Unsupported boundaries.
   - [x] Native writable FILE opens, writes/resize/sync and authority metadata
     (accepted 2026-10-26); see the [qualification](../development/experiments/rust-native-files.md).
   - [x] Directory enumeration and namespace mutation (accepted 2026-10-26);
     see the [qualification](../development/experiments/rust-directories.md).
   - [x] Native remove_dir_all and fs::copy (Cargo/tempfile/installers), accepted
     2026-10-26; see the [qualification](../development/experiments/rust-copy-remove.md).
   **After this task chronium can:** read, write, seek, sync and enumerate granted
   files from std without an ambient filesystem or process launch mechanism.
5. [x] **Hexyl.** Pin/patch/build/package hexyl with the read-only File subset;
   compare host output and honest errors, file/pipe/console behavior and package
   install/verify in one- and four-CPU QEMU. See the
   [record](../development/experiments/rust-hexyl.md).
   **After this task chronium can:** install and use a std-based hex viewer on
   files or pipelines in Pyxis.
6. [ ] **Profile close-out.** After the remaining worker/filesystem gates,
   qualify the complete first profile and publish concise implemented interfaces
   and limits. Hexyl delivery alone does not complete the std milestone.
   **After this task chronium can:** use the documented complete first std profile.

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
file redirection and absent/denied inputs. Observe errors, bounding the
known upstream persistent-read loop with the existing job supervisor;
colour Auto/NO_COLOR and terminal width work on supplied streams. Qualify ordinary
package install/verify and subsequent launches. Full default-glyph rendering,
general crates, native hardware and performance require their own evidence.

Any new authority or kernel contract, cross-language unwind requirement or path/
cleanup behavior not covered above stops for a proposal. No compatibility shim,
successful unsupported stub or Unix-family shortcut closes such a gap.

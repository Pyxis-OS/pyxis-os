# Git on Pyxis

Investigation, accepted direction and task status. The libgit2 source-acquisition
investigation dates from 2026-10-09 (#568); the Git CLI and HTTP transport
investigation from 2026-10-11 (#711). **Fact** means measured compilation or
inspected source/contracts; **Inference** means a proposed native adaptation. No
Git or libgit2 executable has run on Pyxis, and no port, source pin, ABI or
compiler-container change is delivered by this document. Builder/staging
requests preempt this work. This supports [source builds](source-builds.md) after
hosted Clang; obtaining pinned source is separate from providing a developer's
Git CLI.

**Accepted route:** a native source-fetch command using a restricted libgit2 port,
with a bare object store and explicit tree export. It removes the process/shell
dependency and avoids a working-tree index. It still needs libc, path,
object-store and transport work. The owner also wants a complete Git CLI
eventually, with its larger process, worktree and transport contracts handled
separately; the CLI tasks below are proposed, not assigned, and do not replace
the accepted route. Neither route automatically ports the shell-based ports
runner.

## Task status and accepted decisions

Delivered libc prerequisites of the source-fetch route:

- [x] Task 1: public `O_RDWR` and `O_CREAT|O_EXCL`; userland #171, Pyxis #573.
  See [qualification](../userland/libc-portability.md#readwrite-and-exclusive-create-qualification).
- [x] Task 2: public `pread` and `pwrite`; userland #172, Pyxis #576. See
  [positioned I/O](../userland/libc-portability.md#positioned-file-io-and-qualification).
- [x] Task 3: `lstat`, `access`, `rmdir` and the four `arpa/inet.h` byte-order
  functions; userland #174, Pyxis #585. See
  [path and byte-order checks](../userland/libc-portability.md#path-and-byte-order-qualification).
  The owner-accepted access profile 1 (2026-10-09) is recorded there.

Working path and environment (`chdir`/`getcwd`, `setenv`/`unsetenv`; children
receive explicit snapshots, no writable `environ` or `putenv`), the bounded
[`realpath`](../userland/paths.md#proved-realpath) and
[`fdopen`/`dup`](../userland/stdio.md#opening-and-ownership) are delivered by
[process state](../userland/process-state.md) and Neovim task 6 slice 2.

These tasks unblock shared dependencies, not a functioning fetch tool. Later
work needs separate owner assignment and covers remaining libc support, the
restricted libgit2 source/path/cache closure, a native HTTPS transport, then
exact-OID verification and bounded tree export. Pack/index/blob hash
verification must remain enabled. Require the pin to resolve to a commit, not an
arbitrary object or substituted ref, and check completeness of its reachable
tree/blobs before reporting success. A Git OID verifies source identity relative
to the trusted recipe pin; it does not authenticate recipe authors or replace TLS.

The owner accepted these three defaults on 2026-10-09:

1. **Goal: pinned source acquisition via libgit2 first.** A full
   `clone/fetch/checkout/status/log` CLI remains the eventual goal, requiring its
   own native launcher and index-cache work.
2. **Tree profile: ordinary files/directories, with 100644/100755 Git blobs both
   materialized as native files and their original modes retained in the fetched
   tree.** Document that there is no POSIX execute bit. Reject symlinks and
   gitlinks before export; never dereference a symlink or silently omit a
   submodule. Adding native symlinks/metadata or resolving recipe submodules
   needs separate decisions. The ports runner already pins extra source
   repositories separately, but the complete recipe-source closure has not been
   audited for these entry kinds.
3. **Storage and publication: one fresh private `tmp://` store and destination,
   discard on failure; no shared persistent cache or overwrite.** Preflight the
   tree before writing, with an explicit caller-selected resource budget, and let
   the runner consume it only after successful verification/export. Do not
   promise an atomic directory switch: native directory rename is absent.
   Persistent caching and publishing installed binaries remain later contracts.

The current ports runner uses `os.execute`, `io.popen`, `git init`, `git apply`,
checkout and rev-parse; a fetch command alone does not port that runner, the
patch step or build tools. Its integration must preserve trusted recipes, pins,
ordered patches and failure reporting without a POSIX shell, and add no
successful fake operations.

Delivery follows [repository ownership](../development/sdk-and-repositories.md):
libc changes in userland, libgit2 recipes/patches in ports, a native command and
TLS adapter in userland, parent integration only after published dependencies.
The base SDK stays independent of TLS/libgit2.

## Evidence and build boundary

**Fact, measured:** `make -j16 sdk` succeeded with Clang/LLD 23.1.3 in the
existing `pyxis-builder:pyxis-llvm23.1.3-49e2c1a` container without changing the
compiler or SDK declarations, and the pinned zlib 1.3.2 and Mbed TLS 4.1.1 recipes
built against it. Git 2.53.0 and libgit2 1.9.3 are selected releases, not a claim
about the newest upstream. The unmerged
[probe snapshot](https://git.internal/PyxisOS/pyxis-os/src/commit/e9c427cb/probe)
(branch `probe/git-sdk-gaps`) keeps commands, the SDK manifest and diagnostics; it
contains no functioning port. The measured builds are the initial 2026-10-09
snapshot (libgit2 probes) and a 2026-10-11 refresh at Pyxis `78536b51`
(Git CLI probes); neither ran any target executable, QEMU boot, timing,
durability experiment or on-Pyxis operation.

Both archives came from the owner's existing `raw-github` cache:

| Source/cache entry | Observed SHA-256 |
| --- | --- |
| `https://repo.internal/repository/raw-github/git/git/archive/refs/tags/v2.53.0.tar.gz` | `0492bcd8a9fc99bedca75dbec7c8c15b24dd1501c82d4518294ee0850f91f219` |
| `https://repo.internal/repository/raw-github/libgit2/libgit2/archive/refs/tags/v1.9.3.tar.gz` | `d532172d7ab24d2a25944e2434212d63ee85f3650e97b5f7579e7f201a78ad64` |

These identify the probe bytes; they are not release-signature checks. Production
recipes need an explicitly reviewed checksum or commit pin. The upstream URLs are
`https://github.com/git/git/archive/refs/tags/v2.53.0.tar.gz` and
`https://github.com/libgit2/libgit2/archive/refs/tags/v1.9.3.tar.gz`; Git 2.53.0 is
annotated tag `a978e09acb5a3db0959fc757df17b27e8706d26b`, peeled commit
`67ad42147a7acc2af6074753ebd03d904476118f`. Git is GPL-2.0; libgit2 is GPL-2.0
with its linking exception. Nothing was downloaded from upstream. The owner
created `git.internal/mirrors/libgit2`, `mirrors/git` and `mirrors/curl` (anonymous
HTTPS `ls-remote` succeeded) and reports a `raw-gitlab` cache on `repo.internal`;
no recipe or mirror branch is followed yet.

### Measured compilation

| Probe | Result |
| --- | --- |
| Git `make -k -j16 git` with threads, mmap, gettext, iconv, Perl/Python/Tcl, OpenSSL, curl and expat disabled (`uname_S=Pyxis`, project and bundled regex/poll headers before SDK headers, `NO_REGEX=NeedsStartEnd`, `NO_STRTOUMAX`, `NO_POLL`, `NO_NSEC`, `NO_UNIX_SOCKETS`) | Exit 2; every translation unit fails at unconditional `compat/posix.h:110`, absent `sys/time.h`. No Git link closure is established |
| Selected Git `statinfo`, `read-cache`, `entry`, `run-command`, `commit`, `clone`, `index-pack` objects with empty absent-header stand-ins | Exit 2; time, ownership, stat and process failures remain |
| libgit2 static CMake build (tests/CLI/threads/SSH/HTTPS/nanosecond stat disabled, SDK zlib, system regex, bundled HTTP parser) | Configure passed; compilation failed at `src/util/git2_util.h:101`, missing `arpa/inet.h` (supplied since by task 3) |
| libgit2 `USE_HTTPS=mbedTLS` | Stock finder does not find the required crypto library; with stand-ins, `streams/mbedtls.c:37` fails on missing `mbedtls/entropy.h` |
| Real-SDK control executable taking addresses of `realpath`, `getcwd/chdir`, `setenv/unsetenv`, `fdopen`, `dup/dup2`, `setvbuf`, `stat/lstat/fstat`, `open`, `pread/pwrite`, `rmdir/access`, byte-order functions, `mkstemp` | Target link passed; symbol availability only, not semantics |

`NO_CURL` removes HTTPS, so the Git probe is a core build, not an HTTPS candidate.
Empty stand-in headers declare and implement nothing; they only reveal later
dependencies. No replacement declarations, SDK edits or host libc were used.

## Current libc boundary for Git

**Fact:** [libc portability](../userland/libc-portability.md),
[process state](../userland/process-state.md), [paths](../userland/paths.md) and
[stdio](../userland/stdio.md) describe the delivered interfaces. A missing header
in Git's broad Unix includes is not an instruction to add every POSIX API; many
(`sys/time.h`, `signal.h`, `sys/wait.h`, `poll.h`, `sys/socket.h`, `netdb.h`,
`pwd.h`, `sys/mman.h`, `pthread.h`, `termios.h` and others) remain absent, some
only as unconditional includes of disabled features.

| Area | Current evidence | Git consequence / classification |
| --- | --- | --- |
| Working path, environment, realpath | Real `chdir/getcwd`, copied env mutation, bounded proved scheme path | Git absolute-path detection (`scheme://` is not a root to Git), discovery/root traversal, PATH/exec resolution and config paths need a native scheme/capability backend, not colon-split Unix PATH. |
| Descriptor/FILE association | Real `fdopen`, shared-object `dup/dup2`, bounded buffering and append | No descriptor inheritance across spawn and no POSIX pipe/select/fcntl semantics. Flush FILE output before delegation; preserve short writes and errors. |
| Metadata | Type/size plus independently valid dev/ino/mtime (`st_valid`) | ctime, owners, permission bits and persisted-cache guarantees remain unavailable; see worktree section. |
| Errors | ENOTDIR, ELOOP, ENAMETOOLONG, EINTR vocabulary | EINTR is compatibility vocabulary; native calls do not report signal interruption. `ENOEXEC` is still undefined. |
| Time | `compat/posix.h:187-193`, `date.c:134`, `trace.c:114` use timeval/`gettimeofday` | Genuine consumer boundary. Adapt to native wall/monotonic clock authority or implement a proven libc extension; no fake clock values. |
| Integer parsing / regex | `NO_STRTOUMAX` and `NO_REGEX` select Git's own fallbacks | Available source choices, not new libc dependencies. SDK TRE lacks `REG_STARTEND`; `strtok_r` is delivered. |
| Mapping / polling | `NO_MMAP` selects an owned-buffer fallback; `NO_POLL` selects `compat/poll` | Owned buffers cost memory. The poll fallback still calls select (`compat/poll/poll.c:402`), so it supplies no native waiting; adapt actual pipe consumers. |
| UID/modes/process IDs | `st_uid`, `get[e]uid`, `pid_t`; `ident.c:46` getpwuid; tempfiles/locks use `getpid` | Needs native authority, explicit author/committer identity and random names. No fabricated UID ownership, permissive `safe.directory` bypass, umask, PID or successful chmod. |
| Optional Unix services | Socket, terminal, syslog, resource, passwd headers appear broadly | Exclude genuinely omitted features or adapt specific consumers. Sockets, terminal emulation, shell, gettext, OpenSSL and libcurl are not prerequisites for the offline profile. |

General ISO C and proven libc extensions belong in userland libc; Git-specific
authority, process and index policy belongs in the port. No standard API returns
invented success.

## Route 2: a native fetch command using libgit2

Libgit2 performs negotiation, pack processing and object access in process.
`USE_THREADS=OFF` and `USE_SSH=OFF` genuinely remove those runtime dependencies;
its bundled HTTP parser and collision-detecting SHA-1 backend need no new
libraries, and zlib supplies compression. A native command can fetch into a bare
repository, resolve the **exact full pinned commit**, walk its tree, and write raw
blobs through libc, without a working-tree index, hooks/filters, or
`status/log/commit/push`. This is a smaller route, not an unmodified libgit2 build:

| Area | Inspected gaps and proposed adaptation |
| --- | --- |
| Filesystem core | Real `O_RDWR/O_EXCL`, `pread/pwrite`, `lstat/access/rmdir` and byte-order helpers are delivered (access cannot check X_OK or providers; lstat cannot expose symlink metadata). Path/cwd closure and the directory sync bridge remain. Standard support belongs in libc. |
| Mapping | No public `mmap`; libgit2's `NO_MMAP` fallback reads into owned memory and rejects writable mappings. Shared-file mapping is unnecessary for the slice; bounded allocation and the remaining source/path closure still need work. |
| Metadata without checkout | ODB alternate deduplication reads `st_ino`; pack/config/file-buffer caches read mtime/inode; discovery reads device identity. A single fresh store with no alternates/discovery and explicit cache reloads can be adapted without fabricated stat fields; that adaptation is unbuilt. |
| Worktree features | All-source builds still compile `index.c` and iterators, which read stat ctime/mtime/ino/uid/gid; runtime avoidance is not a compile fix. Restrict or adapt that source closure explicitly and reject unsupported APIs. `checkout` with suppressed index writes still has stat/filter/mode assumptions. |
| Unix support | Remaining names include `gettimeofday`/`struct timeval`, `readlink/link/symlink/chmod` and `utimes`. Avoid omitted feature code, or implement standard behavior where native objects support it. |
| Stock networking | Socket/connect/send/recv, `getaddrinfo`, `inet_pton` and poll/select are absent. Native TCP/DNS exists but is not a BSD socket API. Use libgit2's stream/subtransport extension for platform integration; ordinary missing libc calls remain shared libc work. |
| TLS | Stock `FindmbedTLS.cmake` requires `libmbedcrypto`; Pyxis exports `libtfpsacrypto`. Source includes `mbedtls/entropy.h` and `ctr_drbg.h`, absent in this 4.1.1 export, and seeds its own global entropy/DRBG. Renaming a library cannot provide Pyxis clock/random/trust authority. Prefer an adapter to existing native `libtls`, outside the base SDK. |

Inspected libgit2 locations:
[indexer.c](https://github.com/libgit2/libgit2/blob/v1.9.3/src/libgit2/indexer.c#L683),
[NO_MMAP fallback](https://github.com/libgit2/libgit2/blob/v1.9.3/src/util/posix.c#L227),
[ODB identity](https://github.com/libgit2/libgit2/blob/v1.9.3/src/libgit2/odb.c#L675),
[file stamps](https://github.com/libgit2/libgit2/blob/v1.9.3/src/util/futils.c#L1149),
[TLS backend](https://github.com/libgit2/libgit2/blob/v1.9.3/src/libgit2/streams/mbedtls.c#L36).

## Filesystem contract shared by both routes

**Source-inspected**, not newly qualified runtime behavior. The
[libc policy](../userland/libc-portability.md), [paths](../userland/paths.md),
[native writer](../devices/filesystem-native-adapter.md) and pinned headers are
the authorities.

| Requirement | Current Pyxis contract and consequence |
| --- | --- |
| stat ino/dev/ctime/mtime/mode | `struct stat` supplies type/size and independently valid `st_dev`, `st_ino`, `st_mtim` with `st_valid`. Identity comparison requires retained references; pathname stat releases its reference. No ctime, owners or permission bits are invented. Mtime can repeat or backtrack and is not a change counter. See [worktree, index and publication](#worktree-index-and-publication). |
| Exclusive lock files | Native `path_create_file` and `mkstemp` create exclusively; public `O_CREAT\|O_EXCL` gives real EEXIST after authority checks. Grants, not mode bits, establish authority. |
| Atomic file publication | Same-volume file rename replaces a file atomically, including across directories; held victim handles survive. Directory rename is unsupported; cross-volume failure maps to ENOTSUP, not EXDEV. |
| Symlinks and executable bits | NPFS has regular files/directories only; host enumeration may report symlinks but lookup refuses them. No chmod/symlink interface or stored execute bit. Loading requires READ authority; Git tree mode and native launch authority are different concepts. |
| File and directory fsync | Public `fsync` accepts only writable file descriptors. Native directory sync exists but no directory-fd bridge exposes it; Git/libgit2 open a directory read-only then fsync it, which cannot work today. Close is not durability. |
| Object-store ordering | Lock/temp file data must complete and sync before durable namespace publication; rename to an absent name does not flush file contents. Do not infer Git-style crash recovery from atomic rename. Disposable RAM stores avoid persistent-cache durability, not creation/publication correctness. |
| Directory enumeration | `opendir/readdir/closedir` exist; no `d_ino`, dirfd, `rewinddir` or `readdir_r`. Enumeration is live, omits dot entries and may fail EAGAIN after mutation. A fetch tool should avoid shared writers and handle invalidation. |

Native UTF-8, case-sensitive components are limited to 255 bytes. Tree export must
preflight names, reject traversal/invalid components and collisions, and stay
within the granted destination. Native paths reject leading `/`, while both
upstreams recognize Unix roots rather than `scheme://`; a capability-correct path
adaptation is required even if every missing declaration were supplied.

## Git CLI: restricted profile

`clone`, `fetch`, `checkout`, `status` and `log` are C builtins whose basic
nonrecursive use needs no POSIX shell script. A restricted CLI turns off pagers
(`--no-pager`), auto-maintenance, parallel checkout (`checkout.workers=1`),
fsmonitor and hooks, and excludes SSH, credential helpers, recursive submodules,
external diff/textconv, shell aliases and filters. Required filters must be
refused, not silently skipped; repository config and attributes cannot re-enable
unsupported execution. Clone/checkout normally invoke `post-checkout`, so the
no-hook policy must be deliberate. Shell-based commands outside this slice need
not be installed; sample hooks/templates are optional.

### Process model and a first CLI slice

[`run-command.c:start_command`](https://github.com/git/git/blob/v2.53.0/run-command.c#L677)
creates pipes, forks, redirects descriptors, changes child cwd and execs.
`prepare_cmd:411` can add a shell fallback even without `use_shell`.
`wait_or_whine:558` expects Unix exit/signal status. `start_async:1152` uses a
pthread callback, or **forks the callback with copied memory under NO_PTHREADS**
(`NO_PTHREADS` converts asynchronous sideband work to `fork`; it does not remove
concurrency). A launcher can replace executable children; it cannot serialize
arbitrary C callbacks and parent pointers without a separate design. There is no
justification for kernel `fork`, POSIX signals or pretend successful child
execution.

| Command or facility | Source-inspected process boundary | Inference: first supported profile |
| --- | --- | --- |
| init / ordinary add | Basic local C paths; `add -e/-p` involve editor/interactive execution | Offline candidates after path/object/worktree closure; refuse interactive edit/patch initially |
| commit | Editor, signing, hooks; prepare-commit-msg is not disabled by `--no-verify` | Unsigned `commit -m/-F`, explicit author/committer identity and enforced external-execution refusal |
| status / log / diff | Optional pager, external diff, textconv, fsmonitor, signatures | Content-correct status, no-pager log, internal diff; refuse those extensions explicitly |
| checkout | Serial local path; post-checkout hooks, filters, submodules, parallel workers expand it | Serial ordinary-file checkout with the mode policy below; refuse omitted features |
| clone / fetch | Launch `remote-https` ([transport-helper.c](https://github.com/git/git/blob/v2.53.0/transport-helper.c#L142)), pack ingestion children (`index-pack`/`unpack-objects`, [fetch-pack.c](https://github.com/git/git/blob/v2.53.0/fetch-pack.c#L961)) and connectivity `rev-list` ([connected.c](https://github.com/git/git/blob/v2.53.0/connected.c#L102)); legacy transport also launches `fetch-pack` ([remote-curl.c](https://github.com/git/git/blob/v2.53.0/remote-curl.c#L1191)) | Later network slice; no POSIX shell needed for these internal executables, but native launcher/streams plus sideband adaptation are mandatory |
| push | remote helper/send-pack, pack-objects, asynchronous sideband ([send-pack.c](https://github.com/git/git/blob/v2.53.0/send-pack.c#L60)) | Later authenticated remote-write slice, beyond the read-only gateway |
| Hooks, aliases, pager/editor, filters | `hook.c:13/123`, `pager.c:136`, `editor.c:60` rely on executable discovery or shell commands | Enforce unsupported-feature policy after config/attributes/environment are read. Do not treat `access(X_OK)=ENOTSUP` as "no hook." |
| Auto-maintenance | `run-command.c:1816` can launch maintenance even when `gc.auto=0` | Enforce `maintenance.auto=false`/no automatic child work initially |

The offline paths are candidates by inspection, not demonstrated Pyxis commands.
Initial policy also excludes credential helpers, shell aliases, recursive
submodules and repository-controlled signing/verification executables. Repo
configuration must not re-enable a feature or silently omit configured
hooks/filters; existing hooks/filters needing execution cause an explicit refusal
of the affected operation.

**Inference: native run-command backend.** Consume the shared
[native command execution](../userland/native-jobs.md) foundation for streams,
cwd/env snapshots, result/stop and cleanup. Git owns bundle resolution, enforced
external-feature policy and fork-callback/sideband adaptation. Resolve internal
Git children from one retained bundle revision, launching the same native Git
executable with a subcommand argv; resolve separately packaged helpers through
explicit roots and READ/LAUNCH authority. Keep [bundle](../userland/program-bundles.md)
command/resource views, stack/capture limits and grant attenuation; unpacked
bundles work, native ZIP loading is unimplemented. No `/proc` executable
discovery or shell fallback.

Snapshot the copied environment with Git's child delta and an owned native cwd;
resolve a child cwd override without mutating the parent. Create directional
native pipe pairs, close unused parent ends after launch capture, flush delegated
FILE output, and own each child completion observer exactly once. Positive short
progress, EOF/EPIPE, failed/unknown launch and EXITED/FAULTED/TERMINATED remain
distinct. Native file stream delegation starts at offset zero; pipe read-ahead
stays in the parent's memory. See [stdio delegation](../userland/stdio.md#standard-streams-formatting-and-exit)
and [launcher/process contracts](../userland/process-state.md).

Use native wait/termination for owned children. Faults must not become synthetic
signal numbers or successful exit; uncertain launch/mutation must not be retried
as if nothing happened. Shell supervision owns foreground-group termination;
group-bound child launchers provide LAUNCH, not nested group supervision. A
network sideband callback needs an explicit helper process or a bounded event
pump; substituting `launcher_launch` for `fork` cannot work.

### Worktree, index and publication

**Fact:** native [`struct stat`](../../userspace/libc/include/sys/stat.h) has
`st_valid` bits for dev, ino and mtime, independently; type/size are
unconditional. Identity tokens promise comparison only while original references
remain held. NPFS creation time is not POSIX metadata-change ctime.

| Git need and source | Native capability / honest refusal | Inference: port boundary |
| --- | --- | --- |
| [`statinfo.c:23/63`](https://github.com/git/git/blob/v2.53.0/statinfo.c#L23) assumes ctime/mtime/dev/ino/uid/gid; the [index format](https://github.com/git/git/blob/v2.53.0/Documentation/gitformat-index.adoc#L66) persists 32-bit cache fields | Optional identity/time does not give durable reopened-file identity, UID/GID/ctime or monotonic freshness | Preserve Git's disk format but make unavailable cache hints unusable for native freshness; use actual content comparison initially |
| `core.checkStat=minimal` / `core.trustctime=false` / `core.filemode=false` | Minimal still tests whole-second mtime and size | These cannot prevent same-size edits from being hidden; do not fabricate epoch/zero stat facts |
| [`read-cache.c:229/307/381/442`](https://github.com/git/git/blob/v2.53.0/read-cache.c#L229) hashes content and handles racy entries | Native reads/hash can establish blob equality under supported conversion policy | Adapt comparators and racy logic together; forcing DATA_CHANGED alone can skip hashing at :472 and falsely report dirty files. Disable/adapt stat-validity, untracked and fsmonitor caches too |
| Execute bit / [`entry.c:86`](https://github.com/git/git/blob/v2.53.0/entry.c#L86) opens executable entries with 0777 | Public file creation accepts 0666 only | Preserve tracked 100755 in Git metadata, create native regular files with 0666; new files default 100644 |
| Symlink / lstat / [`entry.c:313`](https://github.com/git/git/blob/v2.53.0/entry.c#L313) | NPFS has no symlinks; host lookup refuses them with ELOOP | Refuse symlink checkout initially; `core.symlinks=false` text materialization is a possible separately accepted profile. Refuse gitlinks/recursive submodules initially |
| index.lock / [`tempfile.c:137`](https://github.com/git/git/blob/v2.53.0/tempfile.c#L137) uses `O_RDWR\|O_CREAT\|O_EXCL`, 0666 | Real exclusive creation and EEXIST under parent authority | Use that exclusion with checked writes/close; preserve denial/outcome uncertainty. Private 0600 temp requests elsewhere need deliberate capability policy, not discarded privacy intent |
| Lock/ref/object publication ([`tempfile.c:336`](https://github.com/git/git/blob/v2.53.0/tempfile.c#L336), [`object-file.c:626`](https://github.com/git/git/blob/v2.53.0/object-file.c#L626)) | Atomic same-volume file replacement. No hardlinks, directory rename or cross-backend fallback | Adapt object rename publication, retaining collision checks. No fake link/chmod or recursive directory move |
| Crash durability (`object-file.c:790`, `refs/files-backend.c:2022`) | Public `fsync` accepts writable files; no directory-fd bridge | Define checked file sync, atomic publication and native directory sync. After rename, a sync failure reports uncertainty; close/rename alone cannot promise crash safety |

The [filesystem writer](../devices/filesystem-native-adapter.md) is the
durability authority: rename-to-absent commits the namespace without necessarily
flushing contents; replacement flushes the moved file first. A content-based
freshness profile costs full scans and hashing; no estimate was measured. Reads
concurrent to unrelated mutation need an operation-level policy; full hashing does
not manufacture a filesystem snapshot.

Git also bypasses comparison for CE_VALID (assume-unchanged), CE_SKIP_WORKTREE and
CE_FSMONITOR_VALID (`read-cache.c:399-404`, refresh:1357-1368); `core.ignoreStat`
can create assume-valid entries (`environment.c:352`). The first profile must
refuse `core.ignoreStat` and unsupported assume-valid/skip-worktree/sparse index
entries rather than advertise content-correct status while honoring those
shortcuts; supporting them needs a later explicit profile.
[`setup.c:1380`](https://github.com/git/git/blob/v2.53.0/setup.c#L1380) assumes
Unix repository ownership; a native trust/authority check must replace it
("current UID owns everything" and `safe.directory=*` are not solutions). Access
authority and whether repository configuration may launch programs are distinct.

### Threads and gates 5-8

**Fact:** `NO_PTHREADS` is a genuine serial pack/index choice
([`thread-utils.c:21`](https://github.com/git/git/blob/v2.53.0/thread-utils.c#L21)
returns one CPU; `pack-objects:3158` and `index-pack:1361` have serial paths). Git's
local dummy pthread creation/join fail rather than succeed. This is no permission
to export fake locks/pthreads from Pyxis libc. Refuse `GIT_FORCE_THREADS` and
other worker overrides in the profile.

**Inference:** a serial offline CLI and separately launched serial Git children
need no new thread API; their libc cwd/environment/descriptors/allocator state is
process-local. Smart transport still needs non-fork sideband execution, which
serial pack workers do not remove; a native sideband helper or event-pump
adaptation can proceed without claiming pthread support. Parallel workers
require all [thread gates](threads.md#delivery-gates): 5 lifecycle/join/guarded
stacks; 6 real parking/wakeup; 7 static TLS/per-thread errno and compiler
support; 8 synchronized libc allocator/descriptors/FILE/caches and a real
pthread/C11 profile. Gate 5 alone is insufficient; all four were unchecked at the
inspected main (#709's prototype is not a merged pthread runtime). Git's serial
option should not expand hosted Clang's accepted real TLS/synchronization work.

## HTTPS transport

[Existing HTTPS](../userland/https.md) provides certificate-verified TLS 1.2/1.3
over native TCP/DNS/random/clock grants, with custom CA bundles, DNS identity
verification, deadlines and short transfers. `git.internal` may need its private CA
supplied under that explicit trust policy. Do not disable verification or add
fallback plaintext/ambient network authority. Pyxis reaches `git.internal` through
the planned [read-only LAN gateway](source-builds.md#reaching-the-sources), which
forwards only smart HTTP discovery and `git-upload-pack`.

The [FILE snapshot provider](../userland/http-fetch.md) sends GET only, without
custom request headers or body, allows at most 16 MiB across an HTTP open chain,
and supports bounded GET redirect chains. It can read
`info/refs?service=git-upload-pack` but **cannot perform smart fetch**, which
needs discovery GET and `git-upload-pack` POST, the Git content types, pkt-line
framing, negotiation/sideband and a potentially large streamed pack. HTTP/1.1
suffices. A stream or native smart subtransport must reuse verified TLS and add
HTTP framing, bounded pack writes and one owned deadline; it justifies no kernel
Git or HTTP mechanism. See the
[upstream wire specification](https://git-scm.com/docs/gitprotocol-http).

Libgit2 1.9.3's HTTP transport uses legacy v0 with no `Git-Protocol` negotiation
header. Depth requires advertised `shallow`; explicit OID fetch requires
`allow-tip-sha1-in-want` or `allow-reachable-sha1-in-want`, so fetching an
arbitrary hidden commit is not guaranteed. An advertised named ref can be a
transport hint, but a moved ref must never replace the recipe pin; an older commit
may require more history or an owner-published reference.

**Measured on the Linux host:** anonymous discovery from
`https://git.internal/PyxisOS/pyxis-os.git` and the existing Kilo mirror advertised
`shallow`, both `allow-*-sha1-in-want`, sideband and `object-format=sha1`. Host Git
2.56.0 with `protocol.version=0` fetched Pyxis commit
`26770a0cb95afb4fcc7b0aa6a023565becf99ef7` at depth 1 and verified FETCH_HEAD. This
establishes that server route for one pin; it is not a libgit2 transaction, an
on-Pyxis TLS/network result, or a promise for all mirrors.

## Smart HTTP without libcurl

**Fact:** Git's [remote-helper protocol](https://git-scm.com/docs/gitremote-helpers)
is a replacement seam for git-remote-http/https. Existing remote-curl uses
discovery GET, request headers and POST to upload-pack/receive-pack, streaming
callbacks and child pipes (see
[`remote-curl.c:901`](https://github.com/git/git/blob/v2.53.0/remote-curl.c#L901),
[`http.c`](https://github.com/git/git/blob/v2.53.0/http.c), the
[Git HTTP specification](https://git-scm.com/docs/gitprotocol-http/2.53.0)). No
libcurl port exists; zlib and Mbed TLS alone do not supply it.

**Shared dependency, moved by the owner 2026-10-11; not implemented:**
[native HTTP requests](native-http-requests.md) owns the engine, accepted prepared
transaction, request-scoped GET/POST headers, credentials and generic adapters
(formerly Claude tasks 1a-1c and this document's HTTP task 4). The separate
body/control/response grants, freeze-before-submit, one-shot commit, unknown
remote outcome/no replay and request-private credential rules remain accepted. Git
consumes those operations and supplies no second transaction interface or
application-side HTTP/TLS client. The
[scheme write mapping](userspace-scheme-providers.md#future-writes-and-shell-operations)
and [prepared handoff](userspace-scheme-providers.md#prepared-requests-and-shell-handoff)
distinguish GET (`cat`/`"r"`), staged POST (`"w"`) and staged POST then response
reading (`"w+"`); commit is explicit, flush/close never submit, and body write
authority alone cannot commit. The Git helper retains control and routes
directional child pipes into each prepared body and back from its response;
children receive no request controls or credentials.

| Git exchange | Mapping onto the accepted scheme/prepared-request design |
| --- | --- |
| Discovery | GET `info/refs?service=git-upload-pack` or `git-receive-pack` through the read path (not PREPARE's POST); request-scoped `Git-Protocol` is carried on the discovery GET when selecting v2 |
| Each upload-pack RPC | A fresh PREPARE for `/git-upload-pack` with Git headers and one deadline; stage the current RPC body, commit once, read/validate the response through its separate READ grant and status. Each v2 RPC prepares a new transaction; repeated commit never sends the next RPC |
| Each receive-pack RPC | The same lifecycle for `/git-receive-pack` with explicit submission/remote-write authority and the pack body; failed/unknown submission cannot be replayed automatically |

Finishing a body means finishing **that RPC's staging**, not waiting for a Git
child to exit. Fetch-pack/send-pack stay alive awaiting the response and then
generate another RPC; the helper uses Git's framing to delimit each request,
commits it, forwards its response and retains the child across exchanges
(`remote-curl.c:1122-1128`, :1514-1528). Waiting for child exit would deadlock.

Git-specific extensions to the shared design:

| Extension | Fit and scope |
| --- | --- |
| Git request headers | `Git-Protocol` (including on discovery GET), Git `Content-Type` values (`application/x-git-upload-pack-request`, `...receive-pack-request`) and Accept values fit validated request-scoped headers; framing headers remain provider-owned |
| Response media type | Expose response Content-Type among selected response headers so the helper validates advertisements and results; response metadata, not readable request headers |
| Helper credential path | Accepted interim arrangement: trusted native preparation receives only the selected credential FILE, privately reads it and attaches the header to that request. No argv/env/log/header readback or ambient credential store. The owner accepts this for now; anonymous read-only gateway fetch comes first |
| Large push bodies | Consume the [shared HTTP staging contract](native-http-requests.md#accepted-owner-decisions) for pack-sized bodies; freeze each RPC before submission; production after submission is outside the first shared profile |

The eventual credential path is the parked
[credential store as a capability service](credentials-and-biometrics.md#credential-store-as-a-capability-service),
which already names SSH keys and Git commit signing as first consumers; it is a
later dependency, not a task here. **Owner direction, 2026-10-11:** no artificial
response-size cap; the shared milestone owns streamed GET/POST with explicit final
completion and large staged bodies (replacing the complete-only snapshot and
response-grant timing; not implemented). Git must not publish successful refs from
an incomplete pack, and must finish each RPC's body independently of child exit.

**Implemented boundary:** [`httpfs`](../../userspace/httpfs/main.c) rejects WRITE;
[`libhttp`](../../userspace/libhttp/fetch.c) sends GET and owns whole responses;
the [provider ABI](../../include/abi/provider.h) has no prepared exchange. Bounded
GET redirects, verified TLS and explicit trust/network authority are documented in
[HTTP fetch](../userland/http-fetch.md) and [HTTPS](../userland/https.md).
Prepared POST, its controls and generic adapters are shared prerequisites, not
implemented. The helper uses the provider; TLS stays there.

Replacing only `http.c` is insufficient: `http.h` exposes curl types and
`remote-curl.c` sets curl options/callbacks directly. Replacing both layers could
preserve more upstream negotiation behavior but would still consume the shared
provider contract rather than supply a fake curl ABI. A libcurl port preserves
HTTP/auth/proxy features at the cost of a separate platform/TLS port: `http.c:1656-1700`
uses `curl_multi_fdset`/select, and native libc has no general socket/poll bridge.
Mbed TLS 4.1.1 integration is unprobed for curl; no curl build or performance
comparison ran.

The accepted [LAN gateway](source-builds.md#reaching-the-sources) exposes discovery
and upload-pack only. Clone/fetch can use that read-only profile; push needs
separate receive-pack authorization, credentials and uncertain-write policy. Full
transport eventually needs v0 compatibility, authenticated fetch, push and
explicit proxy behavior; none is implicitly assigned. These are Git helper policy
and coverage, not provider interfaces. Git 2.53 falls back to v0 for receive-pack
(`remote-curl.c:494-501`), so a v2-only helper cannot implement complete push.
Native launcher/pipes and non-fork sideband remain prerequisites irrespective of
the HTTP consumer route.

## Proposed CLI delivery tasks

Ordered CLI tasks for separate owner assignment. Shared libc and native HTTP
prerequisites can also serve the accepted libgit2 route, which retains its status
and should not wait for a complete CLI. Ownership follows
[SDK/repository boundaries](../development/sdk-and-repositories.md): general libc
in userland, Git recipe/adaptation in ports, provider/helper support in userland,
parent pins only after dependencies are published. No compiler rebuild is implied.

1. **Close the real serial core build and path/clock/identity boundary.** Pin the
   reviewed official source in a separately assigned ports task, keep object hash
   verification, audit actual source consumers and use honest native paths,
   author identity and clock/random authority. Omit unused Unix features; adapt
   shared libc only where native behavior exists. Bundle native code/resources
   within measured loader/stack/heap limits (no size/admission result exists yet).
   **After this task the owner can:** run Git version and object plumbing against
   an explicit private local object store, without a shell or network claim.
2. **Deliver the offline worktree/index and publication profile.** Content-based
   freshness without unsupported assume-valid/skip-worktree/sparse shortcuts,
   modes/unsupported-tree policy, lock exclusion, native file/directory sync,
   explicit repository-config trust and external-feature refusals. Later
   authorized qualification must cover same-size edits, optional metadata absent,
   clean status, collisions/denials and publication uncertainty.
   **After this task the owner can:** init, add, commit -m/-F, status, no-pager
   log, internal diff and serial checkout for supported ordinary local repositories.
3. **Consume shared command execution; adapt Git run-command/sideband.** Generic
   stream/cwd/env/result/stop/cleanup work lives in
   [native command execution](../userland/native-jobs.md). Git retains its
   executable bundle/grant policy, helper flow and non-fork callback/sideband
   adaptation; remove shell/fork fallback without a kernel POSIX process model.
   Descendant-enabled subcommands and remote helpers require
   [nested execution groups](nested-execution-groups.md), a parked required
   direction the command milestone's direct-child profile does not deliver.
   Qualify Git-specific short transfers, EOF, failures and deadlock bounds.
   **After this task the owner can:** use supported internal Git child commands
   and pack ingestion with native lifecycle/error reporting, before network use.
4. **Shared dependency: native HTTP requests (moved, not completed).** Require
   [that milestone](native-http-requests.md), including request-scoped GET
   headers, streamed completion and large staged bodies; Git owns no duplicate
   HTTP task.
   **After that milestone the owner can:** use reusable HTTP through shell/Lua
   without first delivering a Git helper.
5. **Deliver the Git remote helper and read-only clone/fetch slice.** Discovery,
   v2 and explicit v0/fallback profile, serial pack ingestion, completeness/hash
   checks and bounded staging over the owner gateway; refuse unsupported auth,
   proxies and writable services explicitly.
   **After this task the owner can:** clone/fetch supported repositories over
   verified HTTPS and use the offline commands on supported trees.
6. **Add authenticated receive-pack and wider CLI execution deliberately.**
   Credentials, pack-objects/send-pack/sideband, remote-write uncertainty; then
   separately supported native editors/pagers/hooks/filters and other full-CLI
   facilities. No shell-string success where only native launch is implemented.
   **After this task the owner can:** push to explicitly authorized repositories
   and use each newly qualified native execution feature; remaining refusals stay
   visible rather than being described as a complete unrestricted Git port.
7. **Optional parallel pack/index after gates 5-8.** Audit the actual pthread
   subset and resource concurrency; retain a measured serial baseline.
   **After this task the owner can:** choose qualified threaded pack/index work;
   threads are not a prerequisite for tasks 1-6's serial architecture.

## Owner decisions, proposed defaults

Proposals, not accepted implementation:

1. **First CLI scope:** default to the enforced offline serial profile, explicit
   no external hooks/filters/pager/editor/signing, with commit -m/-F. The complete
   CLI remains eventual; restricted libgit2 source fetch remains the accepted
   first acquisition route.
2. **Worktree fidelity:** default to real content comparisons, retained tracked
   100755 metadata without native execute permissions, new files 100644, initial
   symlink/gitlink refusal, and honest native sync/publication errors. No synthetic
   ownership/cache fields, unsupported index shortcuts or private-mode privacy
   promises.

Response delivery is [HTTP decision 1](native-http-requests.md#accepted-owner-decisions),
accepted 2026-10-11 and delivered by the shared provider independently of this CLI.

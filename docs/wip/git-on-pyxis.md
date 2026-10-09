# Git source acquisition on Pyxis

Investigation, accepted direction and task status, 2026-10-09. The original
compile evidence below records the investigation snapshot. The owner accepted
the three decisions and assigned only the first libc task; later work remains
unassigned. Its [status](#first-task-status-and-accepted-decisions) supersedes the
historical O_RDWR/O_EXCL gap entries.
This supports [source builds](source-builds.md) after hosted Clang. The question
is obtaining pinned source, separately from providing a developer's Git CLI.

**Accepted route:** a native source-fetch command using a restricted
libgit2 port, with a bare object store and explicit tree export. It removes the
process/shell dependency and avoids a working-tree index. It still needs libc,
path, object-store and transport work; neither route built against the SDK in
the recorded probes. The owner also wants a complete Git CLI eventually, with
its larger process and worktree contracts handled separately.

## Evidence and probe boundary

The work began in a fresh worktree at Pyxis
`26770a0cb95afb4fcc7b0aa6a023565becf99ef7`, with these exact pins:

| Input | Revision |
| --- | --- |
| userland | `fe6f3efb0847cc500a68cb782304eab781bdff58` |
| ports | `a642f07382e14bd233ac1be2b6a814e95c32d835` |
| filesystem | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| compiler fork | `49e2c1a1518b3e4687b52ceb6001069c1b6d261e` |

Measured inputs were Git 2.53.0 and libgit2 1.9.3, selected releases rather than
a claim about the newest upstream release. A fresh `make -j16 sdk` succeeded
with Clang/LLD 23.1.3 in the existing
`pyxis-builder:pyxis-llvm23.1.3-49e2c1a` container, digest
`sha256:50bfb587f2e73d6478ae1c91e86984ccb8223baa4367fe27fa227808e87bc038`.
The pinned zlib 1.3.2 and Mbed TLS 4.1.1 recipes also built successfully against
that SDK. No target build used host libc or replacement SDK declarations.

The unmerged [probe snapshot](https://git.internal/PyxisOS/pyxis-os/src/commit/e9c427cb/probe)
retains commands, the SDK manifest, interface matrix and compiler diagnostics.
Its branch is `probe/git-sdk-gaps`, commit `e9c427cb`. The SDK manifest marks
the parent modified because of untracked probe files;
tracked SDK inputs and all initialized submodules were unchanged. It contains
no functioning Git/libgit2 port. The docs branch contains no probe code.

During investigation main advanced to `c2407b60` for audio integration, pinning
userland `68cff906013a77201299687604cf0e3b02f8a06f`. Inspection of that exact
userland delta found no libc changes; it adds audio to the runtime. The measured
builds remain the initial snapshot above, not a claim of a rebuild at the later head.

Both release archives came from the owner's **existing** `raw-github` cache:

| Source/cache entry | Observed SHA-256 |
| --- | --- |
| `https://repo.internal/repository/raw-github/git/git/archive/refs/tags/v2.53.0.tar.gz` | `0492bcd8a9fc99bedca75dbec7c8c15b24dd1501c82d4518294ee0850f91f219` |
| `https://repo.internal/repository/raw-github/libgit2/libgit2/archive/refs/tags/v1.9.3.tar.gz` | `d532172d7ab24d2a25944e2434212d63ee85f3650e97b5f7579e7f201a78ad64` |

These identify the actual probe bytes; they are not release-signature checks.
Production recipes need an explicitly reviewed checksum or commit pin. The
corresponding upstream URLs are `https://github.com/git/git/archive/refs/tags/v2.53.0.tar.gz`
and `https://github.com/libgit2/libgit2/archive/refs/tags/v1.9.3.tar.gz`.
Git is GPL-2.0; libgit2 is GPL-2.0 with its linking exception. Source was not
downloaded directly from upstream. SSH queries for `mirrors/git` and
`mirrors/libgit2` returned “Cannot find repository”; those repositories are not
prerequisites if a future recipe uses the existing checksum-pinned cache entries.
After accepting the direction, the owner created `git.internal/mirrors/libgit2`,
`mirrors/git` and `mirrors/curl`; anonymous HTTPS `ls-remote ... HEAD` succeeded
for all three during task 1. The owner also reports a `raw-gitlab` cache on
`repo.internal`. This task adds no recipe, follows no mirror branch and fetches
no new Git/libgit2/curl source.

### Measured compilation

| Probe | Result |
| --- | --- |
| SDK control executable and available libc symbol links | Passed; symbol availability only |
| Git `builtin/clone.c`, and `make -k -j16 git` with threads, mmap, gettext, iconv, Perl/Python/Tcl, OpenSSL, curl and expat disabled | Failed at `compat/posix.h:110`: missing `sys/time.h` |
| libgit2 static CMake build, tests/CLI/threads/SSH/HTTPS/nanosecond stat disabled, SDK zlib, system regex, bundled HTTP parser | Configure passed; compilation failed at `src/util/git2_util.h:101`: missing `arpa/inet.h` |
| libgit2 configure with `USE_HTTPS=mbedTLS` and actual port prefix | Failed: stock finder does not find the required crypto library |
| Selected upstream translation units with empty absent-header stand-ins | Failed; exposed missing functions, constants and stat members below |
| libgit2 TLS source with those stand-ins and the real port configuration | Failed at `streams/mbedtls.c:37`: missing `mbedtls/entropy.h` |

The no-curl Git probe is a **core build probe**, not an HTTPS candidate. Git's
`NO_CURL` removes HTTPS. Empty diagnostic headers declare and implement nothing;
they only let the compiler reveal later dependencies. Those results are not a
successful cross-build. The investigation ran no target executable, QEMU boot,
timing measurement, filesystem durability experiment or on-Pyxis clone. Task 1's
later runtime qualification is recorded separately below.

Independent interface probes compile real SDK headers and try target links.
They confirm `open/read/write/close`, `lseek/ftruncate/fsync`, `stat/fstat`,
`mkdir/rename/unlink/mkstemp`, `opendir/readdir/closedir`, `getenv`, `fileno`,
`fflush`, `strdup/strndup/strnlen/strcasecmp/strcasestr`, `regcomp`, `qsort`,
`time/timespec_get`, `gmtime[_r]/localtime[_r]` and `strftime` are available.
This does not establish the semantics needed by either consumer.

## Route 1: a useful Git CLI

`clone`, `fetch`, `checkout`, `status` and `log` are C builtins. Their basic
nonrecursive use does not require a POSIX shell script. A useful HTTPS slice
still requires native launcher integration and more than the main executable:

1. Git launches the libcurl-linked `remote-https` helper with pipes
   ([transport-helper.c](https://github.com/git/git/blob/v2.53.0/transport-helper.c#L142)).
   No libcurl port exists at the inspected ports pin. It needs its own SDK/network/TLS
   investigation or a replacement native Git HTTP helper; zlib and Mbed TLS alone
   do not supply libcurl.
2. Protocol v2 uses `stateless-connect` and fetch-pack logic within the main Git
   process. Legacy fallback launches `fetch-pack`
   ([remote-curl.c](https://github.com/git/git/blob/v2.53.0/remote-curl.c#L1191)).
   Pack ingestion launches `index-pack` or `unpack-objects`; connectivity launches
   `rev-list` ([fetch-pack.c](https://github.com/git/git/blob/v2.53.0/fetch-pack.c#L961),
   [connected.c](https://github.com/git/git/blob/v2.53.0/connected.c#L102)). These are
   builtin subcommands but execute as children along these paths.
3. `NO_PTHREADS` converts asynchronous sideband work to `fork`; it does not remove
   concurrency ([run-command.c](https://github.com/git/git/blob/v2.53.0/run-command.c#L1194)).
   Launch, stream delegation, wait, failure and cancellation must be redesigned
   around Pyxis processes. There is no justification for kernel `fork`, POSIX
   signals or pretend successful child execution.

An explicit restricted CLI could turn off pagers (`--no-pager`), auto-maintenance,
parallel checkout (`checkout.workers=1`), fsmonitor and hooks, and exclude SSH,
credential helpers, recursive submodules, external diff/textconv, aliases that
run shell commands and filters. Plain `log` remains useful. Required filters
must be refused, not silently skipped; repository config and attributes cannot
silently re-enable unsupported execution. Clone/checkout normally invoke
`post-checkout`; their no-hook policy must be deliberate. Shell-based commands
outside this slice need not be installed. Sample hooks/templates are optional.

### Concrete Git gaps

The following missing declarations were independently probed against the SDK;
their use is visible in the selected upstream sources:

| Area | Missing surface / behavior |
| --- | --- |
| Processes and streams | `fork`, `execve/execvp`, `waitpid`, `pipe`, `dup/dup2`, `fcntl`, `fdopen`, signal functions/types/constants; `getpid/getppid`, mutable child environment (`setenv/unsetenv/putenv`). Replace the consumer's process machinery with native launcher/stream/environment contracts. |
| Paths and traversal | `chdir`, `getcwd`, `realpath`, `lstat`, `access`, `rmdir`, `readlink`, `symlink`, `link`; absolute-path recognition and colon-separated PATH assume Unix. `scheme://` is not recognized as an absolute root by Git. |
| Files and index | `O_RDWR/O_EXCL/O_APPEND`, permission constants, `chmod/fchmod`, stat identity/time/owner fields; exclusive tempfile modes include 0600, which public Pyxis open refuses. Working-tree status needs an honest content comparison or real metadata contract. |
| Other support | `gettimeofday`, `mktime`, `strtok_r`, `strerror_r`, `strtoumax`, `umask`, owner lookup; missing `EINTR`, `ENOTDIR`, `ENOEXEC` appear in deeper compilation. UID ownership checks have no native counterpart and must be replaced by the actual capability model. |
| Optional/fallback integration | `mmap/munmap`, sockets/DNS, `poll/select`; `NO_MMAP` has buffered code, but helper I/O still needs native waiting. SDK regex lacks Git's `REG_STARTEND`; Git's `NO_REGEX=NeedsStartEnd` selects its bundled regex, rather than making regex unnecessary. |

Header probes fail for `sys/time.h`, `signal.h`, `sys/wait.h`, `sys/resource.h`,
`poll.h`, `sys/poll.h`, `sys/select.h`, `sys/socket.h`, `sys/un.h`,
`netinet/in.h`, `netinet/tcp.h`, `arpa/inet.h`, `netdb.h`, `pwd.h`, `grp.h`,
`utime.h`, `sys/param.h`, `sys/utsname.h`, `sys/ioctl.h`, `sys/statvfs.h`,
`syslog.h`, `termios.h`, `libgen.h`, `iconv.h`, `sys/mman.h`, `pthread.h`,
`sched.h`, `sys/random.h` and `fnmatch.h`. Some are broad unconditional includes
or disabled-feature dependencies; this is not a requirement to implement them
all. Existing `locale.h`, `strings.h`, `regex.h` and minimal `wchar.h` compile.

`core.checkStat=minimal` still compares mtime and size; `core.trustctime=false`
and `core.filemode=false` do not solve missing metadata. Zero or constant
timestamps can hide same-size edits. Supporting reliable `status` is materially
more work than exporting a pinned tree.

## Route 2: a native fetch command using libgit2

Libgit2 performs negotiation, pack processing and object access in process.
`USE_THREADS=OFF` and `USE_SSH=OFF` genuinely remove those runtime dependencies.
Its bundled HTTP parser and collision-detecting SHA-1 backend configure without
new libraries; zlib's exported core provides the compression interface. A native
command can fetch into a bare repository, resolve the **exact full pinned commit**,
walk its tree, and write raw blobs through libc. It need not create a working-tree
index, run hooks/filters, or offer `status/log/commit/push`.

This is a smaller route, not an unmodified libgit2 build:

| Area | Measured/inspected gaps and proposed adaptation |
| --- | --- |
| Filesystem core | Same missing `O_RDWR/O_EXCL`, `lstat/access`, `pread/pwrite`, `rmdir`, path/cwd support and directory sync bridge. Deeper `indexer.c` diagnostics identify positioned writes and endian functions (`htonl/ntohl`, with `htons/ntohs` elsewhere). Standard support belongs in libc. |
| Mapping | No public `mmap`; libgit2's `NO_MMAP` fallback reads into owned memory and explicitly rejects writable mappings. The non-Windows pack indexer uses `pwrite`. This avoids shared-file mapping for the proposed slice, but needs bounded allocation and real positioned I/O. |
| Metadata even without checkout | ODB alternate deduplication reads `st_ino`; pack/config/file-buffer caches read mtime/inode; discovery reads device identity. Bare fetch alone does not eliminate these compile/runtime assumptions. A single fresh store with no alternates/discovery and explicit cache reloads can be adapted without fabricated stat fields; that adaptation remains unbuilt. |
| Worktree features | Building all sources still compiles `index.c` and iterators, which read stat ctime/mtime/ino/uid/gid. Runtime avoidance is not a compile fix. Restrict or adapt that source closure explicitly; reject unsupported APIs. `checkout` with suppressed index writes still has stat/filter/mode assumptions. |
| Unix support | Shared headers still include networking headers with HTTPS off. Missing `gettimeofday`, `struct timeval`, `ino_t`, `lstat`, `readlink/link/symlink/chmod`, `getcwd`, `utimes`, `EINTR/ENOTDIR` appear in selected-source diagnostics. Avoid omitted feature code or implement standard behavior where native objects support it. |
| Stock networking | Socket/connect/send/recv, getaddrinfo/freeaddrinfo/gai_strerror, inet_pton and poll/select are absent from SDK probes. Native TCP/DNS exists, but is not a BSD socket API. Use the library's stream/subtransport extension for platform integration; ordinary missing libc calls remain shared libc work. |
| TLS | Stock `FindmbedTLS.cmake` requires `libmbedcrypto`; Pyxis exports `libtfpsacrypto`. Source includes `mbedtls/entropy.h` and `mbedtls/ctr_drbg.h`, absent in this 4.1.1 export, and seeds its own global entropy/DRBG. Renaming a library cannot repair the backend or provide Pyxis clock/random/trust authority. Prefer an adapter to existing native `libtls`, outside the base SDK. |

Relevant inspected libgit2 locations are
[indexer.c](https://github.com/libgit2/libgit2/blob/v1.9.3/src/libgit2/indexer.c#L683),
[NO_MMAP fallback](https://github.com/libgit2/libgit2/blob/v1.9.3/src/util/posix.c#L227),
[ODB identity](https://github.com/libgit2/libgit2/blob/v1.9.3/src/libgit2/odb.c#L675),
[file stamps](https://github.com/libgit2/libgit2/blob/v1.9.3/src/util/futils.c#L1149),
and [TLS backend](https://github.com/libgit2/libgit2/blob/v1.9.3/src/libgit2/streams/mbedtls.c#L36).

## Filesystem contract shared by both routes

These are **source-inspected**, not newly qualified runtime behavior. The
[libc policy](../userland/libc-portability.md), [paths](../userland/paths.md),
[native writer](../devices/filesystem-native-adapter.md) and exact pinned headers
are the authorities.

| Requirement | Current Pyxis contract and consequence |
| --- | --- |
| stat ino/dev/ctime/mtime/mode | `userspace/libc/include/sys/stat.h` has only type-only `st_mode` and `st_size`. NPFS stores creation/modification timestamps, but FILE ABI does not export identity/time. Creation time is not POSIX ctime, and timestamps can repeat/backtrack. Do not synthesize cache-validating fields. |
| Exclusive lock files | Native `path_create_file` and `mkstemp` perform exclusive creation. Task 1 exposes that route through public `O_CREAT\|O_EXCL`, with real EEXIST after authority checks and existing failure unwinding. Grants, not mode bits, establish authority. |
| Atomic file publication | Native same-volume file rename can replace a file atomically, including across directories; public `rename` uses that operation. Directory rename is unsupported. Cross-volume failure maps to ENOTSUP, not EXDEV. Held victim handles survive replacement. |
| Symlinks and executable bits | NPFS has regular files/directories only. Host enumeration may report symlinks but lookup refuses them. No chmod/symlink interface or stored execute bit exists. Executable loading requires READ authority; Git tree mode and native launch authority are different concepts. |
| File and directory fsync | Public `fsync` only accepts writable file descriptors. Native directory sync exists and syncs the current pool, but no directory-fd bridge exposes it. Git/libgit2 open a directory read-only then fsync it, which cannot work through today's descriptor layer. Close is not durability. |
| Object-store ordering | Lock/temp file data must complete and sync before durable namespace publication. Rename to an absent name does not flush cached file contents by itself. Do not infer Git-style crash recovery merely from atomic rename. Disposable RAM stores avoid persistent-cache durability, not creation/publication correctness. |
| Directory enumeration | `opendir/readdir/closedir` exist; no d_ino, dirfd, rewinddir or readdir_r. Enumeration is live, omits dot entries and may fail EAGAIN after mutation. A fetch tool should avoid shared writers and handle invalidation explicitly. |

Native UTF-8, case-sensitive components are limited to 255 bytes. Tree export
must preflight names, reject traversal/invalid components and collisions, and
stay within the granted destination. Native paths reject leading `/`, while both
upstreams recognize Unix roots rather than `scheme://`. A capability-correct
path adaptation is required even if every missing declaration were supplied.

## HTTPS and smart HTTP

[Existing HTTPS](../userland/https.md) provides certificate-verified TLS 1.2/1.3
over native TCP/DNS/random/clock grants. It already supports custom CA bundles,
DNS identity verification, deadlines and short transfers. `git.internal` may
need its private CA supplied under that same explicit trust policy. Do not
disable verification or add fallback plaintext/ambient network authority.

The [FILE snapshot provider](../userland/http-fetch.md) sends GET only, without
custom request headers or body; it buffers at most 16 MiB and does not follow
redirects. It can read `info/refs?service=git-upload-pack`, but **cannot perform
smart fetch**. Smart HTTP needs service discovery GET and `git-upload-pack` POST,
the Git content types, pkt-line framing, negotiation/sideband and a potentially
large streamed pack. HTTP/1.1 suffices; HTTP/2 is not required. A stream or native
smart subtransport must reuse verified TLS and add appropriate HTTP framing,
bounded pack writes and one owned deadline. It does not justify a kernel Git or
HTTP mechanism. See the [upstream wire specification](https://git-scm.com/docs/gitprotocol-http).

Libgit2 1.9.3's inspected HTTP transport uses legacy v0, with no `Git-Protocol`
v1/v2 negotiation header. Depth requires advertised `shallow`; explicit OID
fetch requires `allow-tip-sha1-in-want` or `allow-reachable-sha1-in-want`.
Fetching any arbitrary hidden commit is not guaranteed. An advertised named ref
can be a transport hint, but a moved ref must never replace the recipe pin.
An older commit may require more history or an owner-published reference.

**Measured on the Linux host**, anonymous discovery from
`https://git.internal/PyxisOS/pyxis-os.git` and the existing Kilo mirror advertised
`shallow`, `allow-tip-sha1-in-want`, `allow-reachable-sha1-in-want`, sideband and
`object-format=sha1`. Host Git 2.56.0 with `protocol.version=0` successfully fetched
Pyxis commit `26770a0cb95afb4fcc7b0aa6a023565becf99ef7` with depth 1 and verified
FETCH_HEAD. This establishes that server route for one pin; it is neither a
libgit2 transaction nor an on-Pyxis TLS/network result, nor a promise for all mirrors.

## First task status and accepted decisions

- [x] Task 1: public **`O_RDWR` and `O_CREAT|O_EXCL` in userland libc**, implemented
  and manually qualified; pending owner review and dependency merge.

[Userland PR #171](https://git.internal/PyxisOS/pyxis-userland/pulls/171) publishes
`ff278aec50adfaf6af8d8c15062084a8594642e3`. This integration pins that published
commit. Merge userland first, then the Pyxis gitlink/docs PR. No later task starts
as part of this delivery.

The two-file implementation reuses internal read/write descriptors and native
exclusive creation, retains the 0666-only policy, and rejects unsupported
flags/modes explicitly. O_EXCL without O_CREAT and combined O_WRONLY|O_RDWR fail
with EINVAL before varargs or path work. Capability authority, allocation
preflight and uncertain-close rules are unchanged. The ordinary image build and
interactive nested-KVM QEMU/GDB inspection qualified duplicate-name EEXIST
without truncation, seek/read/write on one descriptor and denied-create cleanup.
[Qualification details](../userland/libc-portability.md#readwrite-and-exclusive-create-qualification)
separate these observations from inspected invariants and unexercised storage/
failure cases. No kernel change, new test infrastructure or compiler rebuild.

That task unblocks a concrete shared dependency, not a functioning fetch tool.
Subsequent separately approved work would cover positioned I/O and remaining
libc support; restricted libgit2 source/path/cache closure; a native HTTPS
transport; then exact-OID verification and bounded tree export. Pack/index/blob
hash verification must remain enabled. Require the pin to resolve to a commit,
not an arbitrary object or substituted ref. Check completeness of its reachable
tree/blobs before reporting success. A Git OID verifies source identity relative
to the trusted recipe pin; it does not authenticate recipe authors or replace TLS.

The owner accepted these three defaults on 2026-10-09:

1. **Goal: pinned source acquisition via libgit2 first.** A full
   `clone/fetch/checkout/status/log` CLI remains the eventual goal, requiring its
   own native launcher and index-cache work.
2. **Tree profile: ordinary files/directories, with 100644/100755 Git
   blobs both materialized as native files and their original modes retained in
   the fetched tree.** Explicitly document that there is no POSIX execute bit.
   Reject symlinks and gitlinks before export; never dereference a symlink or
   silently omit a submodule. Adding native symlinks/metadata or resolving recipe
   submodules needs separate decisions. The ports runner already pins extra
   source repositories separately, but the complete recipe-source closure has
   not been audited for these entry kinds.
3. **Storage and publication: one fresh private `tmp://` store and
   destination, discard on failure; no shared persistent cache or overwrite.**
   Preflight the tree before writing, with an explicit caller-selected resource
   budget, and let the runner consume it only after successful verification/export.
   Do not promise an atomic directory switch: native directory rename is absent.
   Persistent caching and publishing installed binaries remain later contracts.

The current ports runner also uses `os.execute`, `io.popen`, `git init`,
`git apply`, checkout and rev-parse; a fetch command alone does not port that
runner, the patch step or build tools. Its integration must preserve trusted
recipes, pins, ordered patches and failure reporting without requiring a POSIX
shell. This proposal adds no successful fake operations.

Delivery follows [repository ownership](../development/sdk-and-repositories.md):
libc changes in userland, libgit2 recipes/patches in ports, a native command and
TLS adapter in userland, parent integration only after published dependencies.
The base SDK stays independent of TLS/libgit2. This task changes only the userland
pin and related docs in Pyxis. No new upstream source or compiler container is
needed. The original investigation probe branch stays unmerged; task-owned
qualification processes are stopped. Stop for owner review of task 1.

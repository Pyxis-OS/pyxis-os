# Git CLI on Pyxis

Owner-assigned investigation, 2026-10-11. **Fact** means measured compilation or
inspected source/contracts; **Inference** means a proposed native adaptation.
No Git executable ran on Pyxis. No port, source pin, ABI or compiler-container
change is delivered by this document. Implementation tasks below are proposed,
not assigned. Builder/staging requests preempt this investigation.

The accepted first source-acquisition route remains the restricted
[libgit2 fetch command](git-on-pyxis.md#task-status-and-accepted-decisions), with
a fresh private store and explicit regular-file tree export. Its three shared
libc prerequisites merged; the fetch command itself remains unbuilt. The owner
also wants a complete Git CLI eventually. This investigation separates a useful
offline CLI profile from subsequent native child execution and network transport.
Neither profile automatically ports the shell-based ports runner.

## Reproducible build boundary

**Fact, measured:** a fresh `make -j16 sdk` succeeded using the published builder,
without changing its compiler or the SDK declarations. Inputs:

| Input | Exact revision or identity |
| --- | --- |
| Pyxis current main | `78536b51abcce7beae8788f04bf95744600c1d91` |
| userspace | `df210f1a2ed9e55ab9a83b0d6318d1d6e14db1af` |
| ports | `df3210b04142295206d0b60850cbe5959dd2cffa` |
| filesystem | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |
| compiler fork / Clang and LLD | `49e2c1a1518b3e4687b52ceb6001069c1b6d261e` / 23.1.3 |
| builder | `git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a` |
| observed builder digest | `sha256:2d68864eb9c467ef2564d68b99c6ff17d585dc6524898d06c3ed19678db18067` |
| compiler-rt archive in SDK | SHA-256 `ee3101ef8e5de19a0db80001733998c673859eca081a7b239dc22ae8887d4f31` |
| official Git release | 2.53.0; annotated tag `a978e09acb5a3db0959fc757df17b27e8706d26b`, peeled commit `67ad42147a7acc2af6074753ebd03d904476118f` |

Git 2.53.0 is deliberately the same [official release](https://github.com/git/git/releases/tag/v2.53.0)
as the 2026-10-09 probe, so libc progress can be compared without changing Git.
It is not a claim about the latest release. The existing owner cache supplied
`https://repo.internal/repository/raw-github/git/git/archive/refs/tags/v2.53.0.tar.gz`;
the archive SHA-256 matched
`0492bcd8a9fc99bedca75dbec7c8c15b24dd1501c82d4518294ee0850f91f219`.
The owner mirror `https://git.internal/mirrors/git.git` resolved the tag and
peeled commit above. This verifies observed source bytes/tag resolution, not a
local cryptographic release-signature check. No direct upstream download or
fallback was used. Git is GPL-2.0.

Commands ran on Linux in temporary detached-source/probe directories. Repository
and linked-worktree paths were mounted unchanged into the published container;
`--pull=never --userns=keep-id` reused it. The main SDK source was clean. The
following reproduces the measured compiler invocation after exporting the SDK:

```sh
make -j16 sdk
SDK=/tmp/pyxis-git-cli-worktree/build/sdk
GIT_SOURCE=/tmp/pyxis-git-cli-upstream/git-2.53.0
ZLIB_WORK=/tmp/pyxis-git-cli-upstream/zlib
lua ports/build.lua zlib --sdk "$SDK" --work "$ZLIB_WORK"
CC=x86_64-unknown-pyxis-clang
CPPFLAGS="-I. -Icompat/poll -Icompat/regex --sysroot=$SDK/sysroot -nostdinc \
  -isystem $($CC -print-file-name=include) -I$SDK/sysroot/usr/include \
  -I$ZLIB_WORK/stage/dev/include"
CFLAGS="-std=gnu23 -O2 -g3 -ffreestanding -fno-stack-protector \
  -fno-pic -fno-pie -mno-red-zone -march=x86-64 -ferror-limit=0"
make -C "$GIT_SOURCE" --output-sync=target -k -j16 git \
  uname_S=Pyxis uname_M=x86_64 uname_O=Pyxis \
  CC="$CC" AR=llvm-ar CPPFLAGS="$CPPFLAGS" CFLAGS="$CFLAGS" \
  LDFLAGS="--sysroot=$SDK/sysroot -static -no-pie -L$ZLIB_WORK/stage/dev/lib" \
  NO_PTHREADS=YesPlease NO_MMAP=YesPlease NO_CURL=YesPlease \
  NO_EXPAT=YesPlease NO_OPENSSL=YesPlease NO_GETTEXT=YesPlease \
  NO_ICONV=YesPlease NO_PERL=YesPlease NO_PYTHON=YesPlease NO_TCLTK=YesPlease \
  NO_NSEC=YesPlease NO_REGEX=NeedsStartEnd HAVE_STRINGS_H=YesPlease \
  NO_STRTOUMAX=YesPlease NO_UNIX_SOCKETS=YesPlease NO_POLL=YesPlease V=1
```

The zlib recipe, unchanged at the pinned ports revision, built 1.3.2 from its
owner cache and staged actual headers/static library. Passing `uname_S=Pyxis`
avoids selecting Linux's `/proc`, getrandom and other host-platform assumptions.
Project and bundled regex/poll headers must precede SDK headers: Git also has a
`path.h`, and its GNU regex fallback needs its own `regex.h`. The Pyxis driver
uses the target sysroot/startup/runtime and P1F linker; no host libc was linked.
The build failed before a Git link, so no complete link closure is established.
`NO_CURL` removes HTTP helpers: this is a core probe, not an HTTPS build.

| Probe, measured | Result and limit |
| --- | --- |
| Fresh SDK and pinned zlib | Passed; shared build prerequisites only |
| Unmodified Git `make -k -j16 git` with options above | Exit 2; target translation units fail at unconditional `compat/posix.h:110`, absent `sys/time.h` |
| Real-SDK control executable taking function addresses and referencing constants/stat members | Target P1F link passed for `realpath`, `getcwd/chdir`, `setenv/unsetenv`, `fdopen`, `dup/dup2`, `setvbuf`, `stat/lstat/fstat`, `open`, `pread/pwrite`, `rmdir/access`, byte-order functions and `mkstemp`; `O_APPEND`, ELOOP, ENAMETOOLONG and dev/ino/mtim/valid fields compiled |
| Independent real-header declaration probes | `strtok_r` exists; `strtoumax`, `strerror_r`, `gettimeofday`, `utimes`, `chmod`, `readlink`, `symlink`, `link`, `fork`, `execvp`, `waitpid`, POSIX `pipe/fcntl/poll` undeclared in the inspected public headers |
| Diagnostic-only empty absent headers, selected `statinfo`, `read-cache`, `entry`, `run-command`, `commit`, `clone`, `index-pack` objects | Exit 2; time/ownership/stat/process failures remain. No replacement declarations, implementations or SDK edits; no object/link success is asserted |

For the last probe, add a scratch include directory after real SDK/dependency
headers, containing empty files only for absent includes in `compat/posix.h` and
`git-compat-util.h`; substitute those seven `.o` targets for `git`. This reveals
later diagnostics, not runtime support. Logs/scripts remain unmerged local
probe evidence, outside the docs branch. No QEMU boot, on-target repository
operation, transport transaction, durability or timing check ran.

## What the old gap tables mean today

**Fact:** [libc portability](../userland/libc-portability.md),
[process state](../userland/process-state.md), [paths](../userland/paths.md) and
[stdio](../userland/stdio.md) now describe these delivered interfaces. A missing
header in Git's broad Unix includes is not an instruction to add every POSIX API.

| Area | Current evidence | Git consequence / classification |
| --- | --- | --- |
| Working path, environment, realpath | Real `chdir/getcwd`, copied env mutation, bounded proved scheme path | Old name gaps closed. Git absolute-path detection, discovery/root traversal, PATH/exec resolution and config paths still need a native scheme/capability backend, not colon-split Unix PATH. |
| Descriptor/FILE association | Real `fdopen`, shared-object `dup/dup2`, bounded buffering and append | Old name gaps closed. These do not inherit descriptors across spawn or create POSIX pipe/select/fcntl semantics. Flush FILE output before delegation; preserve short writes and errors. |
| Metadata | Type/size plus independently valid dev/ino/mtime | Old absent-identity/mtime table superseded; ctime, owners, permission bits and persisted-cache guarantees remain unavailable. See below. |
| Errors | ENOTDIR, ELOOP, ENAMETOOLONG and EINTR vocabulary supplied | EINTR exists as compatibility vocabulary; native calls do not report signal interruption. Do not infer signals from the constant. |
| Time | Git `compat/posix.h:187–193`, `date.c:134`, `trace.c:114` use timeval/gettimeofday | Genuine consumer boundary. Adapt to real native wall/monotonic clock authority or implement a proven libc extension; no fake clock values. |
| Integer parsing / regex | `NO_STRTOUMAX` selects Git's real conversion fallback; `NO_REGEX` selects its GNU regex implementation | These are available source choices, not mandatory new libc dependencies. SDK TRE lacks REG_STARTEND. `strerror_r` absence alone is not an identified core blocker; `strtok_r` is delivered. |
| Mapping / polling | `NO_MMAP` selects owned-buffer fallback; `NO_POLL` selects `compat/poll` | Owned buffers avoid mmap, with memory costs. Poll fallback still calls select (`compat/poll/poll.c:402`), so it does not supply native waiting. Adapt actual pipe consumers. |
| UID/modes/process IDs | Common Git headers inspect `st_uid`, get[e]uid and pid_t; `ident.c:46` consults getpwuid; tempfiles/locks use getpid | Native authority, explicit author/committer identity and random names need deliberate adaptations. No fabricated UID ownership, permissive safe.directory bypass, umask, PID or successful chmod. |
| Optional Unix services | Socket, Unix socket, terminal, syslog, resource, passwd and other headers appear broadly | Exclude genuinely omitted features or adapt specific consumers. Full BSD sockets, terminal emulation, shell, gettext, OpenSSL and libcurl are not prerequisites for the proposed offline profile. |

The diagnostic stat errors include `st_mtime`/`st_ctime` access and UID/GID;
native `st_mtim` exists but is optional. Renaming a field or disabling nanoseconds
does not establish Git's freshness assumptions. General ISO C/proven libc
extensions belong in userland libc; Git-specific authority/process/index policy
belongs in the port. No standard API should return invented success.

## Process model and a first CLI slice

**Fact:** [`run-command.c:start_command`](https://github.com/git/git/blob/v2.53.0/run-command.c#L677)
creates pipes, forks, redirects descriptors, changes child cwd and execs.
`prepare_cmd:411` can add a shell fallback even without `use_shell`.
`wait_or_whine:558` expects Unix exit/signal status. `start_async:1152` uses a
pthread callback, or **forks the callback with copied memory under NO_PTHREADS**.
A launcher can replace executable children; it cannot serialize arbitrary C
callbacks and parent pointers without a separate design.

| Command or facility | Source-inspected process boundary | Inference: first supported profile |
| --- | --- | --- |
| init / ordinary add | Basic local C paths; `add -e/-p` involve editor/interactive execution | Offline candidates after path/object/worktree closure; refuse interactive edit/patch initially |
| commit | Editor, signing, hooks; prepare-commit-msg is not disabled by --no-verify | Unsigned `commit -m/-F`, explicit author/committer identity and enforced external-execution refusal |
| status / log / diff | Optional pager, external diff, textconv, fsmonitor and signatures | Content-correct status, no-pager log, internal diff; refuse those extensions explicitly |
| checkout | Serial local path; post-checkout hooks, filters, submodules and parallel workers expand it | Serial, ordinary-file checkout with mode/metadata policy below; refuse omitted features |
| clone / fetch | Launch remote-https, pack ingestion children and connectivity rev-list; legacy transport also launches fetch-pack | Later network slice; no POSIX shell needed for these internal Git executables, but native launcher/streams plus sideband adaptation are mandatory |
| push | remote helper/send-pack, pack-objects and asynchronous sideband | Later authenticated remote-write slice, beyond read-only source-fetch gateway |
| Hooks, aliases, pager/editor, filters | `hook.c:13/123`, `pager.c:136`, `editor.c:60` rely on executable discovery or shell commands | Enforce unsupported-feature policy after config/attributes/environment are read. Do not silently treat access(X_OK)=ENOTSUP as “no hook.” |
| Auto-maintenance | `run-command.c:1816` can launch maintenance even when gc.auto=0 | Enforce maintenance.auto=false/no automatic child work initially |

Source details: [pack ingestion](https://github.com/git/git/blob/v2.53.0/fetch-pack.c#L912),
[connectivity](https://github.com/git/git/blob/v2.53.0/connected.c#L25),
[push pack generation](https://github.com/git/git/blob/v2.53.0/send-pack.c#L60).
The simple offline paths above are candidates by inspection, not demonstrated
Pyxis commands. Initial policy also excludes credential helpers, shell aliases,
recursive submodules and repository-controlled signing/verification executables.
Installation defaults are insufficient: repo configuration must not reenable a
feature or silently omit configured hooks/filters. Existing hooks/filters needing
execution should cause an explicit refusal of the affected operation.

**Inference: native run-command backend.** Consume the shared
[native command execution](native-command-execution.md) foundation for streams,
cwd/env snapshots, result/stop and cleanup. Git owns bundle resolution, enforced
external-feature policy and fork-callback/sideband adaptation. Resolve internal
Git children from one retained bundle revision, launching the same native Git executable with a
subcommand argv. Resolve separately packaged helpers through explicit roots and
READ/LAUNCH authority. Keep [bundle](program-bundles.md) command/resource views,
stack/capture limits and grant attenuation; unpacked bundles already work, native
ZIP loading remains unimplemented. No /proc executable discovery or shell fallback.

Snapshot copied environment with Git's child delta and an owned native cwd;
resolve a child cwd override without mutating the parent. Create directional
native pipe pairs, close unused parent ends after launch capture, flush delegated
FILE output, and own each child completion observer exactly once. Positive short
progress, EOF/EPIPE, failed/unknown launch and EXITED/FAULTED/TERMINATED remain
distinct. Native file stream delegation starts at offset zero; pipe read-ahead
stays in the parent's memory. This is not fork-inherited descriptor/buffer state.
See [stdio delegation](../userland/stdio.md#standard-streams-formatting-and-exit)
and [launcher/process contracts](../userland/process-state.md).

Use native wait/termination for owned children. Faults must not become synthetic
signal numbers or successful exit; uncertain launch/mutation must not be retried
as if nothing happened. Shell supervision owns foreground-group termination.
Group-bound child launchers provide LAUNCH, not nested group supervision. A
network sideband callback needs an explicit helper process or a bounded event
pump; simply substituting launcher_launch for fork cannot work.

## Worktree, index and publication

**Fact:** native [`struct stat`](../../userspace/libc/include/sys/stat.h) has
`st_valid` bits for dev, ino and mtime, independently. Type/size are unconditional.
Identity tokens promise comparison only while original references remain held;
pathname stat closes its lookup reference. Mtime may repeat/backtrack and is not
a change counter. NPFS creation time is not POSIX metadata-change ctime.

| Git need and source | Native capability / honest refusal | Inference: port boundary |
| --- | --- | --- |
| [`statinfo.c:23/63`](https://github.com/git/git/blob/v2.53.0/statinfo.c#L23) assumes ctime/mtime/dev/ino/uid/gid; [index format](https://github.com/git/git/blob/v2.53.0/Documentation/gitformat-index.adoc#L66) persists 32-bit cache fields | Current optional identity/time does not provide durable reopened-file identity, UID/GID/ctime or monotonic freshness | Preserve Git disk format but make unavailable cache hints unusable for native freshness; use actual content comparison initially |
| core.checkStat=minimal / core.trustctime=false | Minimal still tests whole-second mtime and size | These switches cannot prevent same-size edits from being hidden; do not fabricate epoch/zero stat facts |
| [`read-cache.c:229/307/381/442`](https://github.com/git/git/blob/v2.53.0/read-cache.c#L229) hashes content and handles racy entries | Native reads/hash can establish actual blob equality under supported conversion policy | Adapt comparators and racy logic together. Forcing DATA_CHANGED alone can skip hashing at :472 and falsely report dirty files. Disable/adapt stat-validity, untracked and fsmonitor caches too. |
| Execute bit / [`entry.c:86`](https://github.com/git/git/blob/v2.53.0/entry.c#L86) opens executable entries with 0777 | Public file creation accepts 0666 only; loading requires READ, not an executable bit | Preserve tracked 100755 in Git metadata, deliberately create native regular files with 0666; newly added files default 100644. core.filemode=false alone does not fix checkout creation. |
| Symlink / lstat / [`entry.c:313`](https://github.com/git/git/blob/v2.53.0/entry.c#L313) | NPFS no symlinks; host lookup refuses them with ELOOP; lstat cannot expose a link entry | Refuse symlink checkout initially. Git's core.symlinks=false text materialization is a possible separately accepted profile, not a claim of native symlinks. Refuse gitlinks/recursive submodules initially. |
| index.lock / [`tempfile.c:137`](https://github.com/git/git/blob/v2.53.0/tempfile.c#L137) uses O_RDWR\|O_CREAT\|O_EXCL and 0666 | Real exclusive creation and EEXIST now available under parent authority | Use that exclusion and checked writes/close; preserve denial/outcome uncertainty. Private 0600 temp requests elsewhere require deliberate capability policy, not discarded privacy intent. |
| Lock/ref/object publication / [`tempfile.c:336`](https://github.com/git/git/blob/v2.53.0/tempfile.c#L336), [`object-file.c:626`](https://github.com/git/git/blob/v2.53.0/object-file.c#L626) | Atomic same-volume regular-file replacement; held victim handles survive. No hardlinks, directory rename or cross-backend fallback | Adapt/select object rename publication, retaining collision checks. Do not implement a fake successful link/chmod or recursive directory move. |
| Crash durability / object-file.c:790, refs/files-backend.c:2022 | Public fsync accepts writable files; native directory sync has no directory-fd bridge. Namespace insertion need not flush cached data. | Define checked file sync, atomic publication and native directory sync. After rename, a sync failure reports uncertainty; close/rename alone cannot promise crash-safe repository writes. |

The native [filesystem writer](../devices/filesystem-native-adapter.md) is the
durability authority: rename-to-absent commits namespace without necessarily
flushing file contents; replacement flushes the moved file first. Persistence
claims need actual supported sync operations. A first content-based freshness
profile costs full scans and hashing; no performance estimate was measured.
Read concurrent to unrelated mutation still needs an operation-level policy;
full hashing does not manufacture a filesystem snapshot.

Git also bypasses comparison for CE_VALID (assume-unchanged), CE_SKIP_WORKTREE and
CE_FSMONITOR_VALID (`read-cache.c:399–404`, refresh:1357–1368); core.ignoreStat can
create assume-valid entries (`environment.c:352`). The proposed first profile
must refuse core.ignoreStat and unsupported assume-valid/skip-worktree/sparse
index entries rather than advertise content-correct status while honoring those
shortcuts. Supporting their deliberate user-promised semantics needs a later
explicit profile.

[`setup.c:1380`](https://github.com/git/git/blob/v2.53.0/setup.c#L1380) assumes Unix
repository ownership. A native trust/authority check must replace that assumption;
“current UID owns everything” and safe.directory=* are not solutions. Access
authority and whether repository configuration may launch programs are distinct.

## Smart HTTP without libcurl

**Fact:** Git's [remote-helper protocol](https://git-scm.com/docs/gitremote-helpers)
provides a replacement seam for git-remote-http/https. Existing remote-curl uses
discovery GET, request headers and POST to upload-pack/receive-pack, streaming
request/response callbacks and child pipes. See versioned
[`remote-curl.c:901`](https://github.com/git/git/blob/v2.53.0/remote-curl.c#L901),
[`http.c`](https://github.com/git/git/blob/v2.53.0/http.c) and the
[Git HTTP specification](https://git-scm.com/docs/gitprotocol-http/2.53.0).

**Shared dependency, moved by the owner 2026-10-11; not implemented:**
[native HTTP requests](native-http-requests.md) owns the engine, accepted prepared
transaction, request-scoped GET/POST headers, credentials and generic adapters
formerly in Claude tasks 1a–1c and this document's HTTP task 4. The separate
body/control/response grants, freeze-before-submit, one-shot commit, unknown
remote outcome/no replay and request-private credential rules remain accepted.
Git consumes those operations; it supplies no second transaction interface or
application-side HTTP/TLS client. Neither this move nor the shared prerequisite
completes the Git helper, clone/fetch or push tasks.

The [scheme write mapping](userspace-scheme-providers.md#future-writes-and-shell-operations)
and [prepared handoff](userspace-scheme-providers.md#prepared-requests-and-shell-handoff)
already distinguish GET (`cat`/`"r"`), staged POST (`"w"`) and staged POST then
response reading (`"w+"`). Commit is explicit; flush/close never submit, and body
write authority alone cannot commit. The Git helper retains control and routes
directional child pipes into each prepared body and back from its response;
children receive no request controls or credentials.

| Git exchange | Mapping onto the accepted scheme/prepared-request design |
| --- | --- |
| Discovery | GET `info/refs?service=git-upload-pack` or `git-receive-pack` through the read path. Discovery remains GET, not PREPARE's POST. Request-scoped Git-Protocol must also be carried on discovery GET when selecting v2. |
| Each upload-pack RPC | A fresh PREPARE for `/git-upload-pack`, Git request headers and one deadline; stage the current RPC body, commit once, and read/validate the response through its separate READ grant and status. Each subsequent v2 RPC prepares a new transaction; repeated commit never sends the next RPC. |
| Each receive-pack RPC | The same lifecycle for `/git-receive-pack`, with explicit submission/remote-write authority and the pack/request body. Push needs large body handling below; failed/unknown submission cannot be automatically replayed. |

Finishing a body means finishing **that RPC's staging**, not waiting for a Git
child to exit. Fetch-pack/send-pack can remain alive awaiting the response and
then generate another RPC. The helper uses Git's framing to delimit each request,
commits it, forwards its response and retains the child across exchanges
(`remote-curl.c:1122–1128`, :1514–1528); waiting for child exit would deadlock.

Git-specific extensions to that shared design are limited to:

| Extension | Fit and scope |
| --- | --- |
| Git request headers | Git-Protocol, including discovery GET, and Git Content-Type values such as application/x-git-upload-pack-request and application/x-git-receive-pack-request fit validated request-scoped headers. Git's Accept values use the same arrangement; framing headers remain provider-owned. |
| Response media type | Expose response Content-Type among selected response headers, so the helper validates advertisements and upload-pack/receive-pack results. It is response metadata, not readable request headers. |
| Helper credential path | Use the accepted interim request-scoped arrangement: trusted native preparation receives only the selected credential FILE, privately reads it and attaches the appropriate header to that request. No argv/env/log/header readback or ambient credential store. The owner accepts this approach for now; anonymous read-only gateway fetch comes first. |
| Large push bodies | Consume the [shared HTTP staging contract](native-http-requests.md#accepted-owner-decisions) for pack-sized bodies under actual resources. Freeze each RPC before submission; production after submission is outside the first shared profile. |

The eventual credential path is the parked
[credential store as a capability service](credentials-and-biometrics.md#credential-store-as-a-capability-service):
capabilities scope access to items, and secrets are used through operations rather
than exported; raw export is a separate explicit right. It already names SSH keys
and Git commit signing as first consumers. This is a later dependency, not an
implementation milestone or an added task here; HTTPS uses the interim
request-scoped arrangement until that service is assigned and delivered.

**Owner direction, 2026-10-11:** no artificial response-size cap. The
[shared milestone's accepted response/storage decisions](native-http-requests.md#accepted-owner-decisions)
now own streamed GET/POST, explicit final completion and large staged bodies.
The owner accepted replacing Claude's complete-only snapshot and response-grant
timing with a forward-only stream and explicit final status; it is not implemented.
Git must not publish successful refs from an incomplete pack, and must finish
each RPC's body independently of
child exit. Preserve the helper's existing anonymous-first and interim
request-scoped credential direction.

**Implemented boundary at the inspected revisions:**
[`httpfs`](../../userspace/httpfs/main.c) still rejects WRITE;
[`libhttp`](../../userspace/libhttp/fetch.c) sends GET and owns whole responses;
the current [provider ABI](../../include/abi/provider.h) has no prepared exchange.
[Bounded GET redirects](../../userspace/lib/provider_http.c) are implemented,
along with verified TLS and explicit trust/network authority documented in
[HTTP fetch](../userland/http-fetch.md) and [HTTPS](../userland/https.md).
Prepared POST, its controls and generic adapters remain shared prerequisites,
not implemented capabilities. The helper uses the provider; TLS stays there.

Replacing only http.c is insufficient: http.h exposes curl types and remote-curl.c
sets curl options/callbacks directly. Replacing both layers could preserve more
upstream negotiation behavior, but would still consume the shared provider
contract rather than supply a fake curl ABI. A libcurl port preserves its HTTP/auth/proxy feature
set at the cost of a separate platform/TLS port: Git http.c:1656–1700 uses
curl_multi_fdset/select, and native libc has no general socket/poll bridge.
Mbed TLS 4.1.1 integration is unprobed for curl; an existing libtls port does not
establish it. No curl build or performance comparison ran.

The accepted [LAN gateway](source-builds.md#reaching-the-sources) exposes discovery
and upload-pack only. Clone/fetch can use that read-only destination profile;
push needs separate receive-pack authorization, credentials and uncertain-write
policy. Full transport eventually needs v0 compatibility, authenticated fetch,
push and explicit proxy behavior; none is implicitly assigned here. These are
Git helper/protocol policy and coverage, not additional provider interfaces.
Git 2.53 falls back to v0 for receive-pack (`remote-curl.c:494–501`); a v2-only
helper cannot implement complete push. Native launcher/pipes and non-fork
sideband remain prerequisites irrespective of the HTTP consumer route.

## Threads and gates 5–8

**Fact:** NO_PTHREADS is a genuine serial pack/index choice.
[`thread-utils.c:21`](https://github.com/git/git/blob/v2.53.0/thread-utils.c#L21)
returns one CPU; pack-objects:3158 and index-pack:1361 have direct serial paths.
Git's local dummy pthread creation/join fail, not succeed. This is not permission
to export fake locks/pthreads from Pyxis libc. Refuse GIT_FORCE_THREADS and other
unsupported worker overrides in the profile; index-pack has that force branch.

**Inference:** a serial offline CLI and separately launched serial Git children
need no new thread API. Their libc cwd/environment/descriptors/allocator state
is process-local. Smart transport still needs non-fork sideband execution; serial
pack workers do not remove that independent dependency. A native sideband helper
or event-pump adaptation can proceed without claiming pthread support.

Parallel workers require all [thread gates](threads.md#delivery-gates):
5 lifecycle/join/guarded stacks; 6 real parking/wakeup; 7 static TLS/per-thread
errno and compiler support; 8 synchronized libc allocator/descriptors/FILE/caches
and a real pthread/C11 profile. Gate 5 alone is insufficient. All four gates were
unchecked at the inspected main; #709's prototype is not a merged pthread runtime.
This differs from hosted Clang's accepted real TLS/synchronization dependency;
Git's source serial option should not expand that compiler assignment.

## Proposed delivery tasks

These are ordered CLI tasks for separate owner assignment. Shared libc/native
HTTP prerequisites can also serve the accepted libgit2 route; that route retains
its status and should not wait for a complete CLI. Native ownership follows
[SDK/repository boundaries](../development/sdk-and-repositories.md): general
libc in userland, Git recipe/adaptation in ports, provider/helper support in
userland, parent pins only after dependencies are published. No compiler rebuild
is implied by these docs or ordinary libc/port work.

1. **Close the real serial core build and path/clock/identity boundary.** Pin the
   reviewed official source in a separately assigned ports task, keep object hash
   verification, audit actual source consumers and use honest native paths,
   author identity and clock/random authority. Omit unused Unix features; adapt
   shared libc only where native behavior exists. Bundle native code/resources
   within measured loader/stack/heap limits; no size/admission result exists yet.
   **After this task the owner can:** run Git version and object plumbing against
   an explicit private local object store, without a shell or network claim.
2. **Deliver the offline worktree/index and publication profile.** Content-based
   freshness without unsupported assume-valid/skip-worktree/sparse shortcuts,
   modes/unsupported-tree policy, lock exclusion, native file/directory
   sync, explicit repository-config trust and external-feature refusals. Later
   authorized qualification must cover same-size edits, optional metadata absent,
   clean status, collisions/denials and publication uncertainty.
   **After this task the owner can:** init, add, commit -m/-F, status, no-pager log,
   internal diff and serial checkout for supported ordinary local repositories.
3. **Consume shared command execution; adapt Git run-command/sideband.**
   The generic stream/cwd/env/result/stop/cleanup work moves, not completes, to
   [native command execution](native-command-execution.md). Git retains its
   executable bundle/grant policy, helper flow and non-fork callback/sideband
   adaptation; remove shell/fork fallback without a kernel POSIX process model.
   Qualify Git-specific short transfers, EOF, failures and deadlock bounds.
   **After this task the owner can:** use supported internal Git child commands
   and pack ingestion with native lifecycle/error reporting, before network use.
4. **Shared dependency: native HTTP requests (moved, not completed).**
   Require [that milestone](native-http-requests.md), including request-scoped GET
   headers, streamed completion and large staged bodies. No duplicate HTTP
   implementation task is owned by Git.
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
7. **Optional parallel pack/index after gates 5–8.** Audit the actual pthread
   subset and resource concurrency; retain a measured serial baseline.
   **After this task the owner can:** choose qualified threaded pack/index work;
   threads are not a prerequisite for tasks 1–6's proposed serial architecture.

## Owner decisions, proposed defaults

1. **First CLI scope:** default to the enforced offline serial profile, explicit
   no external hooks/filters/pager/editor/signing, with commit -m/-F. Complete CLI
   remains eventual; restricted libgit2 source fetch remains the accepted first
   acquisition route. Defaults here are proposals, not accepted implementation.
2. **Worktree fidelity:** default to real content comparisons, retained tracked
   100755 metadata without native execute permissions, new files 100644, initial
   symlink/gitlink refusal, and honest native sync/publication errors. No synthetic
   ownership/cache fields, unsupported index shortcuts or private-mode privacy
   promises.

Response delivery is now [HTTP decision 1](native-http-requests.md#accepted-owner-decisions),
accepted by the owner on 2026-10-11, not implemented. The shared provider delivers
it independently of this CLI; other Git direction and implementation status stay
unchanged.

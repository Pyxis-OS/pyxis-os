# Git on Pyxis

Status: **slices 1 and 2 merged; slice 3 assigned** (chronium, 2026-10-24).
The final slice adds authenticated HTTPS push/pull and the newly assigned
`git init` plus remote setup. The three decisions below remain proposed;
no slice 3 implementation is included. The goal is editing in Neovim with
clangd, compiling with Clang, then committing and pushing from Pyxis.

## Accepted direction

- **go-git** is the implementation for both the developer CLI and future source
  fetching (chronium, 2026-10-22). Those are separate deliverables; this milestone
  delivers the CLI. It does not port the shell-based ports runner or build a
  source-fetching service. The earlier C Git/libgit2 investigations remain in
  Git history.
- **Three slices, in order** (chronium, 2026-10-24): HTTPS clone/log/status;
  add/commit/diff with author name/email from a file in `home://`; then
  authenticated HTTPS push/pull to Forgejo/GitHub.
- **No native symlinks or execute bit** (chronium, 2026-10-24): the working-tree
  profile is `core.symlinks=false` and `core.fileMode=false`. A Git symlink checks
  out as a regular file containing its target. Native execute-bit changes are
  ignored; the Git tree/index modes remain intact. Real native support is later.
- **No commit signing** (chronium, 2026-10-24). Commits are unsigned; chronium
  will not push to the Pyxis repositories from Pyxis yet. Slice 3 uses a token
  stored as plain text in the user's home configuration until the credential
  manager exists. No Unix permission bits or signing-key protection are implied.

## Milestone checklist

- [x] **Slice 1 — clone, log, status.** Build the optional native Go CLI, make
  go-git/go-billy use the real filesystem and client-network contracts, and
  qualify public HTTPS clone plus clean/edited status in QEMU. Capture matched
  Linux/Go and nested-QEMU clone samples. Implementation and initial checks are
  in [the Git guide](../userland/git.md) and
  [qualification record](../development/experiments/git-slice1/README.md);
  functional QEMU checks and the matched-source horse/Linux versus nested-QEMU
  comparison pass. Median clone times are 5.358 s on horse and 9.571 s in nested
  QEMU; the record explains the differing environments and limits.
- [x] **Slice 2 — add, commit, diff.** Deliver native index/object/ref publication,
  author configuration under held `home://`, unsigned commits and diffs.
  Upward discovery, invocation-relative staging, checked index/ref publication
  and QEMU/Linux inspection pass; see the
  [local workflow record](../development/experiments/git-slice2/README.md).
- [ ] **Slice 3 — init, remote, push, pull.** Deliver new-project initialization
  and authenticated HTTPS against Forgejo/GitHub using the accepted temporary
  token storage. Settle the three decisions below before implementation; no
  silent conflict resolution or success after a refused remote update.

After slice 1, chronium can clone an ordinary public repository, read its
history and see a Neovim edit in status. After slice 3, he can complete the
edit/build/commit/push loop. Slices 1 and 2 are merged; authenticated transfer
and new-project initialization remain open.

## Slice 2 planning notes

- Upward `.git` discovery is implemented for repository commands, within the
  caller's held ancestor authority.
- Keep slice 1's sync-on-close behavior unchanged for now: every writable
  checkout file is synced, beyond the accepted requirement to sync before
  publishing a rename. Measure this on vela before deciding whether to narrow
  it. It may contribute to clone cost; the host/nested comparison does not
  attribute the difference to sync.

## Accepted slice 2 contract (chronium, 2026-10-24)

All three decisions below are accepted (chronium, 2026-10-24); implementation
is authorized. Implementation and qualification are in the linked slice 2 PRs.

### 1. Author configuration

**Accepted:** read `home://.config/git/config` through the held `home`
root, only when committing. Use Git-style literal data:

```ini
[user]
    name = Chronium
    email = chronium@example.invalid
```

Require one nonempty name and email; reject malformed, duplicate or unsupported
settings, includes and control characters that cannot form a Git identity.
This file supplies both author and committer. No commands, capability changes,
repository-local identity override, environment override, host HOME search or
new configuration-writing command. An absent/unreadable home or invalid identity
refuses commit without changing HEAD; add, diff, log and status do not need it.
The file grants no authority beyond identifying the unsigned commit's author.

### 2. Commit messages and publication

**Accepted:** `git commit -m MESSAGE`, one nonempty message argument.
No editor, stdin-message mode, amend, automatic staging, empty commit or signing.
An `$EDITOR`-based message is wanted later; it is outside slice 2.
Commit the existing index on the current checked-out branch of a cloned
repository; detached HEAD and unresolved/conflicted index entries refuse in
this slice. An unchanged index refuses rather than making an empty commit.

Supply explicit author/committer signatures, with checked native wall time and
the configured local zone offset. Unavailable wall time refuses with a clear
error, without silently substituting uptime or a fabricated timestamp. Do not
let go-git load system/global author defaults. Publish objects before the
branch update compared against the observed HEAD/branch, and report the hash
only after
checked publication/close. Failure can leave unreachable objects; no automatic
collector, stale-lock stealing or stronger power-loss guarantee is added.

### 3. Staging and diff surface

**Accepted:**

- `git add [--] PATH...` stages explicit files/directories, including tracked
  removals beneath those operands. `git add -A` stages the whole repository.
  Recursive directories respect ignore rules for new files; tracked edits and
  removals still stage. No built-in glob/pathspec language or force-add option.
- `git diff` shows worktree versus index; `git diff --cached` shows index versus
  HEAD. Plain unified text patches, three context lines, no colour or pager;
  binary differences get a marker, not a fake text patch. These read-only
  commands return zero when differences exist and nonzero on errors. Path
  filtering and binary patch output can wait.

New regular files use Git mode 100644; existing Git modes and the accepted
symlink-as-target-text/fileMode=false profile remain intact. Gitlinks stay
uninitialized; this slice does not stage submodule changes. Stage into an
exclusive `index.lock` held across the read/modify/publish operation; check
encode/sync/close and rename before reporting success. A busy lock refuses without retrying or stealing it. No successful
partial directory scan is reported after an error.

### Discovery, ownership and qualification

Upward `.git` discovery is already assigned for repository commands. Choose the
nearest real `.git` directory along the retained cwd's granted ancestor chain;
stop at its authority boundary. Never recover ancestors from descriptive cwd
text or another broader root, follow a `.git` pointer file, or walk into a
parent after encountering malformed/inaccessible metadata. Resolve explicit add
operands relative to the invocation directory, keeping them inside the selected
repository; do not silently reinterpret them relative to its root.

Use ordinary held directory/file rights for mutations and identity reads; no
new kernel/ABI or grant is proposed. Keep current sync-on-close behavior until
native measurement authorizes any change. The merged slice 1 source and its
recorded clone/status checks are the baseline, not a new timing claim.

Implement in userland, with reusable Go corrections in the
fork only if required, then publish dependency PRs before the parent pin. In
QEMU qualify nested-directory discovery, restricted-root refusal, explicit and
all-tree staging, tracked deletion, same-size edits, ignored/untracked files,
unstaged/staged text and binary diffs, commit/hash/history/clean status,
missing/invalid identity, empty/conflicted/detached refusal, and offline and
read-only authority profiles. Inspect resulting commits, trees and index with
Linux Git. No native/PXE action or fault injection is assigned. After this
slice chronium can edit, stage, review and make an unsigned local commit;
authenticated transfer remains slice 3.

## Slice 3 contract — proposed

Assigned (chronium, 2026-10-24), including `git init` and a remote-add command.
All three defaults below are **proposed, not accepted**.

### 1. Credentials and origin matching

**Recommended default:** a separate literal Git-style file at
`home://.config/git/credentials`, opened only for HTTPS operations:

```ini
[credential "https://git.internal"]
    username = chronium
    token = REPLACE_LOCALLY
[credential "https://github.com"]
    username = chronium
    token = REPLACE_LOCALLY
```

Match one exact normalized HTTPS origin: host is case-insensitive, omitted
port means 443; another host, subdomain or port never matches. No wildcard,
repository-path inference, environment override, helper, prompt or credential
in a repository URL. Duplicate origins or malformed settings refuse. Missing
file/entry permits anonymous clone/pull; push requires a matching entry. Private
clone uses this same credential selection, not a second authentication path.

Send the selected username/token as HTTPS Basic authentication. Keep tokens out
of argv, progress, errors, PRs and repository-local config. Only same-origin
redirects are allowed for authenticated requests; do not forward or reselect
credentials across origins. Anonymous clone/pull retain HTTPS-only redirects.
TLS verification and the existing CA bundle remain mandatory. The file is
plain text: anyone holding sufficient home READ can read it, and home writers
can replace it; no Unix mode-bit secrecy or credential-manager protection.

### 2. Push and pull behavior

**Recommended default:** `git push [REMOTE]` and `git pull [REMOTE]`, default
remote `origin`, current branch to/from that same branch name. One branch per
operation; no forced push, deletion, tag/refspec surface, auto-upstream mutation,
merge, rebase, stash, hooks or submodule initialization. Detached HEAD refuses.
Push needs a local commit. Pull may populate an unborn branch from an existing
remote tip with the same index/overwrite guards; a missing remote branch refuses.
A rejected/non-fast-forward push returns an error, never success. If transport
failure leaves remote publication uncertain, say so and do not retry silently.

Pull fetches and fast-forwards only. Require a clean tracked tree and index,
and refuse untracked paths that would be overwritten. Re-check after fetch;
refuse divergence without moving local HEAD or discarding edits. A local branch
that already contains the remote tip stays ahead. Fetched objects/tracking refs
may remain after a refused local update.

Use the existing locks and checked data-before-name publication. Checkout and
index completion precede the branch update compared against the observed HEAD;
never call the pinned library's HEAD-first pull path unchanged. An I/O failure,
cancellation or power loss can still leave partial worktree changes and/or
unreachable objects; report that failure and preserve the actual state, with
no automatic reset or whole-worktree crash-atomicity claim. Concurrent edits
are not a snapshot or a supported parallel checkout transaction. Sync-on-close
stays unchanged pending the native measurement already recorded.

### 3. Init and remote setup

**Recommended default:** `git init` initializes the current held directory,
with an unborn `main` branch; no bare mode, parent search or reinitialization.
Existing contents stay untouched; any existing `.git` refuses. Failure can leave
an explicitly reported partial `.git`, which is not silently removed or adopted.

Support the first nonempty staged commit with no parent, and normal unborn
status/log/staged-diff behavior (no history yet; compare the index with an empty
tree). Existing identity, unsigned-commit and `-m` rules apply. `$EDITOR` remains
later work.

Use `git remote add NAME HTTPS_URL`, `git remote set-url NAME HTTPS_URL` and
`git remote -v`. Add refuses an existing name; set-url requires an existing one.
One public HTTPS URL per remote, with no credentials, query or fragment; the
URL selects a destination, never filesystem authority. Read/modify/publish
repository config under `config.lock`, checking sync/close before replacing it.
No additional remote-removal, branch-management or global config command.

```sh
mkdir project
cd project
git init
git add README.md
git commit -m "Initial commit"
git remote add origin https://git.internal/USER/TEST-REPOSITORY.git
git push
git pull
```

### Ownership and qualification

Userland owns the CLI/adapters; reusable Go corrections go in the fork only if
required. No new kernel/ABI, launcher or network authority is proposed. Hold
owned inputs and drain actual native calls through cancellation as before.

Use slice 2's merged source and recorded local workflow as the baseline. Build
both targets and qualify init/first commit, remote config, clean fast-forward,
already-current/ahead, divergence, dirty/index and untracked collision refusals,
auth failure, rejected push, offline/missing-authority behavior and cancellation
in QEMU. Check resulting local and remote refs/objects independently with Linux
Git. Record partial-update limits honestly; no new fault injection or native/PXE
action is assigned.

Authenticated Forgejo/GitHub qualification needs disposable repositories and
narrow tokens provisioned by chronium, outside chat and repository history.
Do not create repositories on his behalf or push unsigned test commits into
Pyxis project repositories. Dummy-token local HTTPS checks can cover ordinary
request/refusal behavior, but do not stand in for provider qualification.

Source inspection: pinned go-git
[BasicAuth](https://github.com/go-git/go-git/blob/bdae57cc427f94a6f7c627201bfe14508b5e19bd/plumbing/transport/http/common.go),
[pull](https://github.com/go-git/go-git/blob/bdae57cc427f94a6f7c627201bfe14508b5e19bd/worktree.go)
and [init](https://github.com/go-git/go-git/blob/bdae57cc427f94a6f7c627201bfe14508b5e19bd/repository.go);
merged userland `f3fa4b5a30af` supplies the existing locks/identity/profile.

## Dependencies and ownership

[Go tasks 3 and 4](go-runtime.md) are merged: native files, supplied streams,
TCP/DNS/TLS and readiness-backed networking are available. The native-task limit
is 256 live/retiring tasks per process, not the earlier 64; networking uses
readiness sets rather than expanding `wait_many`. Native qualification limits
remain those in the [Go guide](../development/go-toolchain.md).

Reuse the [Caddy package route](../userland/caddy.md): the pinned Pyxis Go
compiler writes P1F, sources/modules come through the internal proxy, the target
build uses a fixed vendored graph offline, and the package retains source/compiler
provenance and all applicable notices. Git is optional, outside the core archive,
and is not published to `packages.pyxis.nexus` until a release.
[Repository ownership](../development/sdk-and-repositories.md) still applies.
No LLVM container rebuild or kernel/ABI change is presently justified. Go-port
corrections go to `pyxis-go`, before the parent's updated compiler pin; dependency
PRs must be published before their parent pins.

## Accepted slice 1 contract (chronium, 2026-10-24)

All three defaults below are accepted. Implementation and qualification follow
in focused dependency and integration PRs.

### 1. First-party CLI ownership

**Accepted:** `git/` in **pyxis-userland**, with its own Go module,
manifest and narrowly scoped dependency patches. Pyxis owns the optional build
and `packages/git.lua` integration, using the existing Go compiler pin. No new
repository, broker, wrapper process or general Git framework. An actual reusable
Go syscall/filesystem correction belongs in the Go fork, not in command-local
raw capability calls.

The same CLI source/dependency revisions will build for Linux and Pyxis for the
matched baseline.

### 2. Upstream pins

**Accepted:** stable **go-git v5.19.3**, commit
`bdae57cc427f94a6f7c627201bfe14508b5e19bd`, and its declared **go-billy v5.9.2**,
commit `19ea4dd91ebc56e1b6cdea6890a180a789bea20d`. Retain a fixed `go.mod`/`go.sum`,
record local patches and compiled-module notices, verify sums before vendoring,
and build the target offline without changing the lock inputs.

The [release](https://github.com/go-git/go-git/releases/tag/v5.19.3) is stable;
v6 is still a prerelease. Its
[go.mod](https://github.com/go-git/go-git/blob/bdae57cc427f94a6f7c627201bfe14508b5e19bd/go.mod)
requires Go 1.26, within the existing Go 1.27.3 fork. Both modules' exact
`.info`, `.mod` and `.zip` artifacts were available with normal TLS from
`https://repo.internal/repository/go-proxy/` during source research. The full
CLI graph and its cache/notices closure are qualified by the slice 1 builds.
If another artifact is missing, name its exact module/version for the cache; never fall
back upstream.

### 3. Filesystem, authority and failure profile

**Accepted:** an ordinary working tree rooted at the selected retained
cwd, with `.git` as a real child directory. `git clone HTTPS_URL [DIRECTORY]`
creates one fresh child of that cwd (URL basename if omitted); an existing
destination refuses. `git log [-n COUNT]` and `git status` run at the repository
root, with no upward discovery. Full ordinary clone, no shallow history and no
recursive submodule initialization. Gitlinks remain recorded but uninitialized;
LFS pointers remain file contents. Unsupported transports/commands/formats fail
explicitly. No external pager, hooks, filters, editor or credential helper.

Use the ordinary Go file adapters through a rooted Billy integration, without
re-resolving descriptive cwd text. Refuse `.git` pointer files, `commondir`,
alternate object stores, separate worktrees and config/environment redirection
outside the selected tree. Slice 1 reads repository-local configuration only;
no global config/attributes/includes, tokens or ambient HOME search. Preserve
upstream path validation and treat repository names as components, never as
`scheme://` authority. Parent discovery and linked worktrees are outside slice 1.

The minimum selected launch profile is:

| Input | Default |
| --- | --- |
| Runtime | Required memory/manage, threads/manage, clock/read+precise+sleep and random/read. |
| Network | Optional TCP CONNECT, `dns_udp` OPEN and `service/create`. Offline log/status still admit; clone refuses missing required authority. No LISTEN. The constructor is the existing general IPC constructor, not a completion-only right. |
| Storage | Retained cwd/root authority already supplied by the launcher. Clone needs content mutation rights on its parent/child; log/status remain observational and need only lookup/enumeration/file reads. File sync uses real WRITE authority. |
| TLS | Held read-only `boot` root for the existing CA bundle and Go's verifier. HTTPS only, including redirects; no insecure TLS switch. |
| Streams | Supplied stdout/stderr. No launcher, job work, pipe creation or separate HTTP-provider grant is needed. |

Directory rights are existing selected launch inputs, not authority a bundle
manifest can mint. `resource_dirs` grants only read-only directories inside the
bundle. A caller may already delegate broader roots; the command's rooted access
profile is **not** a new process-wide sandbox or a change to ambient delegation.
HTTPS uses Go `net/http` over native TCP/DNS/netpoll, not the HTTP provider.

First Ctrl+C requests cancellation; existing native/group termination remains
forceful. Do not retry/resume clone automatically. On failure or cancellation,
report it and retain the partial destination rather than recursively deleting
possibly changed contents; clean up only still-owned unpublished temporary/lock
files when their outcome is confirmed. Forced termination can leave partial
files/locks. No automatic stale-lock stealing or collector. A clone is not a
whole-tree atomic transaction. Propagate I/O, sync, close and resource errors;
never print a clean status after an incomplete scan. Live status is not a
snapshot of concurrent edits.

The default promises real data sync before name publication, **not** whole-clone
crash atomicity or a qualified power-loss namespace-completion guarantee.
Directory sync is not fabricated. A stronger crash-completion promise or a new
sync/authority interface needs a decision before adding it. Cancellation does
not imply that an already blocking native FILE call has completed; retain its
ownership until actual completion. There is no claimed whole-clone time, byte or
memory bound from the library's context/depth options.

## Source-inspected integration work for slice 1

The existing native [mutation contract](../interfaces/filesystem-mutations.md),
[writer](../devices/filesystem-native-adapter.md), [paths](../userland/paths.md)
and [Go adapter](../development/go-toolchain.md) are authoritative. This is
source inspection, not a build or runtime result. Native inspection used Pyxis
`b4648cde` and its pinned Go fork `d37622551cd6`; upstream inspection used the
exact go-git/go-billy revisions pinned above:

- **Platform selection.** go-billy's negative POSIX build constraint selects
  `os_posix.go` on Pyxis, importing `x/sys/unix`, `Flock` and `syscall.Umask`.
  go-git imports `osfs` and other transport packages even if only HTTPS is used.
  Add narrow, honest platform adapters/build constraints; do not enable Unix
  tags or install successful no-op flock/umask/chmod implementations.
- **The accepted mode profile needs code.** This go-git release does not consume
  `core.symlinks`/`core.fileMode` as the required behavior. Adapt checkout and
  index-aware status while retaining original tree/index modes; native Symlink
  stays unsupported. The
  [checkout](https://github.com/go-git/go-git/blob/bdae57cc427f94a6f7c627201bfe14508b5e19bd/worktree.go)
  and
  [status comparison](https://github.com/go-git/go-git/blob/bdae57cc427f94a6f7c627201bfe14508b5e19bd/utils/merkletrie/filesystem/node.go)
  cannot simply be used unchanged.
- **Locking and publication already matter to clone.** The ordinary ref-update
  path calls Billy `Lock`, even during clone; clearing the advertised lock bit
  does not avoid it. Use real exclusive-create lockfiles and correct ref/index/
  config publication where the native contract supplies that operation. Billy's
  [File interface](https://github.com/go-git/go-billy/blob/19ea4dd91ebc56e1b6cdea6890a180a789bea20d/fs.go)
  has no Sync method. Audit open as well as closed pack/object/ref/index/config
  writers; upstream Close/rename and direct truncation are not sufficient.
  Sync modified data before publishing a name; do not change `os.Rename` to
  pretend it flushes data, or use a cross-store copy/delete fallback.
- **Directory sync is a rights distinction.** Libc's public directory-fsync
  bridge is absent. Pinned Go does dispatch native directory SYNC, but ordinary
  directory opens attenuate away CREATE/REMOVE, so `os.Open(parent).Sync()` is
  denied. An explicitly supplied sufficiently authorized handle can sync; it
  is not obtained merely by spelling a path. File Sync commits that file's data
  and staged pool metadata, not all other files' cached data. A pre-rename sync
  alone does not confirm the later namespace edit.
- **Enumeration and status metadata.** Native enumeration is live, UTF-8 and
  case-sensitive, has no dot/inode entries, and can report CHANGED. Libc exposes
  EAGAIN; the pinned Go port currently maps it to EBUSY. Reconcile that adapter
  mismatch; status must propagate a failed/incomplete listing rather than treat
  it as clean or spin indefinitely. Native components are at most 255 bytes.
  If a file/index timestamp is unavailable, hash contents rather than reusing
  an unproved metadata-cache hit; same-size edits must be detected.
- **Ordinary Go I/O remains the boundary.** Current Go FILE calls use small
  inline transfers, preserve partial offsets and release the P while a blocking
  call occupies an M. Measure their cost on actual pack/checkout operations;
  no new bulk-I/O ABI, scheduler or kernel lock is assumed necessary by this
  proposal. Correct genuine mismatches in Go/billy/storage, not command-local
  raw calls or fake success.

## Slice 1 qualification

Before target changes, capture the matched Linux baseline using the same CLI,
go-git/go-billy revisions and agreed clone/mode profile. Record the compiler,
module graph, host, destination backing and exact commands. No native Git versus
Go timing substitution. Resolve a real public HTTPS repository to a recorded
HEAD; use the same repository/ref for Linux and Pyxis runs and detect movement.
A suitable first subject is a public Pyxis repository mirror, with submodule
recursion off. Do not use licensed/private game data or credentials as fixtures.

In four-or-more-vCPU QEMU with working native TCP/DNS and CA reads:

1. Clone over verified HTTPS into a fresh writable native destination; compare
   HEAD, history and checked-out contents with the same pinned Linux build.
2. Run log and status, confirm the fresh tree is clean, edit a tracked file in
   Neovim and confirm status reports the edit. Include a same-size edit and the
   accepted executable/symlink-as-text profile when present in the chosen tree.
3. Check read-only log/status without network grants; explicit clone refusal
   without needed network/write authority; ordinary failed/cancelled clone and
   its reported partial destination. Mutation during enumeration must not yield
   a false clean result. No unsafe sync/lock fallback.
4. Repeat matched fresh-destination clones, recording samples and variation,
   repository/ref, source revisions, filesystem/cache conditions, CPU/RAM,
   accelerator and network path. Label horse/Linux and nested-VM samples
   separately; their difference is not an isolated kernel/filesystem benchmark.
   Do not reuse dirty/incomplete clones as fresh samples.

Use ordinary builds, interactive QEMU and debugger inspection; no new test
framework, self-tests, fault injection, CI jobs or boot automation. Keep the raw
records under `/shared/present/git/`, with a short results/limits record in the
implementation PR. No native/PXE action or public package publication is
assigned by this milestone. This section records the qualification scope;
completed results are in the
[qualification record](../development/experiments/git-slice1/README.md).

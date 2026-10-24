# Git on Pyxis

Status: **slice 1 implemented and qualified for review**; slices 2 and 3 remain
unimplemented. The three-slice direction and all three slice 1 decisions are
accepted (chronium, 2026-10-24). The goal is editing in Neovim with clangd,
compiling with Clang, then committing and pushing from Pyxis.

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
- [ ] **Slice 2 — add, commit, diff.** Deliver native index/object/ref publication,
  author configuration under held `home://`, unsigned commits and diffs. Settle
  the identity-file and message-entry surface before implementing this slice.
- [ ] **Slice 3 — push, pull.** Deliver authenticated HTTPS against Forgejo/GitHub
  using the accepted temporary token storage. Settle credential selection,
  redirection and pull/conflict behavior before implementing this slice; no
  silent conflict resolution or success after a refused remote update.

After slice 1, chronium can clone an ordinary public repository, read its
history and see a Neovim edit in status. After slice 3, he can complete the
edit/build/commit/push loop. Slice 1 is implemented in the submitted PRs; the
later outcomes remain open.

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

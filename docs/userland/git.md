# Git

The optional first-party `git` package uses go-git v5.19.3 and go-billy v5.9.2.
It supplies HTTPS clone/push/pull, init, remote setup, log, status, add, unsigned
commit and diff. The three-slice CLI milestone is implemented under the
accepted contracts (chronium, 2026-10-24).
The [clone qualification](../development/experiments/git-slice1/README.md) and
[local workflow qualification](../development/experiments/git-slice2/README.md)
record measured checks and their limits. The [slice 3 record](../development/experiments/git-slice3/README.md)
records disposable Forgejo transfer and pull checks.

## Build and install

Build the [pinned Go cross-toolchain](../development/go-toolchain.md), then use
a clean source checkout:

```sh
make git-package GO_PREFIX=/absolute/pyxis-go REPOSITORY=/absolute/repository
```

The build downloads dependencies only from the internal module proxy, verifies
the fixed module/checksum inputs, applies recorded patches and compiles offline.
It stages the compiler revision, source hashes, patches and compiled-module
notices. Git stays outside the core archive and default package set. The package
target creates an unsigned development repository snapshot; use the existing
[signing workflow](../interfaces/packages.md) for release publication. No public
package publication follows from building it.

From a configured repository containing the package, run `pkg install git`.
The launcher's current command view then resolves `git`.

## Commands and storage

```sh
git clone https://github.com/Pyxis-OS/pyxis-os.git pyxis
cd pyxis
git log -n 5
git status
```

Clone creates one fresh child of the selected retained cwd. The destination may
be omitted to use the URL's last component without `.git`; existing destinations
refuse. Only HTTPS URLs are admitted, without embedded credentials,
query or fragment; redirects must stay HTTPS. Log/status run from the repository
root or a subdirectory, including without network authority, and never write.

The working tree and real `.git` directory stay under the selected retained
repository root. There
is no `.git` pointer file, alternate object store, linked
worktree, bare repository, general global configuration/includes, environment path
override, external helper, pager, filter or hook. A spelling never supplies a
missing root or mutation grant. Repository-local ignore files are read, and an
incomplete content or ignore scan fails instead of reporting a clean tree.

The profile is `core.symlinks=false` and `core.fileMode=false`: a symlink blob
becomes a regular file containing its target; executable/symlink tree and index
modes remain intact. Executable-bit changes are ignored. Missing timestamps force
content hashing so same-size edits remain visible. Clone retains full history;
submodules are not initialized and LFS pointers remain ordinary file contents.

## Stage, review and commit

Write literal identity settings to `home://.config/git/config`:

```ini
[user]
    name = Chronium
    email = chronium@example.invalid
```

Commit uses this identity for both author and committer, with checked wall time
and the configured timezone. Missing/unreadable or malformed identity refuses;
the file admits `[user]` name/email and the literal `[http "URL"] sslCAInfo
settings below, with no duplicates or includes. Identity is required only for
commit; HTTPS also parses the file if present. No global/repository identity override or
configuration-writing command is supplied.

```sh
git diff
git add README.md
git add subdirectory
git add -A
git diff --cached
git commit -m "Describe the change"
```

`add PATH...` accepts literal invocation-relative file/directory operands and
stages their tracked removals too. Use `--` before a name starting with `-`.
`add -A` stages the whole repository even from a subdirectory. Ignore rules
exclude new files but do not suppress tracked edits/removals. New regular files
use Git mode 100644; tracked executable and symlink modes remain intact.
Submodule changes are not staged; adding a new nested repository refuses
rather than flattening it into ordinary files.

`diff` compares worktree with index; `diff --cached` compares index with HEAD.
Untracked files do not appear in worktree diff. Text uses plain unified output
with three context lines; binary changes get a marker. Differences alone return
zero; input/read/output errors return nonzero. No pager, colour or path filter.

Commit accepts one nonempty `-m MESSAGE`, commits the existing index and prints
its hash. Detached HEAD, unresolved conflicts and an unchanged index refuse.
No editor, amend, automatic staging, empty commit or signing in this slice.
`$EDITOR` message entry is a later follow-up.

Repository commands find the nearest real `.git` along the retained cwd ancestor
chain. They stop at its authority boundary or malformed/inaccessible metadata;
they never recover authority from stale cwd text or a broader named root.
Invocation-relative add names are mapped by live directory identity before
library work, including after a repository rename. Status/diff are observations,
not snapshots of concurrent edits.

## Initialize and configure remotes

```sh
git init
git remote add origin https://git.internal/USER/PROJECT.git
git remote set-url origin https://git.internal/USER/PROJECT.git
git remote -v
git push -u origin main
```

`init` creates an unborn `main` in the current held directory. It refuses any
existing `.git`; a failed initialization retains and reports its partial
metadata. Existing ordinary contents stay untouched. The first nonempty commit
has no parent; unborn log/status/cached diff work without inventing a HEAD.
Remote changes hold `config.lock` through read/modify/sync/rename and admit one
HTTPS URL per name. Add refuses duplicates; set-url requires an existing remote.

## HTTPS credentials and additional CA trust

Store literal exact-origin entries in `home://.config/git/credentials`:

```ini
[credential "https://git.internal"]
    username = YOUR_USERNAME
    token = YOUR_TOKEN
```

Push requires a matching entry; clone/pull can remain anonymous. Host case and
omitted port 443 normalize. Aliases, subdomains, alternative IP spellings and
other ports never inherit credentials. Authenticated redirects stay on the
selected origin; anonymous redirects must remain HTTPS. Tokens are plaintext
home data, readable/replaceable by holders of the corresponding home authority;
there is no credential-manager or Unix-permission guarantee. Never put tokens
in command arguments, URLs, screenshots, logs or repository-local config.

For a private CA, add this to `home://.config/git/config`, not credentials:

```ini
[http "https://git.internal/"]
    sslCAInfo = home://.config/git/homelab-ca.pem
```

The selected regular PEM file is read through an already-held named root. Its
certificates augment the pinned public roots only for that exact origin,
including redirects; TLS certificate and hostname verification stay mandatory.
An absent, unreadable or malformed selected file refuses. Other origins retain
public roots. A shared system trust store remains later work.

## Push and pull

```sh
git push [REMOTE]
git push -u REMOTE CURRENT_BRANCH
git pull [REMOTE]
```

Push sends the current branch's checked commit only to its configured upstream,
requiring the same branch name (`push.default=simple`). Missing/differently named
upstreams, detached HEAD, unreadable commit tips and non-fast-forwards refuse.
Without `-u`, an explicit remote must be that upstream's remote. No implicit
all-branch or tag push, force, deletion or wildcard refspec. An absent upstream
produces a hint to use `push -u REMOTE CURRENT_BRANCH`.
That form accepts only the current branch and records the upstream only after
confirmed remote success, including an advertised already-current tip. Config
publication holds `config.lock` and rechecks the selected branch, remote and
previous upstream. A rejected or uncertain push never installs an upstream.
After successful setup, bare push uses it.

Remote success requires acknowledgements for the exact destinations requested;
already-current compares the advertised destination. Only then does local
tracking update. A local tracking failure after remote success says so. A
transport failure can leave publication uncertain: inspect the remote before
retrying. There is no automatic retry or rollback of a published remote branch.

Pull defaults to `origin`, fetching one branch with no tags, then fast-forwards
only a clean tracked tree/index. It re-checks after fetch, rejects divergence
and untracked collisions, and keeps an already-ahead local branch. Fetch may
leave objects/tracking refs after a refused local update. A missing remote
branch refuses; an unborn local branch can be populated. Checkout and index
completion precede the compared branch publication. An interrupted checkout
can leave partial files and locks without advancing HEAD; no automatic reset,
merge/rebase/stash or whole-checkout crash-atomicity claim is supplied.

## Authority and failure

The bundle requires memory management, native threads, clock READ/PRECISE/SLEEP
and RANDOM READ. TCP CONNECT, DNS UDP OPEN and `service/create` are optional,
so local log/status/add/commit/diff still launch offline. Clone needs those client-network inputs,
the existing read-only boot CA bundle, and writable destination authority. It
requests no LISTEN, launcher or job grant; Go's TCP/TLS client needs no separate
HTTP-provider grant. Existing launcher provider-inheritance rules still apply.
The caller selects directory roots; manifest declarations do not mint writable
filesystem rights.

Write-access handles perform real file sync on close; publication syncs the
source through a WRITE handle before a same-store rename. Index publication
holds an exclusive `index.lock` across read/modify/encode, sync/close and rename.
Commit holds `HEAD.lock` and compares the observed branch before publishing; ref
updates also use exclusive-create lockfiles. There is no fake directory sync,
cross-store copy fallback or
whole-clone crash-atomicity guarantee. A successful pre-rename sync alone does
not confirm the later namespace edit against immediate power loss.

First Ctrl+C requests cancellation. The transport observes its context;
checkout can finish in-flight work before reporting cancellation. Failure or
cancellation reports and retains the partial destination. Forced termination
can leave partial files and locks. Neither automatic resume nor stale-lock
stealing is provided; remove an incomplete clone explicitly before retrying.
There is no whole-clone deadline, byte or memory bound.

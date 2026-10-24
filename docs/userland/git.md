# Git

The optional first-party `git` package uses go-git v5.19.3 and go-billy v5.9.2.
This first slice supplies public HTTPS clone, log and status. Add, commit,
diff, authenticated push and pull remain separate [milestone slices](../wip/git-on-pyxis.md).
The [qualification record](../development/experiments/git-slice1/README.md)
distinguishes measured checks from the remaining work.

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
refuse. Only public HTTPS URLs are admitted, without embedded credentials,
query or fragment; redirects must stay HTTPS. Log/status run from the repository
root, including without network authority, and never write.

The working tree and real `.git` directory stay under that retained cwd. There
is no upward discovery, `.git` pointer file, alternate object store, linked
worktree, bare repository, global configuration/include, environment path
override, external helper, pager, filter or hook. A spelling never supplies a
missing root or mutation grant. Repository-local ignore files are read, and an
incomplete content or ignore scan fails instead of reporting a clean tree.

The profile is `core.symlinks=false` and `core.fileMode=false`: a symlink blob
becomes a regular file containing its target; executable/symlink tree and index
modes remain intact. Executable-bit changes are ignored. Missing timestamps force
content hashing so same-size edits remain visible. Clone retains full history;
submodules are not initialized and LFS pointers remain ordinary file contents.

## Authority and failure

The bundle requires memory management, native threads, clock READ/PRECISE/SLEEP
and RANDOM READ. TCP CONNECT, DNS UDP OPEN and `service/create` are optional,
so offline log/status still launch. Clone needs those client-network inputs,
the existing read-only boot CA bundle, and writable destination authority. It
requests no LISTEN, launcher or job grant; Go's TCP/TLS client needs no separate
HTTP-provider grant. Existing launcher provider-inheritance rules still apply.
The caller selects directory roots; manifest declarations do not mint writable
filesystem rights.

Write-access handles perform real file sync on close; publication syncs the source through
a WRITE handle before a same-store rename. Ref updates use exclusive-create
lockfiles. There is no fake directory sync, cross-store copy fallback or
whole-clone crash-atomicity guarantee. A successful pre-rename sync alone does
not confirm the later namespace edit against immediate power loss.

First Ctrl+C requests cancellation. The transport observes its context;
checkout can finish in-flight work before reporting cancellation. Failure or
cancellation reports and retains the partial destination. Forced termination
can leave partial files and locks. Neither automatic resume nor stale-lock
stealing is provided; remove an incomplete clone explicitly before retrying.
There is no whole-clone deadline, byte or memory bound.

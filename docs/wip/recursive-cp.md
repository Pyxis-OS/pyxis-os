# Recursive cp

Proposal, 2026-10-09. Nothing here is implemented. It settles the contract for the
deferred "recursive directory copying" item in
[technical debt](../technical-debt.md#native-cp-staging-and-recovery-limits); the
[current cp](../userland/cp.md) copies regular files only.

## Owner decisions

Defaults apply unless the owner answers otherwise.

1. **Existing destinations: refuse, never merge.** A copied tree's root is created
   with exclusive directory CREATE and must not exist. Nothing below the root is
   ever replaced, so file-versus-directory collisions cannot occur. The alternative
   is merging into an existing tree: replace files as today, fail on a kind mismatch.
2. **Mid-tree failure: stop that operand, keep what was copied, no rollback.**
   The alternative is to skip the failing entry, continue and exit with failure.
3. **Bounds: depth 32 below the root, 65,536 entries (files and directories) per
   operand, names up to 255 bytes.** These are cp settings like the 64 temporary
   candidates, not ABI.

## Syntax and destinations

`cp -r [--] source... destination`. `-r` is the only new option; `-R`, `-a` and
clusters stay rejected before opening paths. Without `-r` a directory source fails
as today. File and directory operands may be mixed; file operands behave as today.

- An existing directory destination receives each source at `destination/<basename>`.
  Trailing slashes on a directory source are ignored; a source with no basename
  (`home://`) needs a destination that does not exist.
- A single directory source with a destination that does not exist copies to that
  name; its parent must exist and is not created. Several sources need an existing
  directory, as today. A file destination with a directory source fails.
- The target root already existing, as a file or a directory, fails that operand
  before anything is created (decision 1).
- Empty directories are copied. No modes, owners or times are copied.

## Copying into itself

Handles are not object identities and paths are not canonical, so cp cannot compare
a source and destination directly. It proves the relationship with a unique name:
after creating the new root, cp creates a sentinel `.cp-tree-` plus 16 hex digits
inside it, then walks the whole source tree before copying any data. If any source
directory contains the sentinel, the new root lies inside the source: cp removes
the sentinel and the new root and fails ("destination is inside the source").
`cp -r a a` is refused this way, since it targets `a/a`. A destination beside the source,
or elsewhere under one of its ancestors, is not a cycle and is allowed.

The same walk checks every bound and entry kind, so an over-deep tree, an unsupported
entry or an unreadable directory normally fails before any file is copied. The tree
is live, so the copy pass enforces the bounds and kinds again.

## Atomicity and failure

Native directory rename is unsupported, so a tree cannot be staged and published as
one unit; other processes and a crash can see a partial tree.

- **Files:** each file is staged and renamed exactly as today, into the new
  directory, so a file never appears under its final name incomplete. Interruption
  can leave `.cp-` temporaries.
- **Directories:** a directory is created before its contents. Traversal is
  depth-first with unspecified order.
- **Failure:** the first read, create, write, rename, close or limit failure stops
  that operand (decision 2). cp removes only its own confirmed temporary file. It
  reports `cp: PATH: reason` with the path relative to the source root, then
  `cp: DEST: incomplete copy of SRC (N files, M directories created); nothing removed`.
  Unconfirmed outcomes stop the whole command, as today. Later operands run after
  an ordinary failure, with an aggregate failure status.
- **Cleanup:** there is no rollback. `rm` and `rmdir` are not recursive, so a partial
  tree is removed by hand, bottom up. This proposal adds no recursive remove.
- **Changed source:** enumeration reporting CHANGED is a failure ("source changed
  during copy"). cp does not restart, since a restart would revisit names it has
  already created. The copy is not a snapshot, and file contents keep today's
  initial-size rule.
- **Success** is quiet.

## Bounds and memory

Traversal is iterative over an explicit per-level stack, so the 1 MiB stack is never
used for recursion. Each level holds a source handle, a destination handle, a
16-byte enumeration cursor and a 256-byte name buffer, about 10 KiB at depth 32,
plus the existing 4,080-byte copy buffer. Heap use does not grow with the entry
count. About 70 handles are open at the deepest level. A longer name or a deeper or larger
tree fails with a limit error rather than truncating.

## Authority

No new grant is needed. Each source directory needs LOOKUP, ENUMERATE and READ_FILES
(reads stay READ alone). The destination parent needs LOOKUP, CREATE, WRITE_FILES and
REMOVE as today; created directories are requested with those same four rights, which
a child may always take from its parent. The sentinel and staging use CREATE and
REMOVE. A grant that lacks a right fails at that directory as an ordinary failure.

## Symlinks and special entries

Enumeration of a host export can report symlink, other and unknown kinds, and lookup
refuses them. cp never follows a symlink, recreates one or skips one silently: that
entry is an unsupported-entry failure ("PATH: symlink not copied"), found by the
pre-copy walk when possible. A copy that omitted entries must not report success.

## Validation after approval

Ordinary `make -j16 image`; interactive QEMU copies of a nested tree between archive,
RAM and `host://` with matching file hashes and listings. Refusals: existing target,
copy into itself, a host symlink, depth, entry and name limits, and a read-only
destination, each leaving the original intact and no `.cp-` files. A mid-tree write
failure and CHANGED are reviewed in code if they cannot be provoked.

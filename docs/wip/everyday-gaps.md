# Everyday gaps

Status: **running list, started 2026-10-07.** Small things that are missing or
awkward during real use of Pyxis. An entry here is a reminder, not a decision:
each one still needs a place in a milestone and the usual discussion before work
starts. Add a line when something is noticed, and remove it when it is done.

Commands follow the [ports and native commands](../development/ports.md#ports-and-native-commands)
boundary: anything that shows or changes a Pyxis concept is native, and familiar
text tools may be ports.

| Gap | Noticed | Likely kind | Notes |
| --- | --- | --- | --- |
| Moving directories | 2026-10-10, owner, after `mv` learned to move into a directory | Native, with libc `rename` | `mv` moves files only; libc `rename` and the native rename are file-only. The [directory move proposal](#directory-moves-proposal) is assigned to Codex alpha, 2026-10-12; contract accepted 2026-10-12; implementation authorized. No copy-and-delete fallback. |
| xfer inside a remote mux pane | 2026-10-12, owner, during JVM work | Native (remote client + mux) | Refused by design (`PYXIS_REMOTE_MUX=1`, see [remote-terminal.md](../userland/remote-terminal.md#remote-multiplexer)); workaround: a separate non-mux connection. Pane-aware transfer is the [initial remote multiplexer limits](../technical-debt.md#initial-remote-multiplexer-limits) debt item; owner: no rush. |

The [everyday commands](../userland/everyday-commands.md) reference covers `echo`,
`cp`, sorted/colorized `ls`, `tree`, `mv` and foreground shell-script launch authority.
The [multiplexer](../userland/multiplexer.md) now supports splitting in the
focused root shell's cwd and Ctrl+B digit focus (owner requests, 2026-10-11).

Related candidates already tracked elsewhere: grep, tail, wc, sort and hexdump in
[application ports](application-ports.md), and the boot configuration checker in
[boot configuration checker](boot-configuration-checker.md).

## Directory moves proposal

Status: **all three defaults accepted by the owner, 2026-10-12; implementation
authorized, not yet delivered.**
Scope is this gap. Inspection used Pyxis `8d8a1e39`, userland `5af803ef`
and filesystem `414be0ac`; no runtime qualification is claimed.

### Inspected starting point

The [native rename contract](../../include/abi/directory.h) takes two parent
capabilities and counted names, not paths. The [RAM implementation](../../kernel/object/directory.c)
transfers the child reference under the directory mutation lock, but accepts only
files. [npfs rename](../../kernel/fs/npfs_store.c) commits a namespace transaction
and likewise rejects directories; directory inode parent identity is validated
on lookup. Its worker serializes mutations. The
[path adapter](../../userspace/lib/path.c), [libc rename](../../userspace/libc/file_open.c)
and [mv](../../userspace/mv/main.c) already use the native operation.

The merged [FAT32 mounted writer](../devices/fat32-mounts.md) does have file rename:
the [kernel adapter](../../kernel/fs/fat32.c) and
[shared writer](../../fs/fat32/mounted.c) both exclude directories. Its retained
objects contain parent-dependent locators. A cross-parent directory move would
also need to change its on-disk `..` and repair affected held child locators;
merely changing the moved directory's entry is insufficient. The
[host adapter](../../kernel/fs/hostfs.c) also admits only files, and its external
name/type races differ from the kernel-owned stores.

### Accepted native behavior

Extend the existing rename operation to directories; source kind comes from the
entry under the mutation owner, not a caller's prior lookup. A same-parent rename
changes one entry name; a cross-parent move transfers the same directory identity
and its whole subtree within one store/mount instance. Both are one indivisible
namespace edit. Two scheme/root grants into the same instance are allowed when
both parents' rights suffice; a common disk or pool does not make different npfs
volumes or mount instances interchangeable. Writable RAM roots share the existing
RAM mutation domain. Cross-store/mount moves refuse before changing either name,
with no copy, recursive recreation or delete fallback.

Keep source-parent REMOVE and destination-parent CREATE, plus destination REMOVE
for REPLACE, as today. Holding a child alone gives no power over its parent name;
no new right or descendant traversal grant is created. Names remain single ordinary
components. Roots and final `.`/`..` cannot be moved. REPLACE may replace only an
empty directory with a directory, or a file with a file. Mixed kinds refuse;
nonempty victims return NOT_EMPTY. NO_REPLACE returns ALREADY_EXISTS for a distinct
existing destination. An existing entry moved onto itself succeeds without a
mutation or generation change, including NO_REPLACE, matching RAM/npfs today.

Reject destination equal to the source directory or anywhere below it with
BAD_REQUEST. Check live object/inode ancestry under the same mutation owner as
the final edit, never string prefixes or a userspace preflight. RAM needs internal
ancestry tracking or a bounded iterative tree walk. Its current creation path
does not take the rename mutation lock: all directory topology edits, including
creation, must participate in the serialized check/edit boundary. npfs uses
validated inode parents, as required by the [on-disk format](../../fs/docs/npfs-format.md). Neither exposes parent authority to callers. An incomplete bounded check
returns LIMIT without mutation; corrupt persistent ancestry returns IO. Recheck
names, victim emptiness and ancestry after any preparation that releases ownership.

Publish source removal, destination insertion and directory parent identity
together. Parent enumeration generations change once per affected parent;
the moved directory's unchanged children do not acquire a spurious change. A
replacement victim becomes detached, using existing empty-directory removal
semantics. npfs journals the parent identity with both directory edits and victim
retirement, completing at durable COMMITTED. A rename does not implicitly sync
dirty files inside the subtree; existing file-replacement ordering stays intact.
Ordinary known refusals leave the namespace unchanged; IO/OUTCOME_UNKNOWN retain
existing storage-failure semantics and must not be automatically retried.

### Held authority and command behavior

Open files/directories, cwd snapshots and delegated subtree roots keep the same
objects, rights and relative lookup behavior through the move. Never reopen them
from a descriptive spelling. Existing cwd chains retain their already-granted
handles: `..` still pops that chain, rather than discovering the new physical
parent. A delegated root cannot escape into its new parent. No global cwd repair
or new traversal authority is added. `getcwd` may become stale; a proved spelling
may fail, as documented in [working paths](../userland/process-state.md) and
[native paths](../userland/paths.md). Explicit later traversal can establish a new
chain if the caller already has the needed roots.

Libc `rename(old, new)` uses replacement semantics for files and directories;
it does not interpret an existing destination directory as a container.
Accept directory trailing separators by requiring that operand to be a directory;
retain root and final `.`/`..` refusal. `mv source... existing-directory` keeps its
current basename-to-child rule, now allowing directory sources; otherwise its
destination is the exact new name. Several moves are independent, not a batch
transaction. Report the actual refusal and return nonzero if any move fails;
remove the blanket “Directories cannot be moved” diagnostic.

Use existing statuses: unsupported directory moves or cross-store/mount moves
return BAD_OPERATION (libc ENOTSUP), read-only storage READ_ONLY (EROFS), nonempty
victims NOT_EMPTY (ENOTEMPTY), and kind mismatches WRONG_TYPE (currently EINVAL).
The first slice does not claim POSIX EXDEV or exact EISDIR/ENOTDIR reporting where
the existing native result cannot distinguish them. Successful same-store moves,
empty-directory replacement and held identity follow the familiar rename behavior
without inventing success or implementing copying.

### Accepted decisions — 2026-10-12

1. Atomic same-parent and cross-parent moves within one instance, with cycle
   refusal, same-kind empty replacement and the rights/refusal categories above.
2. Held objects, delegated roots and cwd chains survive; descriptive cwd text
   may become stale. Moves do not grant the new parent or repair existing chains.
3. RAM and writable npfs first. FAT32/host directory moves return BAD_OPERATION;
   read-only stores return READ_ONLY. No cross-store copying. FAT32 directory
   moves wait for held-locator repair, `..` publication and qualification of its
   ordered, non-journaled failure behavior.

### Authorized implementation and qualification

- [ ] Native RAM/npfs directory moves and libc/`mv` delivery.
- [ ] QEMU: same/cross-parent moves, empty/nonempty replacement, self/descendant
  refusal, cross-store/mount refusal and trailing separators. Keep directory/file
  handles, cwd chains and attenuated delegated roots live across moves; exercise
  relative lookup and bounded `..`. Reopen npfs after synchronization/reboot and
  check parent metadata and namespace consistency.

# Native npfs hard links

Writable npfs volumes support multiple names for one regular inode. Aliases
share contents, size and live object identity; they do not copy data or share
extents between different inodes. Directories keep unique ancestry. Symbolic
links, cross-volume links and links through a held orphan FILE are unsupported.

## Interface and authority

[DIRECTORY_LINK](directories.md#native-regular-file-links) takes a source parent
and component name, plus a destination-directory grant and component name.
Source LOOKUP, READ_FILES and WRITE_FILES are required; destination CREATE
suffices. There is no persistent rights ceiling on a name, so partial source
content authority cannot gain its missing right through an alias. Path helpers
add the ordinary LOOKUP requirements for traversal. Both delivered grants retain
their captured authority; later CLOSE or CUT does not undo a completed edit.

SDK `directory_link` and `path_link` expose this operation. Libc `link` and
`ln SOURCE DEST` / `ln SOURCE... DIRECTORY` use it without a copy fallback.
`ln` accepts `--`, with no symbolic-link or forced-replacement flags. Destination
directory detection performs no provider FILE open: an HTTP destination refuses
without issuing a GET. Existing destination names return ALREADY_EXISTS/EEXIST;
directories return WRONG_TYPE/EINVAL; mixed backends or volumes return
BAD_OPERATION/ENOTSUP. Uncertain I/O remains an error, without automatic retry.

[FILE_INFO](file-metadata.md#link-count) reports the number of names on npfs
regular files, including effective one on legacy pools and valid zero on held
detached files. This is distinct from held-reference counts. Other backends and
directories leave LINK_COUNT unavailable unless they supply a real count. Libc
exposes `st_nlink` only with `STAT_NLINK_VALID`; callers must check validity.

## Mutation, lifetime and durability

Namespace insertion/count increment and removal/count decrement are atomic in
the running transaction. Count edits preserve the durable size/mapping prefix
and pending dirty data; adding a name does not publish unwritten growth. Moving
a name preserves its count; replacement decrements the victim's count. Rename
between aliases of the same inode succeeds without removing either name,
including NO_REPLACE. Shared shrink affects that inode through every alias.

Only last-name removal marks DETACHED. Held handles remain usable at count zero;
bounded reclamation waits for their release. Reboot resumes cleanup after
pre-crash handles are gone. LINK cannot recreate a name from an orphan handle.
These edits use the existing filesystem worker, allocation reservations, undo,
data-before-mapping ordering and staged-image reuse exclusion.

LINK and rename do not imply data or namespace durability. Stable publication
writes a new version, links a temporary name and renames it over the stable name,
then uses the existing explicit sync contract. A held old file still identifies
and reads the replaced version. See the native adapter's
[durability contract](../devices/filesystem-native-adapter.md#writeback-recovery-and-errors).

## Format and first use

The [npfs format](../../fs/docs/npfs-format.md) stores a little-endian 32-bit
regular-file name count in the inode reserve, gated by a read-only-compatible
hard-links feature bit. `UINT32_MAX` is the limit; overflow refuses before
activation or namespace effects. FREE/DETACHED use zero; directories have no
count field. Without the bit, the reserve is ignored and legacy implicit one/zero
rules apply. After feature gain, untouched attached legacy files with stored zero
still mean one; feature-aware encoders write explicit positive counts.

The first eligible LINK on a legacy pool validates the request, writes one header
with the bit, flushes, writes the other, flushes, then stages the link. Mount does
not activate the feature; formatting may set it. The bit stays after aliases
disappear. Headers are otherwise immutable while mounted. Failed/uncertain
activation stages no link and follows existing I/O-failure rules.

Two valid headers may differ only in additive feature masks: one copy's three
masks must be supersets of the other's, at least one strictly so. All other bytes
except CRC agree. Open selects the superset and applies feature admission to it;
a writable open repairs and flushes the lagging copy, while read-only opening
does not repair. Unknown features cannot fall back to a weaker header. Older
kernels reject a pool after a crash within the two-flush transition window;
older writers refuse the enabled feature even after the headers agree.

Fsck counts incoming names and checks effective counts, visiting each inode's
mapping once. It still rejects cross-inode block sharing, directory aliases or
cycles, linked DETACHED files and broken cleanup lists. Count mismatches fail
without guessed repair. Replay validates whole transactions; host fsck checks
global structure afterward. Kernel mount performs local validation, not fsck.
Only count-aware writers/replayers explicitly admit the feature. Older readers
remain usable; older checkers may reject aliases.

## Qualification and deployment limits

The [qualification record](../development/experiments/npfs-hard-links-task4/README.md)
records builds, one-/four-CPU QEMU, GDB lifetime/count-limit inspection, checkpoint
and committed host/kernel replay, and repeated matched nested workloads. The
maximum-count check exercises the guard, not a pool with billions of names.

The owner passed native stable publication and held-file lifetime on the
ThinkPad with an approved disposable SanDisk stick on 2026-10-19. Verification
after a real reboot passed names, counts, shared identity and 65,536 expected
bytes; no installed pool was converted. Native power loss, native A/B timings
and a retained live read-only→writable header-repair upgrade are unmeasured.
Fresh read-only/non-repair and writable/repair openings were qualified in QEMU.

INFO grew from 48 to 56 bytes and libc metadata grew. Rebuild integrated consumers
together; independently published toolchain bundles must be refreshed against
the new SDK before use. No compiler/container rebuild is required. Package
integration and dynamic linking remain separate work; symbolic links remain a
later milestone.

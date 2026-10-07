# Filesystem mutations

The native directory protocol supports creation, removal and atomic file
rename/replacement. These operations act on single component names through
capabilities; libc and libpyxis resolve paths in userspace. The initrd remains
immutable. RAM data survives process exit, not reboot.

Removal requires REMOVE on the parent, independently of the child's READ/WRITE
rights. Files and empty directories can be removed; nonempty directories fail.
Existing file handles retain their object. A detached directory can remain
open, but rejects creation of new children. Enumeration returns copied names
and reports CHANGED when its generation no longer matches.

RAM rename requires REMOVE on the source parent and CREATE on the destination;
replacing another entry also requires destination REMOVE. Both parents must
refer to writable RAM directories. The operation supports explicit replace or
no-replace policy, and failure preserves both entries. No missing-destination
interval is observable during replacement. Existing destination handles retain
the old object. A rename to the same entry is a successful no-op.

`directory_remove` and `directory_rename` expose native component operations;
`path_remove` and `path_rename` resolve parents using caller-owned scratch space.
Libc provides `remove` and file-only `rename`. The `rm`, `rmdir` and `mv` utilities
exercise these interfaces; `mv` takes an exact destination file path, without
appending a source basename or falling back to copy-and-delete.

[Doom saves](../userland/doom.md#saves) use these operations to write and close a temporary
save, then atomically replace the selected slot under
`home://doom/saves/<iwad-name>/`. Shareware and retail saves are separated.
Configuration persistence remains independent and disabled.

Directory moves, hard links, recursive deletion and cross-filesystem moves are
not implemented. Directory moves need cycle prevention, lock ordering and a
policy for retained working-directory chains and `..`. The filesystem does not
automatically create missing parents; applications create the directories they
need explicitly. Native storage uses the [mounted pool writer](../devices/filesystem-native-adapter.md)
through these same component operations.

## Native persistent backing

Native CREATE, REMOVE and regular-file RENAME use the same capability checks.
Parents for native rename must belong to one volume; mixed backends return
BAD_OPERATION. REPLACE requires destination REMOVE even when its name is absent.
The serial worker stages CREATE's returned capability before publishing the entry;
failure unwinds that handle. Names are counted UTF-8 components of at most 255 bytes.
No on-disk principal or permission grant participates.

Unlink/replacement records DETACHED cleanup in the same transaction as the name
edit. Successful namespace mutations reach durable COMMITTED before returning.
Surviving handles keep the inode usable. Reclamation waits for references to
drain and persists pointer/bitmap progress in bounded batches, without allocating
new disk blocks. SHRINK may coexist with DETACHED; subsequent writes/resizes wait
for shrink cleanup. Reads respect the smaller size. Growth supplies or zeros newly
exposed bytes, including a retained partial tail, before size publication.

File WRITE permits writes, resizing and sync; size queries accept READ or WRITE.
Directory sync requires CREATE or REMOVE. File/directory sync commits the
pool's current transaction with its ordered-data dependencies; mount WRITE permits
MOUNT_SYNC across the configured disk's mounted pools. Success reaches durable
COMMITTED; checkpoint and durable EMPTY follow before journal/freed-block reuse.
Large writes and cleanup do not promise whole-operation crash atomicity.

Close releases the process's wrapper/cleanup charge and promises no durability.
The mounted pool retains dirty contents and writeback errors after handles close.
Sync reports and acknowledges retained recoverable errors; later sync can succeed
after dirty data is durable. An ongoing failure still fails each attempt. Uncertain
disk I/O stops mutation for the boot and remains an error on every sync.
Periodic full flushing defaults to 30 seconds through menuconfig, and pressure
writeback runs asynchronously. Delayed allocation can encounter ENOSPC at writeback
or sync. Power-off and restart write all dirty data and empty each writable
pool's journal first ([ACPI power-off](../kernel/acpi.md#power-off-and-restart));
there is no sleep.

Read-only mounting requires an EMPTY journal. Writable opening replays a validated
committed log; unknown read-only-compatible features forbid recovery writes.
Ordinary QEMU mutation, synchronization and clean-reboot persistence results are in the
[measurement record](../development/experiments/native-filesystem-task3/README.md).
Committed-log recovery and retained-open unlink were reviewed in code, without
runtime crash injection or a retained-handle exercise.

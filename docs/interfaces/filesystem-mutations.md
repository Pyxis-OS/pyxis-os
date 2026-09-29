# RAM filesystem mutations

The native directory protocol supports creation, removal and atomic file
rename/replacement. These operations act on single component names through
capabilities; libc and libpyxis resolve paths in userspace. The initrd remains
immutable. RAM data survives process exit, not reboot.

Removal requires REMOVE on the parent, independently of the child's READ/WRITE
rights. Files and empty directories can be removed; nonempty directories fail.
Existing file handles retain their object. A detached directory can remain
open, but rejects creation of new children. Enumeration returns copied names
and reports CHANGED when its generation no longer matches.

Rename requires REMOVE on the source parent and CREATE on the destination;
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
need explicitly. These operations do not introduce persistent storage or a
mount framework.

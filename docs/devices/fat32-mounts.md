# Native read-only FAT32 mounts

The BSP filesystem worker serves supported GPT FAT32 partitions through native
FILE/directory objects. The [shared engine](../../fs/docs/fat32-read.md) validates
both GPT copies and the whole volume before admission. Subsequent reads remain
confined to the selected physical extent. No write, flush, repair or implicit URI
mount is performed. Keep media quiescent; raw read handles and external writers
do not provide snapshots.

## Authority and interfaces

[`DISK_OPEN_FAT32`](../../include/abi/disk.h) requires MOUNT on an explicitly selected DISK and takes a
one-based GPT entry plus exact LOOKUP/ENUMERATE/READ_FILES rights, independently
narrowed, including zero. A successful call installs a root and bound MOUNT
control in one capability-table batch. Both slots are reserved before work;
copying the reply into user memory is a separate operation. A failed reply closes
this call's new handles and drains provisional ownership; escaped sibling grants
and already published instances are preserved.

The [control protocol](../../include/abi/mount.h) grants OPEN_ROOT, OBSERVE and RETIRE. OPEN_ROOT accepts only rights
within that opener's original ceiling and returns another root for the same
instance. OBSERVE returns genuine domain, device, GPT entry, physical range,
retired status and observed FAT clean/error flags. Flags are observations, not
integrity or durability promises. Closing a control never retires a mount.

FILE READ/SIZE/INFO, directory LOOKUP/ENUMERATE/INFO and selected-executable
capture use the existing native protocols. Capturing uses the common 128 MiB
serialized-image ceiling; capture backing ceases to pin the mount when its FILE
and operation references drain. Data grants never confer mounting or retirement
authority. Mutation is unavailable. DIRECTORY_FILESYSTEM_INFO remains
BAD_OPERATION because its existing record describes npfs pools.

The [metadata contract](../interfaces/file-metadata.md) reports genuine
domain/object identity, shared by live long-name/ASCII-alias references. Empty
files remain distinct. Full close and remount promise no identity continuity.
File size is valid; directory size and DOS-local modification time are not.
Enumeration returns valid UTF-8 names; lookup supports ASCII long names and
short aliases, case-insensitively. Dot entries confer no parent authority.

## Exclusion and retirement

One worker-owned registry tracks pending admissions, active FAT instances,
retained npfs pools and raw claims by inventoried device and checked half-open
physical range. Reserve before probing. Exact FAT opens share one instance;
other overlapping admissions return BUSY. Whole-disk claims overlap every range;
disjoint partition claims preserve existing transport/NVMe policy. A raw claim
still remains unique per device. npfs retention and duplicate-pool policy are
unchanged.

RETIRE requires its separate right and returns BUSY until data wrappers,
delegated/cwd/queued grants, admitted operations and deferred destruction drain.
It revokes nothing and does not wait for the caller to close references. Control
aliases and the retiring request itself are exempt. OK releases workspace and
exclusion on the worker. Controls then become inert tombstones: OBSERVE and
repeated RETIRE remain safe; OPEN_ROOT returns UNAVAILABLE. New admission gets a
new domain. A completed failed read drains normally.

Final-control close retains a published instance until an authorized opener
reacquires control and retires it, or reboot. This intentionally bounded retention
also applies when its controller faults. Unpublished failure instead releases
backing and reservations after all owned/escaped references drain. No user buffer,
AP stack or held lock crosses storage waits.

Admission settings in the [adapter](../../kernel/fs/fat32.c) are four active instances, 32 MiB aggregate FAT workspace,
the shared 1 MiB/1024 native-wrapper budget, depth 64, 67,108,864 work steps and
16,777,216 sectors per engine operation. The existing serial worker admits at
most 32 jobs with a cooperative 30-second deadline. Pending charges count before
allocation; LIMIT and NO_MEMORY remain distinct. These are implementation settings,
not FAT format limits or a total-memory guarantee.

## Trusted browsing profiles

Build the default image. **Browse scratch FAT32 (native RO)** opens a displayed
disk/GPT entry explicitly through the install-only inventory authority. It closes
the broad DISKS service after selection and retains the selected read-only disk
and bound control. **Browse boot ESP (native RO)** uses only the trusted
boot-provenance issuer: owned normalized Limine disk/partition UUIDs and media
kind must identify exactly one healthy GPT ESP. UUIDs select media, not authenticate
it. Optical/PXE, missing or ambiguous provenance supplies no guessed authority or
fallback to an attached disk. Boot options/configuration cannot replace the IDs.

At `fat32-mount>` use `ls /PATH`, `cat /PATH`, `info /PATH`,
`info /PATH | /OTHER_PATH`, `read OFFSET LENGTH /PATH`, `state`, `close`, `open`,
`retire` or `quit`. Literal spaces are accepted; there is no shell quoting or
expansion. Read only known text at the console: contents can contain control bytes.
`close` releases the held root. `open` reopens the bound active instance or admits
a new one after retirement; `quit` closes data and explicitly attempts retirement.
Errors are reported without automatic retry or repair.

Direct `ls`/`cat` children receive an explicit read-only `esp` startup root,
memory and console streams, with no DISKS, MOUNT, control or descendant launcher.
This is a trusted browsing bootstrap, not an installed shell binding, generic
mount command, editor or writable ESP workflow.

The [qualification record](../development/experiments/fat32-native-mount.md)
separates QEMU/debugger evidence from later owner-routed native ESP checks.

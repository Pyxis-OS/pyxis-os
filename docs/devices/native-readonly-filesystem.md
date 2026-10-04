# npfs mounts

Trusted init selects a GPT partition and native volume, then delegates ordinary
file and directory capabilities. Caelum uses the npfs format-only library and
its kernel inode/cache/writer engine. The old principal-based format is not the
mounted format. Default boot requires no disk or mount configuration.

The authoritative [encoding](../../fs/docs/npfs-format.md) and
[host tools](../../fs/docs/npfs-host-tools.md) live in pyxis-fs. The
[kernel adapter](filesystem-native-adapter.md),
[directory interface](../interfaces/directories.md) and
[mutation contract](../interfaces/filesystem-mutations.md) describe kernel use.

## Configure and delegate

Set `MOUNT_DISK=<canonical-GPT-GUID>` for image assembly. No principal is
supplied. Absent configuration disables native mount authority; malformed or
duplicate configuration fails. Attach the selected disk explicitly.
`VIRTIO_BLK_IMAGE` supports VirtIO, with `VIRTIO_BLK_READONLY=1` for read-only
use. Enabled xHCI also supports boot-present USB BOT disks through discovered
controllers and hubs. USB disks with known clear write protection and successful
blocking cache-synchronization qualification support explicitly requested
writable mounts; other healthy disks retain read-only mounts. Startup does not
probe volumes.

`MOUNT_OPEN_VOLUME` requires OPEN_ROOT and takes a one-based GPT entry, counted
volume name and exact directory rights including LOOKUP. CREATE, WRITE_FILES or
REMOVE additionally require mount WRITE. FILESYSTEM_INFO independently requires
mount OBSERVE. WRITE authorizes an attempt; device and feature restrictions can
still refuse it. Pool/volume IDs identify storage and confer no authority.

The configured GUID selects among all per-device GPT snapshots after sealed boot
discovery and terminal scans; it does not authenticate contents. Duplicate
matching disk GUIDs are an error. A complete empty inventory at scope creation
omits authority. Pending or partial discovery retains the configured scope; the
sole observed valid GUID match may mount even when discovery is incomplete.
Missing devices could conceal a duplicate GUID. Without a match, complete
discovery reports NOT_FOUND and partial discovery reports UNAVAILABLE.
Selected-device errors never choose another disk. Optional mounting suppresses
only absent authority. A live installer raw claim prevents mounts on its device.
The GPT type is not a filesystem selector, and failure does not probe another
partition. Namespace binding names belong to the caller, outside the disk
format. Applications receive only explicitly delegated roots and cannot
reacquire withheld rights through a parent inode.

Prepare and check a standalone pool before copying it into its GPT extent; see
[disposable disks](filesystem-native-adapter.md#prepare-a-disposable-disk).
Do not modify an attached image externally. Guest read-only attachment does not
prevent host writes, and concurrent external writers are unsupported.

## Rights, identity and lifetime

Every operation carries the calling capability's actual rights. Child directory
rights are a subset of its parent's grant; child file READ/WRITE require parent
READ_FILES/WRITE_FILES. FILE_SIZE accepts READ or WRITE; executable capture requires READ.
WRITE authorizes write, resize and file sync, including validation of zero-length
writes. Parent CREATE and REMOVE authorize namespace mutations independently of
child file rights. Directory sync requires either CREATE or REMOVE.

Repeated mounts of the same partition extent share a pool. Wrappers retain inode
references independently of parent directories and mount authority. One BSP worker
owns persistent inode, cache and writer state. Final wrapper retirement releases
its reference and process cleanup charge; it neither flushes nor discards dirty
contents. Mounted pool state and writeback errors remain available for later
synchronization after all process handles close. Clones with a duplicate pool ID
cannot be opened simultaneously on another extent. Any retained pool blocks
exclusive raw writing on its device even after its final handle closes; the
[installer disk service](installer-authority.md) has no teardown exception.

Enumeration uses the shared directory inode's generation and an opaque position.
Independent handles observe the same generation. Mutation invalidates an acquired
cursor and returns CHANGED on the next call; restart from zero explicitly. Short
name buffers preserve the cursor and name buffer. No cursor grants authority or
creates a snapshot.

## Durability and recovery

File and directory sync commit the pool's current metadata transaction after
satisfying ordered-data dependencies. Mount `MOUNT_SYNC` requires WRITE and covers
every mounted pool on the configured disk. Success waits for all work covered by
the call to reach durable COMMITTED; stable images checkpoint in the background
before the journal can be reused. A small sync may wait for unrelated dirty data.

Unsynced data remains in the cache until periodic flushing or memory-pressure
writeback. The interval defaults to 30 seconds through menuconfig and is nominal,
not a crash-loss bound. Allocation is delayed until writeback, so cached writes
can encounter insufficient space later. Close promises no durability. No shutdown,
restart or sleep flush hooks exist yet.

Read-only opening refuses a COMMITTED journal. Writable opening validates and
replays it before reading checkpointed metadata. Unknown required features reject
opening; unknown read-only-compatible features prohibit writes including replay.
A committed log with invalid payload fails recovery. Uncertain write/flush failure
stops mutation and retains cached state and the error; it promises no rollback.
Checksums cover headers/journal, not home metadata or file contents. Host fsck
provides structural reconciliation, not arbitrary repair or content verification.

## Scoped information

FILESYSTEM_INFO copies pool/volume IDs, name, journal sequence, mode and independent
GPT/filesystem degradation flags without traversal or new authority. Capacity is
`(pool_blocks - 2) * 4096`: it includes shared metadata and reserves, not a volume
allowance. Usage, free bytes, quotas and percentages remain unavailable. Do not sum
capacity for bindings or volumes sharing a pool ID.

## Combined workflow validation

Ordinary four-CPU QEMU boots exercised writable and read-only grants, cached
write/read/sync, resize, rename/replacement, removal, native executable capture
and persistence across clean reboots. Host fsck and extracted-file comparisons
passed. The
[measurement record](../development/experiments/native-filesystem-task3/README.md)
contains the obsolete-adapter baseline, writer measurements and validation limits.
Committed-log recovery and memory-pressure failure paths were reviewed in code;
these boots do not qualify power-loss recovery or physical media.

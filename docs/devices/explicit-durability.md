# Explicit filesystem durability

Implemented for npfs namespace/file writeback and the mounted FAT32 writer;
qualified through the native measurement gate on 2026-10-20.

## Durability points

Mutations update the live filesystem view. Closing a handle releases ownership;
it does not promise durability. Explicit synchronization blocks until its scope
has reached the filesystem's durable completion point:

| Operation | npfs | Mounted FAT32 |
| --- | --- | --- |
| FILE SYNC / libc `fsync` | Write back that file, then commit the running metadata transaction | Drain the mounted instance |
| DIRECTORY SYNC | Write back every dirty file in the pool, then commit | Drain the mounted instance |
| MOUNT SYNC | Synchronize the selected disk's pools | Drain the mounted instance |

Background flushing defaults to 30 seconds. Pressure, capacity admission,
retirement where applicable and clean power-off/restart also drain pending work.
The interval is not a guaranteed crash-loss bound. Callers requiring persistence
must synchronize explicitly, including the containing directory after publishing
a name. Shell history, configuration publication, xfer and installed-tool
publication use explicit sync points. Neither close nor a successful rename
alone replaces them.

## npfs running transaction

Namespace edits, volume creation, shrink, detached reclamation and file
writeback stage metadata in one bounded pool-wide redo transaction. Operations
reserve their worst-case metadata and allocation capacity before staging. If the
running transaction cannot admit the operation, the worker commits and
checkpoints first; it never retries a partially staged allocation. Undo restores
the operation's changes without dropping earlier staged work.

Writeback writes data before staging the mapping and length that expose it. The
commit flushes the device before publishing durable COMMITTED. Journal space,
previously durable allocations and homes with retained staged metadata images
cannot be reused until their required checkpoint reaches durable EMPTY. This
also protects metadata allocated and freed in the same transaction.

A replacing rename writes back the moved file and stages its mapping before the
name change; the contents commit no later than the replacement name. Shrink
writes back its target before publishing the smaller size. Ordinary overwrites
remain in place: this does not make file contents or torn blocks atomic.

The [writer contract](filesystem-native-adapter.md#writeback-recovery-and-errors)
and [mutation interface](../interfaces/filesystem-mutations.md) define capacity,
error ownership, held-file cleanup and validated committed-journal replay.

## FAT32 ordered batches

The writer retains an owned coherent pending sector view; reads and validation
observe it before checkpoint. Cluster allocation occurs during mutations. Each
operation reserves its complete workspace before publication; pressure drains
the previous batch first. The default pending-image target is 4 MiB, tunable
through `CONFIG_FAT32_BATCH_MIB`. Larger individual operations use the existing
ordered emitter synchronously rather than acquiring a new file-size limit.

Dirty marking and required mirror repair precede user effects. Checkpoint writes
nonempty DATA, FAT additions, DIRECTORY, RECLAIM and FSINFO phases in order,
flushing after each. Freed clusters remain unavailable until the checkpoint
completes. A replacing rename first checkpoints the mount. An uncertain write or
flush stops that writer and retains its diagnostics/backing; sync never reports
success for an unconfirmed checkpoint.

FAT32 is unjournaled. Phase ordering and reuse safety do not provide atomic
multi-sector rename or crash replay; interruptions can require external repair.
See [FAT32 mounts](fat32-mounts.md) for authority, removal and failure rules.

## Qualification and remaining costs

[Native measurements](../development/experiments/explicit-durability-measurements.md)
compare matched barrier policies on the ThinkPad's disposable 32 GB SanDisk:
extraction plus sync fell from 441.6–455.1 s to 76.2–78.5 s; FAT32 staging plus
sync fell from 11.6–12.1 s to 9.5–10.1 s. These are two samples per policy,
not a general throughput guarantee. Earlier nested measurements and crash checks
remain linked from that record. Near-full reservation precision, small USB
writes and repeated FAT chain-validation reads remain
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1467).

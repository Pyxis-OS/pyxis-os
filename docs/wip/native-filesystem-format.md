# Native filesystem format decisions

Task 1 proposed the format; task 2 implements its codecs and host tools in the
filesystem repository. The authoritative [encoding contract](../../fs/docs/native-format.md)
and [host-tool guide](../../fs/docs/native-host-tools.md) live there. Caelum now
uses these codecs for the [native pool writer](../devices/filesystem-native-adapter.md);
its caching and allocation policy belong to the kernel.

## Decision status

The owner accepted the core geometry and record budgets on 2026-10-03: 4 KiB
blocks, little-endian 64-bit partition-relative pointers, 64 growable volumes,
one shared bitmap, 256-byte inodes, twelve direct and single/double/triple
indirection, simple directory lists and one metadata redo journal. The inode's
72-byte current area, 120 pointer bytes and separate **64-byte future reserve**
remain intact. Timestamps and parent consume 24 formerly spare current bytes,
leaving 12 unassigned bytes. Record offsets, constants and other reserved areas
are now implemented details documented with the codecs.

The [task-1 review](https://git.internal/PyxisOS/pyxis-os/pulls/338) added three
feature classes, creation/modification timestamps, internal directory parents,
mount-time free-inode state and sync completion at durable COMMITTED. Times use
signed 64-bit Unix nanoseconds (UTC), eight bytes each, with saturation on write.
Unknown affected times permit mutation when the wall clock is unavailable;
validity bits distinguish unknown from known zero or clamped endpoints.

For task 2 the owner accepted both proposed defaults: **checksums only on headers
and journal in v1**, and **refuse read-only opening with a committed journal**.
Writable host fsck performs replay; no RAM recovery overlay exists. Unknown
required features refuse opening, unknown read-only-compatible features refuse
writes including replay, and unknown compatible features can be safely ignored.
Home metadata and file contents have no checksums; structural checking is not
arbitrary corruption detection or repair.

## Journal capacity and recovery

The installer/formatter chooses journal size **per pool**, not through a build
setting. The 256 GB target starts at **at least 128 MiB**. Smaller-image invocations
choose an explicit size; the initial host formatter supplies no automatic default.
The region remains fixed in v1. Capacity is not a requirement to fill/write the
whole region each commit. The kernel writer requires capacity for at least
eighteen images before writable opening, independently of the format validator's
smaller minimum. This fits its
indivisible namespace transactions; capacity alone does not increase cache size.

Only one full-metadata-block transaction commits/checkpoints at a time. Ordered
data and payload become durable before COMMITTED; only then may metadata reach
homes. Replay validates the entire log before home writes, without traversing a
partially checkpointed home tree. Invalid committed payload is an error. Homes
must be flushed before publishing/flushing next EMPTY; journal space and freed
blocks cannot be reused earlier. Sequence numbers never wrap. Header conflict,
unknown checked control states and uncertain write/flush failure stop use.

The owner accepted `fsync`/`sync` completion at durable COMMITTED after required
data, with stable checkpoint images processed in the background before the next
commit. Work spanning batches waits for all covering commits and data. Measure
foreground latency separately from the four-flush transaction throughput cost.
Close only releases a handle and has no durability promise.

## Kernel writer contract

Persistent per-volume cleanup lists track DETACHED, SHRINK or both. Unlink,
replacement and shrink publish length/namespace/list changes atomically. Cleanup
removes highest mappings and bitmap bits together in bounded transactions without
new disk blocks; cleared pointers are durable progress. Detached directory cleanup
may retain its length while its empty storage is reclaimed. No surviving handle
references or operations may access an inode/block after reclamation or reuse.
After reboot old handles no longer survive; host fsck checks but does not reclaim
pending cleanup.

Shrink stalls later writes/resizes of the same inode; reads respect its smaller
size. Growth must supply/zero newly exposed bytes, including retained block tails,
before publishing larger size. In-place data changes and work spanning batches
promise no whole-operation content atomicity. Preserve the
[handle-lifetime contract](../interfaces/filesystem-mutations.md).

Build and maintain an in-memory free-inode list at mount after recovery; creation
must not scan the whole inode file. Measure mount scan/memory separately. Directory
parents are internal metadata and grant no parent capability; moves must preserve
backlinks and acyclic ancestry. There are no on-disk users, permissions, symlinks,
hard links or volume quotas in v1.

## Task-3 writeback policy

The owner settled the three remaining writeback choices on 2026-10-03:
[periodic full flushing](native-filesystem.md#accepted-writeback-details) defaults
to 30 seconds through menuconfig, `fsync` commits the whole current transaction,
and allocation is delayed until writeback. The task-3 kernel writer implements them.

No compiler rebuild was required by task 2. Initial host-tool timing and write-byte
observations are in the tool guide; kernel latency and virtual-device write/flush
counts are in the [task-3 measurement record](../development/experiments/native-filesystem-task3/README.md).
Physical SSD wear remains unmeasured. See the
[design limits](../technical-debt.md#native-filesystem-design-limits).

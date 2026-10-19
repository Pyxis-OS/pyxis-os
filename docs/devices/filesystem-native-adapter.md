# npfs kernel adapter

Caelum links the npfs format-only codecs and owns persistent pool, inode, cache
and writer state in the kernel. One serial BSP worker serves the existing file,
directory and mount protocols. The authoritative
[format](../../fs/docs/npfs-format.md) and
[host-tool guide](../../fs/docs/npfs-host-tools.md) remain in pyxis-fs.

## Build and ownership

[Build rules](../../kernel/fs/build.mk) compile the format library freestanding;
it contains no I/O, allocator, cache or authority policy. Caelum supplies its
memory symbols and implements block access and allocation policy. Host formatting,
inspection and whole-image checking remain separate tools. The existing compiler
is sufficient; no container rebuild is needed.

The [job interface](../../include/kernel/fs/npfs.h) uses shared kernel storage.
Trusted ROOT submission supplies a configured disk GUID or an installer disk
object's boot-device ID, plus GPT entry, volume name and exact directory rights
including LOOKUP. GUID selection searches all completed per-device GPT snapshots
and rejects duplicate matches; device selection uses only that inventoried ID.
Incomplete inventory and live raw claims prevent opening. Installer disk objects
permit only read-only roots through this same path; see
[installer authority](installer-authority.md). User handlers capture requests and
validate buffers before publishing to the BSP executor, which forwards them to
the worker. No user pointer, replaceable capability entry or AP stack crosses to
the worker. The calling capability retains its node through the uninterruptible
wait. Completion detaches node, destination, table and queue loans before waking.

ROOT/LOOKUP transfer one object reference on success; CAPTURE transfers owned
launch staging. Callers detach outputs before releasing the reusable request.
CREATE alone lends the caller's capability table exclusively. The worker stages
its returned capability before publishing the namespace edit and unwinds the
capability if creation fails. Rename borrows both parent capabilities through
completion. Group stop waits for active loans to return.

The worker owns engine calls with interrupts enabled. Short queue/reference,
allocator and block submission sections run with interrupts disabled; no held
lock spans disk waits. Native wrappers embed file/directory objects and retain
inode references, without using RAM file buffers or directory entry lists.
Retirement transfers wrappers to the worker without allocating. It releases the
process cleanup charge while dirty contents and errors remain pool-owned.
Same-device, same-extent mounts share retained pools; another extent with the
same pool ID is rejected. Closing all wrappers does not evict mounted pool state or synchronize it.
A retained pool excludes overlapping raw-write claims, including when read-only
or its final handle has closed. The shared worker-owned
[extent registry](fat32-mounts.md#exclusion-and-retirement) also covers FAT mounts
and pending admissions. Whole-disk claims overlap every extent; a claim on a
disjoint partition remains subject to the existing device policy.

## Rights and ordinary calls

The handler and worker both check actual caller rights against the retained node's
ceiling. Lookup grants only attenuated child rights. Names are case-sensitive UTF-8
components of 1–255 bytes, without NUL, slash, `.` or `..`. Parent inode fields
supply no capability authority and are not exposed as `..` entries.

READ and executable capture require file READ; SIZE accepts READ or WRITE.
WRITE, RESIZE and file SYNC require WRITE; even an empty write reaches the worker's
authority/argument checks.
CREATE/REMOVE and file/directory rename use directory CREATE/REMOVE rights; replacement
also requires destination REMOVE. Directory moves journal the inode parent
backlink with both namespace edits and preserve held identity; replacement
requires an empty directory. Native rename requires the same volume, and
mixed backends fail BAD_OPERATION. Directory sync requires CREATE or REMOVE.
Archive, RAM and HOST behavior remains separate.

Reads and enumeration stage output and publish no bytes/name on failure. Capture
requires the full recorded file length, rejects empty executables and uses the
shared 128 MiB serialized-image ceiling. Its owned buffer remains outside adapter
wrapper accounting; the 256 MiB mapped image span is independent.
Enumeration uses a shared directory generation, not wrapper identity or an immutable
view. Mutation returns CHANGED for an acquired stale cursor. Short-buffer replies
preserve input position; cursors are opaque and provide no snapshot.

FILESYSTEM_INFO requires only its separate directory right and copies retained
pool/volume metadata without disk reads. Root acquisition of that bit requires
mount OBSERVE; requesting mutation bits independently requires mount WRITE.

## Writeback, recovery and errors

Mount reads and validates the full allocation bitmap after recovery, retaining
one bit per pool block in BSP-owned VM storage. Mapping checks and allocation
read memory; allocation skips full 64-bit words and starts at the first free
block found during mount. Journal bitmap images overlay the checkpointed copy.
Only checkpoint completion through durable EMPTY folds them into that copy;
healthy abort drops the overlay, preserving the checkpointed bits. Allocation
takes only blocks free in both the checkpointed copy and the overlay, with no
staged metadata image for that home. The checkpointed bitmap protects durable
owners until durable EMPTY; image exclusion also protects metadata allocated and
freed within the same transaction, whose two bits can both be clear. Its retained
image must not overwrite a later owner's data or initialize new metadata from
stale contents. Pressure cannot evict the bitmap, and read-only mounts retain it
for later writable upgrade.

The worker also keeps a [bounded metadata read cache](npfs-metadata-cache.md)
for directory, inode-file and indirect pages. Up to 512 KiB/pool is allocated
lazily and best effort; pressure discards these clean pages. Journal overlays,
checkpoint invalidation and allocation invalidation preserve coherence. The
[matched warm-open measurements](../development/experiments/npfs-metadata-cache/README.md)
record its observed effect and remaining qualification gaps.

Internal multi-page reads aggregate consecutive uncached physical pages, up to
64 KiB and the device transfer cap, using temporary BSP-worker-owned backing.
Cached/dirty pages, journal overlays, holes, physical discontinuities and
nonresident mapping metadata stop a run. Only requested aligned interiors are
eligible; partial pages and ordinary bounded FILE calls keep single-page reads.
Mandatory cache allocation precedes optional staging. Staging allocation failure
uses the single-page path, and every exit releases it. Completed pages are copied
into the ordinary cache and consumed before reserving another slot.

A failed block ticket publishes none of its run; earlier successful chunks retain
their confirmed byte count. There is no failed-run retry. Executable capture
requires complete success and discards its unpublished buffer otherwise. See the
[qualification record](../development/experiments/npfs-read-runs.md) for QEMU
byte/cleanup and native A/B results, including unqualified failure and native
responsiveness paths.

The worker maintains dirty data and one pool-wide metadata redo transaction.
File synchronization writes back that file, then commits the running transaction;
directory synchronization writes back every dirty file in the pool first; mount
synchronization covers pools on its configured disk. Durable COMMITTED is the
completion point. Checkpointing and durable EMPTY precede reuse of journal space,
previously durable allocations and homes with staged metadata images. Cleanup updates pointers and bitmap together
in bounded batches, retaining persistent list membership through completion.

Create, remove, link, rename, shrink, volume creation, detached-file reclamation and
file writeback stage their metadata images in the running transaction and
return without committing it. Sync, the background interval, memory pressure, power-off and
restart commit it. Before staging, each operation reserves its worst case
(`MOUNT_NPFS_MIN_JOURNAL_IMAGES`, 18 images; ten for a writeback or cleanup
step), and allocating ones also need up to eight free blocks (four per mapping
step); when the transaction lacks that room, or too few blocks are allocatable
while it holds blocks it freed, the worker commits and checkpoints it inside
the call. Operations never
split across commits, so recovery lands between whole operations. A failed
operation restores undo copies of images that earlier operations staged and
drops its own, leaving the rest of the transaction intact. A transaction holds
at most 128 images.

Writeback writes each step's data to its homes before staging the mapping and
size that reference it; every commit flushes the device before publishing
COMMITTED, so committed metadata never names unwritten or stale blocks.

Rename without replacement does not write back cached files. Replacement rename
first writes back the moved file and stages its mapping, so the new contents
commit no later than the name and a cached save cannot replace durable contents
with an empty or partial file after a crash. Remove and rename replacement stage the
victim's last staged mapping/size with its decremented link count, marking
DETACHED only at zero names and preserving cached contents for retained handles.
LINK similarly stages the source's durable prefix with its incremented count;
it never publishes unwritten dirty growth. Same-inode rename preserves both names
and counts. Reclamation requires zero names and zero held references. Shrinking resize writes back only its target before staging
the smaller size; its own delayed allocation can still fail. Unrelated dirty
files wait for sync or background/cache-pressure writeback. The
[namespace qualification](../development/experiments/npfs-namespace-writeback/README.md)
records the earlier I/O coupling and retained-open dirty replacement; the
[durability baseline](../development/experiments/explicit-durability-baseline.md)
records the per-call costs this replaced.
Cleanup reclaims at most 64 mappings and ten metadata images per step.

Delayed allocation happens at writeback. Periodic full flushing defaults to 30
seconds through menuconfig; pressure notifications wake the worker for asynchronous
reclamation without changing an allocator's result. Close has no durability
promise. Power-off and restart flush every writable pool and checkpoint it to an
EMPTY journal, then seal the pools ([ACPI power-off](../kernel/acpi.md#power-off-and-restart)).

Read-only opening requires EMPTY. Writable opening validates the complete committed
payload before replay writes. Successful replay logs the committed sequence and
metadata image count only after the newer EMPTY control is durable. Read-only
refusal logs that journal replay is required and advises opening read-write once
to recover; the call still returns READ_ONLY. Unknown required features reject
opening; unknown read-only-compatible features forbid replay and other writes.
The complete native writer explicitly admits the hard-links RO-compatible bit.
Header selection accepts identical copies or a strict componentwise feature-mask
superset with every other raw byte identical except the checksum. It selects the
superset before feature admission. An admitted writable open or upgrade repairs
its valid lagging subset by copying the selected raw header and flushing; read-only
opening does not repair. A single valid copy remains usable without repairing
its invalid partner merely on opening.

The first eligible LINK on a legacy pool reserves the operation, then writes and
flushes the opposite header before writing and flushing the selected valid copy.
The raw additive encoder preserves ignored reserve bytes. This also preserves a
valid copy throughout activation on a degraded pool. The gained bit is permanent,
outside namespace undo, and is never set merely by mounting. Interrupted activation
selects the feature-bearing copy on reopening; an older kernel refuses the unequal
valid headers in that window. Header I/O uncertainty latches the existing pool
failure and stages no LINK. Names/counts remain ordinary running-transaction edits.
Uncertain write or flush failure stops mutation and retains dirty state/error.
Structural fsck is separate from local opening and traversed-record validation.
Each sync retries dirty writeback, so a still-full pool reports NO_SPACE on every
failed attempt. Sync acknowledges recoverable retained errors when reporting
them; file sync sees only its own file's, while directory and mount sync see the
pool's. An unacknowledged error from earlier writeback, such as a background
flush, can still be reported once after writeback succeeds; a later sync can then
succeed once all dirty data is durable. Terminal uncertainty is never acknowledged
away for the boot.

Contiguous file writeback uses an optional 128 KiB gathering buffer, released by
pressure maintenance; allocation failure keeps single-block writes available.
Journal payload and adjacent checkpoint images use contiguous transfers without
changing durability fences. The [matched measurements](../development/experiments/npfs-io-runs/README.md)
record fewer requests and lower sync latency on emulated storage.

Checked partition-relative I/O supports 512-byte and 4 KiB device blocks, including
unaligned partition starts on 512-byte media. Transfers obey device limits and
successful reads must return the complete requested extent. Admission permits 32
active/queued jobs with a cooperative absolute 30-second deadline from publication,
including executor time. Adapter payload is bounded at 1 MiB and 1,024 wrappers.
These are implementation limits, not total-memory or hard CPU-time guarantees.

## Prepare a disposable disk

Use the native tools, which operate on standalone regular pool files rather than
GPT disks. Format and check the pool before copying it into a new partition:

```sh
make -j16 fs-tools
build/fs-tools/mkfs.npfs --image /tmp/native-pool.raw --size 64MiB \
  --journal 1MiB --volume system --source /path/to/unchanging/source
build/fs-tools/fsck.npfs --image /tmp/native-pool.raw
truncate -s 68M /tmp/native-disk.raw
sgdisk --clear --set-alignment=1 --new=1:2049:+64M \
  --disk-guid=12345678-1234-4567-89ab-0123456789ab /tmp/native-disk.raw
dd if=/tmp/native-pool.raw of=/tmp/native-disk.raw bs=512 seek=2049 \
  conv=notrunc status=none
```

Configure that GUID through `MOUNT_DISK` and attach the disk with `VIRTIO_BLK_IMAGE`.
Use `VIRTIO_BLK_READONLY=1` only for read-only mounting. Never modify the disk while
attached. Pool size/journal selection is explicit; the 256 GB target uses at least
128 MiB of journal. The codec's minimum journal does not prove writer admission. Writable admission
requires `MOUNT_NPFS_MIN_JOURNAL_IMAGES` (18) from the mount ABI, covering the
largest namespace transaction. The kernel writer and raw installer compatibility
inspection share this requirement.

## Task 3 validation

Ordinary builds and interactive four-CPU nested-KVM boots exercised warm reads,
grow/prepared writes, cached close/reopen, file/directory/disk synchronization,
resize, creation, rename/replacement, removal, UTF-8 names and native executable
capture. The [task-3 measurement record](../development/experiments/native-filesystem-task3/README.md)
contains commands, revisions, repeated intervals and virtual-device byte counts.
The initial writer's batched grow-write measured a 251.179 ms transfer median and
207.348 ms sync median; its complete command submitted 7,786,496 target bytes for
6 MiB of logical data.
Those are warm nested-VM workload intervals, not owner-host latency or SSD wear.

Clean restart retained the written files; extracted contents matched their source
and native host fsck passed. Read-only aliases rejected mutation, trusted disk sync
succeeded, an ordinary remote shell lacked mount authority, and fastfetch queried
writable and read-only volumes after the userland INFO correction. The native
USB-image consumer built and checked successfully; no USB boot is claimed.

Committed-journal recovery has been exercised at runtime: host `fsck --replay`
and kernel replay at writable mount recovered a committed transaction, with
contents verified, later mutation successful and host fsck clean; read-only
opening refused it with the logged explanation.

The [2026-10-04 follow-up record](../development/experiments/npfs-runtime-qualification/README.md)
exercises a 64 MiB pool reaching delayed-allocation ENOSPC, background retention
and one-time sync reporting, then successful writes after freeing space. A
retained-open 32 MiB file remained readable after unlink, with complete extracted
bytes verified; cleanup reclaimed its blocks after the final close. An unclean
stop after synchronized unlink while a reader still held the inode left persisted
DETACHED membership, which a later writable mount reclaimed. Stopped pools passed
host fsck. The record includes exact commands, configuration and internal debugger
observations; public FILESYSTEM_INFO does not expose free blocks.

That restart covers pending cleanup before reclamation, not interruption at
arbitrary points in a cleanup batch. Such interruption, uncertain I/O, allocator
pressure and read-only-device opening remain source-reviewed only. No
physical-media qualification has been performed.

Manual debugger work follows the existing [ownership rules](../development/gdb.md);
no sleeping worker or engine call may be injected as a stopped debugger call.

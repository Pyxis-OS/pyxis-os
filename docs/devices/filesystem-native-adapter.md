# Native filesystem kernel adapter

Caelum links the native format-only codecs and owns persistent pool, inode, cache
and writer state in the kernel. One serial BSP worker serves the existing file,
directory and mount protocols. The authoritative
[format](../../fs/docs/native-format.md) and
[host-tool guide](../../fs/docs/native-host-tools.md) remain in pyxis-fs.

## Build and ownership

[Build rules](../../kernel/fs/build.mk) compile the format library freestanding;
it contains no I/O, allocator, cache or authority policy. Caelum supplies its
memory symbols and implements block access and allocation policy. Host formatting,
inspection and whole-image checking remain separate tools. The existing compiler
is sufficient; no container rebuild is needed.

The [job interface](../../include/kernel/fs/native.h) uses shared kernel storage.
Trusted ROOT submission supplies the configured disk GUID, GPT entry, volume name
and exact directory rights including LOOKUP. User handlers capture requests and
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
Same-extent mounts share retained pools; another extent with the same pool ID is
rejected. Closing all wrappers does not evict mounted pool state or synchronize it.

## Rights and ordinary calls

The handler and worker both check actual caller rights against the retained node's
ceiling. Lookup grants only attenuated child rights. Names are case-sensitive UTF-8
components of 1–255 bytes, without NUL, slash, `.` or `..`. Parent inode fields
supply no capability authority and are not exposed as `..` entries.

READ, SIZE and executable capture require file READ. WRITE, RESIZE and file SYNC
require WRITE; even an empty write reaches the worker's authority/argument checks.
CREATE/REMOVE and file rename use directory CREATE/REMOVE rights; replacement
also requires destination REMOVE. Native rename requires the same volume, and
mixed backends fail BAD_OPERATION. Directory sync requires CREATE or REMOVE.
Archive, RAM and HOST behavior remains separate.

Reads and enumeration stage output and publish no bytes/name on failure. Capture
requires the full recorded file length, rejects empty executables and caps staging
at 16 MiB. Its owned buffer remains outside adapter wrapper accounting.
Enumeration uses a shared directory generation, not wrapper identity or an immutable
view. Mutation returns CHANGED for an acquired stale cursor. Short-buffer replies
preserve input position; cursors are opaque and provide no snapshot.

FILESYSTEM_INFO requires only its separate directory right and copies retained
pool/volume metadata without disk reads. Root acquisition of that bit requires
mount OBSERVE; requesting mutation bits independently requires mount WRITE.

## Writeback, recovery and errors

The worker maintains dirty data and one pool-wide metadata redo transaction.
File/directory synchronization commits the whole current pool transaction after
ordered data; mount synchronization covers pools on its configured disk. Durable
COMMITTED is the completion point. Checkpointing and durable EMPTY precede reuse
of journal space and freed blocks. Cleanup updates pointers and bitmap together
in bounded batches, retaining persistent list membership through completion.

Creation, rename and shrinking resize conservatively flush prior dirty pool data
before their namespace/size transaction. Their success already reaches COMMITTED,
so these operations can wait for unrelated writes or fail on delayed allocation.
Remove instead stages the target's last durable record with DETACHED, preserving
retained cached contents while allowing space recovery after disk-full writeback.
Cleanup reclaims at most 64 mappings and ten metadata images per transaction.

Delayed allocation happens at writeback. Periodic full flushing defaults to 30
seconds through menuconfig; pressure notifications wake the worker for asynchronous
reclamation without changing an allocator's result. Close has no durability
promise. There are no shutdown/restart/sleep flush hooks.

Read-only opening requires EMPTY. Writable opening validates the complete committed
payload before replay writes. Unknown required features reject opening; unknown
read-only-compatible features forbid replay and other writes. Uncertain write or
flush failure stops mutation and retains dirty state/error. Structural fsck is
separate from local opening and traversed-record validation.
Sync acknowledges recoverable retained writeback errors when reporting them;
later success requires all dirty data to be durable. Ongoing failures still fail
each sync attempt. Terminal uncertainty is never acknowledged away for the boot.

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
build/fs-tools/mkpyxisfs-native --image /tmp/native-pool.raw --size 64MiB \
  --journal 1MiB --volume system --source /path/to/unchanging/source
build/fs-tools/pyxisfs-native-fsck --image /tmp/native-pool.raw
truncate -s 68M /tmp/native-disk.raw
sgdisk --clear --set-alignment=1 --new=1:2049:+64M \
  --disk-guid=12345678-1234-4567-89ab-0123456789ab /tmp/native-disk.raw
dd if=/tmp/native-pool.raw of=/tmp/native-disk.raw bs=512 seek=2049 \
  conv=notrunc status=none
```

Configure that GUID through `MOUNT_DISK` and attach the disk with `VIRTIO_BLK_IMAGE`.
Use `VIRTIO_BLK_READONLY=1` only for read-only mounting. Never modify the disk while
attached. Pool size/journal selection is explicit; the 256 GB target uses at least
128 MiB of journal. The codec's minimum journal does not prove writer admission.

## Historical task 3 validation

Native writer runtime validation is pending. The
[task-3 measurement record](../development/experiments/native-filesystem-task3/README.md)
contains the obsolete adapter baseline and the planned matched writer measurements.
Earlier immutable-view/principal-based checks do not qualify this implementation.
Manual debugger work follows the existing [ownership rules](../development/gdb.md);
no sleeping worker or engine call may be injected as a stopped debugger call.

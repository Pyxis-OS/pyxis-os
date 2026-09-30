# Native filesystem kernel adapter

Caelum links the pinned read-only filesystem core and provides an internal,
serial BSP worker for GPT-selected pool/volume preparation. This completes task 3
of the [native mount milestone](../wip/native-readonly-filesystem.md). There is no
native directory/file capability, mount syscall, bootstrap principal configuration
or automatic volume probe yet. Opening backing state confers no object authority.

## Build and ownership

[Build rules](../../kernel/fs/build.mk) compile the seven read-only core sources
with kernel code model, no red zone, general registers only, freestanding compiler
headers and no builtin/libc dependency. Construction and whole-image checking
remain host operations. Kernel bundle provenance includes the filesystem pin and
local changes. No compiler-container rebuild is needed.

The [internal interface](../../include/kernel/fs/native.h) accepts a caller-owned
`nativefs_job` containing the GPT disk GUID in on-disk byte order, one-based GPT
entry number and counted volume name. Submission is BSP/IF=0 and lends the entire
record until detached completion. The caller must use shared kernel storage and
must not access it while queued/active. Rejected admission leaves it unchanged.
A successful job transfers one retained volume reference to the caller. Completed
records can be consumed/reset/reused; no completion or job registry is retained.
This internal asynchronous interface is separate from `bsp_request` and grants
no application permission to nominate a principal or bypass persistent policy.
Task 4 will add typed user-request forwarding and policy-approved objects.

Startup creates one BSP kernel task which parks until work arrives. Only that
worker performs pool/volume operations or invokes the core allocator, with
interrupts enabled except around short kernel allocator/block/queue sections. It waits normally for GPT and block
completion, without holding a lock or sleeping in the BSP executor. There is no
kernel-worker submission to the synchronous user-request executor.

The worker reuses the same pool for the single device's same GPT entry/extent
and shares volumes by retained ID. New extents reserve their selected pool ID
before volume publication. A clone on another partition returns
`CALL_ALREADY_EXISTS` even on the same disk. The reservation lasts through final
close, so a later open of the clone can succeed after the first instance drains.
A failed preparation releases only its own state; there is no idle pool cache.

`nativefs_volume_put` drops a reference on BSP/IF=0. Final release queues embedded
retirement storage and carries execution-group cleanup attribution to the worker.
Retired storage remains charged until actually freed. Close order is volume,
pool, adapter; a busy core close retains storage, identity and pending cleanup.
It is retried on subsequent real work rather than polled. Clients must release
all core children before their last backing reference. The current internal job
opens no views; task 4 must extend this invariant to object/view lifetimes.

## I/O, budgets and errors

Geometry is `floor(partition_sectors / sectors_per_filesystem_block)`. Checked
partition-relative ranges are translated into device sectors without requiring a
4 KiB-aligned partition start on 512-byte media. Trailing partial filesystem
blocks are ignored. A reader has no write/flush callbacks. Each callback accepts
at most 16 filesystem blocks and splits at the actual device transfer limit,
including limits smaller than a filesystem block. One ticket is outstanding at a
time, and each collected read must supply every requested byte.

Admission allows 32 active/queued jobs. The shared core payload cap is 8 MiB;
adapter payload is capped at 1 MiB with at most 1,024 live pool/volume wrappers.
Temporary catalog storage is charged to the adapter cap. Future native objects
must share these budgets rather than receive independent allowances. The core's
own accounting distinguishes cap exhaustion (`CALL_LIMIT`) from an allocator
refusal below the cap (`CALL_NO_MEMORY`). Alignment headers/padding are measured
separately from core payload; TLSF rounding, task stacks and caller job storage
are outside these payload caps.

Every submission receives one absolute 30-second deadline. GPT readiness, block
admission retry, completion waits and successive core calls use that same deadline.
Block saturation sleeps in bounded one-millisecond intervals; a timed-out waiter
abandons its ticket after returning, leaving unresolved DMA with the driver.
The deadline is cooperative, not a hard bound on core computation.

One fresh operation context records the first precise backing error. Only
`PFS_IO` consults it when mapping the result; corruption, denial and LIMIT cannot
inherit a device error. The context is unbound before completion. The completed
job preserves its own core status/backing result for diagnosis. A missing volume
or partition is NOT_FOUND; absent/invalid filesystem or GPT metadata is IO.
Unavailable/unsupported transport or format is UNAVAILABLE. These internal
results do not decide whether future `mount --optional` may ignore missing
hardware; task 5 must preserve the block preparation reason for that decision.

## Prepare a disposable disk

These commands use existing host tools and create new regular files. The example
principal is illustrative and does not establish a trusted boot identity. Never
modify or replace an attached image while a pool is open, including through a
second host tool. Read-only guest attachment does not prevent external writers.

```sh
make -j16 fs-tools
build/fs-tools/mkpyxisfs --image /tmp/native-pool.raw --size 64MiB \
  --volume headers --source /usr/include/linux \
  --owner 0a32efc079ed4c7bab58e224cf119315 \
  --volume empty --owner 0a32efc079ed4c7bab58e224cf119315
truncate -s 68M /tmp/native-disk.raw
sgdisk --clear --set-alignment=1 --new=1:2049:+64M \
  --disk-guid=12345678-1234-4567-89ab-0123456789ab /tmp/native-disk.raw
dd if=/tmp/native-pool.raw of=/tmp/native-disk.raw bs=512 seek=2049 \
  conv=notrunc status=none
build/fs-tools/pyxisfs-inspect --image /tmp/native-disk.raw \
  --gpt-partition 1 --sector-size 512 check
sha256sum /tmp/native-disk.raw
make debug ACCEL=tcg CPUS=4 VIRTIO_BLK_IMAGE=/tmp/native-disk.raw \
  VIRTIO_BLK_READONLY=1
```

Select matching OVMF paths for the host as in the repository README. The partition
starts at sector 2049 deliberately; it is not aligned to a filesystem block.
The GPT type is not used to select or authenticate the filesystem.

For 4 KiB sectors, `guestfish --blocksize=4096 --format=raw -a IMAGE` can initialize
GPT and add a 64 MiB partition at sectors 256–16639. Copy the pool with `dd bs=4096
seek=256 conv=notrunc`. Use `--sector-size 4096` for host inspection and launch
QEMU with `logical_block_size=4096,physical_block_size=4096` on the virtio-blk
device. The ordinary launcher uses the device's default 512-byte sector profile.

## Manual debugger operation

Follow [GDB ownership](../development/gdb.md) and the
[block debugger rules](block-storage.md#manual-debugger-exercise), using TCG for
injected nonblocking calls. Break at `vm_get_stats` before scheduling, select BSP
and enable scheduler locking for calls. Allocate a job, check allocation success,
then fill it explicitly:

```gdb
set $job = (struct nativefs_job *)kmalloc(sizeof(struct nativefs_job))
set *$job = {0}
set $job->disk.bytes = {0x78,0x56,0x34,0x12,0x34,0x12,0x67,0x45,0x89,0xab,1,0x23,0x45,0x67,0x89,0xab}
set $job->partition = 1
set $job->name.length = 7
set $job->name.bytes = "headers"
p nativefs_submit($job)
```

After CALL_OK, disable scheduler locking and resume normal execution. Set a
hardware breakpoint at the worker's yield after completion (locate it with
`list nativefs_worker`). At that stop, the job must be COMPLETE before inspecting
its result. `volume`, its core/record and pool diagnostics identify the retained
volume and selected generation. `core_peak`, `core_heap_peak`, `adapter_peak`,
`core_memory.used`, `adapter_used` and `wrapper_count` expose budget accounting.
`operation` must be NULL after completion. These are debugger observations, not
an application information interface.

At a safe stopped BSP boundary, save/disable IF and enable scheduler locking
before calling `nativefs_volume_put($job->volume)`. Clear the consumed output and
free the completed job with `kfree`. Restore IF and debugger scheduling, then
resume so the worker actually retires the backing. Observe zero live accounting
at its next yield. Do not inject `block_wait`, pool opens or other sleeping core
calls from GDB. No permanent diagnostic application or automatic probe is used.

## Validation

On 2026-09-30, ordinary kernel and host builds passed with GCC 16.2.0 target and
GCC 16.2.1 host compilers. Image assembly used verified SDK/userland/ports bundles
from merged Pyxis `0d03ab8`, filesystem `017996b`, userland `c9ed311` and ports
`6ff8504`. Missing-core-source rejection and prebuilt-kernel reuse were checked.

The 64 MiB pool imported 805 real Linux headers, 838 populated-volume objects and
one empty-volume root. Existing host checking passed both committed states through
512-byte and 4 KiB GPT selection. Interactive QEMU 10.2.2 with the documented AHCI
fix, CPU `max`, 256 MiB, entropy, no HOST/network devices and read-only virtio-blk
ran the normal worker under TCG: four CPUs with 512-byte sectors, and one CPU with
4 KiB sectors. Both opened the selected pool and volume, then released all state.
The four-CPU run also checked an empty volume, repeated shared opens, cloned pool
rejection across two partitions, continued reservation after queuing final put,
successful clone open after final close, missing volume/partition, wrong disk GUID
and 32 accepted jobs followed by unchanged BUSY rejection of job 33. Both whole-disk
SHA-256 hashes were unchanged after shutdown.

| Measured requested storage | Bytes |
| --- | ---: |
| Kernel core peak during populated pool/catalog/volume opening | 255,336 |
| Kernel core peak including adapter alignment headers/padding | 255,566 |
| Kernel adapter peak, including temporary catalog output | 105,808 |
| Retained core pool + volume after opening | 1,048 |
| Retained adapter pool + volume after opening | 1,360 |
| Internal caller-owned job | 320 |
| Live core/adapter payload and wrappers after final release | 0 |

The two peak counters need not coincide; their sum is an upper bound on these
charged/requested allocations, not total heap usage. Each worker additionally
uses the existing 16 KiB kernel stack and 784-byte task record. TLSF overhead and
rounding, mapped heap pool slack, other kernel clients and caller job storage
remain additional costs; no total-kernel peak or owner-host timing is claimed.

Host GDB watchpoints on the same core's charged-memory counter, with an explicit
8 MiB limit, measured root-subtree acquisition at 248,360 bytes and nested
`can/error.h` object acquisition at 315,816 bytes. The root held file read+metadata
and directory lookup+list; the file held read+metadata. Both policy decisions
allowed the complete masks and both views closed successfully, returning charged
memory to zero. These are host policy/path measurements, not guest object lookup.
The initial bounds comfortably cover these inputs; they are not a capacity claim
for every valid image.

An ordinary four-CPU nested-KVM boot without a block device reached userspace
with GPT unavailable and the native worker idle. An initial debugger-injected
allocation under KVM faulted at GDB's stack return address; the manual calls above
were subsequently performed under TCG as required by the block-debugger guide.
This was not a native filesystem operation. All validation VMs/debuggers were
stopped afterward.

Timeout/device failures, actual allocator exhaustion, adapter-cap exhaustion,
BUSY-close recovery, sub-4-KiB device transfer limits and later filesystem
generations have source-review coverage only. No fault injection, new tests,
permanent probes, physical hardware or owner-host performance measurements were
used. Guest policy/view lookup and enumeration remain task 4 validation; native
mount configuration and delegation remain task 5.

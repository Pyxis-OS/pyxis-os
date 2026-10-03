# Internal block storage

Caelum exposes one modern virtio-blk disk through the kernel-only
[block interface](../../include/kernel/block.h). It supports bounded asynchronous
reads, writes and flushes. [GPT discovery](gpt.md) publishes an immutable boot-time
partition map through a separate kernel interface.
[Native filesystem mounts](filesystem-native-adapter.md)
expose directory/file capabilities selected by trusted init, while raw-disk
authority remains kernel-only.

The [Phase B.1 USB contract](../wip/usb-installation.md#phase-b1-read-only-contract)
defines the planned explicit backend selection and asynchronous preparation
barrier needed for USB integration. The behavior below describes the implemented
VirtIO backend.

The block-storage foundation milestone is complete. Its implemented contracts
live here, in [shared VirtIO queues](virtio-queues.md) for filesystem, entropy and
block storage, and in [GPT discovery](gpt.md). The
[native filesystem](filesystem-native-adapter.md) uses this foundation for its
implemented format, cache and writer; installation remains separate.

## Attach a development image

Pass an existing regular raw image explicitly:

```sh
make run VIRTIO_BLK_IMAGE=/absolute/path/to/development.raw
make run VIRTIO_BLK_IMAGE=/absolute/path/to/development.raw VIRTIO_BLK_READONLY=1
```

Without `VIRTIO_BLK_IMAGE`, the launcher attaches no block disk. It never creates
or formats the image and does not accept a host block device. Relative paths are
resolved to absolute paths; commas are rejected because they introduce QEMU
options. `VIRTIO_BLK_READONLY` accepts `0` (default) or `1`. QEMU uses explicit raw
format, writeback caching that honors flushes, and one modern virtio-blk queue.
Writable attachment permits kernel clients to modify the supplied image.
Trusted raw-block clients must preserve GPT metadata throughout the boot;
discovery does not gate raw writes or rescan after them.

The guest requires exactly one modern virtio-blk candidate in a complete PCI
inventory. A known transitional virtio-blk device is present but unsupported;
multiple candidates, including a modern/transitional pair, are ambiguous.
Writable devices must offer flush support. Read-only devices support reads;
writes and flushes return `BLOCK_READ_ONLY`.

`block_preparation_result()` retains the immutable selection/setup reason:
ABSENT, READY, UNSUPPORTED, SETUP_FAILED, AMBIGUOUS or INVENTORY_INCOMPLETE.
Generic allocation, mapping or transport setup failures use SETUP_FAILED rather
than claiming unsupported hardware. Only a complete inventory with no recognized
candidate establishes absence; a later `block_get_info()` failure establishes
neither absence nor the preparation reason. Configured native mount authority
is omitted only for ABSENT; other failures remain visible to trusted init and
cannot be suppressed by an optional mount. A READY result describes preparation,
not a promise that the transport remains operational.

## Geometry and capacity

`block_get_info()` reports logical block size and count, maximum transfer bytes,
request slots, writable/flush support and the latched write-failure state.
Logical blocks are 512 bytes or 4 KiB. The driver translates block addresses to
VirtIO's fixed 512-byte sector units. Zero capacity, partial logical blocks,
unsupported sizes and unusable advertised transfer limits are rejected.

The driver uses one split queue with 32 descriptors, reduced to the device's
smaller supported size. There are at most eight request slots, reduced when the
queue cannot hold three descriptors per slot. Each slot has its own control and
data DMA allocations. Transfers are at most 64 KiB, reduced by the advertised
segment-size limit and rounded down to whole logical blocks. Ring storage,
ownership bookkeeping, DMA buffers and mappings are prepared before AP startup.

The slot budget includes queued, device-owned and completed-but-uncollected
requests. Exhaustion returns `BLOCK_FULL` immediately. Submission, collection,
abandonment and worker I/O allocate nothing. Multiqueue, live resizing, hotplug,
discard, write-zeroes and cache-mode switching are outside this profile.

## Tickets and caller ownership

The nonblocking calls require the BSP with interrupts disabled, outside IRQ or
fault entry. Their pointer arguments refer to caller-owned kernel storage and
are borrowed only during the call. A ticket has one client; it must not be
collected or abandoned while that client is waiting on it.

`block_submit()` validates operation, range and transfer bounds before admission.
Reads and writes require a nonzero count of logical blocks. Writes copy caller
bytes into the reserved DMA slot before returning. Reads retain no caller
destination pointer. Flush takes zero start/count and no data pointer. Successful
admission returns a slot/generation ticket; stale generations cannot identify a
reused slot. Rejection changes neither the output ticket nor the device.

`block_collect()` returns `BLOCK_PENDING` until completion. A return of `BLOCK_OK`
consumes the ticket and fills `block_completion`; the completion's `result` is
the actual I/O outcome. Successful reads copy exactly `completion.bytes` into a
sufficiently large caller destination. Failed reads leave that storage untouched.
An invalid destination or insufficient read capacity leaves the ticket available
for another collection attempt. `BLOCK_BUSY` also consumes nothing.

`block_abandon()` cancels queued work before publication and releases completed
work. Active work loses its client while retaining its slot and DMA ownership
until checked completion or confirmed reset. The worker can already be preparing
an active request before publication, so abandonment at that point need not
prevent the request reaching the disk. Abandonment cannot recall published writes
or guarantee a flush fence.

`block_wait()` is for BSP kernel tasks with interrupts enabled and no held locks.
It takes an absolute monotonic deadline. `BLOCK_OK` means the result is ready for
collection, not that I/O succeeded. `BLOCK_TIMED_OUT` neither consumes nor cancels
the ticket; the client must collect or explicitly abandon it afterward.

## Ordering, persistence and failure

Independent requests may be outstanding and complete in a different order.
Clients must wait between dependent or overlapping operations. Flush fences all
I/O: earlier admitted requests complete before the device flush is published,
and later requests remain queued until that flush completes. Earlier results
need not have been collected to allow progress. A completed write alone does not
promise persistence; a successful subsequent flush is required.

`block_completion.submitted` records whether publication occurred. If a published
write fails, storage may already have changed. Rejected or canceled queued work
has not reached the device. Caller timeout and abandonment never imply rollback.

Ordinary device status errors return `BLOCK_IO_ERROR` or `BLOCK_UNSUPPORTED` for
the request. A failed write or flush latches `write_failed` until reboot. Further
writes and flushes, including admitted work not yet published, fail with
`BLOCK_WRITE_FAILED`; reads can continue while the transport is healthy. Requests
already published retain their own completion outcomes. There is no automatic
retry or successful later flush that clears the latch.

Malformed completions, unexpected device status/configuration changes and a
five-second device request watchdog stop the transport until reboot. Pending
clients receive `BLOCK_UNAVAILABLE`, retaining the publication indication. This
device watchdog is separate from a caller's wait deadline. The driver masks
MSI-X, disables bus mastering and attempts reset with a one-second bound. A
confirmed reset retires queue ownership; an unconfirmed reset leaves it
unresolved. Runtime claims, DMA allocations and mappings remain retained in both
cases. Failure during boot preparation releases resources only after reset and
interrupt disabling have been confirmed. See the
[accepted limits](../technical-debt.md#virtio-blk-failure-and-validation-limits).

## Manual debugger exercise

Use a disposable development image and the ordinary nonblocking API under TCG,
following the [GDB ownership rules](../development/gdb.md). Select the BSP, stop outside
interrupt/fault entry with the kernel space active and IF=0, and lock debugger
execution to that CPU for injected calls. Check that no stopped CPU owns a lock
needed by the call. Allocate ticket, completion and data storage in the kernel;
GDB convenience variables themselves are not addressable kernel output buffers.
Do not call `block_wait()` from GDB: it sleeps and requires a running BSP task.

Inspect geometry, then submit several bounded requests before resuming normal
execution so the worker can publish multiple chains. Restore normal debugger
scheduling before continuing. Stop again at a suitable BSP boundary to collect
results and inspect queue ownership. Exercise independent reads, writes to
known image offsets and an ordered flush, then restart QEMU with the same image
and read the bytes back. Inspect the image from the host only after QEMU stops.
Release caller buffers and resolve or abandon every remaining ticket before
ending the exercise.

Report the configuration, actual outcomes and whether out-of-order completion was
observed. Ordinary QEMU execution does not guarantee reordering. Normal
restart/readback does not establish power-loss recovery or physical-hardware
behavior. This procedure adds no guest test program, self-test, fault injection
or boot/output automation.

## Validation

An ordinary `make -j16` build passed. Manual GDB calls used QEMU 10.2.2, TCG,
one CPU, 256 MiB RAM, entropy enabled and a disposable 32 MiB raw image.
With 512-byte logical blocks and 32 descriptors, eight independent 64 KiB writes
were outstanding together and completed successfully; a ninth admission returned
`BLOCK_FULL`. Completed tickets were collected, and flush succeeded. Invalid
ranges, zero/oversized transfers, stale tickets and queued abandonment were
checked through the public interface.

Debugger stops at queue notification observed WRITE/FLUSH/READ in three stages:
only the write active, then only flush active after write completion, then only
the read active after flush completion. All three completed successfully. Host
inspection after QEMU stopped verified every byte of all nine written 64 KiB
regions and the guest readback; the rejected request's region remained zero.

Restarting against the same image with read-only attachment, 4 KiB logical blocks
and eight descriptors reported two slots. Writes and flushes returned
`BLOCK_READ_ONLY`, an out-of-range read returned `BLOCK_INVALID`, and a third
concurrent read returned `BLOCK_FULL`. Two 64 KiB reads completed out of order
and returned the correct distinct contents, verified in full from guest memory
dumps. An undersized collection buffer was rejected without consuming its ticket.
This also exercised logical-block to 512-byte sector conversion across restarts.

A four-CPU nested-KVM boot with 256 MiB RAM reached userspace with a writable
block disk plus entropy/network devices. The no-disk four-CPU boot passed under
TCG, reporting block I/O unavailable. Initial no-disk KVM failures were traced
to a pre-existing [QEMU AHCI CD-ROM bug](../development/qemu.md#ahci-cd-rom-crash-before-kernel-entry),
also reproduced with the pre-milestone kernel. With upstream's fix applied to
a separate QEMU 10.2.2 build, the same no-disk four-CPU KVM configuration reached
userspace. These are boot and correctness observations, not owner-host
performance measurements.

Subsequent [GPT validation](gpt.md#validation) exercised the sleeping client
wrapper through ordinary kernel-task reads. Active abandonment, write-failure
latch, watchdog, malformed completion and reset-failure paths have code inspection
only. No fault injection, physical-hardware or power-loss validation was performed.

### Foundation completion

The final regression used merged revision `18cf655` with the unchanged pinned
userspace, ports and lwIP repositories. `make -j16 image
PREBUILT='sdk userspace ports'` passed using verified bundles. The four-CPU,
256 MiB nested-KVM boot combined filesystem, entropy, networking and a writable
64 MiB GPT disk with 512-byte logical blocks. It used virtiofsd 1.14.0 and a
local QEMU 10.2.2 build with the documented upstream AHCI fix, plus user-network
and vhost-user support; the installed QEMU package was unchanged.

From the development shell, `iobench write host://written.bin --sync` and
`iobench read host://written.bin` each completed a warmup and five verified
1 MiB samples. `dig example.com` succeeded; entropy's completion count increased
from five to six. Host inspection after shutdown matched the written file to
the complete fixture. These are correctness checks, not performance claims.

Debugger inspection after the operations found:

| Queue | Descriptor count | Completed chains | Outstanding |
| --- | --- | --- | --- |
| Filesystem requests | 16 | 4685 | 0 |
| Filesystem FORGET | 16 | 7 | 0 |
| Entropy | 8 | 6 | 0 |
| Block | 32 | 5 | 0 |

All descriptors were free, publication and completion indices matched, and none
of the queues was stopped. The filesystem session had no retained lookup or open
references. All entropy caller slots were free and cleared, and its full 8 KiB
DMA buffer was zero. GPT published a healthy two-partition snapshot after five
successful block reads and released its scratch allocation. The block device
remained writable with no failure latch. The entire disk image's SHA-256 was
unchanged after shutdown.

Together with the write/flush/restart exercise above and the
[512-byte/4 KiB GPT checks](gpt.md#validation), this closes the foundation's
ordinary-build, interactive-boot and debugger validation task. Earlier profile
and block mutation checks were not repeated by this documentation-only closure.
The [failure and durability limits](../technical-debt.md#virtio-blk-failure-and-validation-limits)
and [GPT limits](../technical-debt.md#gpt-snapshot-and-profile-limits) still apply.

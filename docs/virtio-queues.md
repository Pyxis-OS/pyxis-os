# Shared VirtIO split queues

Filesystem, entropy and block storage use the direct split-queue helper in
[`queue.h`](../include/kernel/virtio/queue.h). Drivers own protocol buffers,
concurrency, deadlines and device reset; the helper owns descriptors, ring
publication, notification ordering and checked completions. Networking retains
its separate packet queue implementation.

## Configuration and memory

The driver selects a power-of-two descriptor count from 1 through 32768, bounded
by the device's advertised maximum. Queue creation allocates appropriately sized
rings, CPU-side chain bookkeeping and a completion snapshot before AP startup.
Creation can fail for lack of memory; the protocol maximum is not a reservation
or a promise that every size fits. No allocation occurs on submission/completion.

The rings occupy a page-rounded contiguous coherent DMA allocation. Payload
buffers are separate driver-owned allocations, with explicit CPU virtual and
physical addresses. The boot-only [DMA buffer helper](../include/kernel/mm/dma.h)
provides zeroed, page-rounded, physically contiguous storage through existing
PMM/VM interfaces. CPU-only queue bookkeeping uses the kernel heap. All allocation
and release runs on the BSP with interrupts disabled, before AP startup.

One BSP worker owns a queue at runtime. IRQ handlers only record activity and
wake that worker. Rings and buffers use the existing coherent x86 DMA contract;
there is no IOMMU, packed ring, indirect descriptor or event-index negotiation.

## Submission and completion

A submission contains a numeric request ID and an array of physical segments,
each with a nonzero byte length and device-read or device-write access. All
readable segments precede writable segments. Total chain bytes fit uint32_t;
physical ranges must not overflow. IDs must be unique among outstanding requests
in that queue. Driver IDs are independent of hardware descriptor heads.

The helper validates the whole submission before consuming free descriptors or
publishing it. Invalid, full and stopped submissions retain caller ownership;
there is no pending-request queue or implicit wait. Acceptance publishes the
chain and lends its buffers to the device. The segment array can then expire,
but payload allocations must remain valid and inaccessible to the CPU until
checked completion or confirmed reset. Drivers are responsible for valid DMA
ranges and preventing conflicting buffer reuse; physical addresses alone do
not prove memory ownership.

Drivers call `virtqueue_notify` after publication, once per request or once for
several submissions, and only after DRIVER_OK. The helper orders publication
before checking device notification suppression. Stopped queues do not notify.

Completion snapshots the used index, applies the DMA read barrier, and validates
all observed entries before recycling any chain. Checks include index advance,
active descriptor heads, duplicates and reported writable lengths. Device fields
are copied once into CPU-owned snapshot storage; recycling follows CPU-owned
chain links. Request IDs and written lengths are returned in completion order,
which need not match submission order. Drivers validate protocol replies.

The caller provides a non-null count and completion storage sized for its maximum
outstanding requests. Insufficient capacity consumes nothing. Only COMPLETE
makes output entries valid; other results return count zero. Corruption stops
the queue and retains all outstanding ownership records. ID uniqueness checks
and completion validation currently scan queue-sized bookkeeping arrays.

## Stop and reset

`virtqueue_stop` rejects submissions, notification and normal completion draining
without returning ownership. `virtqueue_confirm_reset` is called only after the
driver observes a completed device reset. It retires ownership records without
emitting successful completions and leaves the queue stopped. Neither operation
frees memory or permits runtime restart.

Caller timeout does not return DMA ownership. Filesystem and entropy keep their
existing driver-specific deadline and caller-detachment behavior. Runtime
failure retains rings, payloads and bookkeeping until reboot, including after
confirmed reset. Boot cleanup may release unpublished storage or storage whose
device reset has been confirmed. See the existing
[runtime retention tradeoff](technical-debt.md#virtio-fs-runtime-resource-retention).

## Current consumers and validation limits

Filesystem selects 16 descriptors or the smaller supported size (minimum two),
with separate 8 KiB request and reply buffers per queue. Ordinary requests and
high-priority FORGET remain serialized across both queues. Entropy keeps the
same descriptor-selection policy and one 8 KiB writable buffer, with at most
256 bytes requested by its current public operation. Both drivers use request
ID zero while enforcing one outstanding request per queue.

[Block storage](block-storage.md) selects up to 32 descriptors and eight
outstanding requests, each with separate control and up to 64 KiB data storage.
Numeric generation IDs associate completions with tickets independently of
submission order. Flush drains earlier I/O and holds later I/O until completion.

Validation used QEMU 10.2.2 with nested KVM, 256 MiB RAM, one and four CPUs,
virtiofsd 1.14.0, and filesystem, entropy and network devices enabled. Single-CPU
file copy/readback and DNS lookup passed. On four CPUs, the existing iobench
completed 1 MiB host write/sync and read workloads (one warmup and five verified
samples each), followed by a successful DNS lookup. These are correctness
observations, not owner-host performance results.

GDB inspection showed all descriptors returned after operations, matching
available/used indices, no outstanding requests, and cleared entropy storage.
The four-CPU filesystem request queue had consumed 4685 chains and its FORGET
queue seven; entropy had consumed six. Inspection also observed a four-byte
entropy completion with one active writable descriptor on the single-CPU boot.

Subsequent block-driver validation exercised eight outstanding writes under TCG.
A restart with a smaller queue and 4 KiB logical blocks produced two concurrent
reads completing out of order; both tickets returned the correct 64 KiB contents.
See [block validation](block-storage.md#validation). Malformed completion,
reset-failure and allocation-failure paths are inspected rather than fault-injected.
No throughput improvement is claimed by this refactoring.

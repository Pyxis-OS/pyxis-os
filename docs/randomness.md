# Host-backed randomness

Caelum exposes a native `random` capability backed by the modern VirtIO entropy
PCI device. `make run` and `make debug` enable it by default through
`VIRTIO_RNG=1`, using QEMU's `rng-random` backend with host `/dev/urandom`.
`VIRTIO_RNG=0` omits the device. Boot and existing applications work either way;
nonempty random reads without a working device return `CALL_UNAVAILABLE`.

The guest trusts the host-supplied bytes. There is no guest CSPRNG, entropy
estimation, mixing, persistent seed, continuous harvesting or predictable
fallback. In particular, timestamps do not substitute for an unavailable random
source. This does not provide independence from the hypervisor. Libc `rand()`
and the generic UDP ephemeral-port allocator are unchanged.

## Native interface

Init receives a named `random` grant with `RANDOM_RIGHT_READ`. Session and shell
launches explicitly delegate it to children, including background commands.
Normal handle copying/restriction applies; closing a grant releases its reference
and does not shut down the device or revoke other grants.

Libpyxis exports:

```c
enum call_status random_read(handle_t random, void *bytes, size_t length,
    uint64_t deadline_ns);
```

The caller supplies authority and an absolute monotonic deadline, obtainable
through the existing clock interface. No helper discovers startup resources or
selects an alternative source implicitly. Requests use `PROTOCOL_RANDOM` and
`RANDOM_READ`; success returns exactly `length` reply bytes. The helper stages
its reply before copying to the supplied buffer.

- Lengths from zero through 256 bytes are supported; larger requests return
  `CALL_LIMIT`. Zero length needs no buffer or hardware but still checks the
  grant and deadline.
- Deadlines may be at most five seconds ahead, including queued time. A past
  deadline returns `CALL_TIMED_OUT`; a farther future deadline is
  `CALL_BAD_REQUEST`. Waking can be late because of scheduling.
- Success fills the entire requested extent. Every error leaves output
  unchanged; no partial success is exposed.
- Eight global call slots include queued work, the active read and completed
  results whose callers have not resumed. Exhaustion returns `CALL_QUEUE_FULL`.
  Copies of a capability share this service; there is no per-handle reservation.

The call validates output before sleeping. The sole user task retains its
mappings and live grant throughout the call. AP callers publish only captured
length/deadline and stable wait metadata; the worker never accesses their user
memory or private syscall stacks. Each slot owns its staging bytes until the
original caller consumes the result, then clears them before reuse.

## Device and worker ownership

The driver is in `kernel/virtio/rng.c`, with the native object facade in
`kernel/object/random.c`. It reuses PCI claims, register mapping and the split
[queue implementation](virtio-queues.md). Device ID 4 requires no device-specific
configuration region; the transport permits that case while retaining required configuration
for filesystem and network devices. The driver owns one 8 KiB writable DMA
buffer separately from ring storage; no unused request buffer is allocated.
Submissions carry one writable segment and a DMA request ID independent of the
caller slot. The helper allocates the descriptor head.

Preparation runs on the BSP before AP startup. Only `VIRTIO_F_VERSION_1` is
negotiated. Queue 0 selects 16 descriptors or the smaller supported size, with a
minimum of two, and uses boot-allocated coherent DMA storage; MSI-X
entry 0 routes to a dedicated BSP vector. DMA and delivery remain disabled until
the entropy worker starts. The interrupt handler only records notification and
wakes that worker; it does not inspect descriptors or copy bytes.

The worker keeps one device-writable buffer in flight and submits only when a
caller needs bytes. Checked short completions accumulate into that caller's
shared slot under its original deadline. Zero-byte or oversized completions,
unexpected used-ring entries and device status failure stop the service.
Returned DMA storage is cleared before another submission. Each worker pass
yields after processing work, and idle waits use notifications/deadlines.

Caller timeout detaches the active slot before waking its owner. An outstanding
DMA buffer remains device-owned; its eventual completion is discarded, never
assigned to a new caller reusing the old slot. Other queued calls still expire
at their own deadlines while the worker waits for that buffer. A separate
five-second device watchdog bounds a stalled DMA request, so a caller choosing
a short deadline cannot immediately disable the service.

On device failure, new requests fail and pending calls wake unavailable. The
worker disables MSI-X/bus mastering and attempts a bounded reset. DMA storage
and shared mappings remain until reboot regardless of reset success; a failed
reset never authorizes reading or reusing the buffer. No runtime restart,
hotplug, alternate source or provider framework is implemented.

## DNS consumer

[Dig and hostname ping](dns.md) use this capability to choose
transaction IDs and explicit randomized UDP source ports, with bounded collision
retries. They fail when entropy is unavailable; numeric ping does not need
randomness. The generic UDP ephemeral-port allocator is unchanged.
Per-space accounting, fairness and broader random APIs remain future policy.

## Kernel TCP consumer

TCP identity preparation uses the same entropy service from a short-lived BSP
kernel task, independently of userspace grants. A five-second read seeds separate
SipHash keys for sequence numbers and ephemeral ports. Failure disables new TCP
connections for that boot without blocking the network worker or other protocols.
Successful preparation needs no continuing device reads; see [lwIP integration](lwip.md).

References: [VirtIO 1.4 entropy device](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.html)
and [QEMU RNG backends](https://www.qemu.org/docs/master/system/qemu-manpage.html).

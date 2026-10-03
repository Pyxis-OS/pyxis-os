# Hardware-backed randomness

Caelum exposes the native `random` capability using one source selected at boot.
A modern VirtIO entropy PCI device takes priority when present. Without that
device, the BSP entropy worker uses the CPU's RDSEED instruction, with RDRAND as
fallback. `make run` and `make debug` enable VirtIO through `VIRTIO_RNG=1`, using
QEMU's `rng-random` backend with host `/dev/urandom`. `VIRTIO_RNG=0` omits the
device and exercises CPU entropy when the guest advertises those instructions.

The selected hardware is trusted directly. There is no kernel CSPRNG, entropy
estimation, mixing, persistent seed or continuous harvesting. Timestamps and
other predictable values never substitute for randomness. Boot does not require
entropy; nonempty reads fail with `CALL_UNAVAILABLE` when the selected source
cannot supply bytes. VirtIO preparation or runtime failure never switches to the
CPU. Libc `rand()` and the generic UDP ephemeral-port allocator are unchanged.
The accepted later direction is a kernel ChaCha20 generator seeded from hardware;
see [technical debt](../technical-debt.md#cpu-entropy-without-a-kernel-generator).

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

## Service and worker ownership

`kernel/random.c` owns the shared call slots, admission, cancellation, deadlines
and the one BSP worker for either source. `kernel/object/random.c` remains the
native object facade, and TCP identity calls the same service. CPU instructions
and their feature/health state live in `arch/x86_64/random.c`; AP callers never
execute them on behalf of this service. The worker detaches an active slot before
waking its caller, which may immediately consume and reuse it. A partially filled
slot is never returned on failure and is cleared when consumed.

## VirtIO device ownership

The adapter in `kernel/virtio/rng.c` owns PCI resources, DMA storage, transport
activation, submission, completion validation and the device watchdog. It reuses PCI claims, register mapping and the split
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
hotplug or source switching is implemented.

## CPU entropy

Before use, the BSP worker checks the maximum basic CPUID leaf, RDRAND at
`CPUID.01H:ECX[30]`, and RDSEED at `CPUID.(EAX=7,ECX=0):EBX[18]`. Only advertised
instructions execute. Both use the 64-bit operand form, including for reads with
a short final tail.

At worker startup, a bounded self-test obtains four words from each advertised
instruction independently. Each word must pass the runtime checks. A failed
instruction, including one exhausting its self-test carry retries, is disabled
until reboot; testing continues with the others. The CPU service starts if any
instruction passed. Testing RDRAND independently keeps a broken fallback from
remaining hidden behind working RDSEED; its failure does not disable healthy
RDSEED. Each disabled instruction and the surviving source are logged.
These sample and retry counts are implementation choices, not ABI constants.

For each runtime word, RDSEED gets up to 32 attempts; if absent, disabled or all
attempts clear carry, healthy RDRAND gets up to 10 attempts. Failed attempts use `pause` and never
update health history. Exhausting both paths fails that read without partial
output; future reads may try again. A caller's deadline and cancellation are
checked between words, and success is checked against the original deadline.

A carry-set word of zero, all ones, or a repeat fails the health check. Repeats
are checked against both the previous accepted word from that instruction and
the immediately preceding accepted word across instructions. History includes
the self-test and spans request boundaries, discarded refills and short tails.
Zero, all-ones or a per-instruction repeat disables only that instruction until
reboot. A repeat of the previous sample from a different instruction disables
both, because the failure cannot be attributed to just one.

After any runtime health failure, the worker clears the current request's entire
staging buffer and resets its filled count. If an instruction remains healthy,
it refills the request from scratch under the original deadline and cancellation
state. No byte collected before the suspect sample is returned, even if that
byte came from another instruction. If none remains, admission stops and pending
callers wake unavailable until reboot. Each instruction can be disabled only
once, so health recovery adds at most one refill before both are unavailable.
Complete words are checked before their requested bytes enter staging storage. The all-ones check covers
the known firmware failure motivating this policy. These checks detect specific
obvious failures; they do not establish entropy quality or replace hardware
trust. Bytes and health-history values are never logged.

The source/self-test outcome is visible in the boot log. On a ThinkPad PXE boot,
confirm HTTPS service setup and TCP identity no longer report entropy failures.
This alone does not provide networking without a supported NIC.

## DNS consumer

[Dig and hostname ping](../userland/dns.md) use this capability to choose
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

References: [Intel DRNG instruction and retry guide](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-digital-random-number-generator-drng-software-implementation-guide.html),
[AMD 64-bit RDSEED guidance](https://www.amd.com/en/resources/product-security/bulletin/amd-sb-7055.html),
[VirtIO 1.4 entropy device](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.html)
and [QEMU RNG backends](https://www.qemu.org/docs/master/system/qemu-manpage.html).

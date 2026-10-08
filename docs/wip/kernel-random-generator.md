# Kernel random generator

Status: **accepted, 2026-10-08; implementation authorized**. The owner
assigned the [accepted ChaCha20 follow-up](../technical-debt.md#cpu-entropy-without-a-kernel-generator).
Base: `92762b107e813be5b6c5f4600e205049630dd3c6`. The
[baseline](../development/experiments/random-generator/README.md) measures the
unchanged grant on VirtIO and CPU entropy. TLS already uses it; the accepted
[Bluetooth mouse direction](bluetooth-mouse.md) needs private keys and pairing
material from it.
P-256/SMP implementation stays with that consumer.

## Accepted decisions

The owner accepted all three defaults through the orchestrator on 2026-10-08
and explicitly authorized implementation. The [review of #546](https://git.internal/PyxisOS/pyxis-os/pulls/546)
also requests source licence notices, an explicit availability consequence in
the randomness reference, and a recorded debugger observation of the RFC vector.

1. **Ownership and construction — recommend one global generator, owned by
   the existing BSP random worker, using OpenBSD's ChaCha rekey design.**
   This preserves the eight shared staging slots and captured AP requests.
   Per-CPU generation could avoid worker scheduling, but needs distinct streams,
   readiness/reseed publication and a changed admission contract. Defer it until
   measured contention warrants that work.
2. **Seed/reseed policy — recommend preserving source selection, requiring
   a complete 40-byte seed, and refusing output whenever a required reseed fails.**
   VirtIO when present; otherwise healthy RDSEED with RDRAND fallback. Reseed
   on demand after OpenBSD's randomized 1–2 MiB output budget or 60 seconds,
   whichever comes first. The time threshold is accepted Pyxis policy, not an
   OpenBSD claim or entropy estimate. No idle harvesting, persistent seed or
   additional source mixing in this slice. Continuing from an old seed after
   source loss would improve availability but extend the compromise-recovery
   window; recommend deferring that alternative.
3. **Interface and consumers — recommend preserving the existing random grant,
   0–256-byte extent, five-second deadline horizon and all-or-error replies.**
   No getrandom-style syscall, libc change, caller reseed operation or generator
   state in userspace. Pairing and TLS need bytes from today's grant. Increase
   extent/capacity or add another API only for a concrete consumer requirement.

## Construction and erasure

Use the 20-round ChaCha core from [RFC 8439 sections 2.1–2.3](https://www.rfc-editor.org/rfc/rfc8439#section-2.3).
The generator reference is OpenBSD's
[arc4random.c revision 1.58](https://github.com/openbsd/src/blob/be1d1982dd83d04eb51a63726cd066959e1d1907/lib/libc/crypt/arc4random.c)
and its [ChaCha core](https://github.com/openbsd/src/blob/be1d1982dd83d04eb51a63726cd066959e1d1907/lib/libc/crypt/chacha_private.h),
not RC4 despite the API name. Preserve arc4random's ISC notices and the core's
public-domain provenance beside the adapted kernel source and in LICENSING.md.

Follow that construction: initialize from a 32-byte key and eight-byte nonce;
each refill generates 1024 bytes, reserves the first 40 for the next key/nonce,
reinitializes immediately, erases those reserved bytes and the old context,
and serves the remaining 984. Erase consumed output. A reseed XORs the complete
fresh 40 bytes into the reserved material before reinitialization, then discards
all buffered output. Its randomized output budget uses separate private output.

OpenBSD's context uses a 64-bit counter/eight-byte nonce. A context generates
at most 17 blocks before reinitialization, including the private budget draw:
the RFC block layout is identical to a 32-bit counter and 96-bit nonce whose
leading word is zero.
Document that mapping explicitly; do not mix variants or carry a stream across
refills. Reuse reviewed construction/core logic rather than inventing a mixer.
Linux's fast-key-erasure generator is a sound alternative, but its full input
pool/reseed pipeline adds a hash and entropy-accounting machinery this slice
does not need.

Use compiler-resistant erasure for tentative seeds, prior keys/context copies,
reserved bytes and consumed/discarded output, including every failure path.
Never roll back state when a call times out or stops: generated/discarded bytes
remain spent. Do not log seeds, keys, output or health-history words. Ordinary
slot/DMA ownership still controls when their buffers may be cleared.

This state stays in the kernel and is never copied into a new process: no
userspace fork detector is needed. State-only compromise cannot reconstruct
already consumed/erased output under ChaCha's assumptions, but exposes buffered
future output and predicts future output until an independent successful reseed.
Completed slots/caller memory may still hold delivered bytes. Whole-VM snapshots
or clones duplicate generator state; snapshot recovery is outside this task.
No generator certifies a biased/malicious hardware source or defeats a malicious
hypervisor; mixing one selected source does not create independence.

## Ownership, readiness and failures

The existing [BSP worker and scheduler contracts](../kernel/smp.md) remain:
interrupts-enabled, preemptible worker; IF=0 for the request lock and wait
publication; resource lock before scheduler lock; no lock held across a wait.
Only that worker touches key, buffer, reseed counters and CPU health state, so
no generator lock is added. IRQs only notify. APs publish captured length/deadline
and stable wait metadata, never user-memory pointers or generator state.
Detach the active slot before completion; retain no pointer after publication.

Admission and readiness remain distinct. A nonempty request may queue before
the first seed, but cannot succeed until a full accepted seed exists. Boot need
not wait for entropy. The worker owns a separate tentative seed and bounded
five-second collection attempt; caller deadlines/cancellation continue to be
serviced independently. An expired caller neither receives bytes nor owns an
outstanding seed DMA buffer; a valid completed seed may serve other callers.
No source/preparation failure returns predictable or partial bytes.

Retain [existing hardware checks](../devices/randomness.md): CPU boot self-test,
bounded instruction retries and history; after any health failure discard the
whole tentative seed and refill from a survivor under the original seed budget.
Carry-clear exhaustion can fail an attempt and allow a later request to retry.
No healthy CPU instruction or a permanent VirtIO fault leaves the generator
unavailable until reboot. VirtIO failure never silently selects CPU entropy.
Checked positive short VirtIO completions accumulate only into worker-owned
seed storage. Retain its independent watchdog, late-completion discard and
DMA quarantine/reset rules; never erase device-owned storage prematurely.
Each seed attempt has a lifetime independent of caller slots. If its collection
deadline expires with DMA outstanding, clear only CPU-owned accumulation and
discard that attempt's checked late completion; it cannot seed a later attempt.

Before generating another reply, require a successful reseed if its time or
output threshold is due: 60 seconds since successful seed completion, or
remaining budget less than or equal to the next request's length. Charge bytes
extracted for replies even if later discarded, excluding reserved rekey material,
the private budget draw and unused prefetched output. A tentative seed failure
does not replace prior state, but output remains barred until a full required
reseed succeeds. Complete pending requests waiting on that failed attempt once,
unavailable or timed out, with unchanged caller output; only later requests
trigger a new bounded attempt. No unbounded retry loop. Observed permanent source
failure immediately erases generator-owned state and closes nonempty admission,
even with output budget remaining.
Time is a reseed trigger, never seed material. Scheduling/firmware stalls can
delay service; no fixed reseed-completion or wake-latency guarantee is implied.

## Delivery and qualification

- [x] Inspect contracts, choose a referenced construction and capture raw-source
  latency/throughput without changing kernel/runtime code.
- [x] Owner settles the three defaults and explicitly authorizes implementation.
- [ ] Integrate the core/generator and seed lifecycle together; update randomness
  and debt contracts while preserving grant/slot/cancellation behavior.
- [ ] Manually compare RFC vectors and reference state transitions, inspect
  erasure/park/DMA ordering, and repeat the same consumer/image bytes with only
  the kernel changed on both sources. Include natural refill/reseed boundaries,
  ordinary one-/four-CPU boots and existing TLS/TCP consumers. No new workflow,
  self-test, fault injection or benchmark framework is authorized.

Branch: `kernel/random-generator-proposal`; no dependency pin or compiler rebuild
needed. Source adaptation will be committed with provenance/notices; any external
build download requires an owner mirror first. No Bluetooth, libc or random ABI
implementation is included. Baseline guests/clients/builds stopped. Native
ThinkPad latency, CPU supply under load and generator qualification are not
inferred from nested KVM. Implementation and qualification are now authorized.

# Kernel random generator

Implemented 2026-10-08. The owner accepted all three defaults and authorized
implementation on that date through the orchestrator in
[PR #546](https://git.internal/PyxisOS/pyxis-os/pulls/546):

1. One generator owned exclusively by the existing BSP random worker, using
   RFC 8439's ChaCha20 core with OpenBSD's arc4random rekey construction.
2. A full 40-byte hardware seed; required reseeding after a randomized 1–2 MiB
   output budget or 60 seconds, checked on demand and failing closed.
3. The existing random grant, extent/slot limits and deadlines, unchanged.

## Construction

The 20-round core follows [RFC 8439 sections 2.1–2.3](https://www.rfc-editor.org/rfc/rfc8439#section-2.3).
The generator adapts OpenBSD's
[arc4random.c revision 1.58](https://github.com/openbsd/src/blob/be1d1982dd83d04eb51a63726cd066959e1d1907/lib/libc/crypt/arc4random.c)
and [public-domain ChaCha reference](https://github.com/openbsd/src/blob/be1d1982dd83d04eb51a63726cd066959e1d1907/lib/libc/crypt/chacha_private.h).
[Source notices](../../kernel/random/NOTICE) retain the full ISC permission and
Bernstein provenance; [licensing](../../LICENSING.md) records the exceptions.
No external download or compiler rebuild is needed.

A context has a 32-byte key and eight-byte nonce. Each 1024-byte refill reserves
40 bytes for the next key/nonce, reinitializes immediately, erases the reserved
material and serves the remaining 984 bytes. Consumed bytes are erased. Reseeding
XORs a complete fresh seed into the reserved material before reinitialization,
discards all buffered output, and uses a separate private draw for the output
budget. The budget excludes rekey/private/prefetched bytes but charges extracted
reply bytes even when the caller later stops or times out.

OpenBSD's counter occupies words 12/13. Reinitialization bounds each context to
at most 17 generated blocks, including the private draw, so word 13 remains zero.
The RFC block layout is identical with a 32-bit counter and a 96-bit nonce whose
leading word is zero. The debugger's full RFC vector uses an explicit public
state, including its nonzero leading nonce word; it never seeds the live generator.

## Ownership and limits

The [randomness service](randomness.md) retains its BSP worker, eight shared
call slots, captured AP requests, independent caller deadlines and source/DMA
ownership. Only the worker accesses generator state, outside the request lock;
there is no new generator lock, allocation, per-CPU copy or userspace state.
No fork detector is needed. Initial/required seed collection owns a separate
buffer and five-second attempt; failures cannot expose partial material or
reuse an expired attempt's DMA completion.

Compiler-resistant erasure covers tentative seeds, prior contexts, temporary
blocks, consumed/discarded output and shutdown. Cancelled generated replies stay
spent. After consumed output is erased, state-only compromise cannot reconstruct
it under ChaCha's assumptions; buffered/future output remains exposed until an
independent successful reseed. Caller/unconsumed-slot memory may still hold
bytes. Whole-VM snapshots/clones can duplicate initialized state. Hardware trust,
seed availability and native qualification remain
[technical debt](../technical-debt.md#random-generator-trust-and-availability).

The [qualification report](../development/experiments/random-generator/generator.md)
records the full RFC vector observation, both natural reseed triggers, erasure,
ordinary one-/four-CPU boots, existing TLS/TCP consumers and matched before/after
VirtIO/CPU measurements. Native seed quality/performance and failure under load
are not established by those nested-KVM observations.

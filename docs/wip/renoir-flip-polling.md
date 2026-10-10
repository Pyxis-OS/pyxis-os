# Cheaper Renoir flip polling

Status: **proposal only; a follow-up is in progress and no polling change is
implemented.** It concerns the pending-flip polling of the
[Renoir flip backend](../kernel/renoir-flip.md).

The native run measured 369 µs mean validation elapsed, 3.9 ms maximum and about
8 polls per flip, roughly 3 ms cumulative validation elapsed per frame. The
metrics include preemption and clock overhead, so neither the observed wait nor
this estimate establishes an optimization gain.

Recommended default: while a flip is PENDING, read a stable flip-control and
earliest-in-use tuple as a hint for scheduling the full check. Keep one
outstanding request, the 1 ms sleeping polls and the 50 ms/50-poll bound. Full
device, route, immutable-layout and owned-set validation stays mandatory before a
back-surface pixel write, before GPU submission, before front retirement or
capture publication, and before timeout fallback or any fallback copy.
Confirmation must still verify stable primary **and** earliest addresses equal to
the request with pending clear. Light reads alone never authorize a write, reuse
or release. A full-check failure or unknown ownership remains FAILED with the
surfaces pinned. No new masks, write authority, allocations, interrupt or
firmware changes are proposed.

This moves repeated full pending-poll checks to the guarded actions above: expect
fewer full checks plus small tuple reads, with matched native cost and input
qualification required.

**Owner decision before implementation:** accept delayed layout-loss detection
during the write-free PENDING interval, bounded by candidate completion or the
existing 50 ms/50-poll deadline, while full validation stays at every action
above. That cadence is proposed, not accepted. Measure validation and poll cost
separately at the same revision, options and workloads; repeat both games and
confirm that retirement, capture, fallback and FAILED guards remain intact. The
debt entry is [Renoir steady-state validation cost](../technical-debt.md#renoir-steady-state-validation-cost).

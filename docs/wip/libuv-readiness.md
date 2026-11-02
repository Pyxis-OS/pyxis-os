# Libuv persistent readiness

Limits audit work package 5, row 7. WP5.1–WP5.3 accepted (chronium, 2026-11-02),
following the [limits contract](../interfaces/limits.md).

## Accepted behavior

One persistent native set replaces libuv's 31-handle policy and transient wait
arrays. PROCESS completion uses existing WAIT authority and the actual native
retirement boundary. The final WP3 branch already supplies that source support.
Stream and process registrations retain observation storage and revocable
lineage, not logical I/O ownership or a child's VM. No new thread, grant, quota,
regular-FILE or socket profile is added.

A concrete observed-launch operation admits a process watch before the child
is runnable and returns observer plus key together. Known refusal unwinds the
unpublished child/watch; uncertain admission is never replayed. Ordinary launch
remains for its real unobserved callers. The set is owner-local with CONTROL;
the child receives no extra authority. Publication commits the watch after
final group admission checks, before the non-failing queue/install steps.

The loop retains native wake-pipe and temporary cooperative-signal ownership,
withdraws borrowed signal watches before callbacks/checkpoints, rejects stale
keys, and preserves software readiness for buffered reads/write completion and
shutdown. Native replies are at most 64 records per turn, with other loop phases
between turns; this is not a handle count. Absolute application deadlines survive
bounded native wait slices. The existing shared 16 MiB readiness policy remains
explicit; actual refusals propagate through caller-owned reasons and UV errors.

## Delivery

- [x] Fresh rebased consumer baseline before backend changes: four Neovim jobs
  pass; twenty requested jobs admit seven and report/clean all seven on refusal.
- [x] Kernel/SDK observed-launch implementation builds; ordinary four-CPU QEMU
  observes 48 exact process completions and checks retirement, cleanup and
  missing-CONTROL refusal. No race/fault-injection or native claim.
- [ ] Libuv backend, reached Lua/Neovim callers and explicit spawn detail.
- [ ] Neovim outer-loop error policy, decision below.
- [ ] Complete Neovim >31-handle qualification and matched comparison.

## Decision needed

**WP5.4 PROPOSED DEFAULT:** at an actual negative outer-loop uv_run error,
Neovim prints the native error and aborts that editor process. Teardown stops
on the same error instead of retrying indefinitely. Unsaved edits may be lost;
no orderly-save guarantee is implied. Ordinary spawn refusals stay recoverable
command errors. The library adds no implicit output or last-error slot.

Neovim's current loop API returns only a timeout bool and ignores the error.
An alternative recoverable result needs explicit pending-request/borrowed-buffer
ownership, cancellation and user-retry rules. No such policy is implemented or
inferred from its recursive-loop invariant abort. Stop for chronium's decision.

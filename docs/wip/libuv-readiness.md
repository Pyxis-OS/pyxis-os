# Libuv persistent readiness

Limits audit work package 5, row 7. WP5.1–WP5.3 accepted (chronium, 2026-11-02),
following the [limits contract](../interfaces/limits.md).

## Accepted behavior

One persistent native set replaces libuv's 31-handle policy and transient wait
arrays. PROCESS completion uses existing WAIT authority and the actual native
retirement boundary. Merged WP3 already supplies that source support.
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
- [x] Libuv backend, reached Lua/Neovim callers and explicit spawn detail.
- [x] Accepted Neovim outer-loop error policy and implementation; ordinary builds
  and successful loops qualified, without injecting an editor failure.
- [x] Neovim 4/20/64 jobs, callback ownership, bounded batches and nested
  matched comparison; see the [record](../development/experiments/libuv-readiness.md).
- [x] One coordinated native LLVM/container published at c63fb8c; its actual
  SDK source remains `9c26aac9985bc8e1707f270748625f0b292376a9` through rebases.
  Final static-consumer and exact-head CI receipts are recorded on the PR.
  Native performance remains unmeasured.

## Neovim loop errors

WP5.4 accepted (chronium, 2026-11-02): at an actual negative outer-loop uv_run
error, Neovim prints the native error and aborts that editor process. Teardown
stops on the same error instead of retrying indefinitely. Existing swap-file
recovery is unchanged; this does not add an orderly-save step or guarantee that
the most recent edits reached a swap file. Ordinary spawn refusals remain
recoverable command errors. The library adds no implicit output or last-error
slot, and no error becomes a timeout, fake interrupt or invented child exit.

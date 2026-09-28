# I/O reliability and bottleneck attribution

Status: agreed next milestone after the completed
[I/O and IPC baselines](../io-ipc-baselines.md). Endpoint receipt reclamation is
the first priority. Work through the tasks in order, in focused PRs; this document
does not choose a reclamation algorithm or authorize unrelated optimization.

## Outcome and scope

Make sequential IPC and exported-file consumption reliable under sustained use,
then explain the measured RAM file-growth and host FILE costs well enough to
choose justified follow-ups. Finish with comparable benchmark reruns and a
report of what changed, what was learned and what remains unknown.

The baseline exposed three concrete questions:

- Completed endpoint receipts retain delivery slots until BSP destruction.
  Sequential CALL and acknowledged SEND runs reached QUEUE_FULL, and ordinary
  1 MiB HTTP snapshot reads failed with EAGAIN after a successful body fetch.
- RAM writes growing from zero took a 55.654 ms median versus 0.314 ms for
  prepared overwrite in the matched no-sync runs. Allocation, copying, BSP
  service and task resumption have not been separated.
- Host reads took roughly 119 ms per MiB in nested KVM. Guest queueing,
  VirtIO/FUSE transport, host scheduling and backing service are combined;
  host write/sync fixtures were in tmpfs, not on durable storage.

These are recorded observations, not fixed failure thresholds or diagnoses of
allocator, scheduler or device performance. There is no arbitrary throughput
target and no requirement to produce an optimization for every investigation.
An attribution task can finish with evidence that the existing behavior is
appropriate, or with a concrete proposal requiring a separate implementation PR.

Preserve capability authority, subsystem boundaries, BSP allocation/destruction
and VM-mutation rules, and existing scheduling and delivery guarantees. Do not
hide failures by enlarging queues, retrying failed operations, adding sleeps or
shrinking fixtures. Genuine capacity exhaustion must still report QUEUE_FULL.
Public interfaces change only if a concrete need is discussed and agreed.

## Measurement rules

Reuse `iobench`, `ipcbench` and the baseline's deterministic fixtures and timing
boundaries. Keep successful short-batch results as controls alongside the failing
larger cases. Record exact kernel/userland/dependency revisions, commands,
individual samples, clock overhead, CPU placement, devices and backing storage.
Keep nested-VM, owner's-host and physical-hardware results separate.

Before adding instrumentation, agree the specific question, event boundaries,
collection method and lifetime/overhead contract. Prefer narrow counters or
timestamps around the affected path over a generic tracing/profiling framework.
No logging or debugger stops inside timed samples. Instrumentation must not
silently alter scheduling, pace requests or become an unexplained part of a
before/after comparison. State whether measurements include instrumentation.

Do not call an end-to-end interval kernel CPU time. If guest observations can
only bound transport plus host service together, report that limit; host-side
instrumentation needs its own explicit scope. Longer batches, additional fixture
sizes or new benchmark controls also need agreement before implementation.

Use ordinary builds, manual interactive QEMU runs and debugger inspection.
No new tests, fault-injection facilities, boot automation, CI performance gates
or benchmark framework. Existing CI validates integration, not performance.
CALL deadlines still do not bound RECEIVE, pipe or process waits; this milestone
does not add general cancellation or wait APIs.

## Focused PR tasks

- [ ] **1. Resolve endpoint receipt reclamation.** Trace completion, caller
  collection, receipt closure, cancellation and deferred destruction. Propose
  the smallest correction before implementing it: safe logical slot reuse and
  reclamation scheduling are alternatives to evaluate, not preselected designs.
  Preserve in-flight receipt/attachment ownership, timeout and delivery outcomes,
  provider retirement and stale-handle protection. Reusing a delivery record
  must not let an old reference or deferred destructor affect a new delivery.
  Keep allocation/destruction on the BSP under the existing contract.

  Immediately rerun `ipcbench call` and `ipcbench send` with `--messages 256`
  at payload sizes 0, 64 and 4096, plus the existing eight-message controls.
  Repeat full 1 MiB HTTP reads at requests 4088 and 65536, retaining the 32 KiB
  controls. Use one warmup and five verified samples with normal shutdown and
  post-workload cleanup inspection. No retries, pacing or larger delivery table
  may substitute for fixing completed-work capacity retention. If remaining
  failure comes from a different constraint, identify and discuss it rather
  than declaring the reliability task complete. Update the reclamation debt
  entry to reflect the implemented guarantee and any remaining limitation.

- [ ] **2. Attribute RAM file-growth cost.** Separate request publication and
  BSP queue wait, allocation/copy service, and completion-to-resumption time.
  Count growth operations and copied bytes so repeated growth is distinguishable
  from expensive individual operations. Compare grow-from-zero with prepared
  overwrite using the same 1 MiB payload and 4080-byte requests, without sync;
  retain write/copy correctness checks outside transfer timing. Existing private
  memory profiling does not cover this FILE path. Agree narrow instrumentation
  first, then report the dominant costs and unresolved intervals. Propose any
  allocator, growth-policy or notification change separately from attribution.

- [ ] **3. Attribute host FILE latency.** Use matched read/write/copy workloads
  to distinguish guest service-queue wait, submission-to-observed-completion,
  and completion-to-caller-resumption time. Describe what the middle interval
  combines rather than assigning it all to the host filesystem or the device.
  Keep request counts and confirmed short transfers visible; the 4088/4080-byte
  read/write mismatch can add suffix writes. Record daemon, cache and backing
  conditions. Keep sync separate; disk-backed fixtures are needed only if the
  agreed question concerns durable-media sync. Do not change transfer sizes,
  batching, caching or scheduling merely to improve the reported number.

- [ ] **4. Improve resolution and publish the comparison.** Review the evidence
  from the first three tasks and settle the smallest additional batch/coverage
  changes needed for meaningful comparisons. In particular, short native and
  SEND intervals approach clock-call overhead; longer safe workloads can improve
  resolution without subtracting a presumed constant. Repeat affected benchmarks
  after each implemented correction, then collect a final matched matrix including
  the previously failing IPC and HTTP workloads. Preserve the original baseline;
  state when changed workload or timing definitions prevent direct comparison.
  Owner-host measurements are useful when available, but are not required to
  finish this milestone. Publish results and limitations, resolve or update
  relevant technical debt, and move this WIP document into a concise implemented
  report under `docs`, removing the checklist and updating incoming links.

## Completion and boundaries

Completion requires verified consumption of the formerly failing workloads,
evidence-backed conclusions for file growth and host service, and a reproducible
comparison report. Reruns belong both to the relevant correction PR and the final
handoff; do not postpone validation of the reliability fix until task 4.

Userland owns benchmark changes; Pyxis owns kernel changes, integration and the
report. Publish dependent userland PRs before updating gitlinks. No ports or
compiler-container change is anticipated. Resolve implementation and measurement
decisions at the start of each task rather than inventing them in this plan.

Shared bulk buffers, larger FILE payloads, async I/O, cache/read-ahead policy,
allocator concurrency, cross-space contention, attachment-cost studies and
general process CPU accounting remain separate work. This milestone does not
attempt to resolve every coverage gap listed in the baseline. See
[attribution debt](../technical-debt.md#io-baseline-attribution-and-coverage) and
[receipt reclamation debt](../technical-debt.md#endpoint-throughput-limited-by-deferred-receipt-reclamation)
for the original consequences and revisit points.

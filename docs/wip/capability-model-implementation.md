# Capability model implementation

Status: **milestones A and B complete; milestone C remains planned.** The owner
confirmed B complete on 2026-10-16. This implements the settled
[capabilities and providers](capabilities-and-providers.md) design. Codex beta
owns integration; Codex delta owns the remaining kernel work in C. Each task
starts with a short scope note and stops only for unresolved behaviour, authority
or lifetime decisions; ABI layout and other
routine choices need no approval.

Libc, shell, web/HTTP and Git work outside this milestone stays paused until the
task that owns the area is delivered.

## Agreed decisions (2026-10-14)

- **Order:** milestone A, then B, then C. Each is usable on its own. Owner
  exception, 2026-10-15: bring only C9's capability-output handoff forward as an
  A5 prerequisite; general pipe integration and qualification stay in C9.
- **Ownership:** as in the status line.
- **Expansion of `$x`:** text values, from literal assignments such as
  `f="home://notes.txt"`, expand to their text, as in bash. The text is resolved
  through the caller's own environment and confers no authority. Every other
  value (captured `$()` output, replies, prepared requests, devices,
  directories) passes as `x://` with its entry, so `cat $job` reads the reply
  and `echo $job` prints `job://`. Inline `$(…)` used as an argument passes a
  capability, as bash's `<(…)` does; idioms such as `rm $(find …)` do not apply.
- **Remove** works on names. Removing through a held FILE capability without a
  name is refused for now; making `rm` work on both names and capabilities is a
  later item.

## Rules for every task

- **Replace, don't coexist.** Each task removes the interface it replaces and
  updates every in-tree consumer in the same change, across the parent,
  userland and ports repositories, with linked PRs and stated merge order.
- **Interfaces beside code.** Each task updates the matching
  `docs/interfaces/` reference; this document only tracks progress.
- **Measure natively.** Repeat the matched ThinkPad workloads after A and C:
  native/text OPEN, small-program launch, full startup allocation and pipe
  throughput. The [baseline run sheet](../development/experiments/capability-model-baseline.md)
  records the satisfied gate, frozen hands-free Remote method, exact revisions,
  samples and limits. After-A and after-C repeats remain pending; milestone
  completion does not claim those measurements ran.

## Milestone A: the core — complete

Owner-confirmed 2026-10-15: tasks 1–5 are merged. Implemented contracts and
limits live in [forwarders](../interfaces/forwarders.md),
[the environment](../interfaces/environment.md),
[typed OPEN and describe](../interfaces/file-providers.md),
[transactions](../interfaces/transactions.md),
[libc](../userland/stdio.md#typed-resources-and-transactions) and
[the shell](../userland/shell.md#typed-values-and-command-sequences).
The delivered [output handoff and retention interface](../interfaces/output.md)
is C9's starting point, not general capability-pipe integration.

The [shell/capture](../development/experiments/capability-shell.md),
[handoff](../development/experiments/capability-output.md) and
[retention](../development/experiments/retained-values.md) records preserve their
qualification, measured revisions and limits; the pending native repeats remain
listed above.

## Milestone B: HTTP on the model — complete

Owner-confirmed 2026-10-16: tasks 6–8 are merged, including #902,
userland #279, ports #120 and #905. The completed contract lives in
[HTTP resources and transactions](../interfaces/http-transactions.md),
[HTTP fetching and preparation](../userland/http-fetch.md),
[libc transactions](../userland/stdio.md#typed-resources-and-transactions) and
[shell captures](../userland/shell.md#held-output-captures).

Before/after revisions, native and nested measurements, qualification and limits
remain in the [snapshot](../development/experiments/capability-http.md),
[preparation](../development/experiments/capability-http-prepare.md) and
[retirement](../development/experiments/capability-http-retirement.md) records.
Deferred credential storage, provider responsiveness, throughput and transitional
syntax are tracked in the reference documents and technical debt. C9 below is the
remaining implementation plan; B's close does not deliver general capability pipes.

## Milestone C: capability pipes

A5 already supplies byte-or-value output, owned handoff, failed values and
session retention. Its collector remains process-local and nondelegatable;
re-readable values move without copying or another byte-retention charge.
C9 must integrate with those [existing ownership and accounting rules](../interfaces/output.md).
General pipelines remain [byte pipes](../interfaces/pipes.md) until then.

- [ ] **9. Pipes carry capabilities.** A pipe can pass its producer's capability
  to the consumer instead of copying bytes, so files and replies travel without
  a copy and a failed capability fails its reader. Byte pipes remain the base
  case, so programs that only read bytes are unaffected.

## Later

- `rm` on held capabilities as well as names.
- The provider-side open questions in the design: provider lifecycle and the
  cost of interposition, shared memory, and provider policy.

# Pyxis OS / Caelum development

## Starting or resuming work

- Pyxis OS is the operating system; Caelum is its kernel. This file describes
  how to collaborate. Architecture and future design belong in docs, not here.
- BOOTSTRAP.md is the historical bring-up assignment, not today's scope. Do not
  reapply its single-CPU, interrupts-disabled or halt-after-initialization limits.
- Check the working directory, branch, worktrees, local changes and submodule
  state before editing. The initial checkout can be old; find the active work
  rather than assuming it is main. Preserve user changes and local commits,
  including documentation intentionally saved for the next PR.
- Use docs/README.md to find subsystem references and docs/wip/boot-sdk-ports.md
  as the milestone index. Read the selected milestone, its unchecked task and
  relevant subsystem docs/code, not the entire
  planning backlog. Milestone order is flexible; the user's current choice wins.
- Task PRs never edit the milestone index; record progress and checkboxes in the
  milestone's own document. Only assignment, completion or a plan change does.
- A new thread should recover scope from the user's request, milestone checklist,
  Git history and open PRs. Do not rely on private chat history for a decision.
  Check whether a prior PR was merged before selecting the next task. If that
  cannot be established, ask one focused question instead of guessing.

## Discussion, decisions and implementation

- Keep one bounded milestone moving through focused tasks to completion. Do not
  start unrelated tracks, chase the port backlog or add features just because a
  dependency or future design might eventually need them.
- Brainstorming, proposals and code-review requests do not authorize implementation.
  For discussion, explain the proposed contract and tradeoffs without editing or
  creating branches unless asked. For review, report concrete findings first;
  write fixes when requested. Once implementation is authorized, carry that task
  through validation, commits and PR without repeated routine permission requests.
- A plan PR being open, reviewed or updated does not authorize its first task.
  Start a milestone task only after the owner merges the plan or explicitly says
  to begin it. When asked what you did or intend to do, answer from your own
  earlier messages.
- Before implementing a task, identify unresolved behavior, authority, lifetime
  and policy decisions. Propose a concrete small scope and stop to discuss those
  decisions. Routine implementation choices do not require approval. Do not turn
  an unanswered suggestion into an accepted requirement.
- The user reviews the code to understand the system. Prefer explicit, readable
  changes and explain material decisions. State assumptions and limitations
  plainly; distinguish measured behavior, code inspection and speculation.
- Preserve subsystem boundaries and existing behavior outside the assigned change.
  Solve concrete needs without forcing POSIX compatibility, novelty for its own
  sake, or another OS's architecture. Port through native interfaces; do not bend
  the kernel around an individual application or add successful fake operations.
- Libc is one of those native interfaces. A port that needs a standard function
  (ISO C, or a proven extension such as `mkdir`) gets it in userland libc, built
  on native objects; do not hand-roll capability calls in the port instead. Do
  not add kernel mechanisms only to satisfy POSIX semantics, such as POSIX-shaped
  thread syscalls. See [libc portability](docs/userland/libc-portability.md).
- Keep configuration, authored data, draft values, benchmark parameters, machine
  properties and implementation choices distinct from architectural contracts.
  Before adding a validator or assertion, identify the deliberate contract that
  changing the value would violate. Keep one authority for each setting or mutable
  state; investigate overlapping ownership instead of adding synchronization and
  tests that preserve it. Promote a provisional choice into a contract only for an
  explicit architectural reason, recorded with that contract.

## Branches, repositories and delivery

- Unless directed otherwise, fetch current main, create a task branch, make focused
  commits, push and open a PR. Reuse the branch for requested follow-ups to an open
  PR. The user merges; do not merge or push directly to main unless asked. Respect
  explicit exceptions such as leaving a local commit for the next PR.
- Use the installed Forgejo CLI, fj, for PRs, comments and existing CI status.
  Check its help for syntax when needed. If access fails, report the actual error;
  do not assume a missing comment means there was no review or expose credentials.
- Put PR review findings directly in comments on the affected PR, including the
  reviewed revision, code references, consequence and required correction. Read
  existing comments first to avoid duplicate findings. Link dependency findings
  from integration PRs when they affect readiness. Summarize the outcome in chat;
  do not make the user relay review notes between agents. On re-review, record
  which findings are resolved and which remain. If posting fails, give the user
  the findings and the actual error.
- Pyxis owns the kernel, public ABI, SDK export, toolchain integration and image
  assembly. userspace, ports and third_party/lwip are separately versioned
  repositories. Consult docs/development/sdk-and-repositories.md before crossing
  a boundary.
- Check for detached submodule HEADs and local edits. Publish dependency commits
  and open focused PRs in their repositories before updating the parent gitlink;
  never pin an unpublished commit. Link dependent PRs and state merge order. Do
  not silently follow upstream main or update unrelated pins.
- The owner creates repositories and builds/publishes compiler containers. Explain
  exactly when a container rebuild is needed. Ordinary builds use the existing
  compiler and evolving SDK; they should not rebuild the LLVM toolchain in CI.
- Builds download only from the owner's mirrors and caches (port recipes'
  `mirror` URLs, toolchain/build.sh). When a change adds or moves an external
  source, tell the owner which mirror or cache entry it needs, with the upstream
  URL and commit or file, before relying on it. Never fall back to upstream.
- End with PR links, what changed, validation performed, material limits and any
  required owner action. For a task in a milestone, update its checkbox in that
  PR. Do not describe an unrun check as passed or start the next task implicitly.
- Keep the milestone index short. Edit its entry only when work is assigned, a
  milestone completes or the owner changes the plan; record progress and review
  status in the milestone document and the PR. Every PR that touches the index
  conflicts with the next one.

## Implementation style and ownership

- Use freestanding GNU C23, snake_case, two spaces, K&R control braces and function
  braces on their own line. Preserve upstream vendor formatting. Use logical
  blank lines; compactness and fewer lines are not goals.
- Name hardware constants, flags, masks and selectors. Prefer straightforward
  code and small focused helpers over opaque encodings, generic builders, callback
  frameworks or indirection. Keep comments sparse, explaining constraints,
  invariants, ownership and non-obvious ordering rather than restating code.
- Keep Limine in its adapter, hardware details in arch, and allocation policy in
  kernel/mm. Place new files with their subsystem rather than growing a flat
  kernel directory. Early initialization and fault reporting cannot use the heap.
  Do not link kernel or target userspace against host libc; host tools are native.
- Physical addresses are not C pointers. Keep ownership, overflow checks and
  failure unwinding explicit. Follow the current BSP allocation/VM-mutation and
  scheduler handoff contracts in docs/kernel/smp.md and docs/kernel/memory.md.
  Do not casually add allocator locks, remote mutations or access after
  relinquishing ownership.
- Pin dependencies and record local changes and licenses. Do not preserve backwards
  compatibility unless requested. Replace obsolete interfaces, formats and
  implementations, updating in-tree consumers together. Do not increment versions
  merely because implementation changed; do so only when versions must
  intentionally coexist or migration is required.

## Validation and efficient work

- Validate implementation with ordinary builds, interactive QEMU boots and debugger
  inspection. Do not add tests, self-tests, fault injection, CI or boot/output
  automation unless explicitly requested. Documentation-only changes need document
  and link review, not a gratuitous boot.
- When tests are authorized, configure inputs and verify deliberate behavior and
  propagation against independently defined expectations. Do not copy today's
  configuration or fixture contents into multiple expected-value authorities, or
  freeze incidental structure. Exact constants are appropriate when specified by
  a deliberate contract such as the disk format. When replacing an implementation,
  reassess its tests rather than transplanting obsolete assumptions. Green checks,
  coverage and matching documentation do not establish architectural correctness.
- Start with README.md and relevant build/run documentation for commands and options.
  Use make -j16 where appropriate. Match CPU count, devices and accelerator to the
  feature being checked; report the configuration and distinguish nested-VM
  measurements from the owner's host results. Clean up your own QEMU/debugger jobs.
- For milestones and substantial tasks that may affect performance, capture a
  baseline before implementation and repeat matched workloads afterward. Record
  revisions, configuration, commands, repeated samples and variation in the relevant
  docs/PR; distinguish host/nested-VM and profiled/unprofiled results. Explain
  regressions and discuss material tradeoffs rather than expanding scope to optimize
  everything. Use existing tools; this does not authorize new benchmark infrastructure.
- Inspect existing CI for the exact submitted revision, including dependent repo
  jobs when relevant. Report pending, failed or unavailable checks honestly.
  Existing independent bundles can avoid unnecessary rebuilds; follow
  docs/development/build-bundles.md and never substitute stale artifacts for
  changed inputs.
- Read/search only relevant files and summarize build output. Avoid repeatedly
  reading whole documents, polling excessively or rerunning checks after a pass
  without a new change or unresolved concern. Keep progress updates concise.
- Delegate bounded independent work to subagent of your own harness at its strongest
  setting (Codex: gpt-6.1-sol, high effort). If none is available, do the work yourself.
  The primary agent owns design,
  integration and review. Give explicit scope; avoid overlapping edits and use
  isolated worktrees for parallel code work. Delegation is optional, not ceremony.

## Durable documentation and handoff

- Keep decisions outside chat: mark agreed choices, proposals and implemented
  behavior distinctly in the relevant milestone/subsystem docs. Record accepted
  limitations in docs/technical-debt.md with their consequence and revisit point.
  WIP ideas do not authorize code or placeholder APIs.
- Keep README short and practical; no roadmap prose, directory trees, marketing
  language or emojis. Put interface invariants beside code and usage in docs.
- On milestone completion, rewrite its docs/wip document as concise implemented
  behavior, interfaces and limits; move it into the appropriate docs subject
  folder and update links. Remove completed worklists and superseded discussion;
  Git preserves history. Carry
  relevant deferred work into WIP or technical debt without duplicate archives.
- Keep documentation lean; Git preserves everything removed. Once the owner
  accepts a decision, the WIP document keeps the accepted contract and drops the
  alternatives, earlier rounds and review history. A finished task's working
  document is folded into its milestone document or removed.
- An experiment record under docs/development/experiments is a short summary:
  revisions, configuration, commands, measured results with ranges, and limits.
  Raw captures, logs, counter dumps and step-by-step session narration belong in
  the PR or stay local, not in the repository.
- A technical-debt entry is a few lines: the limit, its consequence and when to
  revisit it. Measurements and history live in the linked reference or record.
  Delete an entry when its limit is resolved.
- Before ending unfinished work or handing off to another thread, leave a concise
  recoverable status: branch/PR and dependency revisions, completed work, remaining
  steps or decisions, validation results and any active tool processes. Keep this
  task-specific state in the handoff or milestone notes, not in AGENTS.md.

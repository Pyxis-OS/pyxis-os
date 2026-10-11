# Native command execution

Status: **shared milestone proposal, owner direction 2026-10-11; not implemented.**
Extract common work from [Claude task 1e](claude-on-pyxis.md),
[Git run-command](git-cli-on-pyxis.md#process-model-and-a-first-cli-slice),
Lua/ports and libuv; no consumer must finish first. Each task needs a separate
owner go. The three defaults below are proposed, not accepted.

## Deliverable and boundary

Lua and the shell can launch native argv commands with selected stdin/stdout/
stderr, capture output, apply deadlines and observe per-stage results and cleanup.
The shell already runs native pipelines; the new work is reusable capture,
deadlines, per-stage observation and Lua access, including a concurrent I/O pump.
Use existing FILE, PIPE, CONSOLE, launcher and process contracts. No POSIX shell,
fork, numeric PID killing, synthetic signals or successful `system`/`popen`
substitutes. The shell owns its parser; native callers pass argv/stage arrays.

Consumers are the [Claude harness](claude-on-pyxis.md),
[Git executable children and remote helper](git-cli-on-pyxis.md),
[Lua](../userland/lua.md), the [ports runner](../development/ports.md#building-and-packaging),
[source builds](source-builds.md) and [libuv/Neovim jobs](neovim-libuv.md).
The first shared profile is direct programs and explicitly described pipelines,
with no child launch authority. Git's fork callbacks/sideband adaptation, dynamic
descendants (including make launching cc), PTYs, background jobs and full build-tool
ports remain consumer work. No HTTP, credential-store, thread or FAT32 implementation.

## Inspected foundation and direct-launch assessment

Main `69047eee`, userland `d560fbb0`, ports `df3210b0`; source inspection only.
[Shell pipelines](../userland/shell-streams.md) already select three streams and
use atomic batch launch for up to eight stages. FILE descriptors start at offset
zero; PIPE/CONSOLE are sequential. Delegation transfers no FILE cursor or input
read-ahead. Flush selected buffered output before delegation; borrow live stdio,
not closed immutable startup entries. Absent stdin returns EBADF, not invented EOF.
[Pipes](../interfaces/pipes.md) have try-I/O and mixed readiness waits.

**Direct launch works for this first profile.** [Launch preparation](../../kernel/user/spawn.c)
returns WAIT|TERMINATE for every child. [Process TERMINATE](../../include/abi/process.h)
uses safe stop, covers that process's threads and blocked-operation unwind,
and preserves an exit/fault already committed. With no launcher, space factory
or launch-broker bypass delegated to children, the explicitly listed stages are the whole job.
Terminate each stage, then observe WAIT_COMPLETE and retrieve its real result.
[Foreground Ctrl+C](../../userspace/shell/launch.c) already follows this pattern.
No CREATE_GROUP, nested group or same-space executor is required for ordinary jobs.

A group-bound client's children remain in its existing pane/session group;
[session group termination/completion](../interfaces/execution-groups.md) therefore
covers them without separate sibling-group accounting. Per-process WAIT observes
process/task/VM/grant-table reclamation, **not all deferred object destruction**;
full attributed cleanup remains the group's completion boundary. An open session
group cannot be waited to completion after each job. Release the caller's captures
and pipe endpoints after stage completion; report that boundary honestly.

Closing an observer does not stop execution. Normal cancel/deadline/error paths
must explicitly terminate and observe every admitted stage. If the caller faults
or is forcibly stopped first, children can survive until their enclosing session
group stops; a surviving session does not automatically stop them on a Lua caller's
fault. Ungrouped callers have no such fallback. This profile does not promise
per-job fail-closed behavior on caller death. The accepted trusted Claude harness
keeps its stronger sole-CONTROL fresh-group lifetime; that does not require an
executor for ordinary clients. Termination never rolls back committed effects,
and published HOST work can delay retirement indefinitely.

Lua `pyxis.run` inherits live streams, cwd/roots/environment and ordinary resources,
blocks on process completion and lacks capture/deadlines. `os.execute`/`io.popen`
and libc `system`/`popen` are absent; the host ports runner quotes argv into shell
APIs. Libuv already launches pipe children asynchronously but rejects inherited
FILEs and adds resources to its inherited inventory instead of replacing it exactly.

## Proposed defaults requiring owner decisions

1. **Direct launch through the client's existing launcher.** One userland job
   core retains each returned process observer and uses per-process TERMINATE/WAIT;
   a job is local owned state, not a new kernel object or copied job-lease protocol.
   Use explicit image READ, argv, selected roots/namespace, owned cwd/env snapshots
   and exact child resources. Omit descendant launchers, space factories, other
   administrative grants and launch-capable brokers, including through bundle/
   namespace resources; never add authority implicitly. Libc owns descriptor/FILE bridging,
   userland owns execution, and Lua/ports supply bindings. Preserve ordinary
   `pyxis.run`/`uv_spawn` behavior. Keep the caller-death and deferred-cleanup limits
   above visible; do not replay an uncertain launch. Trusted ungrouped Claude
   supervision remains an optional existing-group backend with its accepted
   fresh-group/sole-CONTROL contract, not a prerequisite for ordinary clients.
2. **First capture keeps the accepted Claude storage/authority profile.** Separate
   private RAM stdout/stderr FILEs are created then unlinked under an explicitly
   provisioned private work root; shared `tmp://` or a filename is not privacy.
   Give children stream WRITE and selected project/runtime/network grants; omit
   private roots, credentials, descendant launchers, named console input/output,
   keyboard/pointer/display and terminal-service bypass grants. Absent stdin is
   deliberate. Claude seals its fresh group; ordinary direct jobs use each stage's
   process observer within the client's existing group.

   Return capped bytes with truncation and local stop cause alongside actual
   EXITED/FAULTED/TERMINATED results; fault details remain in klog. Default to the
   previously proposed configurable 1 MiB per-stream polled trigger: consumer
   policy, not a hard backing quota or universal output cap; it can overshoot.
   No stdout/stderr chronology is promised. Use one absolute job deadline with
   bounded native waits; request stop on deadline/size/cancel, then observe actual
   stage retirement (and the Claude group when used) before releasing captures.
   Check expiry when launch returns: synchronous HOST/image preparation cannot
   be interrupted by a userland deadline, so admission and cleanup have no hard
   wall-time bound. A stop race may yield an already committed normal exit or fault.
3. **Pipelines pump concurrently and return all stage results.** Use explicit
   stage arrays, batch publication and directional pipes; close unused endpoints
   promptly and pump input/stdout/stderr with try-I/O/readiness. Avoid serial
   blocking drains or waiting before draining a full pipe. Short counts, EOF,
   EPIPE and transfer failure stay distinct; progress never refreshes the deadline.
   Check request/attachment and launch metadata limits before effects. Failed
   batch publication creates no children, but prior FILE creation/truncation is
   not rolled back. Stop/observe each stage on cancellation or failure. Preserve
   shell last-stage success by default; native callers and prepared HTTP
   redirection can require all producers to succeed.

## Ordered delivery tasks

- [ ] **1. Small first task: one-child direct job core.** Explicit streams and
  resource lists, owned cwd/env, native result, terminate and stage retirement
  over existing launcher/process APIs. No Lua, shell grammar, service protocol or
  capture policy. Qualify normal/failing/stopped children and observer ownership.
  **After this task the owner can:** launch one known native program with selected
  streams from a LAUNCH-authorized caller, inspect its result and stop it.
- [ ] **2. Private FILE capture and deadlines.** Implement the profile above for
  direct callers and the trusted Claude group backend; retain storage/observers
  through actual stage retirement, with sizes, truncation and stop cause.
  **After this task the owner can:** capture separate stdout/stderr from silent,
  noisy and failing programs and distinguish timeout from actual termination.
- [ ] **3. Native pipeline and event pump.** Reuse batch launch and bounded pipes,
  concurrent input/output, per-stage results, early-reader-close handling,
  deadlines and all stage/endpoint cleanup. No threads assumption.
  **After this task the owner can:** run a described pipeline with captured output
  without deadlocking on a full stdout/stderr pipe.
- [ ] **4. Lua/shell delivery and reusable port adapters.** Add native Lua argv/
  pipeline/capture options and shell capture/deadline/per-stage observation with
  existing launch authority. Preserve ordinary run/pipeline behavior. Carry
  exact per-stage results into HTTP redirection coordination. Offer libuv FILE/
  exact-resource selection as opt-in, preserving ordinary `uv_spawn` inheritance
  and loop/callback behavior. Give ports native argv run/capture replacements
  for host `os.execute`/`io.popen`; full recipes still need tools, adaptation and
  the descendant profile below. Qualify caller exit/fault limits separately from
  session cleanup.
  **After this task the owner can:** run/capture direct commands and native
  pipelines from Lua or the shell, with real deadlines and results.

## Deferred descendant supervision

The first consumer needing descendants (Git child/helper chains, make→cc, or
another profile requiring fail-closed job lifetime) must stop for an authority/
lifetime proposal. A per-process stop cannot cover unknown descendants. Evaluate
an outside-group executor/sibling groups then; no executor, job-lease objects or
nested-group kernel design is assigned by this milestone. Such a proposal must
settle copied ownership vs observation, lost handoffs, client/coordinator death,
sibling-group cleanup and responsive admission despite blocking native launch/
HOST preparation. It must fit the [capability-valued environment direction](capability-environment.md).

Assigned tasks use ordinary builds and interactive 1/4-CPU QEMU/GDB, with matched
baselines where existing paths change. Exercise denied/absent streams, exact
resources/no launch bypass, FILE cursor independence, short/full two-way pipes,
silent children, early reader exit, uncertain publication, deadline races,
caller death and session stop. Inspect owned handles/registrations and distinguish
stage retirement from group cleanup. This proposal runs no new tests or boots.

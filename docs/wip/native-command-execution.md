# Native command execution

Status: **shared milestone proposal, owner direction 2026-10-11; not implemented.**
Extract common work from [Claude task 1e](claude-on-pyxis.md),
[Git run-command](git-cli-on-pyxis.md#process-model-and-a-first-cli-slice),
Lua/ports and libuv; no consumer must finish first. Each task needs a separate
owner go. The three defaults below are proposed, not accepted.

## Deliverable and boundary

Lua and the shell can launch native argv commands with selected stdin/stdout/
stderr, capture output, apply deadlines and run real pipelines between native
programs, observing actual results and cleanup. Use existing FILE, PIPE, CONSOLE,
launcher, process and execution-group contracts. No POSIX shell, fork, numeric
PID killing, synthetic signals or successful `system`/`popen` substitutes.
The shell owns its parser; native callers pass argv/stage arrays, not shell text.

Consumers are the [Claude harness](claude-on-pyxis.md),
[Git executable children and remote helper](git-cli-on-pyxis.md),
[Lua](../userland/lua.md), the [ports runner](../development/ports.md#building-and-packaging),
[source builds](source-builds.md) and [libuv/Neovim jobs](neovim-libuv.md).
The first shared profile is direct programs and explicitly described pipelines.
Git's in-memory fork callbacks/sideband adaptation, dynamic descendant tooling,
PTYs, background jobs and full build-tool ports remain separate consumer work.
No HTTP, credential-store, native-thread or FAT32 implementation is included.

## Inspected foundation

Main `69047eee`, userland `d560fbb0`, ports `df3210b0`; source inspection only.
[Shell pipelines](../userland/shell-streams.md) already select three streams and
use atomic batch launch for up to eight stages. FILE descriptors start at offset
zero; PIPE/CONSOLE are sequential. Delegation transfers no FILE cursor or input
read-ahead. Flush selected buffered output before delegation; borrow live stdio
bindings, not closed immutable startup entries. Absent stdin returns EBADF,
not invented EOF or `/dev/null`.

[Pipes](../interfaces/pipes.md) already have try-I/O and mixed readiness waits;
concurrent draining can use them without threads. [Process results](../interfaces/processes.md)
distinguish signed EXITED, FAULTED and TERMINATED; fault details remain in klog.
[Execution groups](../interfaces/execution-groups.md) provide safe stop and
cleanup observation. Closing a process observer does not kill. Termination
requests stop, not rollback, and published HOST work can delay cleanup indefinitely.

Lua `pyxis.run` inherits streams, cwd/roots/environment and ordinary resources,
blocks on process completion and has no capture/deadline/group profile.
`os.execute`/`io.popen` and libc `system`/`popen` are absent. The host ports runner
currently quotes argv into those shell APIs. Libuv already launches pipe children
and observes them asynchronously, but rejects inherited FILE streams and adds
resources to its inherited inventory rather than replacing it exactly.
The gap is reusable execution/ownership policy and adapters, not missing pipes.

## Proposed defaults requiring owner decisions

1. **One userland job core, with an outside-group coordinator for ordinary clients.**
   Use explicit image READ authority, argv, selected roots/namespace, owned cwd/
   environment snapshots and an exact child-resource inventory. Never inherit
   executor authority merely because it holds it. Put descriptor/FILE bridging
   in libc, execution ownership in userland and application bindings in Lua/ports.
   Keep ordinary `pyxis.run` behavior separate.

   Trusted ungrouped callers such as the accepted Claude harness use the core
   directly with CREATE_GROUP and retain sole CONTROL. For LAUNCH-only or already
   group-bound Lua/shell clients, explicitly provision an ungrouped executor
   endpoint in the participating space; it creates sibling job groups and holds
   sole CONTROL. This avoids granting ordinary shells CREATE_GROUP or inventing
   nested groups. Its provisioned space is fixed; payload space/PID names confer
   no authority. No automatic executor discovery or privilege upgrade.

   An owned job lease gives its client status/stop authority; copies deliberately
   prolong that ownership. Commands receive none. The executor retains no client
   lease after handoff; explicit job-stop or last-owner retirement stops the job,
   and executor death closes CONTROL. Cancelling a status/result CALL does not
   stop a job with surviving owners. Separate observation from ownership so observers
   cannot keep execution alive accidentally. Do not equate CALL cancellation with
   revocation of admitted launch work or replay an uncertain launch. Design the
   object/rights/copy/inheritance/retirement contract before task 2, consistently
   with [capability-valued variables](capability-environment.md).
2. **First capture keeps the accepted Claude profile.** Use separate private
   RAM-backed stdout/stderr FILEs, created then unlinked before launch under an
   explicitly provisioned private work root; never infer privacy from shared
   `tmp://` or a filename. Give children only stream WRITE, selected project and
   runtime/network grants; omit private work roots, credentials, descendant launchers,
   named terminal input/output, keyboard/pointer/display and terminal-service
   bypass grants. Absent stdin is deliberate. Seal the fresh group after launch.
   The direct harness retains sole CONTROL as already accepted; the executor
   frontend supplies the equivalent ownership for authorized ordinary clients.

   Return capped bytes with truncation and separate local stop cause alongside
   actual process results. Default to the previously proposed 1 MiB per-stream
   polled size trigger, configurable by the caller; this is not a hard backing
   quota and can overshoot. No stdout/stderr chronology is promised. One absolute
   job deadline uses bounded native waits underneath. Deadline/size/last-owner
   retirement requests termination; continue observing actual process/group cleanup before
   claiming completion or releasing captures. A termination race may still yield
   an already committed normal exit or fault. Result limits are consumer policy,
   not a universal command-output cap.
3. **Pipelines use one supervised job and concurrent I/O.** Accept explicit stage
   arrays, use existing batch publication and directional pipes, close unused
   endpoints promptly, and pump both output directions with try-I/O/readiness.
   Do not drain blocking stdout then stderr, wait before draining a full pipe,
   refresh the overall deadline after progress, or hold unrelated grants to keep
   peers alive. Short counts, EOF, EPIPE and transfer failure remain distinct.
   Check request/attachment and launch-metadata limits before effects. Batch
   publication failure creates no children; prior FILE creation/truncation
   effects are not rolled back. Return every described stage's result and final
   capture/cleanup status.
   Preserve the shell's existing last-stage success policy; native callers and
   prepared HTTP redirection can require all producers to succeed. No descendant
   launcher by default: controlled descendant admission needs its own later
   authority/lifetime profile, not an implicit executor or foreign-group grant.

Jobs created by an outside executor are sibling groups, not members of the
client's pane/session group. Client-group completion alone cannot prove their
cleanup. Frontends must cancel and separately observe registered jobs before
promising complete session/pane cleanup. Unexpected client death retires its
leases; the executor stops/reaps on last-owner retirement, while deliberately
retained owner copies prolong the job. Do not promise a bounded
cleanup time or hide surviving work behind a root exit status.

## Ordered delivery tasks

- [ ] **1. Small first task: one-child job core for trusted coordinators.**
  Build on native launch/process/group APIs: explicit streams and resource lists,
  owned cwd/env, native result, terminate and cleanup. No Lua, shell grammar,
  executor protocol or capture policy yet. Qualify real normal/failing/stopped
  children with existing debugger/manual tools.
  **After this task the owner can:** launch one known native program from an
  opt-in trusted coordinator with selected streams and inspect/stop its real job.
- [ ] **2. Same-space executor and job leases.** First stop for the object/rights/
  lifetime/inheritance review in decision 1. Add capability-authorized grant handoff,
  bounded request metadata/attachment staging, exact child authority and explicit
  admission failure. Bind published work to a retained owner or executor cleanup
  even when submission/handoff is abandoned. Handle partial preparation, lost
  replies and retirement; no raw caller pointers or numeric-handle handoff. Keep execution responsive to
  status/stop and deadlines while waiting. Native launch is a blocking CALL and
  HOST preparation has no fixed wait bound: the review must settle bounded
  admission and isolation of blocking preparation, not assume one service loop
  can block on launch/cleanup while servicing other jobs, future threads or a
  new kernel asynchronous launch API.
  **After this task the owner can:** submit and stop a real job from an authorized
  LAUNCH-only/group-bound client without granting it group-creation authority.
- [ ] **3. Private FILE capture and deadlines.** Implement the profile above for
  direct coordinators and executor clients; keep buffers/storage/caps owned until
  actual cleanup. Record captured sizes, truncation, result and stop trigger.
  **After this task the owner can:** capture separate stdout/stderr from silent,
  noisy and failing programs and distinguish timeout from actual termination.
- [ ] **4. Native pipeline and event pump.** Reuse batch launch and bounded pipes,
  support input production while both outputs drain, per-stage results and reader
  early-close policy. Integrate deadline, error/cancel and all endpoint cleanup.
  **After this task the owner can:** run a described native pipeline with captured
  output without deadlocking on a full stdout/stderr pipe.
- [ ] **5. Lua/shell delivery and reusable port adapters.** Add native Lua argv/
  pipeline/capture options and shell job controls with explicit executor authority;
  preserve ordinary run/pipeline behavior. Carry exact per-stage results into
  prepared-redirection coordination. Adapt libuv's explicit FILE/resource/launcher
  selection through an opt-in shared-core profile, preserving its loop/callback
  contract and existing ordinary `uv_spawn` authority/inheritance by default.
  Give the ports runner native argv run/capture replacements for host
  `os.execute`/`io.popen`, not shell-string emulation; running whole recipes still
  needs hosted tools and native recipe adaptation. Qualify client exit/fault,
  supervisor death and pane/session job cleanup separately from client-group wait.
  **After this task the owner can:** run and capture commands and native pipelines
  from Lua or the shell, with real deadlines/results and explicit cleanup.

Qualify assigned tasks with ordinary builds and interactive 1/4-CPU QEMU/GDB;
record matched launch/pipe baselines where changes affect existing paths.
Exercise absent/denied streams, exact grants, same-file cursor independence,
short transfers, full two-way pipes, silent children, early reader exit,
publication failure/unknown outcome, deadline races and client/coordinator death.
Inspect owned handles, registrations and cleanup back to zero. No new test
infrastructure or claim of native validation is made by this proposal.

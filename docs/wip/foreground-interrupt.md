# Foreground command interruption

Status: direction and first-slice decisions agreed 2026-10-02. Review added the
interrupt event contract below and found two policy questions still open:
unsaved Kilo edits and raw-keyboard commands. Implementation is not started.

Ctrl+C currently only cancels the shell's line being edited. It cannot stop a
running foreground command, locally or through the
[remote terminal](../userland/remote-terminal.md). The only way to recover from
a stuck command is to end the whole session. The
[technical-debt entry](../technical-debt.md#process-termination-and-ctrl-c)
records this gap. The goal is for Ctrl+C to terminate the running foreground
command or pipeline and return to the shell, with explicit authority and no
general signal mechanism.

## Reusable machinery

- **Per-task stopping already exists.** Execution-group termination calls
  `task_request_stop` on each member. That function and the subsystem unwind in
  the [termination ownership matrix](../interfaces/execution-group-termination.md)
  (blocked syscalls, pipes, terminals, endpoints, network, BSP/HOST loans and
  launch capture) all operate on one task. Interruption reuses them unchanged.
- **Groups are the wrong unit.** A remote shell and its foreground job share the
  session's group, and the local shell is ungrouped. Group termination would
  need nested job groups with hierarchical membership, completion accounting and
  termination ownership. That is not needed here.
- **Process handles cannot reach their task.** A process observer is a
  completion record with no task pointer. A lock-protected task link, cleared at
  task completion, is the only new kernel lifetime this needs.
- **A foreground job is only the shell's direct children.** Ordinary commands
  receive no launcher (only `session` successors do), so a job consists of the
  pipeline stages. Terminating those processes covers the whole job.

## Agreed design

1. **Process-level termination, not nested job groups.** A new
   `PROCESS_RIGHT_TERMINATE` and operation on process handles requests a stop
   through `task_request_stop`.
   - The request is idempotent.
   - An exit or fault that already happened keeps its result. Otherwise the
     observer reports PROCESS_TERMINATED, which typed completion already
     reports as `terminated`.
   - The shell holds this right for its own children and never forwards it.
   - It is general enough for a later process-control tool; no such tool is
     part of this work.
2. **Ctrl+C is recognized only while interrupts are armed.**
   - A new `CONSOLE_RIGHT_INTERRUPT` on the existing console input object can
     arm and disarm interruption.
   - While armed, an input byte 3 is removed from the input stream and becomes
     an interrupt event, observable through `wait_many`. Applications never
     receive that byte.
   - Both input producers apply the same rule: local console keyboard text and
     terminal injection, which carries remote input. The remote client already
     forwards Ctrl+C as byte 3 and needs no change.
   - The new right goes only on the root shell's own input grant. Commands'
     standard input stays READ-only and cannot observe or consume interrupts.
     No new shell-only handle or session object is introduced.
   - Event details are under [interrupt event contract](#interrupt-event-contract).
3. **The shell owns the policy.**
   - It arms interrupts before launching a foreground job's stages, so Ctrl+C
     during launch cannot reach a just-started child as data. It disarms them
     after the job, after a launch failure, and always before its own line
     editing, so Ctrl+C still cancels a typed line there.
   - Its foreground wait becomes `wait_many` over the stage observers plus the
     interrupt event.
   - On an interrupt it terminates every stage, then keeps its existing waits,
     diagnostics, FRESH_LINE and typed completion. Pipelines still report the
     last stage.
   - Background jobs are unaffected.
4. **Immediate termination, without opt-out, in the first slice.** While
   armed, Ctrl+C typed as console or terminal text terminates the foreground
   job.
   - Without signals or threads, a cooperative interrupt could only reach a
     program that is reading input. The main need is stopping CPU loops,
     blocked readers and hung commands.
   - The Lua REPL changes: Ctrl+C ends Lua instead of cancelling the line.
   - A nested interactive `shell` gets READ-only input and cannot arm, so Ctrl+C
     in the outer shell ends the whole inner shell.
   - A per-application passthrough mode is part of the deferred cooperative
     interrupt decision below. The two open questions that follow may pull a
     minimal form of it into this slice.

### Open: unsaved Kilo edits

Kilo deliberately ignores Ctrl+C so that unsaved edits cannot be lost by
accident (pinned upstream `kilo.c`, the `CTRL_C` case). The first-slice rule
would terminate Kilo and discard unsaved work. Choose one:

- Accept and document the data-loss risk, and validate it explicitly.
- Add a minimal passthrough to this slice, for example an application request
  on its own input handle. Kilo, and libterm's line editor for the Lua REPL,
  would then receive byte 3 as data. This needs a Kilo port patch and
  settles part of the deferred passthrough question now.

### Open: raw-keyboard commands

A local foreground command that acquires the raw keyboard, such as Doom,
receives key events directly. Those events never become console text, so
armed recognition does not see Ctrl+C. Choose one:

- Intercept an armed Ctrl+C in keyboard routing before raw delivery, and
  define how the matching press and release events are suppressed. Doom uses
  Ctrl as fire.
- Exempt raw-keyboard owners: they already own every key event, as passthrough
  would. Remote terminals have no raw keyboard.

## Interrupt event contract

These are observable semantics, proposed in review:

- **Armed interval.** Arming starts with a clear latch. Disarming clears it, so
  nothing recognized during one interval can affect a later job.
- **One coalesced latch.** While armed, each Ctrl+C byte is removed from input
  and sets one latch; repeated presses coalesce. Readiness is level-triggered
  and does not consume the latch, like other `wait_many` events. There is no
  separate acknowledgement operation.
- **Typeahead is discarded.** When the latch is set, input queued before the
  Ctrl+C is discarded, so text typed into a hung command cannot run as the next
  shell command. Bytes arriving later are kept.
- **Shell sequence.** Arm, launch, then wait on stage completion plus the
  interrupt. On interrupt, terminate every stage and stop watching the interrupt.
  Stay armed while waiting for all stages, so further Ctrl+C presses are
  swallowed instead of reaching a stage or spinning the wait. Disarm only after
  the job finishes.
- **Completion race.** Terminating a process that already finished does
  nothing. Process results stay immutable, so a job that finishes before its
  termination takes effect reports its real result. A latch set after the last
  stage completes is cleared by the disarm.
- **Arming lifetime.** Closing the last interrupt-authorized grant on the input
  object disarms and clears the latch.
- **Unarmed input.** Byte 3 queued while unarmed keeps its normal meaning as
  data, including for the shell's own line editor.

## To verify during implementation

These are implementation checks, not open policy:

- Whether stopping an ungrouped task is safe. Today the stop path only runs for
  group members, and the local shell's children are ungrouped.
- Lock order between the process record's task link, task completion and the
  scheduler queues.
- The process-to-task link must be detached under its lock before task memory
  is freed. Today `reap_completed` frees the task before it publishes process
  completion, so the late completion call cannot be the only detach point.

## Deferred

- **Cooperative interrupts.** For example, an interrupt delivered as the result
  of the next console read, with a second Ctrl+C escalating to termination.
  Decide this together with an application passthrough mode, the Lua REPL and
  other raw-input programs.
- Interrupting background jobs, job control, suspend/resume and a separate
  process-control tool.
- Nested execution groups and descendants of foreground jobs. These need
  revisiting if ordinary commands ever receive a launcher.

## Focused tasks

1. [ ] **Process termination.** Add the terminate right and operation, the
   process-to-task link and its lifetime. Verify the ungrouped stop path.
2. [ ] **Interrupt arming and events.** Add the interrupt right, armed Ctrl+C
   recognition on console and terminal input, and the `wait_many` event,
   including cleanup when the arming grant closes.
3. [ ] **Shell interruption.** Add the root shell's interrupt grant in local and
   remote session setup, the foreground `wait_many` loop and terminate-on-interrupt.
   Update shell, terminal, remote and execution-group docs and the
   technical-debt entry.
4. [ ] **Validation.** QEMU, local and remote:
   - a CPU loop, a blocked reader and a pipeline;
   - typed `terminated` completion;
   - Ctrl+C at the prompt still cancels the line;
   - Ctrl+C during launch, repeated Ctrl+C and typeahead disposal;
   - Ctrl+C as a job finishes, without affecting the next job;
   - Lua REPL, Kilo and raw-keyboard behavior, as decided above;
   - background jobs and other sessions unaffected;
   - debugger inspection of retirement and released waits.

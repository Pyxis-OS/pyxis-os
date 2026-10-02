# Foreground command interruption

Status: direction and first-slice decisions agreed 2026-10-02. Implementation
is not started.

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
3. **The shell owns the policy.**
   - It arms interrupts while waiting for a foreground job and disarms them
     before its own line editing, so Ctrl+C still cancels a typed line there.
   - Its foreground wait becomes `wait_many` over the stage observers plus the
     interrupt event.
   - On an interrupt it terminates every stage, then keeps its existing waits,
     diagnostics, FRESH_LINE and typed completion. Pipelines still report the
     last stage.
   - Background jobs are unaffected.
4. **Immediate termination, without opt-out, in the first slice.** While
   armed, Ctrl+C always terminates the foreground job.
   - Without signals or threads, a cooperative interrupt could only reach a
     program that is reading input. The main need is stopping CPU loops,
     blocked readers and hung commands.
   - Kilo does not use Ctrl+C.
   - The Lua REPL changes: Ctrl+C ends Lua instead of cancelling the line.
   - A per-application passthrough mode is part of the deferred cooperative
     interrupt decision below.

## To verify during implementation

These are implementation checks, not open policy:

- Whether stopping an ungrouped task is safe. Today the stop path only runs for
  group members, and the local shell's children are ungrouped.
- Lock order between the process record's task link, task completion and the
  scheduler queues.
- That arming state cannot outlive the shell: it is cleared when the input
  object's last interrupt-authorized grant closes.
- What happens to a byte 3 queued before arming or after disarming. Bytes keep
  their original meaning unless they arrive while armed.

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
   - Lua REPL and Kilo behavior;
   - background jobs and other sessions unaffected;
   - debugger inspection of retirement and released waits.

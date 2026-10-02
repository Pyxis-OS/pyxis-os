# Foreground command interruption

Status: direction, first-slice decisions and the interrupt event contract
agreed 2026-10-02, including the review follow-ups: minimal passthrough,
raw-keyboard exemption and typeahead disposal. Arming and passthrough lifetimes
were settled as returned handles on 2026-10-02, before task 2. Task 1 (process
termination) is implemented; see [processes](../interfaces/processes.md).

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
   - A new `CONSOLE_RIGHT_INTERRUPT` on the existing console input object
     authorizes arming. Arming returns an armed handle to the same input
     object, and closing that handle disarms (see
     [handle lifetimes](#handle-lifetimes)).
   - While armed, an input byte 3 is removed from the input stream and becomes
     an interrupt event, observable through `wait_many`. Applications do not
     receive that byte unless passthrough is active (decision 5).
   - Both input producers apply the same rule: local console keyboard text and
     terminal injection, which carries remote input. The remote client already
     forwards Ctrl+C as byte 3 and needs no change.
   - The new right goes only on the root shell's own input grant. Commands'
     standard input stays READ-only and cannot observe or consume interrupts.
     No new object type or session object is introduced; armed and
     passthrough handles refer to the existing input object.
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
4. **Immediate termination, with application passthrough.** While armed,
   Ctrl+C typed as console or terminal text terminates the foreground job,
   unless the reading application has requested passthrough.
   - Without signals or threads, a cooperative interrupt could only reach a
     program that is reading input. The main need is stopping CPU loops,
     blocked readers and hung commands.
   - A nested interactive `shell` gets READ-only input and cannot arm, so Ctrl+C
     in the outer shell ends the whole inner shell.
5. **Minimal passthrough, in this slice.** An application can ask, on its own
   READ-authorized input handle, to receive Ctrl+C as data. The request returns
   a passthrough handle to the same input object.
   - While any passthrough handle exists, an armed Ctrl+C byte is delivered as
     input: no latch is set and no typeahead is discarded.
   - Closing the passthrough handle withdraws the request, and process exit
     closes it. It never affects other input objects.
   - libterm exposes the request. Its line editor holds a passthrough handle
     only while reading a line, so in the Lua REPL Ctrl+C cancels the typed line
     as before, while running Lua code can still be interrupted.
   - Kilo deliberately ignores Ctrl+C to protect unsaved edits (pinned upstream
     `kilo.c`, the `CTRL_C` case). A Kilo port patch requests passthrough for
     the editing session, so Ctrl+C cannot discard unsaved work.
   - Accepted cost: a program holding passthrough cannot be interrupted with
     Ctrl+C. Remote sessions keep Ctrl+] and group termination as the fallback;
     a local program stuck in passthrough still has no recovery short of ending
     its session.
6. **Raw-keyboard owners are exempt.** A local command that acquires the raw
   keyboard, such as Doom, receives key events directly, and those never become
   console text. Armed recognition applies only to text input, so these
   programs end through their own controls. Remote terminals have no raw
   keyboard. Intercepting raw events was rejected, because Doom uses Ctrl as
   fire and the matching press and release would need suppression.

## Handle lifetimes

Agreed before task 2. The kernel tracks closure by counting grants per right,
not per handle: two READ handles to one input object are indistinguishable,
and an application's READ handle is often a standard input shared with other
processes. Interrupt-capable input also passes through init, session and the
startup script before reaching the root shell, so several processes hold
copies. Arming and passthrough are therefore separate handles to the same input
object, each carrying a single dedicated right whose grants the input object
counts. The existing terminal hangup right uses the same accounting.

- **Armed handle.** ARM_INTERRUPT on a handle with `CONSOLE_RIGHT_INTERRUPT`
  returns a handle carrying only the armed right. `wait_many` observes the
  interrupt on it. Closing the last armed grant, including at process exit,
  disarms and clears the latch, so a shell that fails while armed disarms
  automatically. Only one armed interval exists per input object: arming while
  armed returns BUSY.
- **Passthrough handle.** PASSTHROUGH on a READ handle returns a handle
  carrying only the passthrough right. Passthrough is active while any such
  grant exists. A copied or transferred passthrough handle extends it; that is
  no new authority, because any reader can request passthrough.
- **Where interrupt authority starts.** The kernel's initial per-space `input`
  grant and the terminal-create `input` include `CONSOLE_RIGHT_INTERRUPT`.
  Standard input stays exactly READ, so commands never receive it as a stream.
  Session setup forwards it to the root shell's `input` resource in task 3.
- An owner recorded per process and cleared at exit, like raw keyboard
  acquisition, was rejected. It needs per-process state for each input and has
  no answer for a handle passed to another process.

## Interrupt event contract

These are observable semantics, agreed after review:

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
- **Arming lifetime.** Closing the last armed grant disarms and clears the
  latch. Arming while armed returns BUSY.
- **Unarmed input.** Byte 3 queued while unarmed keeps its normal meaning as
  data, including for the shell's own line editor.
- **Passthrough.** Any passthrough grant overrides arming for that input
  object: byte 3 stays data and nothing is discarded.
- **Input loss and full queues.** Recognition happens before the local
  console's input-loss check and before a terminal's capacity check, since the
  Ctrl+C discards queued input anyway. In a terminal injection, bytes before the
  Ctrl+C are discarded with the queue and count as accepted; bytes after it are
  queued normally. A terminal hangup reports `WAIT_ERROR` on its armed handles.

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
  This would build on the first-slice passthrough request, and could give
  programs holding passthrough a way to be interrupted safely.
- Interrupting background jobs, job control, suspend/resume and a separate
  process-control tool.
- Nested execution groups and descendants of foreground jobs. These need
  revisiting if ordinary commands ever receive a launcher.

## Focused tasks

1. [x] **Process termination.** Add the terminate right and operation, the
   process-to-task link and its lifetime. Verify the ungrouped stop path.
   - Implemented: launch observers carry WAIT and TERMINATE, and libpyxis has
     `process_terminate()`. The link is detached under the completion lock in
     the reaper before the task is freed, and when a preparation is discarded.
   - Ungrouped stopping needed no change: stop checks run at syscall entry and
     exit, preemption and interrupt return, use only the task's own flag, and
     retire through the ordinary reaper.
   - Evidence: four CPUs on nested KVM, with a temporary uncommitted shell hook
     that terminated a foreground job after a delay.
     - Remote (grouped): a Lua CPU loop, a terminal-blocked `cat`, and two- and
       three-stage pipelines of blocked readers and CPU loops reported
       `terminated` for every stage.
     - Local (ungrouped): the same with `cat` and a Lua loop piped to `cat`.
       The shell then kept editing and ran later commands.
     - A process that had already exited kept its exit status 5.
     - A WAIT-only copy was denied, and repeated requests succeeded.
     - GDB saw each stop request reach the task, with a null group for local
       children. Every TERMINATED completion found the link already cleared.
     - Both remote sessions ended with a complete FINAL.
2. [x] **Interrupt arming and events.** Add the interrupt right, armed and
   passthrough handles with their grant accounting, armed Ctrl+C recognition on
   console and terminal text input, typeahead disposal and the `wait_many`
   event. Grant the interrupt right on the kernel's initial console input and
   terminal-create input. Add libpyxis wrappers.
   - Implemented as described in
     [interrupt arming and passthrough](../userland/terminal.md#interrupt-arming-and-passthrough).
     Framebuffer consoles and terminal sessions share one interrupt state with
     its own lock, taken inside the input lock. Existing session setup still
     narrows `input` to READ, so behavior is unchanged until task 3.
   - Evidence: four CPUs on nested KVM, with an uncommitted shell test command
     and temporary forwarding of the interrupt right to the root shell.
     - Local Development shell and a quiet remote session: the root shell's
       input had READ|INTERRUPT. Arming succeeded, a second arm was BUSY and
       arming standard input was DENIED.
     - `abc`, Ctrl+C, `de` while armed set the latch, reported by repeated
       polls without consuming it; a later read returned only `de`. Remotely
       the same bytes arrived as one injection.
     - A blocking `wait_many` woke with WAIT_INTERRUPT on Ctrl+C. Re-arming
       after closing started with a clear latch.
     - With passthrough, `x`, Ctrl+C, `y` read back as `78 03 79` with no latch.
       Unarmed Ctrl+C at the prompt still cancelled the line.
     - A remote shell that exited holding an armed handle released it (GDB:
       armed 1 to 0, latch cleared), and the terminal was destroyed with zero
       armed and passthrough counts.
     - After review, local input overflowed by 4,200 keys while armed: without
       Ctrl+C the read reported INPUT_LOST; with Ctrl+C, `d`, `e` after the
       same flood the latch was set and the read returned only `de`.
     - Terminal hangup reporting WAIT_ERROR, and the readiness worker
       preemption the review found, were reviewed in source only.
   - Found for task 3: when a terminal's input queue is full, the remote
     server stops reading frames until its pending injection drains, so a
     later Ctrl+C never reaches the kernel while the command does not read.
     Local console input is unaffected because a full queue latches input loss
     instead of blocking.
3. [x] **Shell interruption.** Add the root shell's interrupt grant in local and
   remote session setup, the foreground `wait_many` loop and terminate-on-interrupt.
   Add the libterm passthrough call and use it in the line editor. Update shell,
   terminal, remote and execution-group docs and the technical-debt entry.
   - Implemented as described in
     [interrupting foreground commands](../userland/shell.md#interrupting-foreground-commands).
     - `session` launches, the session program and the remote server forward
       READ|INTERRUPT; every other launch narrows `input` to READ.
     - The shell arms only SHELL_FOREGROUND jobs, and only with the right and
       a clock.
     - libterm's line helpers hold passthrough while editing and report
       ERROR if they cannot withdraw it.
   - Decided 2026-10-02: the remote full-queue case found in task 2 is recorded
     as technical debt, not fixed here.
   - Evidence: four CPUs on nested KVM, ordinary image, no test hooks.
     - Local Development shell: a Lua CPU loop, a blocked `cat` and a Lua loop
       piped to `cat` each reported `Process terminated` for every stage.
       `lz` typed into the running loop was discarded. Ctrl+C at the prompt
       still cancelled the line.
     - Lua REPL: Ctrl+C cancelled a typed line and the REPL continued; a
       running `while true do end` was terminated.
     - Quiet remote machine session: the same loop, `cat` and a three-stage
       pipeline reported `terminated` completions. Three presses coalesced.
       `lz` injected with the Ctrl+C into a blocked `cat` was discarded.
       Ctrl+C at the prompt cancelled the line, and the next commands
       reported `exited 0`.
     - The broader matrix (Ctrl+C during launch, completion races, Kilo, Doom,
       background jobs, debugger inspection) is task 5.
4. [ ] **Kilo passthrough.** A ports patch requests passthrough for the Kilo
   editing session. Kilo's own Ctrl+C handling is unchanged.
5. [ ] **Validation.** QEMU, local and remote:
   - a CPU loop, a blocked reader and a pipeline;
   - typed `terminated` completion;
   - Ctrl+C at the prompt still cancels the line;
   - Ctrl+C during launch, repeated Ctrl+C and typeahead disposal;
   - Ctrl+C as a job finishes, without affecting the next job;
   - Kilo keeps unsaved edits on Ctrl+C;
   - in the Lua REPL, Ctrl+C cancels a typed line but interrupts running code;
   - Doom with the raw keyboard is unaffected;
   - background jobs and other sessions unaffected;
   - debugger inspection of retirement and released waits.

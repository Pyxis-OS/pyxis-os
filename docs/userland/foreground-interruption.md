# Foreground interruption

Ctrl+C interrupts the running foreground command or pipeline, locally and
through the [remote terminal](remote-terminal.md). A direct foreground process
can opt in to a cooperative notification for cleanup on the first press; the
second press forces termination. A process without a receiver is terminated
on the first press. The shell uses its held input and child-process authority,
without Unix signal emulation. At the prompt,
Ctrl+C still cancels the line being edited.

## Pieces

- **Process termination.** Launch gives the parent each child's observer with
  WAIT and TERMINATE. TERMINATE uses the per-task safe stop that execution
  groups use, so blocked operations unwind. It is idempotent, and a result that
  was already committed is kept. See [processes](../interfaces/processes.md).
- **Interrupt arming.** `CONSOLE_RIGHT_INTERRUPT` on a console or terminal
  input authorizes arming. Arming returns a separate armed handle, which
  `wait_many` observes with `WAIT_INTERRUPT`. While armed, Ctrl+C typed as text
  is removed from the input, increments a pending count capped at two and
  discards earlier typeahead. ACK consumes only one press without disarming.
- **Passthrough.** Any reader can request a passthrough handle. While one exists,
  Ctrl+C on that input is ordinary data. Both kinds of handle are counted grants
  of the input object, so closing them, including at process exit, ends the
  state. See [interrupt arming and passthrough](terminal.md#interrupt-arming-and-passthrough).
- **Cooperative receiver.** A process creates one local receiver without a new
  grant. A holder of its process TERMINATE right can request a notification,
  which coalesces until consumed. Closing the last receiver detaches immediately;
  a readiness observer cannot keep it opted in. See
  [the native contract](../interfaces/processes.md#cooperative-process-interruption).
- **Shell policy.** The shell arms before launching a foreground job, waits on
  stage completion and the interrupt together, acknowledges the first press
  before cooperative dispatch, and retains the same armed handle for forceful
  escalation. It disarms after collecting actual results. See
  [interrupting foreground commands](shell.md#interrupting-foreground-commands).
- **Passthrough users.**
  - libterm's line helpers hold passthrough while a line is being edited, so
    the Lua REPL cancels a typed line while its running code stays
    interruptible.
  - vi holds passthrough for its whole session, so Ctrl+C cannot discard
    unsaved edits; Escape followed by `:wq` saves and quits, and `:q!` discards
    changes.
- **Raw keyboard owners.** Programs such as Doom receive key events rather than
  console text, so they are never interrupted; Ctrl stays a game key.

## Authority

Interrupt authority starts on the kernel's initial per-space `input` grant and
on each terminal-create `input`. Session setup passes it only to `session`
successors and to the remote root shell. Every other launch narrows `input` to
READ, and standard input is validated as exactly READ, so commands can neither
arm nor observe the terminal latch. Cooperative process receivers need no
terminal authority and confer no control over another process. TERMINATE, group
stop and session disconnect remain forceful; owned shell scopes do not gain
a cooperative grace period.

## Limits

Accepted limits are recorded in
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1360):

- Only the shell's direct children are terminated.
- Background jobs cannot be interrupted, and there is no job control.
- A program holding passthrough cannot be interrupted.
- A nested interactive shell is ended as a whole.
- Remote Ctrl+C can be held back behind more than 4 KiB of unread typeahead.

Startup scripts also hold the right. When Ctrl+C terminates the last stage of a
script's foreground command, that stage has failed, so the script stops. An
earlier pipeline stage that is terminated after the last stage has already
succeeded does not stop it; the existing last-stage rule governs. No current
startup script runs a foreground command.

Deferred: job control and suspend/resume, a process-control tool, and wider
supervisor/descendant delivery. The native receiver does not imply Go signal
support until the fork adapter lands.

## Validation evidence

QEMU 10.2.2 under nested KVM, four CPUs, 256 MiB, VirtIO NET with loopback TCP
forwarding. Tasks were validated as they landed. The final pass used Pyxis
`f364445`, the task 4 branch head and identical in content to the merged
`af188ee`, with userland `0c4743d` and ports `2f9d55d`, and ran with GDB attached
for the kernel traces.

- **Termination** (temporary shell hook during task 1).
  - Grouped (remote) and ungrouped (local) CPU loops, blocked readers and
    pipelines reported `terminated`.
  - A process that had already exited kept status 5, and a WAIT-only handle was
    denied.
  - GDB showed each stop request reach its task, and every TERMINATED
    completion found the task link cleared.
- **Arming and passthrough** (temporary hook during task 2, local and remote).
  - Arming twice returned BUSY, and arming standard input was DENIED.
  - The latch was level-triggered, and `abc`, Ctrl+C, `de` read back as `de`.
  - Passthrough delivered byte 3 as data.
  - A shell that exited while armed released the grant.
  - After a 4,200-key overflow, Ctrl+C still latched and later input was kept.
- **Shell, local and remote, ordinary image.**
  - A Lua loop, a blocked `cat` and pipelines of up to three stages were
    terminated, with typed `terminated` completions.
  - Repeated presses coalesced, and typeahead such as `lz` + Enter was discarded.
  - Ctrl+C at the prompt cancelled the line.
  - In the Lua REPL, Ctrl+C cancelled a typed line and terminated running code.
- **Ctrl+C during launch.** GDB set the latch at kernel launch entry, after the
  shell armed and before the stages existed. Both stages of `cat | cat` were
  terminated, and no byte reached them.
- **Ctrl+C as a job finishes.** GDB set the latch while the stage's completion
  was being published.
  - `lua -e "print(5)"` kept `exited 0`, and no stop request reached a task,
    since the link was already cleared.
  - The next job armed with a clear latch and exited normally.
- **Kernel trace for an ordinary interrupt.** Arm, latch, stop request,
  TERMINATED completion with the task link cleared, disarm clearing the latch,
  then passthrough for the next line.
- **Other sessions.** Ctrl+C in one remote session left another session's loop
  running until that session's own Ctrl+C.
- **Background jobs.** A background Lua loop printed its result after a
  foreground `cat` was interrupted.
- **Doom.** Ctrl and Ctrl+C acted as game input (ammo dropped from 50 to 47) and
  Doom kept running. After F10/Y, Ctrl+C interrupted the next command.
- **Startup script.** With a temporary `lua` loop added to the Development
  startup script, Ctrl+C reported `boot://init:6: shell: lua: Process
  terminated` and the script stopped with status 1.

Some paths were reviewed in source only:

- discarding a launch preparation;
- a fault racing a terminate;
- terminal hangup reporting `WAIT_ERROR`;
- the remote full-queue limit.

GDB's all-stop mode cannot reproduce true concurrency, so the timing cases were
recreated by setting state at chosen points.

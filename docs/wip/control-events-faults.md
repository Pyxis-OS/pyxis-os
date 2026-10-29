# Control, events and faults

Status: **direction, 2026-10-08.** The owner supplied a long starting point for
signal-like functionality; this records its direction, the review adjustments
and one accepted decision. It is not a subsystem or an implementation milestone,
and it authorizes no code.

## Direction

Pyxis does not adopt POSIX signals for native programs. Signals bundle unrelated
things: termination, user interrupts, hardware faults, timers, child completion,
configuration reloads and application notifications. Pyxis keeps them separate:

- **Control** expresses intent. Asking a program to stop or reload is a
  cooperative message, handled at a point the program chooses. Making a program
  stop is a separate, kernel-enforced operation that needs a control
  capability.
- **Events** report reality: a process exited, a terminal was resized, a
  deadline expired, input closed. Programs wait for events or observe state; the
  kernel does not run callbacks in the middle of their execution. State changes
  (dimensions, battery) keep only the latest value with a generation, while
  commands that each matter queue with explicit overflow rules.
- **Faults** explain failure. A segmentation fault is a process outcome with
  structured details, not a message.

There are no asynchronous handlers interrupting arbitrary code. A program with
an event loop waits on its control channel beside its other I/O; a compute-bound
program checks for pending control at safe points it chooses.

Policy belongs to [Continuum](spaces.md#asterism), not Caelum: how long to wait
for a graceful stop before enforcing it, whether a failed reload becomes a
restart, and what a fault does to a [haven](spaces.md#asterism).

## What Pyxis already has

- Enforced termination of a process or an
  [execution group](../interfaces/execution-groups.md), with immutable, repeatable
  completion.
- Process results that distinguish EXITED, FAULTED and TERMINATED
  ([processes](../interfaces/processes.md)). Fault details such as the faulting
  address are not part of the result yet.
- One-way SEND and request/reply CALL on [endpoints](../interfaces/endpoints.md).
- `wait_many`, combining several kinds of readiness.
- State with change notification: the display's RESIZED with a geometry
  generation, battery state, and
  [foreground interruption](../userland/foreground-interruption.md) for Ctrl+C.

## Owner decision

Accepted 2026-10-08: **Continuum creates each program's control channel and
grants its receiving end at launch,** as a named `control` resource, like the
terminal grants. Programs do not register their own endpoints. A program that
ignores the resource still works, and Continuum always knows whom it can ask.

## Notes

- **A small shared vocabulary is unavoidable.** Continuum must be able to ask any
  program to stop, so "stop, with a deadline" needs one meaning everywhere, and
  "reload" probably does too. Define only what the first haven needs; programs
  define their own messages beyond that.
- **Port compatibility.** The accepted [cooperative libc signal profile](../userland/cooperative-signals.md)
  supplies selected standard names over native events at audited userland
  boundaries. It does not introduce asynchronous native callbacks or fault
  handlers. TERM/HUP mapping still requires the control resource's actual
  vocabulary and lifecycle.
- **Ctrl+C** keeps the current supervisor policy. A port opts into the existing
  singleton process-interrupt receiver through its SIGINT disposition; native
  raw PASSTHROUGH and supervisor escalation retain their existing precedence.

## Accepted first libc slice

Accepted 2026-10-28, implemented and QEMU-qualified 2026-10-29:
cooperative SIGWINCH and opted-in SIGINT,
initial-thread ownership, real masks/dispositions/pending state, safe-point
unwinding and limited restart. Handler installation selects terminal stdout,
then stderr, then stdin unless an optional native override selects another
endpoint. SA_RESTART, SA_RESETHAND, SA_NODEFER and minimal SA_SIGINFO are real;
SA_ONSTACK and child/job-control flags refuse. See the
[contract and limits](../userland/cooperative-signals.md).

The same slice replaces ppoll/pselect's former no-op masks with atomic
mask-change/pending-check/enrollment/restoration semantics. No build enabling
handlers may retain the ignored-mask implementation. SIGPIPE remains EPIPE.

Native programs keep this document's no-signals direction. No compute-loop
interruption, fault recovery, arbitrary-process kill or automatic thread
migration is introduced. TERM/HUP, owned-child CHLD and clock ALRM still need
source-specific consumer/ownership decisions. Continuum's accepted control
resource belongs to that milestone; the compatibility layer does not create a
competing stop/reload channel.

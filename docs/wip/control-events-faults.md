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
- **ISO C's `signal()` and `raise()`.** libc currently exposes honest failing
  shims for both; no handler is installed or delivered. `raise` would be
  synchronous. A ported program's `signal(SIGTERM, handler)` could be
  mapped onto the `control` resource, running the handler at libc's blocking
  calls; decide that when a port needs it.
- **Order.** The first consumer is graceful stop and reload of one haven, which
  needs Continuum. The control channel belongs to the Continuum milestone, not a
  separate track.
- **Ctrl+C** keeps today's foreground interruption. A program could later opt
  into a cooperative first stage through its `control` resource; nothing is
  imposed globally.

## Parked libc compatibility proposal

Cooperative libc signal compatibility is **proposed, not accepted or assigned**
(chronium, 2026-10-27). Revisit it only for a real consumer after the
[raw-termios follow-up](terminal-ui.md#libc-compatibility-follow-up). Possible
source mappings are resize to SIGWINCH, opted-in foreground interruption to
SIGINT, future control-channel stop/reload to SIGTERM/SIGHUP, owned child
completion to SIGCHLD and clock expiry to SIGALRM. SIGPIPE remains an EPIPE
error rather than handler delivery. These possibilities do not settle the
source binding, thread/mask, safe-point, EINTR/restart or lifetime contracts.

The descriptor-readiness contract (chronium, 2026-10-28) accepts both NULL and
non-NULL `ppoll`/`pselect` signal-mask pointers as a documented no-op while no
signals are delivered. Before this parked work enables delivery, those waits
must gain the real atomic mask-change/sleep and restore-on-return semantics in
the same milestone. Keeping the no-op after enabling handlers is not allowed.
This coupling accepts no handler, source mapping or restart policy by itself.

This is compatibility for ports, not a change to the native no-signals design.
No asynchronous handler, compute-loop interruption, fault recovery or arbitrary
process signalling is authorized. Continuum's previously accepted ownership of
the named control resource remains separate from these unaccepted libc mappings.

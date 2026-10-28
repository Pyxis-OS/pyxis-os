# Cooperative libc signals

Accepted 2026-10-28; the first slice is implemented, with
[initial QEMU qualification and outstanding checks](../development/experiments/cooperative-signals.md).
This is an opt-in compatibility layer for ports. Native programs retain
[control, events and faults](../wip/control-events-faults.md), with no
asynchronous kernel callbacks or fault-signal recovery.

## Sources and ownership

SIGWINCH observes the selected terminal's geometry generation. Installing a
handler selects terminal stdout, then stderr, then stdin, using existing
descriptor authority. The selected fd/open-object generation is retained;
close/replacement ends observation rather than following a replacement fd.
A later registration selects afresh. The optional native override is proposed
as `pyxis_signal_set_terminal(fd)`; ordinary ports need no binding call.

Installing a SIGINT handler or SIG_IGN opts into the existing singleton native
process-interrupt receiver. A conflicting native owner returns EBUSY without
changing the prior action. SIG_DFL closes only libc's receiver and restores the
existing supervisor policy. SIGWINCH alone does not opt into interruption.
Raw PASSTHROUGH still makes Ctrl+C byte 3. Supervisor escalation and the
forceful policy of capture/script/helper contexts are unchanged.

SIGPIPE remains EPIPE-only; DFL/IGN can be queried/set, but no PIPE handler is
delivered. TERM/HUP require the future control-channel protocol, CHLD requires
owned-child registry/lifetime decisions, and ALRM awaits its consumer. Their
registrations fail ENOTSUP. Faults, job control, arbitrary-process kill and
realtime queues are outside this profile.

## Delivery, actions and masks

Only the genuine initial thread owns actions, masks, pending events and local
raise. Worker action/mask/raise calls fail ENOTSUP; workers never TAKE the
process receiver. Initial-thread retirement disables the layer and releases
its sources before destructors, without migrating callbacks to a worker.
Caller-owned signal-set value operations remain usable from workers.

Handlers are ordinary C calls at audited outer libc boundaries, after FILE,
cursor, allocator and internal locks, native waits and operation references
are released. Nested libc work defers to its outer admission. A libc-safe
boundary does not make an application's window pointers or locks reentrant.
Handlers must return normally or terminate; longjmp, C++ exception and
thread-exit escapes are unsupported.

`sigaction`, persistent `signal`, set operations, `sigprocmask`, `sigpending`
and `sigsuspend` have real state. Blocked events coalesce; source interests are
omitted/disarmed after latching a pending event, rather than repeatedly waking
on the same level. Ignored events are discarded. Unmasking dispatches eligible
pending events at a clean boundary. Handler errno is restored on return.

SA_RESTART, SA_RESETHAND, SA_NODEFER and SA_SIGINFO are supported, including
combinations. RESETHAND resets the stored action before invocation. NODEFER
omits automatic self-blocking but not an explicit self-bit in sa_mask; eligible
nested delivery happens at clean cooperative checkpoints, not instruction
boundaries. SIGINFO supplies si_signo and SI_USER for raise or SI_KERNEL for
native notifications; other fields are zero and context is NULL. SA_ONSTACK
and child/job-control flags refuse. No flag succeeds while its behavior is
silently ignored.

`ppoll`/`pselect` install a real temporary mask around pending checks, source
enrollment and wait/dispatch, and restore the caller's mask on every return.
NULL keeps the current mask; worker non-NULL masks fail ENOTSUP. A caught event
during the wait returns EINTR even with SA_RESTART, preserving staged fd
results. Delivery enabled only by restoring the old mask preserves an already
selected I/O/timeout result. `sigsuspend` restores its mask and returns EINTR
after a caught handler. Neither interface uses the former no-op masks.

## I/O and restart limits

Known-uncommitted console/byte-PIPE reads may restart under SA_RESTART, using
the same open-object identity and original MIN/TIME deadline. Replacement fails
rather than reading a reused fd. Writes do not automatically restart. Positive
progress wins over EINTR, and no accepted prefix or uncertain mutation is
replayed. O_NONBLOCK keeps its actual TRY result and does not acquire a blocking
wait or change the shared timing/raw state.

Audited stdio dispatches after outer cleanup; interruption is ferror/EINTR,
not feof. Completed elements, consumed input and staged output remain real.
An unaudited nested formatting/scanning path defers callbacks until its safe
outer boundary; there is no generic callback in stream cleanup or syscall
return. Nanosleep reports actual remainder and does not automatically restart.

Synchronous positioned FILE/provider calls, including FILE values carried by
PIPE, can delay delivery until their actual return. Mutex, condition, once,
address waits and joins retain their no-EINTR protocols. A compute-only loop
need never reach a checkpoint. Forced native stopping remains available.
The consequence and revisit point are tracked in
[debt #1556](https://git.internal/PyxisOS/pyxis-os/issues/1556).

## Process boundaries and rebuilding

`raise` targets the local initial-thread state for WINCH/INT; DFL local INT
uses ordinary `_Exit(130)`, not a fabricated native signal-termination result.
There is no PID/group kill or pthread_kill interface. Fresh native images start
with default actions, empty masks/pending state and no terminal binding or
receiver. Handler addresses and masks are not serialized into the environment.
Shebang children retain their existing held-source/grant path; an in-process
sourced script shares its process state.

Existing resize, interruption and readiness operations supply the native
mechanisms; this slice identifies no new kernel ABI. Rebuild the SDK/libc and
relink static consumers and affected ports with the real mask semantics.
No compiler-container rebuild or native fault/termination policy change is
required.

# Readiness sets

A readiness set retains observations of held native handles and returns only
ready registrations. It supplies no I/O authority. The ABI is
[readiness_set.h](../../include/abi/readiness_set.h); transient
[wait_many](../../include/abi/wait.h) still accepts at most 32 interests.

## Creation and ownership

`SYSCALL_READINESS_SET_CREATE` takes the reply address and capacity in RDI/RSI.
It needs no creation grant. The returned handle has CONTROL and WAIT:

| Right | Operations |
| --- | --- |
| CONTROL | REGISTER, UPDATE, REMOVE, WAKE and INFO. |
| WAIT | WAIT, and READABLE/CLOSED observation through wait_many. |

Every operation checks the creating process. A copied or forwarded handle in
another process is unusable there. Closing the last CONTROL grant closes the
set and wakes collectors; WAIT-only copies can observe closure. Owner stop or
exit closes all its sets. Source detachment drains in bounded BSP-worker steps,
with execution-group cleanup held until that drain finishes. Closed handle
copies may retain inert set storage, charged until actual reclamation.

## Registration

REGISTER captures target storage, event authority and an independent pending
forwarder lineage. It does not copy a logical source grant. Closing the last
pipe or TCP grant therefore still produces normal endpoint shutdown while an
observer exists. Retarget does not move a registration; CUT queues one terminal
DENIED result and independently schedules source detachment, even without WAIT.

Sources are native pipes, console and terminal input/output, TCP streams/listeners,
and [process interrupt receivers](processes.md#cooperative-process-interruption).
Their event, rights, transport and owner checks are the same
as wait_many. Unsupported sources, exported protocols and sets themselves are
refused. Ordinary half-close and buffered EOF remain level conditions, preserving
surviving directions. Whole-source errors release observation storage after the
worker joins; the charged terminal registration remains until REMOVE. Console
input overflow and keyboard unavailability are recoverable level WAIT_ERROR
conditions with OK status. They leave the registration/source attached, are
suppressed while disarmed, and wake a consumer to retry I/O; a read may report
INPUT_LOST once before subsequent input resumes.

Pipes and console/terminal application input/output also accept directional
`WAIT_READ_STATUS`/`WAIT_WRITE_STATUS` under their existing READ/WRITE rights.
These interests report only existing `WAIT_PEER_FIN`, `WAIT_WRITE_CLOSED` and
`WAIT_ERROR` conditions, without ordinary readable/writable readiness or the
status interest bits themselves. Framebuffer read status observes input loss
and keyboard unavailability; write status observes unavailable output. They
consume no input, acknowledge no loss and reserve no I/O. An active status-only
interest can therefore wait for closure/error while ordinary data/capacity is
ignored. Setting the active mask to zero retains the existing disarmed behavior.
Other source families do not accept these interests.

The caller specifies nonzero allowed events, an active subset (possibly zero),
an observed geometry generation where applicable, and an opaque tag. REGISTER
returns a non-reused set-local identity and generation. UPDATE cannot expand
the allowed mask; it changes the active mask/observed generation, returns a new
generation and performs a fresh level check. Stale keys return NOT_FOUND;
identity/generation exhaustion returns LIMIT instead of wrapping. Each copied
source can have an independent registration.

Admission reserves storage and attaches hooks before its first level check.
Reply publication uses a short current-process mapping lease; failed admission
unwinds its provisional record before returning. REMOVE excludes producers,
joins source/lineage cleanup and removes queued records before acknowledging.
Controls blocked by a borrowed publication wait in a separate queue; FINISH or
closure resumes them, without retrying them continuously on the BSP.
Concurrent removal waits for any bounded in-flight publication to finish. A
previously returned record remains a stale observation; consumers must check its
registration generation before reuse or I/O.

## Waiting

Readiness is level-triggered and reserves no I/O. TRY I/O may still return
WOULD_BLOCK. WAIT rechecks candidate state, returns each registration at most
once per batch, and rotates persistent ready entries fairly. Results contain
identity, generation, tag, ready bits and native status, never target handles.
Terminal errors are delivered once with non-OK status and zero event bits, even
while ordinary conditions are disarmed. CUT is rechecked at final reply publication.

One collector may wait per set; competing WAIT returns BUSY. Controls can run
while it waits. The caller provides result storage and a capacity; at most 64
records are returned in one reply. This bounds scratch/copy work, not the number
of registrations. Stale candidates are examined in resumable worker steps
before ending their pass. Failed output validation consumes neither terminal
notifications nor a pending explicit wake.

Deadline zero polls. Finite deadlines are absolute monotonic times, at most
30 seconds ahead, as for wait_many. Readiness wins over expiry; otherwise an
expired finite wait returns TIMED_OUT. Reissue longer waits against the
application's original deadline. WAKE coalesces and returns the WOKEN flag even
with no source events; a wake racing reply publication remains pending for the
next wait. No mapping lease or private user pointer is borrowed across sleep.

## Storage and worker ownership

All sets of a process share a 16 MiB readiness-metadata budget. INFO reports
its ceiling, charged bytes and this set's registration count. Set/registration
headers, embedded lineage and queue nodes, indices and peak index growth are
charged before publication. Allocation can return NO_MEMORY independently of
budget LIMIT. Existing registrations survive refusal. Target backing and global
allocator overhead remain subject to their own limits; this budget is not a
physical-memory reservation or a bound on retained target resources.

Source-local watcher links and dirty queues replace persistent-path global
rescans. Producers mark a source dirty without allocation or a watcher walk.
Workers evaluate and fan out in bounded, preemptible steps; hot sources rotate
rather than monopolizing the queue. Hash indices grow geometrically and rehash
incrementally. Earliest collector deadlines are ordered per worker; insertion
walks yield, while the sleep decision reads only the first deadline. These are work-unit bounds,
not a per-process worker-time quota: the 16 MiB account bounds retained metadata,
not BSP time or a maximum interrupt/launch latency.

TCP queries, attach/detach and source shutdown use the network worker. Other
source locks stay authoritative. Metadata allocation/free remains BSP-owned;
source callbacks and AP publication use short queue guards. Forwarder CUT may
nest the readiness guard, never the reverse. No actor query, allocation, source
release or sleep occurs under that guard. An account cleanup token attributes
asynchronous drain to the creator's group.

Owned native endpoint receivers also support READABLE/CLOSED through their
existing owner-local RECEIVE authority. Observation retains storage, not a
logical receive grant; final receiver close cancels owned work and remains
observable, and REMOVE joins producer detachment.

The source set does not include regular FILEs, UDP, graphics or arbitrary
providers. Those retain their existing interfaces; future source hooks do not
change the set's ownership or budget contract. The existing TCP connection pool
is a separate limit and is not enlarged by this interface.

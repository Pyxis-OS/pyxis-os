# Endpoints

The endpoint service accepts `CREATE` from a process holding an explicit
`ENDPOINT_SERVICE_RIGHT_CREATE` grant. Creation allocates sixteen delivery
records and returns two handles: a receiver owned by the creating process and
a callable handle that it may grant to clients. Creation supplies no namespace,
file, network or other resource authority. The receiver and delivery receipts
cannot be copied, attached to a message or inherited by a child. The callable
handle can be copied with equal or reduced resource and transport authority. There is no global endpoint
name or implicit discovery.

Creation and export use typed requests on the common
[BSP executor](../kernel/smp.md#scheduling-and-ownership) FIFO. Creation claims
both result slots before publication; export claims one and carries an owned
receiver snapshot plus copied descriptor. The sole caller keeps process/owner-list
storage alive until completion, without exclusive table ownership. An early
completion records notification without enqueueing a still-running caller.
Neither operation changes private mappings or needs a VM handoff.

The BSP allocates backing with IF=0 and no queue lock held. Validated owned
grants move atomically into the claims. Export rechecks closure, duplicate ID and
export limit before publication; failure releases backing and the claim. No
nested executor wait occurs. Completion clears inputs/claims before waking the
caller and makes no further request access afterward.

The [ABI](../../include/abi/endpoint.h) defines three distinct protocols for
client delivery, receiving and replying, plus the service creation/export protocol. The
[userspace helpers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/endpoint.h) wrap the native CALL
and CLOSE syscalls:

| Helper | Handle | Behavior |
| --- | --- | --- |
| `endpoint_create()` | Service grant | Create a receiver and callable handle |
| `endpoint_export()` | Service and owned receiver | Create an exported client with an ID, protocol and authority ceiling |
| `endpoint_withdraw()` | Owned receiver | Stop an export and cancel its pending work |
| `endpoint_retire_ack()` | Owned receiver | Release a retired export ID after its notification |
| `endpoint_invoke()` / `endpoint_notify()` | Exported client | CALL / SEND a provider protocol operation |
| `endpoint_request()` | Callable handle | Admit a request with an optional deadline, then wait for its result or transport failure |
| `endpoint_send()` | Client handle | Admit a one-way message and return without waiting for completion |
| `endpoint_receive()` | Receiver handle | Take a cancellation, retirement notice or oldest queued message |
| `endpoint_reply()` | CALL receipt handle | Complete that request and consume the receipt |
| `endpoint_finish()` | Receipt handle | Finish a SEND or canceled CALL, or abandon an unanswered CALL |

Client SEND requires `HANDLE_TRANSPORT_SEND`; CALL requires both SEND and
`HANDLE_TRANSPORT_RECEIVE`. Client RECEIVE authorizes only that call's response,
with no standalone operation or access to the incoming queue. The separate
process-owned receiver uses RECEIVE for incoming work. Creation gives the client
both transport bits and zero resource rights. The receiver has RECEIVE transport
and `ENDPOINT_RECEIVER_RIGHT_CONTROL` resource authority for export management.
Copies and transfers attenuate each mask independently. Native objects have zero
transport authority and retain their protocol-specific resource rights.

Each request, send or reply carries at most 4,096 application bytes and four explicit
capability attachments. The kernel copies only the stated payload length into
endpoint-owned storage. The bytes are opaque; an embedded pointer or numeric
handle cannot grant authority in another process. An attachment selects a
sender handle, resource rights and transport authority, each no greater than its
existing grant. The sender keeps
its original handle, while the recipient gets a new local handle and reference.
The kernel retains attachments while queued or awaiting collection, so closing
the sender's handle does not invalidate an admitted transfer. Invalid source
handles and rights increases fail before admission or reply completion. On
RECEIVE, an unavailable destination table leaves the request queued. Recipient
handles are installed all at once or not at all.

Sixteen delivery records bound queued messages, received unfinished messages,
and completed calls awaiting collection. Calls and sends share the same FIFO
and capacity. A full endpoint returns `CALL_QUEUE_FULL` without delivering the
new message or waiting for capacity. Receiving does not release its record:
a provider can receive further work and complete it in a different order.

RECEIVE supplies a kernel-authenticated `ENDPOINT_MESSAGE_CALL` or
`ENDPOINT_MESSAGE_SEND` kind. A CALL receipt grants one reply; successful REPLY
consumes it. A SEND receipt has no operation rights: REPLY returns `CALL_DENIED`
and leaves it live. `endpoint_finish()` wraps CLOSE, finishing a SEND or waking
an unanswered caller with `CALL_ABANDONED`. Separately received attachment
handles remain owned by the provider. A CALL record becomes reusable once the
caller has collected its outcome and the final receipt reference is released;
a SEND record becomes reusable when its receipt is finished. Final receipt
release performs this logical cleanup synchronously under the endpoint lock,
without allocation. Completed work does not wait for BSP reclamation before
returning its delivery slot. The endpoint backing allocation is destroyed
separately on the BSP after its endpoint, export and receipt owners are gone.

Successful SEND means admission only, with no result or later completion report.
The sender can close its original handles and exit before RECEIVE. The admitted
message owns copied bytes and attachment references, and its receipt retains
endpoint storage without a sender task or waiter. Provider closure discards
queued sends and releases their retained references; process teardown also
releases delivered receipts.

CALL returns the delivery state separately from the syscall status. A failed
call reports whether the request reached the receiver; delivery does not prove
the provider performed an operation. A successful reply also contains a separate,
opaque application result. A client should not retry a delivered mutation solely
because its transport result is uncertain. Closing the receiver immediately
shuts down its endpoint and wakes affected callers with `CALL_ENDPOINT_CLOSED`.
Process exit has the same effect on every receiver it owns, even while clients
still hold callable handles. Final object reclamation is deferred to the BSP.

## Receiver readiness

`wait_many` accepts an owned receiver with READABLE and/or CLOSED, requiring
`HANDLE_TRANSPORT_RECEIVE`; CONTROL remains export-management authority. Client
handles and receipts are not receiver interests. The existing 32-interest bound,
zero-deadline poll and absolute deadline apply. Receivers can share a wait with
console, pipe and TCP interests.

READABLE is level-triggered while an ordinary message, cancellation or retirement
notice is queued. RECEIVE preserves its cancellation/retirement/message priority.
Receiver shutdown reports CLOSED automatically with READABLE, suppresses READABLE
and adds no ERROR. Export withdrawal or final-client closure does not close the
receiver; retirement remains a receivable notice.

Readiness consumes and reserves nothing. A queued CALL can expire between the
scan and RECEIVE; RECEIVE retains its existing blocking semantics. Registration
precedes the first locked scan and is removed before releasing the retained
receiver. Producers notify the existing readiness workers after unlocking, only
on a transition to readable or closed while interests are registered. An earlier
transition is observed by the initial scan; a later transition notifies it, with
the existing remembered notification covering wake-before-park. Empty receivers
without interests cause no readiness scan or notification on the IPC path.

## Deadlines and cancellation

CALL accepts an absolute monotonic deadline in nanoseconds; zero means unlimited.
The deadline uses the same epoch as CLOCK NOW and is supplied to the provider
in `deadline_ns`. It is never restarted after admission, receipt delivery or
capability-table growth. SEND and REPLY descriptors must set their deadline to
zero. SEND still reports only admission.

An expired deadline returns `CALL_TIMED_OUT`. Before delivery, expiry removes
the queued request and releases its retained attachments and unpublished receipt,
with `ENDPOINT_NOT_DELIVERED`; the record remains until the caller collects its
outcome. After delivery, it returns `ENDPOINT_DELIVERED`:
the provider may have performed the operation, so this is no basis for automatic
retry. Expiry invalidates reply authority without revoking attachment handles
already delivered to the provider. The canceled receipt retains its delivery
slot until finished or released by provider teardown.

Reply, expiry, abandonment and closure serialize under the endpoint lock.
REPLY checks the deadline before committing; a reply committed before expiry
wins even if the caller collects it later. A valid late REPLY on an open endpoint
returns `CALL_TIMED_OUT` and leaves the receipt available to finish. Malformed
replies retain their validation errors; a closed endpoint reports closure.
Deadline wakeups use the existing
scheduler timed waits and can be late; nanosecond units do not promise exact
scheduling latency.

RECEIVE prioritizes `ENDPOINT_MESSAGE_CANCEL`, then `ENDPOINT_MESSAGE_RETIRE`
notifications over normal FIFO messages. A cancellation notice's `receipt` names
the provider's existing receipt, with no new handle or authority. It has the
original deadline, delivered state, authenticated export metadata and a `reason`
of `CALL_TIMED_OUT` or `CALL_ENDPOINT_CLOSED`, with
zero result, payload and attachments. Pending state lives in the delivery
record, so a full message queue cannot prevent notification. Receiving the
notice clears that pending notification but does not finish the receipt;
`endpoint_finish()` clears any pending notice and closes the receipt. No
notification is needed for expiry before delivery.

The caller claims four exact capability slots before admission so a completed
reply can move its owned attachments without later allocation or retain. Claims
live with caller activity in the delivery record and drain on collection/stop.
REPLY validates destination group policy before committing completion; rejection
leaves the receipt for correction. Successful REPLY has no subsequent capacity
failure. Provider side effects are never implicitly rolled back.

RECEIVE claims one slot for the receipt and one per request attachment before
dequeueing. If its table is short, ordinary BSP growth runs on the common FIFO
outside the endpoint lock, then the receiver reselects/rechecks the live queue.
The invoking handle keeps endpoint storage alive across the wait; no pointer
into replaceable table storage is retained. Growth failure leaves the delivery
queued for a later receive attempt. Endpoint state and wait records live in stable
kernel storage; the endpoint lock may nest the scheduler or short table guard,
but no lock spans user copying,
allocation or parking. See [scheduling ownership](../kernel/smp.md#scheduling-and-ownership).

## Exports and retirement

An endpoint supports up to 64 live or unacknowledged exports, independently of
its sixteen delivery slots. EXPORT requires service CREATE authority and the
process's own receiver with CONTROL authority. It fixes a nonzero object ID,
nonzero protocol, resource-rights ceiling and transport ceiling. IDs are unique
within that receiver until retirement acknowledgment. The receiver owns control;
it is not a client reference.

Exported invocation uses the endpoint CALL/SEND envelope with an inner protocol
and operation. Raw endpoint traffic and replies set both to zero. A mismatched
export protocol returns `CALL_BAD_OPERATION` before admission. RECEIVE supplies
the immutable export ID/protocol, requested operation and the invoking grant's
actual resource rights. The provider checks operation-specific resource rights;
the kernel enforces transport authority without interpreting provider protocols.
Libpyxis FILE helpers select this exported route using the authenticated handle
interface/kind; native FILE calls retain their existing syscall dispatch. See
[userspace file providers](file-providers.md).

WITHDRAW denies new operations with `CALL_ENDPOINT_CLOSED`, removes queued work
and wakes callers with their delivery state. Delivered work retains its receipt
and attachments; CANCEL reports closure, including for SEND with deadline zero.
An already-expired CALL retains its timeout result. Providers must finish retained
receipts before reclaiming state. Withdrawal affects one export, not its siblings.

RETIRE becomes pending after accepted operations drain and either the last client
reference disappears or the export is withdrawn. Client references include local
copies, launch grants and retained attachments. Keeping the original exported
client open prevents natural retirement; provider control does not. RETIRE has no
receipt, payload or attachments and consumes no delivery slot. It supplies the
export ID and protocol; explicit ACK releases the ID. A withdrawn client can
outlive ACK, but remains attached to an invalid old object even if its ID is
reused. Receiver closure or provider exit invalidates every export and discards
control records without waiting for clients or ACKs. Remaining references keep
only safe backing storage until their final BSP release.

## Examples

The exported counter example runs with `session boot://counter.pxe`. Two objects
share a receiver and demonstrate authenticated identity/rights, COPY, launch and
attachment attenuation, protocol rejection, provider-side denial, SEND and
acknowledged retirement. `--withdraw` cancels a delivered call, finishes its
receipt, acknowledges retirement and reuses the ID while a stale grant remains
closed to invocation. `--exit` demonstrates provider teardown with retained
client grants and accepted work. `--queued-withdraw` parks the provider before
withdrawal so a CALL can queue; debugger inspection can confirm admission.
`--retire-full` withdraws an idle export and receives its retirement notice while
all sixteen normal slots remain occupied.

The packaged example runs from the interactive shell with
`session boot://server.pxe`. The server creates an endpoint, launches two clients
with callable grants, receives both requests and replies in reverse order.
`session boot://server.pxe --wide` exercises 4 KiB payloads and four file grants
in both directions. `session boot://server.pxe --abandon` closes one receipt so
its client observes abandonment. `--saturate` retains sixteen received calls,
observes a seventeenth caller's queue-full result, then completes the retained
calls in reverse order. `--close` shows receiver closure after and before
request delivery; `--exit` leaves a received call to process teardown. The shell
delegates launch authority through session handoff. Endpoint creation is also
delegated to providers started through the [publication commands](namespaces.md).
The session handoff exits the shell; boot another session to run another mode.

`--send` grants a client SEND-only authority. It sends 4 KiB and four file grants,
closes its sources and exits before the provider receives the message. The
provider closes its last client handle, receives the message, finishes the
receipt and reads the retained file grants. `--mixed` holds one CALL receipt
while a SEND-only client admits fifteen sends and receives queue-full on its
sixteenth. After that sender exits, the provider receives and finishes the sends
in order, then replies to the held call.

Deadline modes use an explicitly delegated clock grant:

| Mode | Behavior |
| --- | --- |
| `--expired` | Reject an already-expired call without queueing it |
| `--queued-timeout` | Let an admitted call expire before RECEIVE; subsequent work remains receivable |
| `--delivered-timeout` | Block RECEIVE until a cancellation notice, reject a late reply, then finish the receipt |
| `--cancel-full` | Keep one delivered call plus fifteen queued sends; obtain cancellation despite full capacity |
| `--cancel-finish` | Finish a canceled receipt before reading its notice; subsequent RECEIVE returns ordinary work |
| `--deadline-reply` | Reply successfully before a finite deadline |

The timeout modes also inspect the caller's delivery state and the original
deadline. Received file attachments remain usable after cancellation and receipt
completion. The existing `--abandon`, `--close` and `--exit` modes cover receipt
abandonment and provider teardown.

Add `--wait` to a server or standalone counter mode to wait on its receiver and
console input together. Type a key at the initial prompt to start clients;
`server --wait --delivered-timeout` accepts further input while awaiting CANCEL.
`counter --wait --retire-full` observes RETIRE ahead of sixteen queued SENDs.
The option needs input and clock grants; modes without it keep blocking RECEIVE.
See [qualification](../development/experiments/endpoint-readiness/README.md).

[Namespace publication](namespaces.md) provides explicit discovery and handoff.
There is no external cancellation API or automatic restart.
A provider can retain all sixteen slots by leaving delivered receipts unfinished; deadlines release callers, not provider
work. Calls without a deadline can still wait indefinitely, including self-calls
and cycles between blocked single-task processes. External process termination
and Ctrl-C remain [separate technical debt](../technical-debt.md#process-termination-and-ctrl-c).

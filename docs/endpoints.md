# Endpoints

The endpoint service accepts `CREATE` from a process holding an explicit
`ENDPOINT_SERVICE_RIGHT_CREATE` grant. Creation allocates sixteen delivery
records and returns two handles: a receiver owned by the creating process and
a callable handle that it may grant to clients. Creation supplies no namespace,
file, network or other resource authority. The receiver and delivery receipts
cannot be copied, attached to a message or inherited by a child. The callable
handle can be copied with equal or reduced rights. There is no global endpoint
name or implicit discovery.

The [ABI](../include/abi/endpoint.h) defines three distinct protocols for
client delivery, receiving and replying, plus the creation protocol. The
[userspace helpers](https://git.internal/chronium/pyxis-userland/src/branch/main/include/endpoint.h) wrap the native CALL
and CLOSE syscalls:

| Helper | Handle | Behavior |
| --- | --- | --- |
| `endpoint_create()` | Service grant | Create a receiver and callable handle |
| `endpoint_request()` | Callable handle | Admit a request, then wait for its result or transport failure |
| `endpoint_send()` | Client handle | Admit a one-way message and return without waiting for completion |
| `endpoint_receive()` | Receiver handle | Take the oldest queued message, its kind and an owned receipt |
| `endpoint_reply()` | CALL receipt handle | Complete that request and consume the receipt |
| `endpoint_finish()` | Receipt handle | Close a SEND receipt, or abandon an unanswered CALL |

Client SEND requires `ENDPOINT_RIGHT_SEND`; CALL requires both SEND and
`ENDPOINT_RIGHT_RECEIVE`. Client RECEIVE authorizes only that call's response,
with no standalone operation or access to the incoming queue. The separate
process-owned receiver uses RECEIVE for incoming work. Creation gives the client
both rights; copies and transfers can attenuate them.

Each request, send or reply carries at most 4,096 application bytes and four explicit
capability attachments. The kernel copies only the stated payload length into
endpoint-owned storage. The bytes are opaque; an embedded pointer or numeric
handle cannot grant authority in another process. An attachment selects a
sender handle and rights no greater than its existing grant. The sender keeps
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
handles remain owned by the provider. A completed record may stay occupied
briefly after both sides finish, until the BSP reaps the retired receipt object.

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

The caller reserves four capability slots before admission so a completed reply
can transfer its attachments without later table growth. RECEIVE needs one
slot for the receipt and one per request attachment. If its table is short, the
task lends that table to the BSP for growth, outside the endpoint lock, then
rechecks the queued delivery. Growth failure leaves it queued for a later
receive attempt. Endpoint state and wait records live in stable kernel storage;
the endpoint lock may nest the scheduler lock, but no lock spans user copying,
allocation or parking. See [scheduling ownership](smp.md#scheduling-and-ownership).

The packaged example runs from the interactive shell with
`session app://server.pxe`. The server creates an endpoint, launches two clients
with callable grants, receives both requests and replies in reverse order.
`session app://server.pxe --wide` exercises 4 KiB payloads and four file grants
in both directions. `session app://server.pxe --abandon` closes one receipt so
its client observes abandonment. `--saturate` retains sixteen received calls,
observes a seventeenth caller's queue-full result, then completes the retained
calls in reverse order. `--close` shows receiver closure after and before
request delivery; `--exit` leaves a received call to process teardown. The shell
delegates service creation and launch authority only to the session command.
The session handoff exits the shell; boot another session to run another mode.

`--send` grants a client SEND-only authority. It sends 4 KiB and four file grants,
closes its sources and exits before the provider receives the message. The
provider closes its last client handle, receives the message, finishes the
receipt and reads the retained file grants. `--mixed` holds one CALL receipt
while a SEND-only client admits fifteen sends and receives queue-full on its
sixteenth. After that sender exits, the provider receives and finishes the sends
in order, then replies to the held call.

This slice has no deadlines, cancellation, exports, namespace
publication, wait sets or automatic restart. A live provider that does not
reply can block a caller indefinitely. Self-calls and cycles between blocked
single-task processes can deadlock.

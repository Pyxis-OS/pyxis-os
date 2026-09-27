# Request/reply endpoints

The endpoint service accepts `CREATE` from a process holding an explicit
`ENDPOINT_SERVICE_RIGHT_CREATE` grant. Creation allocates sixteen request/reply
records and returns two handles: a receiver owned by the creating process and
a callable handle that it may grant to clients. Creation supplies no namespace,
file, network or other resource authority. The receiver and delivery receipts
cannot be copied, attached to a message or inherited by a child. The callable
handle can be copied with equal or reduced rights. There is no global endpoint
name or implicit discovery.

The [ABI](../include/abi/endpoint.h) defines three distinct protocols for
calling, receiving and replying, plus the creation protocol. The
[userspace helpers](https://git.internal/chronium/pyxis-userland/src/branch/main/include/endpoint.h) wrap the native CALL
syscall:

| Helper | Handle | Behavior |
| --- | --- | --- |
| `endpoint_create()` | Service grant | Create a receiver and callable handle |
| `endpoint_request()` | Callable handle | Admit a request, then wait for its result or transport failure |
| `endpoint_receive()` | Receiver handle | Take the oldest queued request and an owned receipt |
| `endpoint_reply()` | Receipt handle | Complete that request and consume the receipt |

Each request or reply carries at most 4,096 application bytes and four explicit
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

Sixteen delivery records bound queued requests, received requests awaiting
reply, and completed calls awaiting collection. A full endpoint returns
`CALL_QUEUE_FULL` without delivering the new request. Receiving a request does
not release its record: a provider can receive further calls and reply in a
different order. Each receipt identifies one delivery and grants one reply.
Successful `REPLY` consumes it. Closing an unanswered receipt wakes its caller
with `CALL_ABANDONED`; separately received attachment handles remain owned by
the provider. A completed record may stay occupied briefly after both sides
finish, until the BSP reaps the retired receipt object.

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

This slice has no one-way send, deadlines, cancellation, exports, namespace
publication, wait sets or automatic restart. A live provider that does not
reply can block a caller indefinitely. Self-calls and cycles between blocked
single-task processes can deadlock.

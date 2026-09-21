# Request/reply endpoints

An endpoint pair connects two sides. Each side is a reference-counted kernel
object with its own handles and rights; both sides support the same operations.
The boot launcher gives the client CALL rights on one side and the server
RECEIVE | REPLY on the other. Each process also gets its own space's console.
No global endpoint name or implicit authority is involved.

The launcher uses `capability_grant()` to copy an existing grant with reduced
rights before either process runs. The source grant remains valid until closed;
closing it does not revoke the destination. Grants currently require exclusive
BSP ownership of both tables. There is no userspace grant or move operation.

## Messages

The [shared endpoint header](../include/abi/endpoint.h) defines the tagged
messages, packet layout and rights. The [userspace helpers](../userspace/include/endpoint.h)
wrap the existing native CALL syscall:

| Helper | Required right | Result |
| --- | --- | --- |
| `endpoint_request()` | CALL | Block until a reply or peer closure |
| `endpoint_receive()` | RECEIVE | Receive the pending request, or block for one |
| `endpoint_reply()` | REPLY | Complete the delivered request identified by its ID |

Application data is opaque to the kernel and limited to 64 inline bytes per
request or response. It may contain an application-defined tag and structure,
as in [the number service](../userspace/include/number_service.h). Embedded
pointers and handle numbers grant no authority in the receiving process; this
transport does not copy pointed-to data or transfer capabilities.

There is one outstanding request per direction, including the time after
RECEIVE until the caller consumes its reply or closure result. Another caller
gets `CALL_QUEUE_FULL`. A second receive before replying, or while another
receiver is waiting, gets `CALL_BUSY`.
REPLY wakes the caller; the caller copies the result and releases the slot.
The server may immediately receive again while that completion is pending.
CALL and RECEIVE return a packet; REPLY returns no bytes. Errors leave user reply storage
untouched and report zero reply bytes.

The kernel assigns each request a nonzero ID local to its receiving endpoint.
Only a reply to that endpoint's currently delivered ID succeeds. A missing,
stale or undelivered ID is `CALL_BAD_REQUEST`. IDs are never reused: exhausting
the counter makes new calls unavailable. IDs correlate replies; they are not
handles or separate authority.

## Blocking and lifetime

The kernel captures request bytes and validates reply storage before publishing
or consuming a request. Request and reply storage belong to the endpoint pair;
wait records belong to task metadata. Both use heap mappings that remain backed
until shutdown. No other CPU touches a blocked task's kernel stack: its mapping
can later be freed and reused without a remote TLB shootdown. No allocation
occurs in these operations. The process has one task and stable private
mappings; its handle keeps the endpoint alive throughout the operation.
After resuming, the task writes the reply under its own address-space root.

The endpoint lock protects pending requests and receivers. It may nest the
scheduler queue lock, never the reverse. A waker detaches every published
wait pointer before waking its owner and does not use that pointer afterward.
The scheduler remembers wakes that arrive before parking, and only requeues
a task once its saved stack is safe to resume. No lock spans a context switch.
See [scheduling ownership](smp.md#scheduling-and-ownership) for the CPU handoff.

Closing a handle removes that reference immediately. The side closes when its
last reference reaches the existing BSP object-retirement path. Other grants or
kernel references keep it open. Destruction wakes the opposite side's blocked
caller or receiver with `CALL_ENDPOINT_CLOSED`; later operations against the
closed peer return that result too. Process exit or an ordinary user fault
releases remaining handles through the same cleanup path. Closure and remote
wakeup can wait for BSP reaping and the target CPU's next timer tick. The shared
pair allocation is freed after both sides have been destroyed.

There are no deadlines, cancellation, external task termination, wait sets or
asynchronous sends yet. A live service that never replies can leave its caller
blocked indefinitely. A process calling its own service, or two processes
calling each other simultaneously without a receiver, can deadlock. These
limits are explicit; endpoint closure handles a disappearing peer, not a peer
that remains alive but stops making progress.

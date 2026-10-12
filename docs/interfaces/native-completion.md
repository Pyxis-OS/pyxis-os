# Native operation completions

[ABI](../../include/abi/completion.h), [libpyxis helpers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/completion.h).
This bounded companion to readiness copies a native operation into kernel-owned
storage and returns admission status. HTTP/DNS/TLS remain userspace work.

## Authority and admission

Invoke `PROTOCOL_COMPLETION / COMPLETION_SUBMIT` on the existing native target
with its operation rights. The descriptor selects the owner's endpoint receiver
with RECEIVE transport, a nonzero increasing 64-bit correlation ID, native
protocol/operation, absolute monotonic deadline and a copied payload. The protocol
tag supplies no authority. Receivers are not transferable or inherited. Copies
of target grants retain their existing authority; closing the original after
admission does not cancel captured work.

**One unacknowledged native completion per process. A second submission returns
`CALL_BUSY` (9), without waiting or admission, even with another receiver.** The HTTP
provider consequently progresses one async operation at a time. Admission remains
held through receipt acknowledgement, or uncollected work's drain after receiver
closure. See [the limit and revisit point](../technical-debt.md#native-completion-per-process-admission).
Ordinary IPC and TCP TRY remain independent; their own per-object limits still apply.

Before accepting, reserve operation/input/result storage, one of the receiver's
sixteen delivery records, exact receipt/returned-grant table slots, native worker
admission and cleanup ownership. `CALL_QUEUE_FULL`, `CALL_BUSY`, `CALL_LIMIT` and
`CALL_NO_MEMORY` refuse without submitting I/O or producing a completion.
Admission may require a short BSP allocation handoff, never the submitted I/O wait.
No result buffer is retained or needed on SUBMIT. Successful return means admission
only. IDs are consumed on acceptance; earlier or zero IDs are `CALL_BAD_REQUEST`,
and exhausting the receiver's ID range returns `CALL_LIMIT`.

## First operation profile

Submission metadata is separate from the bounded 4,096-byte payload. All integers
and addresses use the existing native ABI conventions. There are no borrowed
user buffers, pointers in delivered data or per-operation capability handles.

| Native target / operation | Copied payload and result |
| --- | --- |
| TCP service CONNECT | `completion_connect_request`; result `tcp_connect_reply` with zero handle plus attached TCP_RIGHTS grant |
| UDP SEND | `completion_udp_send_request` followed by exact datagram bytes; native status, successful accepted length as known progress |
| UDP RECEIVE | `completion_udp_receive_request`; native peer/count reply followed by datagram |
| FILE READ / WRITE / RESIZE / SIZE | Existing address-free native request/reply layouts, including READ's count prefix |
| DIRECTORY CREATE | `completion_create_request` plus one name component; exclusive FILE READ/WRITE creation, attached FILE_RIGHTS grant |
| DIRECTORY REMOVE | `completion_remove_request` plus one name component; native result |

FILE/DIRECTORY backends are RAM, archive reads and mounted npfs. HOST, exported
providers, lookup, enumeration, rename and sync are unsupported here. Existing
rights, immutable-backend refusals and native bounds apply: READ 4,088 bytes,
WRITE 4,080, UDP 1,472; names must fit the copied request and backend limits.
No global RAM-name limit is introduced. CONNECT/UDP RECEIVE/FILE/DIRECTORY deadlines
are at most 30 seconds ahead, UDP SEND at most five seconds. Expired submission
returns `CALL_TIMED_OUT`; oversized waits return `CALL_BAD_REQUEST`. Queue time
uses the same absolute deadline. Expiry requests cancellation after admission;
it does not promise ownership reclamation by that instant.

## Collection and acknowledgement

`endpoint_receive()` returns the authenticated `ENDPOINT_MESSAGE_COMPLETION` kind.
`protocol`, `operation` and `correlation_id` identify the native operation;
`result` is its native `call_status`, separate from RECEIVE's transport status.
`reason` records cancellation (`CALL_ABANDONED`, `CALL_TIMED_OUT` or
`CALL_ENDPOINT_CLOSED`) without rewriting a real native result. `completion_flags`
reports NOT_STARTED, CANCEL_REQUESTED and PROGRESS_KNOWN; `progress` is meaningful
only with PROGRESS_KNOWN. Known read/write prefixes may accompany an error;
OUTCOME_UNKNOWN never means safe replay. Authority is attached, never serialized
as a payload handle. Native completion object_id and rights fields are zero.

An unfinished reservation is not READABLE. Terminal publication joins normal FIFO
below endpoint CANCEL/RETIRE notices, using existing level-triggered receiver
readiness. Register before scan; notify only a real transition with waiters.
`wait_many` neither collects nor reserves progress, and promises no cross-object
ordering. A receiver can share a wait with console/pipe/TCP interests.

Completion RECEIVE uses its preclaimed slots. A short result buffer returns
`CALL_BUFFER_TOO_SMALL`; invalid output returns `CALL_BAD_BUFFER`. Both retain the
queued result, attached grants and claims for retry. A bounded mapping lease keeps
validated output stable through the atomic copy/install handoff. Ordinary message
collection semantics remain unchanged.

Closing the received zero-right receipt acknowledges completion and releases its
admission/storage. It cannot REPLY, copy or transfer. Returned grants are independent.
No ACK causes backpressure, not eviction. CANCEL after ACK returns `CALL_NOT_FOUND`.
A lost successful collection is not another operation; exit closes owned receipts
and disposes unreported grants.

## Cancellation and teardown

Invoke COMPLETION_CANCEL on the owned receiver with RECEIVE and its correlation ID.
It reserves no new slot, returns an idempotent acknowledgement while pending or
terminal, and never collects a result. Unknown/acknowledged IDs return
`CALL_NOT_FOUND`; a closed receiver returns `CALL_ENDPOINT_CLOSED`.

If cancellation wins before backend start, NOT_STARTED records no effects.
Otherwise interrupt only at safe boundaries and drain existing worker ownership;
a completed write/create/handshake or consumed datagram keeps its real outcome.
Filesystem journal and physical ticket ownership drain at their original bound;
cancellation does not stop midway through a journal transaction. No rollback,
compensating delete, secure erasure or automatic replay is implied. A successful
CREATE before unlink and owner death can leave an entry in private staging storage.

Receiver close/exit stops admission and releases table claims immediately. Pending
work retains independently owned records, target references and cleanup attribution,
with no worker borrow of a process/table/user VM. Uncollected outcomes are disposed
only after drain; a new receiver cannot bypass the process gate meanwhile.
Collected results keep their receipt lifetime. Process exit releases the gate's
owner reference; deferred workers keep its independent storage. Execution-group
cleanup covers drain and transitive releases, without a fixed cleanup deadline.

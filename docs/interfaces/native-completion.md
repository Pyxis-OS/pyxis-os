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

Concurrent operations share a separate **16 MiB per-process completion-metadata
allowance**, across all receivers and native network/filesystem backends. Budget
exhaustion is `CALL_LIMIT` before admission. The account includes its header,
operation/request/result storage, dedicated delivery/receipt claims, captured
lineage metadata and backend/capture reservations. It does not charge an entire
pre-existing target object graph again. Network operation storage also consumes
the independent shared TCP/UDP allowance; filesystem operations do not.
FILE_READ_BULK separately reserves up to 1 MiB from a **16 MiB per-process bulk-data
allowance**, before endpoint/backend admission. Its fixed completion payload is
only `file_read_reply`; collect data with the receipt's `ENDPOINT_COLLECT_BULK`
right, then CLOSE/ACK. Failed/short collection preserves the result; success
consumes data. Actual native status and cancellation reason remain independent.
A nonzero inner FILE deadline can shorten, never renew, the required finite
outer submission deadline. Metadata accounting is unchanged.

FILE_TRANSFER charges its descriptor, job/state and completion metadata, not its
requested I/O length: the sole filesystem worker allocates/frees at most one
1 MiB transfer scratch buffer before terminal publication.

Keep the charge through receipt acknowledgement or uncollected-work drain and
actual deferred metadata reclamation. Closing a receiver does not reset the
account; independently owned cleanup survives process exit. A new admission may
still refuse while acknowledged storage is awaiting reclamation. Receiver
creation still requires the held `service/create` grant; no constructor grant
is implied by TCP/UDP authority. An explicit bundle declaration can receive the
trusted launcher's held constructor privately; ordinary children do not inherit
it implicitly. Ordinary IPC, receiver delivery slots and
per-object direction ownership retain their separate bounds.

Before accepting, reserve operation/input/result storage, one of the receiver's
sixteen delivery records, exact receipt/returned-grant table slots, native worker
admission and cleanup ownership. `CALL_QUEUE_FULL`, `CALL_BUSY`, `CALL_LIMIT` and
`CALL_NO_MEMORY` refuse without submitting I/O or producing a completion.
Admission may require a short BSP allocation handoff, never the submitted I/O wait.
No result buffer is retained or needed on SUBMIT. Successful return means admission
only. IDs are consumed on acceptance; earlier or zero IDs are `CALL_BAD_REQUEST`,
and exhausting the receiver's ID range returns `CALL_LIMIT`.

Forwarded SUBMIT elects admission against CUT immediately before consuming the
correlation ID. Accepted admission keeps its real result; the cloned backend
lineage stays pending until its separate effects boundary. CUT before backend
start therefore produces a DENIED completion rather than rewriting successful
SUBMIT as a refusal after commitment.

## First operation profile

Submission metadata is separate from the bounded 4,096-byte payload. All integers
and addresses use the existing native ABI conventions. There are no borrowed
user buffers or pointers in delivered data. FILE_TRANSFER names a source handle
only during admission: the kernel captures its READ grant and lineage before
acceptance; workers never resolve a caller-local handle.

| Native target / operation | Copied payload and result |
| --- | --- |
| TCP service CONNECT | `completion_connect_request`; result `tcp_connect_reply` with zero handle plus attached TCP_RIGHTS grant |
| UDP SEND | `completion_udp_send_request` followed by exact datagram bytes; native status, successful accepted length as known progress |
| UDP RECEIVE | `completion_udp_receive_request`; native peer/count reply followed by datagram |
| FILE READ_BULK | `file_bulk_read_request`; fixed count metadata, separately collected copied bytes |
| FILE TRANSFER | `file_transfer_request`, captured source READ plus destination WRITE; confirmed count in `file_transfer_reply` |
| FILE READ / WRITE / RESIZE / SIZE | Existing address-free native request/reply layouts, including READ's count prefix |
| DIRECTORY CREATE | `completion_create_request` plus one name component; exclusive FILE READ/WRITE creation, attached FILE_RIGHTS grant |
| DIRECTORY REMOVE | `completion_remove_request` plus one name component; native result |

Ordinary FILE/DIRECTORY backends are RAM, archive reads and mounted npfs. The
bounded [FILE_TRANSFER](file-providers.md#file-dispatch-and-lifetime) path additionally supports RAM/initrd-to-FAT32 and FAT32-to-RAM, at most 1 MiB.
Other FAT32 operations, HOST, exported providers, lookup, enumeration, rename and
sync are unsupported here. Existing
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
`reason` records cancellation (`CALL_ABANDONED`, `CALL_TIMED_OUT`, `CALL_DENIED` or
`CALL_ENDPOINT_CLOSED`) without rewriting a real native result. `completion_flags`
reports NOT_STARTED, CANCEL_REQUESTED and PROGRESS_KNOWN; `progress` is meaningful
only with PROGRESS_KNOWN. Forwarder CUT before start produces DENIED; a started
operation retains its real result and drains ownership before publication.
Known read/write prefixes may accompany an error;
OUTCOME_UNKNOWN never means safe replay. Authority is attached, never serialized
as a payload handle. Native completion object_id and rights fields are zero.

An unfinished reservation is not READABLE. Terminal publication joins normal FIFO
below endpoint CANCEL/RETIRE notices, using existing level-triggered receiver
readiness. Register before scan; notify only a real transition with waiters.
`wait_many` and [readiness sets](readiness-sets.md) observe owned receiver
READABLE/CLOSED without collecting or reserving progress. Persistent subscriptions
retain storage rather than a logical RECEIVE grant; REMOVE joins detachment.
Neither wait API promises cross-object ordering. A receiver can share a wait
with console/pipe/TCP interests.

Completion RECEIVE uses its preclaimed slots. A short result buffer returns
`CALL_BUFFER_TOO_SMALL`; invalid output returns `CALL_BAD_BUFFER`. Both retain the
queued result, attached grants and claims for retry. A bounded mapping lease keeps
validated output stable through the atomic copy/install handoff. Ordinary message
collection semantics remain unchanged.

Closing the received zero-right receipt acknowledges completion; its independent
storage and charge are released on actual reclamation. It cannot REPLY, copy or transfer. Returned grants are independent.
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
only after drain; a new receiver cannot bypass the process account meanwhile.
Collected results keep their receipt lifetime. Process exit releases the account's
owner reference; deferred workers keep its independent storage. Execution-group
cleanup covers drain and transitive releases, without a fixed cleanup deadline.

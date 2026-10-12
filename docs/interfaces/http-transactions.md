# Prepared HTTP transactions

`httpfs` serves native prepared GET/fixed-body POST alongside the existing
[complete FILE snapshots](file-providers.md#http-and-https-snapshots).
The [ABI](../../include/abi/http_transaction.h) and SDK's `http_transaction.h`
define PREPARE, COMMIT, ABORT and STATUS. HTTP/TLS remain userspace protocols;
[STREAM](../../include/abi/stream.h) supplies forward-only response delivery.
Libc/Lua streaming and shell transaction coordination remain later tasks of the
[native HTTP milestone](../wip/native-http-requests.md).

## Authority and preparation

PROVIDER_PREPARE uses OPEN_READ for GET and independently delegated OPEN_WRITE
for POST. A read-only provider or JSON alias cannot add submission authority.
Stock services publish `json+http`/`json+https` aliases with the source's masks;
those aliases select the JSON content-type default, not a different transaction.

One copied endpoint payload contains the 40-byte prepare header, URI and bounded
`name: value\r\n` fields, at most 4,096 bytes in total. The absolute monotonic
deadline must be nonzero and future; it covers staging, submission and framing,
including requests longer than 30 seconds. Wait slices remain natively bounded.
The provider owns Host, length and transfer framing; conflicting, duplicate,
malformed or CR/LF-bearing fields fail before networking. GET supports private
request-scoped fields too. Mark credential-bearing requests explicitly; request
fields never appear in STATUS, argv, environment or logs. POST and prepared GET
return the first response without following redirects. Anonymous legacy snapshot
GET retains its existing redirect/trust policy.

PREPARE performs no network submission. It returns independently exported
control, optional body and observation grants. Native bindings own these handles;
copying and child delegation use explicit narrower capability copies, never
serialized handle numbers or ambient environment inheritance. A producer needs
only body WRITE, a reader response/observation, and a monitor STATUS. Control
inheritance is opt-in. Independent provider lifetime lets outside holders survive
shell/job exit; closing a binding releases its references, never submits it.

POST requires a separately named `staging` native directory grant from the
trusted launcher. This implementation admits a writable mounted npfs directory
only. The launcher must isolate it from clients, including their other aliases;
a shared home/tmp filename does not establish privacy. Without provisioning,
POST is denied and GET remains available. The provider creates exclusively,
unlinks before exporting a writer, and privately retains READ|WRITE backing.
No total staged-body ceiling replaces actual storage/representability limits.
RAM staging is not implemented. Unlink/close does not guarantee secure disk erase.

## Facets and lifetime

| Facet | Operations and ownership |
| --- | --- |
| Control | Attenuable COMMIT and ABORT rights; no credential/body read authority |
| Body | FILE WRITE, SIZE and RESIZE; no READ; SYNC is unsupported and never submits |
| Response | STREAM READ; copies share one forward cursor; no offsets, seek or SIZE |
| Observation | STATUS only, independently retained from control/response |

The first COMMIT freezes all writer copies before admitting another mutation,
drains an already admitted mutation, pins the length and starts at most one
exchange. Known failed/uncertain mutations prevent submission. An unobserved lost
WRITE reply is not proof of producer success: the coordinator must prevent COMMIT
when a producer fails. Closing/flushing a writer never commits.

COMMIT's deferred receipt owns opening work until headers yield response and
observation grants. Its first reply has HTTP_COMMIT_RESPONSE; subsequent COMMIT
returns status without another reader or replay. Canceling that receipt before
handoff aborts unfinished work. After successful REPLY, reader retirement governs
even if client collection fails. A lost delivered mutation/commit has an uncertain
outcome and is never retried automatically.

Retiring control before COMMIT abandons staging, even with a writer/observer
held. After handoff, the last response reference aborts unfinished reception.
Separate observation exports keep only final state, not staging or active work.
ABORT is idempotent local cancellation, not remote rollback; completed framing
stays immutable. Cancellation/expiry retains native storage until completion
collection/drain and acknowledgement permit reclamation. Provider death fails
held operations, without replacement/rebinding or automatic replay.

## Reads and final results

A positive-capacity READ requests at most 4,088 bytes. It waits for data or a
terminal result; zero capacity is BAD_REQUEST. Only fully validated framing gives
successful zero-byte EOF. A truncated body, deadline or provider failure returns
an error after any readable prefix. A lost delivered READ reply may consume
bytes; do not replay it or turn observation success into IPC success.

STATUS separates transaction phase, submission certainty (not started, attempted,
sent), HTTP status, framing (pending, complete, failed), native/source/cleanup
errors, uploaded/received/consumed counters, TLS diagnostics and whether TLS
cleanup was attempted. It includes bounded response Content-Type and Location,
never request headers. A completed 4xx/5xx retains its body and HTTP status;
framing success does not imply HTTP success. Missing final observation proves
nothing. Nonempty decoded 205 Reset Content fails while preserving status 205.

## Service and limits

One active exchange per provider process covers preparation through terminal
framing/drained abort. Other valid preparations/legacy OPEN return CALL_BUSY;
there is no queue or automatic replan. Aliases and clients share that instance.
A response has an 8 KiB resident queue with backpressure; total transfer bytes
are not a RAM reservation. Terminal readers/observers can exhaust explicit
export/storage admission: 63 transaction export slots, reserving four for GET
and five for POST, including independent handoff observation. Legacy snapshots
retain their 16 MiB chain and 64 MiB storage budgets.

The loop uses receiver/TCP readiness, TRY_RECEIVE and absolute-deadline parking.
Native completions cover DNS UDP SEND/RECEIVE, TCP CONNECT and staged FILE/
DIRECTORY I/O, one unacknowledged operation per process (second submission
CALL_BUSY). TLS handshake/transfer uses cooperative native TRY I/O and stable
plaintext retry arguments; accepted bytes are never replayed. A TLS read requiring
output while an application write is pending fails explicitly.

**Remaining synchronous waits:** DNS ID/port entropy up to **3 seconds**, TLS
entropy up to **5 seconds**, both additionally bounded by the request deadline.
STATUS/ABORT cannot run during these calls, clock/UDP open/cleanup IPC,
capability export/table admission, allocation or local cryptography. Trust/PSA setup precedes publication. No random completion
or fabricated entropy-WANT result is implemented. Cooperative TLS cleanup closes
native transport without attempting close_notify; STATUS explicitly records that
cleanup was not attempted. Authenticated-EOF rules for received bodies remain.
See [responsiveness debt](../technical-debt.md#http-provider-responsiveness) and
[native completion admission](native-completion.md#authority-and-admission).

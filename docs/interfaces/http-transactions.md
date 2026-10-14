# Prepared HTTP transactions

`httpfs` serves native prepared GET/fixed-body POST alongside the existing
[complete FILE snapshots](file-providers.md#http-and-https-snapshots).
The [ABI](../../include/abi/http_transaction.h) and SDK's `http_transaction.h`
define private PREPARE, STATUS and METADATA. Controls use the protocol-neutral
[transaction ABI](transactions.md) for STAGE, COMMIT and ABORT. HTTP/TLS remain userspace
protocols;
[STREAM](../../include/abi/stream.h) supplies forward-only response delivery.
[Libc streaming](../userland/stdio.md) uses these facets; the [shell coordinator](../userland/shell.md#prepared-http-requests)
uses the same facets for interactive and trusted-bootstrap requests.
[Standalone scripts](../wip/native-http-requests.md#parked-tasks-5b-and-5c)
are parked by the owner until the capability model is resolved and are not delivered.

## Authority and preparation

PROVIDER_PREPARE uses OPEN_READ for GET and independently delegated OPEN_WRITE
for POST. A read-only provider or JSON alias cannot add submission authority.
Stock services publish `json+http`/`json+https` aliases with the source's masks;
those aliases select the JSON content-type default, not a different transaction.

One copied endpoint payload contains the 344-byte prepare header, URI and bounded
`name: value\r\n` fields, at most 4,096 bytes in total (3,752 URI/header bytes).
The header includes a 304-byte context: the normalized initial origin, sticky
origin-crossing flag and remaining header-byte, field and informational-response
allowances. This context grants
no destination authority and has no total-body allowance. The 16-byte success
reply reports the method and `HTTP_TRUST_ORIGIN_ONLY` restriction for a custom
trust profile, followed by control, optional body and observation grants.
The absolute monotonic deadline must be nonzero and future; it covers staging,
submission and framing, including requests longer than 30 seconds. Wait slices
remain natively bounded.
The provider owns Host, length and transfer framing; conflicting, duplicate,
malformed or CR/LF-bearing fields fail before networking. GET supports private
request-scoped fields too. Mark credential-bearing requests explicitly; request
fields never appear in STATUS, argv, environment or logs. POST and explicitly
prepared GET return the first response without following redirects. Ordinary
libc URL GET uses a typed PREPARE/COMMIT bridge with the
[anonymous redirect policy](../userland/http-fetch.md#redirect-chains), one
deadline and aggregate framing charges across hops. Custom-trust origin changes
are refused by both the bridge and destination provider. The bridge retains each
scheme's selected provider binding for the chain; namespace replacement affects
future opens. Explicit provider OPEN retains the legacy FILE snapshot route.

PREPARE performs no network submission. It returns independently exported
control, optional body and observation grants. Native bindings own these handles;
copying and child delegation use explicit narrower capability copies, never
serialized handle numbers or ambient environment inheritance. A producer needs
only body WRITE, a reader response/observation, and a monitor STATUS. Control
inheritance is opt-in. Independent provider lifetime lets outside holders survive
shell/job exit; closing a binding releases its references, never submits it.

POST requires a separately named `staging` native directory grant from the
trusted launcher. The provider admits writable native RAM or npfs directories, validated by their
explicit filesystem type. Trusted startup isolates them from every client
root/cwd/alias and drops ancestor grants before interactive handoff; a hidden
home/tmp name is not private. Ordinary profiles explicitly declare RAM staging,
with a default 16 MiB body cap. `httpfs --staging-cap BYTES` configures that RAM
policy; oversized WRITE/RESIZE returns CALL_LIMIT before mutation and prevents
commit. Native allocation failure remains a separate real error. Protocol,
copy-buffer and RAM page-index metadata are additional bounded charges.

Large uploads opt into dedicated private npfs backing through the named source
`http_staging`; it has actual storage/representability limits rather than a
transport cap. There is no automatic RAM/npfs fallback. Missing optional or
provider-refused selected backing leaves GET available and POST denied; required
boot sources retain their existing startup refusal. The provider creates
exclusively, unlinks before exporting a writer and privately retains READ|WRITE
backing. Unlink/close does not promise secure disk erase.

## Facets and lifetime

| Facet | Operations and ownership |
| --- | --- |
| Control | TRANSACTION STAGE, COMMIT and ABORT, independently attenuable; no credential/body read authority |
| Body | FILE WRITE right, permitting WRITE, SIZE and RESIZE; no READ; SYNC is unsupported and never submits |
| Response | STREAM READ right; copies share one forward cursor; no offsets, seek or SIZE |
| Observation | HTTP_TRANSACTION STATUS right (bit 2), authorizing STATUS and METADATA; independently retained from control/response |

All facets are exported grants with CALL transport. GET preparation transfers
two grants; POST transfers three. STAGE can obtain another owned writable body
reference. COMMIT transfers one typed response reader; preparation retains the
separate observation grant. A declared response stdin delegates only STREAM READ and its
STATUS-only companion; see [startup streams](processes.md#startup-record).

The first COMMIT freezes all writer copies before admitting another mutation,
drains an already admitted mutation, pins the length and starts at most one
exchange. Known failed/uncertain mutations prevent submission. An unobserved lost
WRITE reply is not proof of producer success: the coordinator must prevent COMMIT
when a producer fails. Raw body FILE close/flush never commits. Libc transaction FILE flush and final
owning close use the generic COMMIT operation; see [stdio](../userland/stdio.md).

COMMIT's deferred receipt owns opening work until headers yield a typed response
reader. Its reply uses provider_open_reply; subsequent COMMIT cannot hand out
another reader or replay submission. Canceling that receipt before
handoff aborts unfinished work. After successful REPLY, reader retirement governs
even if client collection fails. A lost delivered mutation/commit has an uncertain
outcome and is never retried automatically.

Retiring control before COMMIT abandons staging, even with a writer/observer
held. After handoff, the last response reference aborts unfinished reception.
Separate observation exports keep only final state, not staging or active work.
ABORT is idempotent local cancellation, not remote rollback; completed framing
stays immutable. The first ABORT receipt waits for pending native operations to
drain and cleanup to release active work, then reports cleanup status. Duplicates
while that receipt is pending reply immediately and do not prove drain completion.
Cancellation/expiry retains native storage until completion
collection/drain and acknowledgement permit reclamation. Provider death fails
held operations, without replacement/rebinding or automatic replay.

## Reads and final results

A positive-capacity READ requests at most 4,088 bytes. It waits for data or a
terminal result; zero capacity is BAD_REQUEST. Only fully validated framing gives
successful zero-byte EOF. A truncated body, deadline or provider failure returns
an error after any readable prefix. A lost delivered READ reply may consume
bytes; do not replay it or turn observation success into IPC success.

The 2,328-byte STATUS reply separates transaction phase, submission certainty
(not started, attempted, sent), HTTP status, framing (pending, complete, failed),
native/source/cleanup errors, uploaded/received/consumed counters, TLS diagnostics and whether TLS
cleanup was attempted. It includes bounded response Content-Type and Location,
never request headers. The separate 2,177-byte METADATA reply uses the same STATUS
right and returns this transaction's actual endpoint URI (without `json+`) and
media type. It keeps URI and redirect Location from sharing an oversized reply.
A completed 4xx/5xx retains its body and HTTP status; framing success does not
imply HTTP success. Missing final observation proves
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
entropy callbacks up to **5 seconds**, per call and additionally bounded by
the request deadline. These bounds do not preempt an entire cryptographic step.
STATUS/ABORT cannot run during these calls, clock/UDP open/cleanup IPC,
capability export/table admission, allocation or local cryptography. Trust/PSA setup precedes publication. No random completion
or fabricated entropy-WANT result is implemented. Cooperative TLS cleanup closes
native transport without attempting close_notify; STATUS explicitly records that
cleanup was not attempted. Authenticated-EOF rules for received bodies remain.
See [responsiveness debt](../technical-debt.md#http-provider-responsiveness) and
[native completion admission](native-completion.md#authority-and-admission).

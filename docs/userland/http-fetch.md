# Userspace HTTP and HTTPS fetching

The `httpfs` userspace service connects the incremental fetch library to
[prepared HTTP transactions](../interfaces/http-transactions.md). Ordinary C/Lua
FILE consumers and HTTP stdin use forward response streams through `http://`
and `https://` in their delegated namespace. Separate
provider instances serve the two schemes. Publication performs no fetch; boot
and local files remain usable without networking. HTTPS verifies certificates
against packaged public roots and any explicitly configured custom roots.
The [TLS contract](https.md) describes the profile, trust updates, native hooks
and TLS memory accounting.

Transactions separate body/control/response/observation grants and serve bounded
response chunks before completion. The [stdio adapter](stdio.md#http-streams-and-explicit-submission)
supports ordinary GET and explicit POST preparation/commit/status/abort;
[Lua](lua.md) uses the same libc adapter. The HTTP adapters leave TLS to the
provider; libc does not link TLS. The stock JSON aliases preserve read-only
authority; POST requires explicitly provisioned private staging and separate
write authority. Explicit legacy [provider OPEN](../interfaces/file-providers.md)
still returns complete immutable FILE snapshots.

**Pyxis must be able to hit good network targets overall in the future.**
End-to-end HTTP throughput should track the native TCP path. The ThinkPad xfer
measurements of 2026-10-09 (70.5 MiB/s send, 85 MiB/s receive) are the current
[reference measurement](../development/network-throughput.md), **not a contract**.
Track the HTTP/TCP gap with each relevant change. The owner accepted task 3's
native whole-GET cost (+18%, 14.3 → 17.2 ms) on 2026-10-12;
task 4's native stream A/B then recovered that whole-GET cost for a 1 MiB body
(about 28% faster, ~82 MiB/s). [Task 3 measurements](../development/experiments/http-prepared.md),
[task 4 A/B](../development/experiments/http-stream-adapters.md#native-ab--owner-2026-10-13) and
[revisit points](../technical-debt.md#http-provider-throughput) remain explicit.

## Use and startup

Build with `make -j16 image`, then boot with networking explicitly enabled:

```sh
make run CPUS=4 VIRTIO_NET=1
```

In the guest:

```text
cat http://example.com/
cat https://example.com/
cksum http://10.0.2.2:18080/sample.bin
cat http://10.0.2.2:18080/sample.bin | tee home://sample.bin | cksum
cat < http://example.com/
```

For known host files, run `python3 -m http.server 18080 --bind 127.0.0.1` in their
host directory. QEMU user networking exposes that listener at `10.0.2.2`.

Development and read-only init each create a namespace and request
`session --start-services`. After reading configuration and applying any requested
NIC settings, session launches `boot://init-services` with the configured
`DNS_SERVER` and ordinary session grants. That trusted script publishes
`service start http bin://httpfs.pxb`, followed by
`service start --optional --read-only https bin://httpfs.pxb --https`, then consumes the private backing source before handing off to the interactive shell. A reported HTTPS setup failure is logged and
leaves HTTPS unpublished while startup continues. The read-only profile
restricts host-file writes, not HTTP or HTTPS reads. Idle spaces
start no provider. Ordinary session invocation without the flag starts its shell
directly, so handing off within an existing namespace does not republish services.

The declared `httpfs.pxb` selects caller-held grants. Trusted startup supplies a
private RAM staging leaf (16 MiB by default); large uploads opt into an isolated
npfs source. Manual startup after that source is consumed has GET authority but
no POST backing. See [staging and limits](../interfaces/http-transactions.md#authority-and-preparation).

`httpfs` defaults to HTTP. `--https` selects HTTPS and optionally accepts
`--ca-bundle URI` to augment public trust. It selects inherited DNS configuration
without DNS traffic. Publication uses the explicit grant handoff; providers
receive neither the parent namespace nor namespace-creation authority.

```text
service replace --read-only https bin://httpfs.pxb --https --ca-bundle home://custom-ca.pem
namespace remove https
service start --read-only https bin://httpfs.pxb --https
```

The `--read-only` service option attenuates native directory roots and working
directories for that provider launch. Trust loading uses native read-only files,
never a remote provider. See
[trust configuration](https.md#trust-configuration-and-updates) for packaged roots,
native trust loading, bounded setup failure and the restart/update contract.
Each instance owns independent trust, transactions, snapshots and storage;
retained exports survive removal/replacement until their final handles close.
Clients receive delegated I/O/observation/control authority, not trust-management
or additional TCP grants.

## Library and authority

`userspace/libhttp/http.h` declares the incremental GET/POST interface and the
`http_fetch` snapshot collector, `http_body_release` and `http_result_status`. This is an application library, built as `libhttp.a`,
outside the SDK runtime. Its parser dependency is the pinned
[picohttpparser port](../../ports/picohttpparser/README.md), exported separately at
`build/ports-dev/picohttpparser`. Consumers also link userland's `libtls.a` and
the configured Mbed TLS libraries from `build/ports-dev/mbedtls`, in the order
supplied by that export's `share/mbedtls.mk`. All consume the same SDK.
The kernel does not interpret HTTP; the shared provider ABI carries redirect
context/outcomes and libc exposes copied response metadata.

The caller supplies borrowed TCP, UDP, random and monotonic-clock capabilities
and a numeric DNS server through `http_client`. Its explicit HTTP/HTTPS mode
must match the URI; a mismatch fails without falling back to another transport.
Numeric HTTP IPv4 destinations skip DNS and do not need
UDP/random authority. The library does no startup lookup or printing. The
HTTP provider obtains its grants from startup and selects the existing `DNS_SERVER`
configuration. These grants permit access under existing networking policy;
this is not a destination sandbox.

HTTPS additionally borrows a ready `tls_runtime`. The caller initializes it,
imports public roots and any custom augmentation, freezes trust and retains it
through the fetch. Libhttp neither reads trust files nor discovers authority.
One runtime permits one active connection. A fetch owns its TLS connection and
TCP stream; a returned body retains neither. Plain HTTP does not initialize TLS
or require a trust runtime. The TLS allocation cap is separate from body storage.

The result distinguishes HTTP policy/parser failures, allocation/quota failures,
DNS answer failures and native network errors. The final status from a parsed
HTTP response remains available on later failure, with zero meaning no final
status. Native call status and DNS RCODE have separate fields inside the library.
The provider maps these into native operation results; the shared OPEN bridge
preserves final HTTP status separately in `provider_result.provider_status`.
A caller must check the bridge's transport return before using the provider result.

TLS failures retain their category, native status, library code and certificate
verification flags in `tls_failure`. `tls_cleanup` separately records diagnostic
close-notification failures, preserving the original fetch result. The library
status mapper translates certificate rejection to `CALL_DENIED`, TLS protocol
or truncation to `CALL_IO`, allocation quota to `CALL_QUOTA`, allocation failure
to `CALL_NO_MEMORY`, and encoded-size limits to `CALL_FILE_TOO_LARGE`. Native
TLS transport, entropy and clock failures preserve their native status.

## Incremental GET

`http_get_open` performs one GET through the final headers and returns an owned
stream. It follows no redirects. `http_stream_read` copies at most 4 KiB into a
positive-capacity caller buffer; the engine owns fixed scratch, not body storage.
There is no cumulative body allowance. Metadata retains the bounds below, and
byte counters fail on integer overflow. A zero deadline selects 30 seconds; an explicit absolute deadline may be longer.
DNS, connect, upload and response share it. Native waits remain at most 30 seconds;
CONNECT is one bounded attempt, with no reconnect. Terminal transport failures
remain failures. The snapshot collector and redirect bridge keep their existing
30-second budget. The provider uses the separate cooperative
`http_request_start`/`http_stream_step`/completion API; the blocking functions
remain for compatibility callers.

The caller retains `http_response`, the borrowed capabilities and any TLS runtime
until `http_stream_close`. The response separates HTTP status from pending,
complete and failed framing: a framed 4xx/5xx can complete with a body. DATA never
proves completion. Inspect the retained terminal result after COMPLETE/FAILED;
repeated terminal reads do no I/O. A later failure retains the delivered prefix
and its byte count. Close before completion records aborted failure and releases
transport/scratch; it never certifies a partial download.

Fixed bodies finish at their declared length; chunked bodies finish only after
payload delimiters, the zero chunk and bounded validated trailers. Close-delimited
HTTPS requires authenticated TLS EOF. Native handle-close failure invalidates
completion; a TLS close-notification failure after valid framing is diagnostic.
These are the [HTTP framing rules](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3),
with the existing identity-only representation and trailer restrictions.
The snapshot collector keeps its existing HTTP status and storage policy.
`httpfs` progresses both legacy OPEN and typed PREPARE cooperatively; ordinary
libc opens use the latter and consume the live STREAM interface.

Qualification, configuration and measured allocation demand are recorded in the
[task 1 experiment](../development/experiments/http-incremental-get.md).

## Fixed-body requests

`http_request_open` selects GET or POST and accepts copied header spans. POST
borrows a native READ FILE and its exact frozen length, reading from offset zero
in at most 4 KiB chunks. The caller keeps the file unchanged through OPEN and may
release it afterwards. This layer provides no staging writer or freeze mechanism;
those belong to the provider transaction task. A source size/authority failure
occurs before connecting; later read failures retain `source_status`. Native
FILE reads have no interruptible deadline; the engine checks time before sending
again after a read.

Request metadata is limited to 32 KiB on the wire and 256 fields including the
engine's fields. Names must be HTTP tokens; values reject NUL, CR/LF and controls
except HTAB. Case-insensitive duplicate names refuse. The engine owns Host,
Content-Length, Connection and Accept-Encoding. Transfer/content encoding,
Expect, trailers, upgrade and proxy connection controls are unsupported.
GET can use caller fields too. Owned copies and upload scratch are erased on
release; the library never logs request fields.

The upload loop reads responses between send attempts. Final response headers
stop an incomplete upload; a completely framed 4xx/5xx retains its body and
completes normally. `request_started` means a send was attempted;
`request_sent` and `upload_bytes` describe known local acceptance, never remote
execution. A failure after starting may have an unknown remote outcome, even
with zero reported bytes. There is no retry, redirect or replay in this API.
Pending/incomplete TLS output is retired without close_notify; ordinary early
responses remain readable. A read that tries TLS output while an application
write is pending fails explicitly under the
[accepted TLS policy](../wip/native-http-requests.md#task-2-accepted-tls-policy).

[Task 2 qualification and costs](../development/experiments/http-fixed-post.md)
cover local HTTP/HTTPS fixtures in nested QEMU. The provider and libc/Lua
transaction adapters add private staging, freezing and explicit submission; they
do not require the application to link libhttp or TLS.

## Request and response policy

Each ordinary GET hop sends one HTTP/1.1 GET with Host, Connection: close and
Accept-Encoding: identity. The request is framed by its header terminator;
TCP write shutdown waits until the response is complete. URI syntax accepts ASCII DNS names or numeric IPv4,
an optional decimal port, path and query. It rejects credentials, IPv6 literals,
raw characters outside URI syntax and malformed percent escapes. An empty path
becomes `/`; fragments are checked but never transmitted. Encoded path/query
bytes are preserved, without percent decoding or normalization.

HTTPS defaults to port 443 and accepts explicit ports, but requires a DNS name;
numeric HTTPS addresses are unsupported. Verification and SNI use the URI host
without its port and with one terminal DNS dot removed. DNS selects the address,
not the identity. The HTTP Host field retains the original authority. Required
chain, name and validity checks complete before the GET is sent.

Responses use HTTP/1.0 or HTTP/1.1. Status 200 returns bytes; 204 returns an empty
body without waiting for connection close. Other final statuses are retained and
rejected by ordinary GET opens. Explicit transactions retain other final HTTP
statuses and their bodies separately from framing success. The shared typed
GET helper follows the selected redirects below. There is no
decompression, connection reuse, cache or whole-request replay. An HTTPS failure
never triggers a plaintext request.

The client supports Content-Length, chunked and orderly-close framing. Fixed and
chunked bodies must finish completely. Plain HTTP close-delimited bodies accept
orderly TCP EOF; there is no way to prove the origin intended to send no more bytes. Chunked bodies
finish at the terminal chunk and validated trailers, independently of peer EOF.
The pinned parser handles response/header syntax and chunk decoding; the library
validates framing policy and ignored chunk-extension syntax. Trailer fields are
parsed separately, not silently skipped.

For HTTPS, close-delimited bodies require authenticated TLS `close_notify`;
underlying TCP EOF is truncation. Authenticated EOF before a fixed-length or
chunked body completes is still incomplete HTTP input. Complete fixed-length,
chunked and 204 responses do not wait for peer shutdown. On success, the client
attempts its own TLS close notification using at most 100 ms of the remaining
original deadline. Its failure is diagnostic; handle-close failure still fails
the fetch. Failed streams retain their delivered prefix and first failure;
snapshot collection instead discards partial body storage.

The deliberately strict subset requires CRLF and rejects folded fields, duplicate
or comma-list Content-Length (even identical values), conflicting length/transfer
encoding, and transfer codings other than a single `chunked`. HTTP/1.0 transfer
encoding is rejected. Informational responses and 204 cannot carry body-framing
fields. Status 101 is unsupported. Content-Encoding must be absent or a single
`identity`. Duplicate Content-Type/Content-Encoding are rejected. Content-Type is
optional descriptive printable ASCII metadata, at most 127 bytes; excess is a
limit failure. Trailers cannot supply framing, content representation, Host,
Connection or Trailer fields.

## Redirect chains

Accepted **2026-10-09**: follow 301/302/303/307/308, at most **ten redirects**
(eleven requests), checking every target against visited network URLs before
contacting it. Keys normalize scheme/host, effective port, empty path, escapes
of unreserved characters and dot segments; query order stays significant and
fragments are ignored. Comparison does not rewrite outgoing escapes. The cap
also bounds DNS aliases and changing-query chains. Loop/hop exhaustion is CALL_LIMIT.

Require exactly one valid nonempty Location and resolve against the current hop,
including relative/query-only/scheme-relative forms and inherited fragments.
Only HTTP(S) provider targets are allowed; native roots and ambiguous bindings
are refused. Invalid Location/outcomes are CALL_BAD_REQUEST, size excess is
CALL_FILE_TOO_LARGE. Final statuses remain 200/204; unselected 3xx remain errors.
Ordinary opens are GET-only throughout the chain. Explicit preparation returns
the raw response without automatic redirects, including for POST; it never
replays a body.

The caller's shared libpyxis typed GET helper owns the chain and retains each delegated
scheme binding on first use until completion/failure. HTTP providers have no
caller namespace or cross-scheme authority; each owns its TLS runtime. The helper
validates final headers and observation, then explicitly ABORTs an intermediate
response without draining its body. The provider holds the first ABORT receipt
until pending I/O cancellation and stream cleanup finish; the next hop starts
only after that acknowledgment and facet release. The final response owns a
forward STREAM, not body-sized storage. Legacy OPEN instead exchanges
REDIRECT/BYTES outcomes and publishes only a complete snapshot.

HTTP→HTTPS and public-root cross-origin following are allowed. HTTPS→HTTP is
CALL_DENIED with no target contact or TLS-failure fallback. Custom-CA providers
keep their immutable public+custom union only for an initial HTTPS request and
same-origin chain: initial-origin and sticky crossing context are checked before
DNS/connect/TLS. Even publicly signed cross-origin targets through such an
instance are refused; HTTP upgrades into it are refused too. This continuity
policy does not restrict independent direct opens. Rebuild Host/Connection/Accept-Encoding
per hop; no custom headers, credentials, cookies or Referer are supplied.

One original absolute **30 s** deadline, **32 KiB headers/trailers**, **256 fields**
and **eight informational responses** cover the ordinary typed GET chain and
final body consumption. There is no cumulative body cap or deadline refresh.
Unread redirect bodies are neither materialized nor drained. Explicit preparation
may select a longer absolute deadline; it does not use the automatic chain.
Legacy OPEN retains a **16 MiB body allowance** across redirects, conservative
body read-ahead charging and the **64 MiB** snapshot storage quota; an exact-limit
close-delimited snapshot needs a bounded one-byte EOF probe. Neither path retries
a transport failure.

`pyxis_stdio_response()` in `<pyxis/stdio.h>` copies final URL (with fragment),
redirect count, HTTP status and media type from an open FILE without changing
position/indicators or granting authority. Local handles can have absent
metadata; inherited HTTP streams query METADATA+STATUS for the actual final URI,
HTTP status and media type, without recovering the parent's redirect count.
Close frees owned metadata. Native typed URI helpers return the same data. Failed
fopen still reports errno; the native bridge retains diagnostic HTTP status.
[Links](links.md) adopts the final URL, materializes the forward response into
its own cache and requires final completion before publishing cached bytes.

The [qualification record](../development/experiments/http-redirects/README.md)
separates controlled QEMU results from source inspection. Protocol references:
[RFC 9110 redirects](https://www.rfc-editor.org/rfc/rfc9110.html#section-15.4),
[Location](https://www.rfc-editor.org/rfc/rfc9110.html#section-10.2.2), and
[RFC 3986 resolution](https://www.rfc-editor.org/rfc/rfc3986.html#section-5.2).

## Bounds, storage and lifetime

| Resource | Limit |
| --- | --- |
| Complete URI / Location reference | 2,048 bytes |
| Aggregate response headers and trailers, including informational responses | 32 KiB |
| Aggregate fields | 256 |
| Informational responses | 8 |
| Each chunk size/extension line | 8 KiB |
| Legacy OPEN/collector body allowance | 16 MiB; shared across a legacy redirect chain |
| Shared snapshot body-storage reservations | 64 MiB |
| Ordinary typed GET chain and consumption | One original 30-second deadline |
| Legacy OPEN/collector time | 30 seconds, capped by an earlier caller deadline |
| Incremental request time | 30 seconds by default; caller absolute deadline may be longer |
| Request headers, including request line and terminator | 32 KiB; 256 fields including generated fields |

Chunk framing is decoded incrementally with checked sizes, exact CRLF delimiters
and bounded size/extension lines. It has no cumulative framing-overhead allowance.
Parser/request scratch is fixed and bounded separately from snapshot body storage;
the large receive buffer and field array live on the heap. The 16 MiB body and
64 MiB storage rows apply to the snapshot collector, not the incremental engine.

For path/libc HTTP opens the deadline starts before URI processing/binding lookup
and spans all hops. A direct one-hop fetch starts its own bounded deadline. It covers DNS, connect, send and the
TLS handshake when applicable and the complete response. Existing DNS attempts retain their three-second maximum and
at most two attempts, capped by this same absolute deadline. No retry,
informational response or successful short transfer refreshes it. CPU-side work
is checked before returning success as well.

For snapshot collection, the caller zero-initializes a `http_storage` account and
keeps it alive until all associated bodies are released. A successful result owns its bytes and capacity;
`http_body_release` frees them and returns the reservation. The account is for
single-task use. Copies of the C structure do not create additional ownership.
Release an existing successful result before reusing its output structure.

Fixed-length bodies reserve the declared size. Unknown lengths grow in bounded
powers of two, retaining the old allocation/reservation until the new allocation
exists and the bytes are copied. The 64 MiB ceiling counts that temporary overlap
as well as previously returned bodies. Allocation can fail below the ceiling;
there is no eviction. An empty body requires no body allocation. Every failed
fetch discards staging, returns its reservation and aborts/closes network state.
No snapshot body is exposed before success. Typed streams expose a prefix while
pending and require final STATUS to establish complete framing; the
[stdio contract](stdio.md#http-streams-and-explicit-submission) covers bounded
reads, terminal failures and inherited response authority.

## Snapshot lifetime and scheduling

The provider shares 63 transaction/snapshot export slots, including empty
snapshots, plus one OPEN service export. A slot is reserved through retirement
acknowledgment; prepared requests reserve multiple slots. Legacy OPEN's returned
FILE grants carry READ and CALL authority. Copies/attachments retain the same
body; READ uses explicit offsets and SIZE remains stable. Every OPEN fetches
independently, with no shared cache or eviction.

A completed fetch moves its body ownership into the export. If reply transfer
fails or the caller's deadline expires, the provisional export is withdrawn and
its local client closed. The receipt is finished; the body remains until RETIRE.
Normal final-client retirement also releases the body/reservation, acknowledges
retirement, then permits slot reuse. Removing a binding stops discovery; retained
files remain served until retirement. Once its OPEN export and all file exports
retire, the old provider exits naturally. Provider failure invalidates its exports.
No automatic supervisor or rebinding is supplied.

The provider's cooperative loop services snapshot reads and lifecycle notices
while one exchange progresses through native completions and TCP/TLS TRY steps.
A concurrent new OPEN/PREPARE returns BUSY; aliases share that process's gate.
Legacy OPEN retains its 30-second/earlier-caller budget. Readiness reserves
nothing and collection uses TRY_RECEIVE. The remaining synchronous DNS entropy
(**3 s**) and TLS entropy (**5 s**) waits, local cryptography and native service
handoffs still delay STATUS/ABORT; see
[service limits](../interfaces/http-transactions.md#service-and-limits).
Ordinary FILE helpers still supply no IPC deadline.

## Operation errors

Native calls return transport/validation failure separately from a valid provider
operation error. Only success transfers the promised grants. The provider maps
errors to existing native statuses; libc translates them into errno and validates
typed response completion.

| Condition | Native result | libc errno |
| --- | --- | --- |
| HTTP 404/410 or DNS NXDOMAIN | NOT_FOUND | ENOENT |
| HTTP 401/403 or missing OPEN authority | DENIED | EACCES |
| POST preparation through a read-only binding | DENIED | EACCES |
| Redirect, unsupported success status, unsupported HTTP feature | BAD_OPERATION | ENOTSUP |
| Other rejected HTTP status, malformed/truncated response or other DNS answer failure | IO | EIO |
| Invalid URI syntax | BAD_REQUEST | EINVAL |
| Fetch/call deadline expiry | TIMED_OUT | ETIMEDOUT |
| URI/header limit or legacy snapshot body limit | FILE_TOO_LARGE | EFBIG |
| Aggregate body storage limit | QUOTA | EDQUOT |
| Snapshot slots or IPC admission full | QUEUE_FULL | EAGAIN |
| Allocation failure | NO_MEMORY | ENOMEM |

Other native network errors retain their existing translation. Final HTTP status,
when available, survives provider failures as diagnostic metadata; a transport
failure does not promise a provider reply. Failed opens return no partial file.
There is no automatic transport retry, reconnection or rebinding. Legacy
anonymous GET redirect following uses the shared open bridge described above.

# Userspace HTTP and HTTPS fetching

The `httpfs` userspace service connects the bounded fetch library to
[provider OPEN and exported FILE snapshots](../interfaces/file-providers.md). Ordinary file
consumers use `http://` and `https://` through their delegated namespace. Separate
provider instances serve the two schemes. Publication performs no fetch; boot
and local files remain usable without networking. HTTPS verifies certificates
against packaged public roots and any explicitly configured custom roots.
The [TLS contract](https.md) describes the profile, trust updates, native hooks
and TLS memory accounting.

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
NIC settings, session launches `app://init-services` with the configured
`DNS_SERVER` and ordinary session grants. That trusted script publishes
`service start http app://httpfs.pxe`, followed by
`service start --optional --read-only https app://httpfs.pxe --https`, then hands
off to the interactive shell. A reported HTTPS setup failure is logged and
leaves HTTPS unpublished while startup continues. The read-only profile
restricts host-file writes, not HTTP or HTTPS reads. Idle spaces
start no provider. Ordinary session invocation without the flag starts its shell
directly, so handing off within an existing namespace does not republish services.

`httpfs` defaults to HTTP. `--https` selects HTTPS and optionally accepts
`--ca-bundle URI` to augment public trust. It selects inherited DNS configuration
without DNS traffic. Publication uses the explicit grant handoff; providers
receive neither the parent namespace nor namespace-creation authority.

```text
service replace --read-only https app://httpfs.pxe --https --ca-bundle home://custom-ca.pem
namespace remove https
service start --read-only https app://httpfs.pxe --https
```

The `--read-only` service option attenuates native directory roots and working
directories for that provider launch. Trust loading uses native read-only files,
never a remote provider. See
[trust configuration](https.md#trust-configuration-and-updates) for packaged roots,
native trust loading, bounded setup failure and the restart/update contract.
Each instance owns independent trust, snapshots and storage;
existing snapshots survive removal/replacement until their final handles close.
Clients receive FILE authority, not trust-management or additional TCP grants.

## Library and authority

`userspace/libhttp/http.h` declares `http_fetch`, `http_body_release`,
`http_result_status` and the result/storage structures. This is an application library, built as `libhttp.a`,
outside the SDK runtime. Its parser dependency is the pinned
[picohttpparser port](../../ports/picohttpparser/README.md), exported separately at
`build/ports-dev/picohttpparser`. Consumers also link userland's `libtls.a` and
the configured Mbed TLS libraries from `build/ports-dev/mbedtls`, in the order
supplied by that export's `share/mbedtls.mk`. All consume the same SDK.
There are no kernel changes or new libc operations.

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

## Request and response policy

One fetch sends one HTTP/1.1 GET with Host, Connection: close and
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
rejected. Redirects are never followed, including HTTPS locations. There is no
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
the fetch. Failed fetches free partial bodies and abort the stream, preserving
their first failure.

The deliberately strict subset requires CRLF and rejects folded fields, duplicate
or comma-list Content-Length (even identical values), conflicting length/transfer
encoding, and transfer codings other than a single `chunked`. HTTP/1.0 transfer
encoding is rejected. Informational responses and 204 cannot carry body-framing
fields. Status 101 is unsupported. Content-Encoding must be absent or a single
`identity`. Duplicate Content-Type/Content-Encoding are rejected. Content-Type is
optional descriptive printable ASCII metadata, at most 127 bytes; excess is a
limit failure. Trailers cannot supply framing, content representation, Host,
Connection or Trailer fields.

## Bounds, storage and lifetime

| Resource | Limit |
| --- | --- |
| Complete URI | 2,048 bytes |
| Aggregate response headers and trailers, including informational responses | 32 KiB |
| Aggregate fields | 256 |
| Informational responses | 8 |
| Each chunk size/extension line | 8 KiB |
| Completed body | 16 MiB |
| Shared body-storage reservations | 64 MiB |
| Overall fetch time | 30 seconds, capped by an earlier caller deadline |

The unmodified parser also rejects excessive chunk framing overhead: once
accumulated overhead reaches 100 KiB, streams dominated by overhead can fail its
ratio bound. Tiny-chunk responses can therefore be rejected below the body limit.
Parser/request scratch is fixed and bounded separately from body storage; the
large receive buffer and field array live on the heap.

The deadline starts before URI processing and covers DNS, connect, send and the
TLS handshake when applicable and the complete response. Existing DNS attempts retain their three-second maximum and
at most two attempts, capped by this same absolute deadline. No retry,
informational response or successful short transfer refreshes it. CPU-side work
is checked before returning success as well.

The caller zero-initializes a `http_storage` account and keeps it alive until all
associated bodies are released. A successful result owns its bytes and capacity;
`http_body_release` frees them and returns the reservation. The account is for
single-task use. Copies of the C structure do not create additional ownership.
Release an existing successful result before reusing its output structure.

Fixed-length bodies reserve the declared size. Unknown lengths grow in bounded
powers of two, retaining the old allocation/reservation until the new allocation
exists and the bytes are copied. The 64 MiB ceiling counts that temporary overlap
as well as previously returned bodies. Allocation can fail below the ceiling;
there is no eviction. An empty body requires no body allocation. Every failed
fetch discards staging, returns its reservation and aborts/closes network state.
No body is exposed before success.

## Snapshot lifetime and scheduling

The provider has 63 snapshot slots, including empty snapshots, plus one OPEN
service export. A slot is reserved through retirement acknowledgment. Returned
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
There is no kill operation or supervisor.

One provider task fetches synchronously. While fetching, reads of existing files,
new opens and retirement processing wait. The supplied caller deadline bounds
network phases; otherwise one fetch has a 30-second budget. Cancellation notices
cannot interrupt a blocking fetch immediately. Expiry is checked by the fetch
library and a canceled reply is finished without terminating the service. This
is not a bound on the total wait for ordinary file helpers, which supply no IPC
deadline. No worker processes, threads or wait-set facility are introduced.

## Operation errors

Native OPEN returns transport/validation failure separately from a valid provider
operation error. Only success transfers a FILE grant. The provider maps errors to
existing native statuses; libc uses its ordinary translation, without HTTP logic.

| Condition | Native result | libc errno |
| --- | --- | --- |
| HTTP 404/410 or DNS NXDOMAIN | NOT_FOUND | ENOENT |
| HTTP 401/403 or missing OPEN authority | DENIED | EACCES |
| Writable open through the OPEN_READ-only binding | DENIED | EACCES |
| Redirect, unsupported success status, unsupported HTTP feature | BAD_OPERATION | ENOTSUP |
| Other rejected HTTP status, malformed/truncated response or other DNS answer failure | IO | EIO |
| Invalid URI syntax | BAD_REQUEST | EINVAL |
| Fetch/call deadline expiry | TIMED_OUT | ETIMEDOUT |
| URI/header/body limit | FILE_TOO_LARGE | EFBIG |
| Aggregate body storage limit | QUOTA | EDQUOT |
| Snapshot slots or IPC admission full | QUEUE_FULL | EAGAIN |
| Allocation failure | NO_MEMORY | ENOMEM |

Other native network errors retain their existing translation. Final HTTP status,
when available, survives provider failures as diagnostic metadata; a transport
failure does not promise a provider reply. Failed opens return no partial file.
There is no automatic retry, reconnection, rebinding or redirect following.

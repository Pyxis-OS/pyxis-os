# Userspace HTTP and HTTPS fetching

`httpfs` implements [typed HTTP resources and transactions](../interfaces/http-transactions.md).
READ returns a completed, re-readable FILE; WRITE or READ|WRITE returns a staged
POST transaction whose COMMIT returns the same snapshot representation. REMOVE
sends DELETE under independently delegated authority. TLS stays in the provider;
ordinary C/Lua FILE consumers do not link TLS or receive network grants.

One instance serves distinct HTTP, HTTPS and JSON root exports. Trusted startup
selects their names and masks; renaming a root does not change its scheme or
representation. HTTP remains usable when optional TLS setup fails. Publication
performs no fetch, and local files remain usable without networking. The
[TLS contract](https.md) describes verification, trust provisioning and memory
accounting.

**Pyxis must be able to hit good network targets overall in the future.**
Track the end-to-end HTTP/TCP gap against the ThinkPad reference of 2026-10-09,
70.5 MiB/s send and 85 MiB/s receive; these are
[measurements](../development/network-throughput.md), not a contract or nested-QEMU
target. Snapshot OPEN includes complete arrival; measure OPEN and whole
consumption separately. Completed RAM reads avoid provider round trips;
receive-before-read remains serialized. See [qualification and remaining limits](#qualification-and-remaining-limits).

## Reusable endpoints

`http prepare` returns a configured capability through shell capture:

```text
api=$(http prepare PATCH http://10.0.2.2:18080/resource --headers-file home://api.headers --timeout 60)
reply=$(printf '{}' > $api)
cat $reply
cat $api
rm $api
```

The last two commands perform GET and DELETE independently; DELETE needs a root
with REMOVE. Each write creates a fresh PATCH transaction and independent reply.
POST and PUT are also supported. Prepare does no network I/O. Header input is a
finite native FILE of CRLF fields, bounded together with the target and prefix
by 4096 bytes, not a text variable or literal credential argument. Missing source
rights remain denied: stock HTTPS is read-only and JSON roots omit REMOVE.
The timeout starts anew on each OPEN/REMOVE (including WRITE staging), not at
preparation. The endpoint retains the root's frozen trust; header-file credentials
and per-root trust are an [interim design](https://git.internal/PyxisOS/pyxis-os/issues/1452).
See the [configured endpoint contract](../interfaces/http-transactions.md#configured-endpoints)
and [command usage](../../userspace/http/README.md). Unsupported capability output
fails without printing a URI/byte substitute.

## Use and startup

Build with `make -j16 image`, then enable networking explicitly:

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

For host fixtures, run `python3 -m http.server 18080 --bind 127.0.0.1` in their
host directory. QEMU user networking exposes that listener at `10.0.2.2`.

Development and read-only init request `session --start-services
boot://init-services`, and installed init `system://config/init-services`. That
trusted script publishes one instance with declared roots:

```text
service start --root json-http=json+http --optional-read-only-root https=https --optional-read-only-root json-https=json+https http boot://httpfs.pxb
```

The HTTP root permits GET, POST and REMOVE; HTTPS is read-only. JSON roots retain
their declared read/write masks and omit REMOVE. JSON selects request header
defaults without validating JSON. Each root has its own controller, so replacing
one leaves the other roots attached to the original instance. Idle spaces start
no provider; ordinary session invocation does not republish existing services.

Trusted startup supplies an explicitly private RAM or npfs backing leaf, then
consumes the backing source before entering the interactive shell. Without that
backing, snapshot GET and POST are unavailable. `--staging-cap BYTES` configures
the RAM POST body cap, default 16 MiB. GET snapshots have no provider byte
quota: private RAM and npfs backing grow until actual capacity or file
representability is exhausted. See [backing and limits](../interfaces/http-transactions.md#private-backing-and-limits).

`--https` selects HTTPS for the default role. `--ca-bundle URI` also selects
HTTPS for that role and loads an explicit custom augmentation of public trust;
additional HTTP roles must be declared explicitly. Trust loading uses native
read-only files, never a remote provider. One runtime serves the instance;
redirects never acquire another root's trust settings. Replacing a root does not
retarget retained results. See [trust configuration](https.md#trust-configuration-and-updates)
for the setup and restart contract.

## Failure reasons

`cat` and shell resource redirects/transaction completion print the retained
provider detail as well as the coarse failure. For example:

```text
cat: https://example.invalid/: Permission denied: Certificate verification failed: expired
```

TLS peer alerts are named when available, such as `protocol_version (70)` or
`handshake_failure (40)`. The latter does not identify whether cipher, signature
or group negotiation failed. Connection reset and deadline failures are reported
separately. Diagnostics are bounded human text, not a machine-readable error API;
[status/outcome mapping](../interfaces/http-transactions.md) is unchanged.
`fopen`, `errno`, `strerror` and `perror` do not acquire a hidden error string or
print automatically. Other programs can retain details explicitly through
[`pyxis_fopen`/`pyxis_describe`](stdio.md).

## Library and authority

[`userspace/libhttp/http.h`](../../userspace/libhttp/http.h) declares the one-hop
GET/POST/PUT/PATCH/DELETE engine. It is an application library, built as `libhttp.a`, outside
the SDK runtime. Its pinned [parser](../../ports/picohttpparser/README.md) and
[Mbed TLS dependencies](../../ports/mbedtls/README.md) use the same SDK. The kernel
handles native capabilities and endpoint transport, without interpreting HTTP.
Shared HTTP-only types live in
[`http_types.h`](../../userspace/include/http_types.h).

A caller supplies borrowed TCP, UDP, random and monotonic-clock capabilities and
a numeric DNS server through `http_client`. Its explicit scheme must match the
URI. Numeric HTTP IPv4 destinations skip DNS and need no UDP/random authority.
HTTPS additionally borrows a ready runtime with frozen trust. Keep the runtime,
capabilities and caller-owned response alive until the engine closes. The library
performs no startup lookup, trust-file loading or request logging. Network grants
follow existing policy; a provider root is not a destination sandbox.

## Incremental GET

`http_get_open` performs one synchronous GET through headers. `http_stream_read`
then copies at most 4 KiB into a positive-capacity buffer. The engine owns bounded
scratch and imposes no cumulative body allowance. It separates HTTP status from
pending, complete and failed framing; DATA or a received prefix never proves
completion. Terminal failures retain their first error and delivered-byte count.
Closing unfinished input records abort and releases its transport.

The provider uses `http_request_start`, `http_stream_step` and native completions
to spool into private backing. A pending operation must drain before engine
close; `http_stream_close` returns BUSY while one remains. These incremental
library interfaces do not expose a live response through provider OPEN or COMMIT.

A zero deadline selects 30 seconds; an explicit absolute deadline may be longer.
DNS, connect, upload and response share it. There is no reconnect or request retry.
The [incremental-GET experiment](../development/experiments/http-incremental-get.md)
records its own revision and configuration.

## Fixed-body requests

`http_request_open` and the cooperative interface accept GET, POST, PUT, PATCH or
DELETE plus copied header spans. GET and DELETE have no body. Write methods borrow
a native READ FILE and its exact frozen length, reading from offset zero without
changing a shared
position. The caller owns freezing; cooperative callers retain the source through
engine close. The provider's staging and COMMIT enforce this lifetime for clients.
Source authority/size is checked before connecting, and later failures retain
`source_status`.

Request headers are bounded to 32 KiB and 256 fields including generated fields.
Names are HTTP tokens; values reject NUL, CR/LF and controls except HTAB.
Case-insensitive duplicate names refuse. The engine owns Host, Content-Length,
Connection and Accept-Encoding; transfer/content encoding, Expect, trailers,
upgrade and proxy connection controls are unsupported. Owned fields and upload
scratch are erased on release.

The upload loop receives responses between send attempts. Final headers stop an
incomplete upload. `request_started` means sending was attempted; `request_sent`
and `upload_bytes` describe known local acceptance, never remote execution.
Failure after starting can leave the remote outcome unknown even with zero
reported bytes. This engine follows no redirects or replay policy; the provider
owns explicit redirect handoffs. Mbed TLS 4.1.1 can require output while reading:
ordinary early responses work, but a read that attempts TLS output while an
application write is pending fails explicitly, without replaying accepted
plaintext. A bounded ciphertext queue is a later alternative if a real server
needs it. See the [fixed-POST qualification](../development/experiments/http-fixed-post.md).

## Request and response policy

Each hop uses HTTP/1.1, Host, Connection: close and Accept-Encoding: identity.
URI syntax accepts ASCII DNS names or numeric IPv4, an optional decimal port,
path and query. Credentials, IPv6 literals, malformed escapes and invalid raw URI
characters refuse. Empty paths become `/`; fragments are checked but never sent.
Outgoing encoded path/query bytes are preserved.

HTTPS requires a DNS name. Verification and SNI use that name without its port
or terminal DNS dot; Host retains the original authority. DNS selects an address,
not the verified identity. Chain, name and validity checks precede application data.

Responses use HTTP/1.0 or HTTP/1.1. Final 2xx succeeds after complete framing;
204 is empty, and a nonempty 205 fails. 404/410 maps to NOT_FOUND, 401/403 to
DENIED, and other final HTTP failures to IO_ERROR. Error bodies are bounded,
independent immutable native snapshots, never successful response content.
Generic descriptions expose outcome, human detail, media type, length and
complete/re-readable state. HTTP codes belong in detail or private observation,
not generic machine fields.

Fixed bodies finish at their declared length; chunked bodies require payload
delimiters, the zero chunk and validated trailers. Plain close-delimited HTTP
accepts orderly TCP EOF, which cannot prove the origin intended no more bytes.
Close-delimited HTTPS requires authenticated TLS EOF; underlying TCP EOF is
truncation. Complete fixed/chunked/204 responses do not wait for peer shutdown.
TLS close notification after valid framing is diagnostic, but native handle-close
failure invalidates completion.

The strict subset rejects folded fields, duplicate or comma-list Content-Length,
conflicting length/transfer encoding, HTTP/1.0 transfer encoding and transfer
codings other than a single `chunked`. Informational responses and 204 cannot
carry framing fields. Status 101 is unsupported. Content-Encoding is absent or a
single `identity`; duplicate Content-Type/Content-Encoding refuse. Content-Type
is optional printable ASCII, at most 127 bytes. Trailers cannot supply framing,
representation, Host, Connection or Trailer fields. There is no decompression,
connection reuse, cache or TLS-failure fallback to plaintext.

## Redirect chains

The provider follows 301/302/303/307/308, with at most ten redirects and a visited
URL check before contacting each target. Keys normalize scheme/host, effective
port, empty path, unreserved escapes and dot segments; query order remains
significant and fragments are ignored. Comparison never rewrites outgoing bytes.
Exactly one valid nonempty Location is required, with relative, query-only and
scheme-relative resolution against the current hop.

301/302/303 switch to GET and drop the body; 307/308 preserve the method and reuse
the frozen POST source only for that explicit redirect. DELETE follows the same
method policy. The old engine's pending source operations and transport cleanup
drain before the next engine starts. Redirect bodies are neither retained nor
drained. No transport error authorizes replay, reconnect or rebinding.

HTTP→HTTPS is allowed; HTTPS→HTTP is refused before target contact. Public trust
permits cross-origin following. Custom public+custom trust stays origin-bound,
including otherwise publicly signed destinations. The provider keeps its one
explicitly provisioned runtime and never discovers destination bindings or trust.
Authorization, Cookie and Proxy-Authorization are removed after an origin change,
stickily through the remaining chain. An explicitly credential-marked private
header block is also discarded on crossing. Request fields are erased on release.

One absolute deadline covers staging, every hop and final spooling: 30 seconds by
default, with explicit longer deadlines allowed. Aggregate headers/trailers,
fields and informational-response bounds cover the chain. No hop or short
transfer refreshes them.

## Bounds, storage and lifetime

| Resource | Bound |
| --- | --- |
| Complete URI / Location reference | 2,048 bytes |
| Aggregate response headers and trailers | 32 KiB |
| Aggregate response fields / informational responses | 256 / 8 |
| Each chunk size/extension line | 8 KiB |
| Request wire headers / fields per hop | 32 KiB / 256 |
| RAM staged POST body | 16 MiB by default |
| GET snapshot backing | Actual RAM/filesystem capacity and representable file size |
| Operation deadline | 30 seconds by default; explicit longer absolute deadline allowed |

GET bodies spool in bounded chunks to private native files without an aggregate
provider byte budget. Actual allocation or filesystem exhaustion fails without
publishing a prefix, eviction or alternate-storage fallback. Integer overflow
and complete framing checks remain. Copy buffers, parser scratch, TLS and
bounded native error bodies also consume actual memory.
Snapshots are frozen, unlinked private native files exported with READ only;
clients receive no backing-directory or backing-WRITE authority.

## Snapshot lifetime and scheduling

OPEN and COMMIT publish only after framing, spooling and cleanup finish. Copies
retain the same backing; positioned reads and SIZE are stable, while distinct
opens fetch independently. Backing remains allocated until final result retirement.
Removing or replacing a name leaves held results attached to their original
instance. Provider exit/cut fails later operations; independently captured native
error-body bytes retain their separate survival contract.

One active exchange includes preparation/staging through completion or drained
abort. Other submissions report BUSY; aliases share the process gate. Existing
results can still be read, subject to serialized native completion admission.
There is no queue, cache or automatic replan. Export slots remain reserved until
RETIRE acknowledgement. Existing synchronous DNS/entropy waits remain bounded
at 3 s/5 s and can delay STATUS/ABORT; native completion paths handle other
supported blocking phases. Ordinary FILE helpers still supply no IPC deadline.

## Operation errors

Libc maps native resource outcomes to errno: NOT_FOUND→ENOENT, DENIED→EACCES,
IO_ERROR/UNCERTAIN→EIO, TOO_LARGE→EFBIG and ABORTED→ECANCELED. A lost delivered
mutation reply remains uncertain and is never replayed. Failed OPEN returns no
successful partial FILE. Native failure inspection can retain real independent
error-body handles; see [stdio](stdio.md#typed-resources-and-transactions).

All clients use typed OPEN and generic transactions. There is no public
continuation PREPARE, HTTP-only libc adapter, observation companion or interim
shell table. `http prepare` configures the same provider engine; retained shell
responses use ordinary typed captures. Links explicitly requests the
[navigation representation](../interfaces/http-transactions.md#navigation-representation)
for final-URL metadata, without changing generic describe. General capability
pipes remain C9. Historical
[redirect qualification](../development/experiments/http-redirects/README.md)
records its own implementation and does not qualify a later revision.

## Qualification and remaining limits

Milestone B closed on 2026-10-16. Ordinary builds and QEMU qualification, exact
revisions and separate native measurements are recorded for
[snapshots](../development/experiments/capability-http.md),
[configured endpoints](../development/experiments/capability-http-prepare.md) and
[retired adapters/scripts](../development/experiments/capability-http-retirement.md).
The owner-native B8 A/B measured whole 1 MiB GET 13.743 → 13.113 ms
(72.8 → 76.3 MiB/s) and POST 34.338 → 31.390 ms, with unchanged native/RAM controls.
These qualify their recorded revisions, not every backing or future consumer.

[Credential storage](https://git.internal/PyxisOS/pyxis-os/issues/1452),
[throughput](https://git.internal/PyxisOS/pyxis-os/issues/1451),
[responsiveness](https://git.internal/PyxisOS/pyxis-os/issues/1453),
[single-exchange admission](https://git.internal/PyxisOS/pyxis-os/issues/1455) and
[staging-worker cost](https://git.internal/PyxisOS/pyxis-os/issues/1486) remain limits.
HTTP/2, SSE, compression, proxies and general credential storage are not supplied.
Git negotiation, source hash verification and consumer-specific budgets belong
to their consumers. General capability pipes remain in
[milestone C](../wip/capability-model-implementation.md#milestone-c-capability-pipes).

# HTTP resources and transactions

`httpfs` implements [typed OPEN and REMOVE](file-providers.md), over the same
native endpoint transport as other providers. HTTP/TLS stay in userspace.
A caller's environment selects an explicitly published root; the provider
receives the opaque target, not that environment name.

## Roots and authority

One instance serves distinct HTTP, HTTPS, JSON/HTTP and JSON/HTTPS root exports.
Trusted startup declares the role, destination name, rights and optionality before
launch. Successful SLOT/controller pairs publish in one environment batch;
the provider cannot choose additional names. Each slot has its own controller:
replacing `http` leaves `json+http` unchanged. Optional TLS roots may be omitted
on TLS setup failure while HTTP publishes. One root's retirement does not stop
other roots/results. Failed multi-root publication drains the unpublished leaf.
Ordinary named single-root service launch keeps its existing policy.

OPEN_READ permits GET; OPEN_WRITE permits creating POST transactions. REMOVE
is independently delegable and never implied by WRITE. Stock HTTP is writable;
HTTPS remains read-only. JSON roots preserve declared read/write masks but omit
REMOVE. JSON selects Accept/Content-Type defaults, does not validate JSON or add
authority. A renamed root retains its scheme/representation and masks.

READ returns a completed re-readable FILE. WRITE or READ|WRITE returns a generic
TRANSACTION with independently attenuable STAGE, COMMIT and ABORT. CREATE/TRUNCATE
select staged body replacement, without remote filesystem existence promises;
APPEND/EXCLUSIVE are unsupported. REMOVE sends DELETE; a successful 2xx means
server acceptance, not proof of immediate remote deletion. Directory removal and
name removal through a held FILE are unsupported.

## Configured endpoints

`http prepare GET|POST|PUT|PATCH URI` captures one inherited root and issues an
immutable PROVIDER in that same instance, without network I/O or reserving the
active exchange. GET requires OPEN_READ; write methods require OPEN_WRITE. The
new grant retains only the captured
root's READ/WRITE/REMOVE masks and lineage. A renamed root keeps its actual role.
Stock HTTPS therefore cannot prepare a write endpoint; JSON roots still omit
REMOVE. Preparation cannot substitute a broader root.

In `reply=$(producer > $endpoint)`, the capture coordinator stages through the
existing bounded worker. Producers get a pipe; the worker receives only the
transaction's writable FILE. The shell keeps STAGE/COMMIT/ABORT, waits for every
pipeline stage and its temporary subgroup, then commits once and hands the typed
response to capture. Producer failure aborts; failed/uncertain outcomes are failed
values, never an empty successful FILE or a retried request. Publication failure
preserves the old binding. This bridge covers nonappend final stdout captures
with a held PROVIDER (at most seven producer stages plus worker); it does not
add general capability pipes. Uncaptured transaction output uses the same worker,
retirement and commit checks, discarding a successful response.

The HTTP-specific operation `HTTP_PREPARE_ENDPOINT` in
[`abi/http_prepare.h`](../../include/abi/http_prepare.h) copies a 48-byte prefix
(method, response flags, duration, issuance deadline, target size, header size), followed by the
opaque root tail and CRLF header fields, within the 4096-byte endpoint payload.
Its typed reply carries one owned PROVIDER. Generic OPEN/REMOVE are unchanged.
The SDK helper `http_prepare_endpoint` borrows the root and returns an owned typed
result; no legacy PREPARE or generic OPEN fallback exists.

A configured endpoint accepts only empty-tail OPEN/REMOVE: READ is GET, WRITE
creates a transaction using its selected method, REMOVE is DELETE if permitted.
Suffix traversal and reconfiguration fail. Each use copies settings into its own
transaction; replies are independent completed snapshots, with no last-response
slot. PUT/PATCH use the same bounded native body and redirect rules as POST.
Existing export admission bounds configuration metadata (CALL_LIMIT); configuration
alone never reports BUSY, while executions retain single-exchange admission.
Final endpoint retirement erases its private settings without aborting independent
transactions/results. Root CUT and provider exit retain the ordinary lineage rules.

`--headers-file PATH` accepts only a finite native READ FILE, including a held
FILE capability; TEXT, provider and stream inputs are refused. Copy once, require
exact length/EOF and validate fields; reject partial/oversized input. No literal
header/token switches. Caller fields form a private credential block erased on
release and stripped on cross-origin redirects; nonsecret JSON defaults remain.
Describe exposes no header bytes. Trust is the captured root's frozen instance
profile. This is [interim credential storage](https://git.internal/PyxisOS/pyxis-os/issues/1452),
accepted by the owner on 2026-10-15, not the final credential design.

`--timeout SECONDS` is a nonzero integer duration, default 30 s. Each READ/REMOVE
starts a fresh deadline at admission; WRITE starts at OPEN, including staging.
Use the earlier of now plus duration and the caller's nonzero absolute deadline;
overflow fails. An endpoint issued with `HTTP_ENDPOINT_IDLE_TIMEOUT` reads the
duration as an inactivity window instead: the deadline moves forward to the last
data received plus the window, so only a stalled transfer fails and a slow one that
stays alive is bounded only by the caller's absolute deadline. A response that has
been received and is draining to staging keeps the window open per storage step. No creation-time expiry. Issuance has a separate bounded private
call; native header I/O keeps its native waits. All three defaults were accepted
by the owner on 2026-10-15.

## Private backing and limits

Trusted startup supplies a separately declared native RAM or npfs leaf, outside
client roots/cwd/aliases, and drops ancestor authority. A hidden name is not
privacy. The provider creates exclusively and unlinks before use; clients never
receive native backing-directory or backing-WRITE authority. Snapshots export
READ only and are frozen after framing. Unlink/close does not promise disk erase.
Missing backing makes snapshot GET/POST unavailable; there is no streamed or
alternate-storage fallback.

GET snapshots have no provider-imposed byte budget for either RAM or npfs.
Their private native files grow until actual backing capacity or representable
file size is exhausted. Memory and storage failures are reported with their
native cause; integer overflow still refuses without publishing a prefix,
eviction or fallback. RAM POST bodies retain their separate 16 MiB upload policy.
Metadata, copy buffers, TLS and bounded native error bodies use actual memory.

Data spools in bounded chunks into the private backing. OPEN/COMMIT publishes only
after complete framing and cleanup; a truncated body or deadline is a failure,
not partial successful content. Capturing the re-readable result holds it without
another A5 byte-retention charge. Backing remains allocated until the last
result retires. Copies share backing, while distinct opens are independent.

One active exchange includes preparation/staging through framing or drained abort.
Other submissions report BUSY; there is no queue or automatic replan. Native
completion admission remains one operation per process. Existing synchronous DNS
and TLS entropy waits remain bounded at 3 s/5 s; this task adds no concurrency.

## Redirects, failures and lifetime

The provider follows 301/302/303/307/308, at most ten redirects with visited-URL
loop detection and relative/scheme-relative resolution. 301/302/303 switch to GET;
307/308 preserve the method and frozen body. Credentials are removed after an
origin change, stickily through the remaining chain. Recognized credential fields
are always stripped; an explicitly credential-marked private header block is
also discarded on crossing. Request fields are erased on release, never logged.

HTTP→HTTPS is allowed; HTTPS→HTTP is refused. Custom trust stays origin-bound.
One explicitly provisioned TLS runtime serves the instance; redirects never
acquire caller/destination bindings or trust. One deadline (a 30 s total by default,
explicit longer values allowed; an inactivity window for an idle-timeout endpoint) covers staging, all hops and spooling, with
aggregate framing bounds. No transport-error retry or uncertain mutation replay.

Final 2xx succeeds. Invalid framing still fails, including nonempty 205 and
forbidden framing on 204. Both refuse surplus bytes already buffered when their
bodyless framing completes. A framed response does not wait for TCP close to
look for later surplus: a peer can keep the connection open after a complete
message. The transport is closed rather than reused. Close-delimited TLS still
requires authenticated EOF. 404/410 maps to NOT_FOUND, 401/403 to DENIED; other final
HTTP failures map to IO_ERROR. Generic descriptions contain outcome, human detail,
declared media, length and complete/re-readable flags; HTTP status is detail text,
not a generic machine field. Bounded error bodies are independent immutable native
snapshots, never ordinary successful response content.

Per-request TLS failures publish bounded human detail from verification flags,
Mbed TLS errors and the last received fatal alert. Certificate expiry, future
validity, name mismatch, trust/signature and reported revocation failures remain
separate reasons; transport reset/timeouts are identified as transport failures.
Native network failures name refused/reset connections, missing routes,
timeouts, unavailable endpoints and resource/admission failures, with their native
status number; unmapped statuses retain a generic label and that number. The
generic resource outcome remains IO_ERROR, so pkg may report, for example,
`Connection refused (native status 22) (fetch status 19)`. If pkg's GET call itself
times out before a provider description arrives, its detail is `HTTP GET timed
out` with native fetch status 17. These diagnostics add no retries or fallbacks.
Unknown library errors retain their numeric code. A generic peer handshake alert
does not establish which cipher, signature or group was incompatible. No peer
certificate text or request credentials enter these diagnostics. Native statuses
and resource outcomes are unchanged; certificate/trust failures remain DENIED.

Accepted reporting policy (chronium, 2026-10-25): `cat` prints retained provider
detail on failed OPEN/read; the shell prints it for failed resource redirection
and transaction completion, before releasing the result. Captures retain the
failed value for their consumer. Detail restrictions still apply, and terminal
output replaces non-printable/non-ASCII bytes with `?`, as `describe` does.
Standard libc error reporting stays unchanged; programs opt into
[`pyxis_fopen`/`pyxis_describe`](../userland/stdio.md) to inspect detail.

Known private RAM uses bounded native reads/writes. Completed successful RAM
snapshots attach their sealed backing to the exported FILE's kernel description;
client READ copies locally without a provider round trip. The export retains
quota lifetime, revocation, descriptions and retirement; the provider collects the
kernel byte counter at retirement. Persistent snapshots keep
provider READ. Frozen reads allocate nothing; writes allocate/copy locally on the
calling CPU. No native backing grant escapes to clients; staged writes finish before engine reads, and snapshot writes
finish before publication. Quota is reserved before growth and deadlines remain
absolute. Persistent backing, directory operations, resize and engine source reads
use native completions.
Successful responses do not allocate diagnostic error-body storage.

COMMIT freezes writers, drains admitted writes and submits once; explicit flush,
final owning close and clean exit follow [libc's transaction rules](transactions.md).
Abort cannot undo a remote side effect. Retiring pending control abandons it;
lost delivered replies remain uncertain. A settled ABORT answers immediately.
Native operations drain before backing is released. Export IDs are not reused
until RETIRE acknowledgement; publication to a retired/clientless facet is normal
cleanup, not fatal to the shared provider.

Opened results stay attached to their original instance across name replacement
and shell exit. Provider exit/cut fails later operations; only independently
captured native error-body bytes retain their A3 survival contract. There is no
response cache or successful partial snapshot.

## Navigation representation

Links opts in through GET configuration with `HTTP_ENDPOINT_NAVIGATION`.
The completed FILE starts with a little-endian 32-bit metadata length (at most
4096), then UTF-8 JSON containing `effective_url` (at most 2048 bytes),
`media_type` (at most 127) and integer `status` (100–599),
then the raw response body. All encoded bytes count against the same private
backing budget. Consumers validate framing, UTF-8, JSON and required fields;
unknown or invalid metadata never supplies a false final URL. Failed outcomes
remain failures, with any framed bytes available only as an error body.
Generic describe stays URI-free.

The shared SDK helper captures the root once. Only a delivered explicit
`CALL_BAD_OPERATION` with an empty reply and no grants permits ordinary READ
through that same held root. Unsupported configuration must refuse without
effects. Every other failure is final: no retry, relookup or silent downgrade.
Native FILE/directory/text paths use ordinary typed OPEN directly.

Public continuation PREPARE, STATUS/METADATA, HTTP-only libc options and stream
companions, and the `=>`/`@`/`request` shell coordinator are removed. Configured
endpoints and ordinary typed captures replace them. General capability pipes
remain C9.

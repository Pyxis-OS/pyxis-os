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

## Private backing and limits

Trusted startup supplies a separately declared native RAM or npfs leaf, outside
client roots/cwd/aliases, and drops ancestor authority. A hidden name is not
privacy. The provider creates exclusively and unlinks before use; clients never
receive native backing-directory or backing-WRITE authority. Snapshots export
READ only and are frozen after framing. Unlink/close does not promise disk erase.
Missing backing makes snapshot GET/POST unavailable; there is no streamed or
alternate-storage fallback.

RAM defaults to a configurable **64 MiB aggregate logical-byte budget per
provider**, shared by staged uploads and retained snapshots, including holes.
Reserve before growth; exactly at the budget succeeds. Overflow is TOO_LARGE
without a published prefix, eviction or fallback. POST retains its separate
16 MiB RAM body cap. Metadata, copy buffers, TLS and bounded native error bodies
are additional charges; the configured numbers are tuning, not ABI constants.
Private npfs is the explicit large-transfer option, bounded by actual storage and
representability rather than a transport body cap.

Data spools in bounded chunks into the private backing. OPEN/COMMIT publishes only
after complete framing and cleanup; a truncated body or deadline is a failure,
not partial successful content. Capturing the re-readable result holds it without
another A5 byte-retention charge. Provider storage stays reserved until the last
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
acquire caller/destination bindings or trust. One absolute deadline (30 s default,
explicit longer values allowed) covers staging, all hops and spooling, with
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

Known private RAM uses bounded native reads/writes: frozen reads allocate nothing,
and writes allocate/copy locally on the calling CPU. Only this worker holds the
native backing; staged writes finish before engine reads, and snapshot writes
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

## Interim adapters through B7/B8

Private PREPARE and STATUS/METADATA still support the shipped libc options and
`=>`/`@`/`request` coordinator. They use the same completed FILE/transaction engine,
with no legacy OPEN wire, REDIRECT result or client-side redirect following.
PREPARE respects its selected root's initial scheme/JSON default. STATUS is an
independent read-only observation; explicit FILE stdin companions preserve it
through SDK/libc/Lua/libuv launch, without name inference or extra body/control
rights. Generic FILE access needs no HTTP observation to prove framing: it was
complete before publication. METADATA's final URL/status remain private adapter
information, never generic facets. Unknown redirect counts are not fabricated.

B7 owns reusable configured endpoints and `http prepare`; B8 retires interim
shell/client spelling adapters and integrates standalone scripts. General
capability pipes remain C9.

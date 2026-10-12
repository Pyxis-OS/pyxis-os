# Native HTTP requests

Status: **shared milestone accepted 2026-10-11; tasks 1–2 delivered; task 3 facets/lifetime accepted; native completion prerequisite delivered for review, 2026-10-12.**
Extracted from [Claude tasks 1a–1c](claude-on-pyxis.md#track-1-delivery-tasks-after-approval)
and [Git's HTTP prerequisite](git-on-pyxis.md#smart-http-without-libcurl).
Their accepted one-shot transaction and credential rules carry forward. The
provider/adapter defaults are accepted, including streamed GET/POST without an
artificial response-size cap. The native completion contract is accepted too. Each implementation task needs its own owner go.

## Deliverable and consumers

Deliver prepared POST and streamed GET/POST through native provider grants,
ordinary shell streams and Lua file I/O. The end state includes
`printf '{}' > json+https://…`, reading that POST's response with `cat`, and a
multi-GB HTTPS download through the shell or Lua. Reading the URI again would
issue GET, not retrieve the POST response; retain its actual response grant.

Consumers are the [Claude harness](claude-on-pyxis.md),
[git-remote-https](git-on-pyxis.md#smart-http-without-libcurl), [pinned source fetch and the LAN gateway](source-builds.md#reaching-the-sources),
and the [ports runner](../development/ports.md#building-and-packaging).
None must implement a private HTTP client or finish its own milestone to obtain
this facility. Git negotiation, pin/hash verification, gateway deployment and
recipe execution remain consumer work. Anthropic SSE/tool-token streaming,
HTTP/2, compression, proxies, PUT/PATCH/DELETE, caching and general credential
storage are outside this first milestone.
HTTP/TLS stay in userspace; any shared stream/readiness contract is generic,
with no HTTP-shaped or POSIX-shaped kernel calls.

## Inspected starting point

Inspected main `7db4cb31`, userland `2bc56bf4`: [libhttp](../userland/http-fetch.md)
now streams GET/fixed-file POST with retained framing results and explicit long
deadlines. Its OPEN/upload path still waits through headers; the provider cannot
call it synchronously from its service loop. Task 3 must expose cooperative
progress between bounded network operations. [httpfs](../interfaces/file-providers.md#http-and-https-snapshots)
still returns complete FILE snapshots, with a 16 MiB chain allowance and 64 MiB
body storage. TLS has one active connection per runtime. Native FILE offsets/SIZE
and copied `pyxis_stdio_response` metadata remain unsuitable for a live response.

## Carried-forward accepted contract

PREPARE fixes endpoint, method, validated request headers, resource admission and
one absolute deadline, with **no network submission**. Read authority permits
GET, not POST; preparation/submission requires separately delegated authority.
Request-scoped headers must also work for GET, including Git-Protocol discovery.
POST returns a staged body WRITE grant and commit/status/abort control. Copies
refer to one transaction. The first commit atomically freezes every body copy
and starts at most one request; later commits/status observe without replay.
Body writers cannot commit, read credentials or obtain the response by writing.
Flush/close never submit; staging FILE_SYNC is explicitly unsupported, not a
POST trigger. The first shell slice has one direct staging writer: independent
FILE descriptor offsets do not coordinate concurrent writes. When no control
invocation is queued/in flight, natural control-export retirement abandons staging
and rejects further body operations even if writers survive. Closing one copied
handle does not revoke queued/admitted work or a control retained elsewhere.

Separate IPC errors, provider errors, submission certainty, HTTP status and
transfer completion. A fully framed 4xx/5xx retains its status and body as a
completed HTTP response. A lost reply after submission leaves the remote outcome
potentially unknown; abort may stop local work, never promise rollback/refund.
Provider death fails held operations, with no rebinding or automatic retry.

The provider owns Host/length/transfer framing and rejects conflicting,
duplicate or CR/LF-bearing headers. Request credentials enter via private native
preparation, are unreadable through returned grants and never enter argv,
environment, history, logs or global defaults. Retain credential bytes only for
their request and release them on cleanup. Credential-bearing requests cannot
redirect or downgrade. Existing anonymous GET redirect and CA policy remains;
POST does not redirect/replay automatically. Response Content-Type and selected
diagnostics may be observed without exposing request headers.

Preparation replies transfer real attached grants; shell bindings are private
capability storage, not numeric handles printed by a helper. Successful handoff
closes provisional client handles; partial handoff unwinds owned grants. Body,
control and response lifetimes follow exported-object retirement, including
in-flight receipts. Metadata/admission bounds and real memory/storage exhaustion
remain explicit failures. Claude's 64 KiB request/1 MiB response budgets are
consumer policy, not limits imposed on Git, GET or downloads.

## Task 3 accepted facets and lifetime

**All three defaults accepted by the owner, 2026-10-12.** This refines the
accepted HTTP contract. Task 5 uses the same request value; the parked
[capability environment](capability-environment.md) and
[nested groups](nested-execution-groups.md) get no implementation here.

### 1. Facets and values — separate exports, typed values, explicit copies

The prepared capability is an opaque control reference to one provider record:
fixed method/endpoint/validated headers, trust context, deadline, resource admission
and state. It grants no network/trust/backing-file or credential-read authority.

| Facet | Rights and behaviour |
| --- | --- |
| Control | Independently attenuable COMMIT/ABORT; repeated COMMIT observes, never resubmits |
| Body | Exported FILE WRITE, including SIZE/RESIZE; POST only; no READ; copies freeze together; SYNC unsupported |
| Response | Generic exported STREAM READ: shared forward consumption, no offset/seek/SIZE; failure is an error, never successful EOF |
| Observation | Separate STATUS-only export: phase, submission certainty, HTTP/framing status, counters and cleanup errors; no mutation/request headers |

PREPARE atomically returns control/body/observation (GET omits body), without
network activity. Existing read-only authority permits GET preparation; POST
needs separate preparation authority, never granted by an alias. First COMMIT
freezes and starts; its retained receipt owns opening work until its deferred
reply hands off a reader after headers, before the whole body, with observation.
That initial handoff is single-use; later readers copy a holder's grant. Later
COMMIT returns status, not another reader. Cancel before REPLY commits aborts
unfinished work and releases provisional readers; after successful REPLY, reader
retirement governs, even if collection fails. Never replay submission/data.

The native binding owns control and selected facets, adding response after handoff;
a response-only value owns reader/observation. Copying retains the same request
and cursor, not a new POST. Unset/replacement closes only that binding's references.
Launch copies selected narrower grants as named resources: producer gets body,
reader gets response/observation, monitor gets observation. Control inheritance is
opt-in and may retain staging past shell exit; ordinary children inherit nothing
merely because the binding exists.
No handle serialization, credential strings or session-wide request publication.
String getenv/startup stay unchanged; future typed lookup/configuration/revocation
remain the parked direction's questions.

Separate exports matter because [retirement](../interfaces/endpoints.md#exports-and-retirement)
counts all references, not rights: observers must not retain control. Even a
zero-right control copy retains its export. Providers authorize facets using
invocation rights, not payload masks, and drop provisional client handles after
handoff. Task 3 supplies native STREAM helpers; task 4 adds stdio/standard-stream support. Lost delivered
READ replies may consume bytes and fail that reader; framing observation cannot
turn its IPC failure into success. Ordinary FILE semantics stay unchanged.

### 2. Lifetime and admission — one reservation, references, fixed deadline

Reserve the instance's one transfer slot at PREPARE; other preparations refuse
until terminal framing/abort, with no queue/automatic retry. Bounded unread bytes
and observations may remain afterwards. Admission counts memory/storage and the
existing 64 live/unacknowledged exports; terminal references can exhaust it.
This exposes shared-instance contention even while a large body is being staged.

COMMIT closes mutation admission, rejects queued/new writes and drains mutations
already admitted by the provider before pinning length. Provider-observed failed
or uncertain mutations prevent submission. Lost client WRITE replies are not
always visible to it; producer/coordinator failure must also prevent COMMIT.
Private disk backing needs an explicitly delegated, isolated mounted directory:
exclusive create, unlink before writer export, retain native READ|WRITE privately.
Clients get no subtree access/writable alias. Missing provision refuses staging;
shared home/tmp names are not privacy. RAM staging is explicit/countable. Erase
owned credentials/scratch; unlink/close does not promise secure disk erasure.

Control retirement before COMMIT abandons staging even with writers/observers.
Closing one copy does not revoke others/admitted calls. After handoff, readers
retain the exchange; their final retirement aborts unfinished reception. Observers
retain small final state, not active work/backing storage. The original deadline
bounds staging/network/framing; completed bytes/status remain readable afterwards.
ABORT is idempotent local cancellation, not rollback; terminal framing is immutable.
Native work retains storage until drain, even after a receipt is finished; no reply
uses a finished receipt and ACK cannot precede safe reclamation. Provider death
fails facets; missing final status fails, never completes.

The provider is independently owned, not a caller's job descendant. Group stop
closes member grants, but outside holders can retain authority. Job/subtree cleanup
is not HTTP completion/rollback; the coordinator ABORTs and observes unfinished
work. Later nesting contains execution/delegation, without implicit HTTP authority.
The loop handles receiver work/retirement and TCP/deadlines under backpressure;
bound deferred receipts and leave room for status/abort. Cleanup has no fixed bound.

### 3. Cooperative I/O prerequisite — bounded native completion

[CONNECT](../../include/abi/tcp.h) waits for handshake; [UDP](../../include/abi/udp.h),
TLS setup and staging I/O also wait. TRY transfers alone do not make the provider
responsive. Keep one provider process and take the generic kernel/ABI prerequisite
from the owner's [endpoint completion direction](later-os-directions.md#completion-based-asynchronous-operations)
before task 3 implementation, rather than assuming threads/helper processes.

Its bounded first scope is TCP CONNECT, DNS UDP SEND/RECEIVE and the native
FILE/DIRECTORY staging operations, plus userspace DNS/TLS steps. Copy bounded
inputs/capture existing grants; one owned receiver collects each correlated final
result, with completion admission reserved before submission. Start with one
pending operation per instance under existing native bounds; TCP TRY/service IPC
continue independently. Cancel/receiver closure/exit must drain ownership and
cleanup, not drop a live receiver's completion or fabricate rollback.

**Owner exception to the planning freeze, accepted 2026-10-12:** this prerequisite
is justified now, limited to the scope above. Its contract below is accepted and
implementation assigned, 2026-10-12. The exception permits no general async rewrite
or additional per-operation capability API. The broader completion direction remains parked. Any synchronous
phase left must be reported; task 3 cannot claim responsive status/abort through it.

## Native completion accepted contract

All three defaults accepted and bounded implementation assigned by the owner,
2026-10-12; the prerequisite is delivered for review:

1. Copied submission through existing native authority, with operation/result,
   receiver delivery, returned-handle capacity and cleanup admission reserved
   before acceptance. One unacknowledged operation per process; a second valid
   submission returns `CALL_BUSY`, even through another receiver.
2. Authenticated results on the owned receiver, retryable RECEIVE after a short
   or invalid output buffer, and receipt CLOSE as acknowledgement. Readiness
   consumes nothing; returned grants remain independent.
3. Cancel/deadline/receiver-close/exit drain independent ownership and preserve
   actual late results and known progress. No implicit rollback, compensating
   deletion or replay. The process gate holds through uncollected drain or
   acknowledgement; a replacement receiver cannot bypass it.

The bounded first scope is TCP CONNECT, DNS UDP SEND/RECEIVE and private native
staging CREATE/REMOVE/READ/WRITE/RESIZE/SIZE on RAM/archive/npfs. HOST and exported
provider forwarding are excluded. See the [completion ABI and limits](../interfaces/native-completion.md),
[admission debt](../technical-debt.md#native-completion-per-process-admission) and
[QEMU qualification/cost finding](../development/experiments/native-completion.md).
The broader completion direction stays parked. Provider/DNS/TLS cooperative
integration and the HTTP fix below remain task 3 work.

**Task 3 implementation follow-up from [daybreak's #751 review](https://git.internal/PyxisOS/pyxis-os/pulls/751):**
fix `libhttp/fetch.c` accepting nonempty 205 Reset Content (the check covers only
204). Enforce zero decoded content and retain 205 on failure; qualify empty/nonempty
fixed, chunked and close-delimited cases. This is distinct from 204 framing
([RFC 9110 §15.3.6](https://www.rfc-editor.org/rfc/rfc9110.html#section-15.3.6),
[RFC 9112 §6.3](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)).

## Accepted owner decisions

All three provider/adapter defaults accepted 2026-10-11; delivery is tasks 3–5.

1. **Forward-only response stream plus read-only final observation.** Make the
   reader available after response headers, before completion, with bounded RAM
   buffering and backpressure. Pending data waits; only validated complete framing
   yields successful EOF. Failure after a readable prefix reports a read error
   and retained failed status, including deadline, provider death and resource
   exhaustion. Missing final status is failure, never completion. HTTP status
   remains separate from stream success and remote submission certainty.
   Give consumers observation without commit/abort rights. Use an honest native
   stream representation and libc adapter; ordinary FILE/SIZE semantics stay
   unchanged. Seeking/size is unsupported on the live stream, and copies share
   consumption rather than promising independent replay of discarded bytes.
   Consumers needing a seekable snapshot materialize into their own native file
   and publish it only after complete status. This explicitly replaces the old
   complete-only response FILE, no-prefix-publication and response-grant timing
   in Claude 1b/1c. It covers plain GET too.
2. **Freeze a provider-private staged file before transmitting large bodies.**
   Use explicitly delegated private writable storage for large POST bodies;
   `tmp://` is RAM and cannot be the multi-GB default. Producers get only the
   exported staging writer, never a writable alias to its backing. Commit pins
   its final length and streams from the frozen body, with no producer writing
   after submission. Keep small RAM staging optional under counted resources;
   no fixed total-body ceiling beyond representability and actual resources.
   Provision backing authority explicitly, never derive privacy from a filename
   or mount a shared scratch tree. Cleanup/uncertain storage operations report
   real errors. Revoke write access through transaction state, not fake success.
3. **Shell redirection is the explicit transaction coordinator.** A write URI
   prepares and stages; the shell commits only after every body-producing stage
   finishes successfully and staging has no unresolved error. On failure it
   abandons without sending. Retain response and status in private bindings for
   subsequent `cat`/status commands. Return without waiting for the entire response
   so a bounded buffer cannot deadlock before its reader starts. Last-stage
   pipeline status alone is insufficient. Native callers may delimit an RPC and
   commit while their child remains alive, as Git requires. Lua uses explicit prepare/commit/status;
   `"w+"` adopts the response reader at its start rather than reading the body
   or issuing GET. Ordinary `fclose` and editor saves remain non-submitting.

## Task 2 accepted TLS policy

**Option A accepted by the owner, 2026-10-12.** Allow ordinary early HTTP
responses during POST upload, but fail explicitly before transmitting if a read
attempts TLS output while an application write is pending. Retain identical
plaintext arguments across write retries; never replay accepted bytes. Stop
uploading on final response headers. If upload is incomplete or a TLS write is
pending, abort/free without close_notify so cleanup cannot flush unsent body.
A completely framed HTTP error response still completes independently of status.

In pinned Mbed TLS 4.1.1 `ssl_msg.c`, the TLS 1.2 HelloRequest path sends
NO_RENEGOTIATION through `mbedtls_ssl_send_alert_message`, which can flush an
existing application record. A later `ssl_write_real` retry can then rebuild the
same plaintext ([upstream record layer](https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-4.1.1/library/ssl_msg.c)).
The guard makes this control-message path an explicit TLS failure instead.
If a real server triggers it, revisit the later alternative: a separate bounded,
counted ciphertext queue, with proven control-record ordering and progress.

Overall deadlines use bounded native TCP waits. One TRY after a slice timeout
distinguishes transient WOULD_BLOCK from terminal timeout without INSPECT.
CONNECT remains one bounded attempt; there is no reconnect or POST replay.

## Ordered delivery tasks

- [x] **1. Incremental libhttp GET and final framing result.** Delivered
  `http_get_open`/`http_stream_read` with bounded DATA, retained completion/failure,
  no total-body allowance and authenticated TLS EOF. The snapshot collector
  preserves existing callers. [Interface and limits](../userland/http-fetch.md#incremental-get);
  [QEMU qualification and costs](../development/experiments/http-incremental-get.md).
  **After this task the owner can:** exercise the native engine against local
  slow/truncated and over-16-MiB GET fixtures and inspect chunks/final results.
- [x] **2. Fixed-body POST and overall deadlines.** Delivered bounded native
  FILE reads, validated copied request headers, early final responses and retained
  HTTP error bodies. Explicit overall deadlines may exceed 30 seconds; bounded
  native waits and stable TLS retry arguments preserve accepted bytes.
  [Interface](../userland/http-fetch.md#fixed-body-requests);
  [QEMU qualification and costs](../development/experiments/http-fixed-post.md).
  **After this task the owner can:** exercise local POST/error/slow-response
  fixtures with a body larger than the resident buffer, without an HTTP port.
- [ ] **3. Prepared provider transactions and responsive stream service.**
  The object/rights/lifetime/inheritance review and native completion contract
  are accepted; the bounded kernel prerequisite is delivered for review.
  Provider integration remains pending.
  Deliver body/control/response/observation grants, freeze, status, abort,
  private storage, credential cleanup and retirement. Start with one active
  exchange per provider instance and explicit admission failure. An instance is
  one provider process with its service exports, TLS/network state and storage;
  aliases such as `https`/`json+https` and all binding clients share its limit.
  Separate processes are independent; per-consumer provisioning/concurrency is
  deferred in [shared-instance admission debt](../technical-debt.md#http-shared-instance-admission).
  A cooperative service loop must progress network work while servicing reads/
  status/abort/retirement; do not fetch synchronously inside OPEN/commit or assume
  future user threads.
  Bound resident queues/scratch and export counts; cumulative transfer bytes
  are not a RAM reservation. Publish streamed GET and request-scoped GET headers
  as well as POST. Retain read-only GET authority and register JSON aliases.
  **After this task the owner can:** use native prepared grants to submit once,
  read a prefix and distinguish complete, failed and unknown outcomes.
- [ ] **4. Generic libc/Lua adapters and stream propagation.** Implement `"r"`,
  staged `"w"` and commit-then-response `"w+"`, plus preparation/status/abort
  helpers, with TLS solely in the provider. Carry read-only final-observation
  authority with inherited response streams; copied OPEN metadata alone cannot
  do this. `ferror`/EOF, unavailable seek/size and early-close cleanup must be
  honest. Audit current seek-dependent HTTP consumers before switching GET;
  explicitly materialize where needed, without restoring the 16 MiB transport cap.
  **After this task the owner can:** stage a POST and stream its response or a
  large GET using Lua file I/O, with explicit final-result/error checks.
- [ ] **5. Shell bindings and end-to-end completion.** Prepare directly in the
  running shell, delegate body stdout and response stdin with observation,
  coordinate all producers, and expose commit/status/abort and retained response
  capability-valued bindings designed to become, or sit under, entries in the
  [capability-valued environment direction](capability-environment.md).
  Exact syntax is a userspace choice,
  not an ABI; environment implementation needs a separate assignment. Demonstrate
  `printf '{}' > json+https://…`, then `cat` on that response grant, never a fresh
  URI GET. A failed response must make its reader/command report failure.
  **After this task the owner can:** script web requests with ordinary writers
  and readers, and stream a multi-GB download through shell pipelines or Lua.

Qualify each assigned task with ordinary builds and manual local HTTP/HTTPS
fixtures in QEMU/GDB. Record baseline/changed revisions and buffer/storage demand;
cover short transfers, copied commit races, producer/pipeline failure, 4xx/5xx,
slow responses beyond 30 seconds, partial body/TLS EOF, exhausted resources,
provider death, early reader close and retirement. Final qualification includes
multi-GB GET and staged POST with bounded resident memory and real disk backing,
unchanged TLS/anonymous redirect policy and current URI readers. Local fixtures
need no paid API call or real secret. The Claude harness, Git and ports get their
own integration/qualification afterwards; their milestones remain unfinished.

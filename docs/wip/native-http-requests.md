# Native HTTP requests

Status: **shared milestone accepted 2026-10-11; tasks 1–4 and 5a delivered; tasks 5b
and 5c parked.** The owner parked 5b (#792, userland #250) and 5c (#799, userland
#254, ports #110) on 2026-10-14 until the capability model is resolved
([shell capability flow](capabilities-and-providers.md), #809), because 5c would add
more URI-spelling coupling. The owner closed those pull requests. Until then the
delivered surfaces below stay as they are. The milestone was extracted from
[Claude tasks 1a–1c](claude-on-pyxis.md#track-1-delivery-tasks-after-approval) and
[Git's HTTP prerequisite](git-on-pyxis.md#smart-http-without-libcurl); their
one-shot transaction and credential rules carry forward. Implemented interfaces and
limits are in [HTTP transactions](../interfaces/http-transactions.md),
[libc streams](../userland/stdio.md#private-http-transition),
[fetching](../userland/http-fetch.md) and the
[shell](../userland/shell.md#prepared-http-requests).

## Deliverable and consumers

Prepared POST and streamed GET/POST through native provider grants, ordinary shell
streams and Lua file I/O: `printf '{}' > json+https://…` with its response read by
`cat`, and a multi-GB HTTPS download through the shell or Lua. Reading the URI again
issues a GET, not the POST's response; keep the actual response grant.

Consumers are the [Claude harness](claude-on-pyxis.md),
[git-remote-https](git-on-pyxis.md#smart-http-without-libcurl),
[pinned source fetch and the LAN gateway](source-builds.md#reaching-the-sources) and
the [ports runner](../development/ports.md#building-and-packaging). None implements a
private HTTP client or finishes its own milestone to obtain this facility. Git
negotiation, pin and hash verification, gateway deployment and recipe execution
remain consumer work. Anthropic SSE streaming, HTTP/2, compression, proxies,
PUT/PATCH/DELETE, caching and general credential storage are outside it. HTTP and
TLS stay in userspace; shared stream and readiness contracts are generic, with no
HTTP-shaped or POSIX-shaped kernel calls.

**Pyxis must be able to hit good network targets overall.** End-to-end HTTP
throughput should track the native TCP path. The ThinkPad xfer measurements of
2026-10-09 (70.5 MiB/s send, 85 MiB/s receive) are the
[reference](../development/network-throughput.md), not a contract. Task 3's whole-GET
cost (+18%, 14.3 to 17.2 ms) was accepted on 2026-10-12, and task 4 recovered it for a
1 MiB GET (about 28% faster, about 82 MiB/s natively). Track the gap with each
relevant change ([task 3](../development/experiments/http-prepared.md),
[task 4 A/B](../development/experiments/http-stream-adapters.md#native-ab--owner-2026-10-13),
[revisit points](../technical-debt.md#http-provider-throughput)).

## Parked tasks 5b and 5c

- **5b, standalone positional scripts** (`shell home://post.pxs`): the script would own
  a subgroup holding its producers and staging worker, so that stopping it stops the
  whole subtree. QEMU showed that terminating the prototype script left its blocked
  producer and worker alive, with the producer retaining console input and no POST.
  The code is preserved in the drafts above, based on #789 (now merged). New shell
  scripts use the `.pxs` extension. Direct bundle-interpreter shebangs are a
  [later direction](later-os-directions.md#lua-follow-ups).
- **5c, plain stdio submission** (#799): an exact-FILE `fflush` would submit a staged
  POST once, replacing the explicit commit helper, with `"w+"` reading its response
  through the same FILE and implicit buffer, `fflush(NULL)`, close, exit and launch
  drains only staging. The owner's queued gallery screenshot (Neovim with a short C
  version of [`ci-jobs-server.py`](../../tools/demo/ci-jobs-server.py) beside a shell
  compiling it) wanted this, and waits with it.

Until either resumes, scripted requests use the explicit prepare, commit, status and
abort helpers in libc and Lua, or the trusted bootstrap profile.

## Carried-forward accepted contract

PREPARE fixes endpoint, method, validated request headers, resource admission and one
absolute deadline, with **no network submission**. Read authority permits GET, not
POST; preparation and submission need separately delegated authority. Request-scoped
headers also work for GET, including Git-Protocol discovery. POST returns a staged
body WRITE grant and commit, status and abort control. Copies refer to one
transaction. The first commit atomically freezes every body copy and starts at most
one request; later commits and status observe without replay. Body writers cannot
commit, read credentials or obtain the response by writing. Flush and close never
submit, and staging FILE_SYNC is unsupported, not a POST trigger. The first shell
slice has one direct staging writer, since independent FILE descriptor offsets do not
coordinate concurrent writes. Control-export retirement with no queued or in-flight
invocation abandons staging and rejects further body operations even if writers
survive; closing one copied handle does not revoke queued work or a control held
elsewhere.

IPC errors, provider errors, submission certainty, HTTP status and transfer
completion stay separate. A fully framed 4xx or 5xx keeps its status and body as a
completed response. A lost reply after submission leaves the remote outcome unknown,
and abort stops local work without promising rollback. Provider death fails held
operations, with no rebinding or automatic retry.

The provider owns Host, length and transfer framing and rejects conflicting,
duplicate or CR/LF-bearing headers. Request credentials enter through private native
preparation, are unreadable through returned grants and never enter argv,
environment, history, logs or global defaults; they are kept only for their request.
Credential-bearing requests cannot redirect or downgrade. Existing anonymous GET
redirect and CA policy remains, and POST never redirects or replays. Preparation
replies transfer real attached grants, and shell bindings are private capability
storage, not printed numeric handles. Body, control and response lifetimes follow
exported-object retirement, including in-flight receipts. Metadata and admission
bounds and real memory or storage exhaustion fail explicitly. Claude's 64 KiB request
and 1 MiB response budgets are consumer policy, not limits on Git, GET or downloads.

## Task 3 accepted facets and lifetime

Accepted 2026-10-12. Task 5 uses the same request value; the parked
[capability environment](capabilities-and-providers.md) and
[nested groups](nested-execution-groups.md) get no implementation here.

The prepared capability is an opaque control reference to one provider record (fixed
method, endpoint, validated headers, trust context, deadline, resource admission and
state). It grants no network, trust, backing-file or credential-read authority.

| Facet | Rights and behaviour |
| --- | --- |
| Control | Independently attenuable COMMIT/ABORT; repeated COMMIT observes, never resubmits |
| Body | Exported FILE WRITE, including SIZE/RESIZE; POST only; no READ; copies freeze together; SYNC unsupported |
| Response | Generic exported STREAM READ: shared forward consumption, no offset, seek or SIZE; failure is an error, never successful EOF |
| Observation | Separate STATUS-only export: phase, submission certainty, HTTP and framing status, counters, cleanup errors; no mutation or request headers |

PREPARE atomically returns control, body and observation (GET omits the body) without
network activity. First COMMIT freezes and starts; its retained receipt owns the
opening work until its deferred reply hands off a reader after the headers and before
the whole body, together with observation. That handoff is single use; later readers
copy a holder's grant, and later COMMIT returns status, not another reader. Cancelling
before the REPLY aborts unfinished work and releases provisional readers; after a
successful REPLY, reader retirement governs. Submission and data are never replayed.
A native binding owns control and the selected facets and adds the response after
handoff; copying retains the same request and cursor, and launch copies selected
narrower grants as named resources (producer gets the body, reader the response and
observation, monitor the observation). Control inheritance is opt-in and ordinary
children inherit nothing merely because a binding exists. No handle serialization,
credential strings or session-wide request publication exist.

Separate exports matter because [retirement](../interfaces/endpoints.md#exports-and-retirement)
counts all references, not rights, so observers must not retain control. Providers
authorize facets by invocation rights, not payload masks.

PREPARE reserves the instance's one transfer slot, and other preparations refuse until
terminal framing or abort, with no queue and no automatic retry; admission also counts
memory, storage and the existing 64 live exports. COMMIT closes mutation admission,
rejects queued and new writes and drains admitted mutations before pinning the
length; failed or uncertain provider-observed mutations prevent submission, and
producer or coordinator failure must too. Private disk backing needs an explicitly
delegated, isolated mounted directory (exclusive create, unlink before the writer
export, native READ|WRITE kept privately); shared `home://` or `tmp://` names are not
privacy, and RAM staging is explicit and counted. After handoff, readers retain the
exchange and their final retirement aborts unfinished reception; observers retain
only small final state. The original deadline bounds staging, network and framing,
and completed bytes and status stay readable afterwards. ABORT is idempotent local
cancellation, not rollback. Provider death fails the facets, and missing final status
fails, never completes. The provider is independently owned, not a caller's job
descendant: group stop closes member grants but outside holders keep authority, and
job cleanup is not HTTP completion or rollback.

The cooperative-I/O prerequisite (TCP CONNECT, DNS UDP and staging operations that
would otherwise block the single provider) was delivered as bounded
[native completion](../interfaces/native-completion.md) (#758) under an owner
exception to the planning freeze on 2026-10-12, plus a header-only
`ENDPOINT_TRY_RECEIVE` ([receiver readiness](../interfaces/endpoints.md#receiver-readiness)).
Still synchronous in the provider: DNS ID and port randomness (3 s), TLS entropy
(5 s), clock and UDP open and cleanup IPC, capability export and table admission,
allocation and local cryptography. The request deadline also bounds entropy waits,
and STATUS and ABORT cannot run during these calls. The 205 correction rejects a
nonempty decoded Reset Content while keeping the HTTP status; empty fixed, chunked
and close-delimited framing stays valid, distinct from 204.

## Accepted owner decisions

Provider and adapter defaults accepted 2026-10-11.

1. **Forward-only response plus read-only final observation.** The reader is available
   after the response headers and before completion, with bounded RAM buffering and
   backpressure. Only validated complete framing yields successful EOF; a failure
   after a readable prefix reports a read error and a retained failed status,
   including deadline, provider death and exhaustion. HTTP status stays separate from
   stream success and submission certainty. Seeking and size are unsupported on the
   live stream and copies share consumption, so a consumer needing a seekable
   snapshot materializes into its own native file and publishes it only after
   complete status. This replaces the old complete-only response FILE.
2. **A provider-private staged file before transmitting large bodies.** Large POST
   bodies use explicitly delegated private writable storage (`tmp://` is RAM, not
   the multi-GB default); producers get only the exported writer, never an alias to
   the backing. Commit pins the final length and streams from the frozen body. Small
   RAM staging stays optional under counted resources, with no fixed total-body
   ceiling beyond representability and real resources, and privacy is never derived
   from a filename.
3. **Shell redirection as the explicit transaction coordinator** (delivered as task
   5a). A write URI prepares and stages and the shell commits only after every
   body-producing stage finishes successfully and staging has no unresolved error;
   a failure abandons without sending. The response and status stay in private
   bindings for later reads, and the redirect returns after the headers so a bounded
   buffer cannot deadlock. Last-stage pipeline status alone is not sufficient. Lua
   uses explicit prepare, commit and status, and `"w+"` adopts the response reader at
   its start. Ordinary `fclose` and editor saves never submit.

Task 4's adapters (all accepted 2026-10-13) are `"r"` as a forward response, `"w"` and
`"w+"` as staged POST with an explicit commit that flushes the FILE and refuses a
failed or uncertain producer, declared paired standard-stream facets (STREAM READ plus
a STATUS-only observation companion for HTTP stdin), and `fopen`/`open` URL GET on
prepared streams that keeps the anonymous redirect rules (ten hops, one deadline, no
downgrade, credentialed requests and POST never redirect). `cp` explicitly
materializes HTTP into its destination staging file and publishes after validated
EOF and final status; native FILE-only APIs stay honest, so a URL needing seek or SIZE
needs caller-owned materialization.

## Task 2 accepted TLS policy

Accepted 2026-10-12. Ordinary early HTTP responses are allowed during POST upload, but
a read that attempts TLS output while an application write is pending fails before
transmitting. Identical plaintext arguments are kept across write retries and accepted
bytes are never replayed. Uploading stops on the final response headers, and an
incomplete upload or pending TLS write aborts and frees without close_notify, so
cleanup cannot flush unsent body. A fully framed HTTP error still completes
independently of its status. In pinned Mbed TLS 4.1.1 the TLS 1.2 HelloRequest path
sends NO_RENEGOTIATION through `mbedtls_ssl_send_alert_message`, which can flush an
existing application record; the guard makes that path an explicit failure. If a real
server triggers it, revisit a separate bounded, counted ciphertext queue. Overall
deadlines use bounded native TCP waits, with one TRY after a slice timeout to tell a
transient WOULD_BLOCK from a terminal timeout. CONNECT stays one bounded attempt with
no reconnect or POST replay.

## Delivered tasks

1. **Incremental libhttp GET and final framing result.** Bounded DATA, retained
   completion and failure, and authenticated TLS EOF.
   [Interface](../userland/http-fetch.md#incremental-get),
   [record](../development/experiments/http-incremental-get.md).
2. **Fixed-body POST and overall deadlines.** Bounded FILE reads, validated copied
   headers, early final responses and retained HTTP error bodies.
   [Interface](../userland/http-fetch.md#fixed-body-requests),
   [record](../development/experiments/http-fixed-post.md).
3. **Prepared provider transactions and a responsive stream service.** Separate body,
   control, response and observation exports, freeze, status, abort, private npfs
   staging and cooperative DNS, TCP and TLS steps.
   [Contract and limits](../interfaces/http-transactions.md),
   [record](../development/experiments/http-prepared.md); one active exchange per
   process is shared by aliases and clients
   ([admission debt](../technical-debt.md#http-shared-instance-admission)).
4. **Libc and Lua adapters and stream propagation.** Streamed `"r"`, staged `"w"` and
   commit-then-response `"w+"`, with prepare, status and abort helpers; child stdin
   receives declared reader and observation facets.
   [Interfaces](../userland/stdio.md#private-http-transition),
   [record](../development/experiments/http-stream-adapters.md).
5. **5a, interactive and trusted-bootstrap bindings** (#780 contract, #785). `=> name`,
   redirection-only `@name` and explicit `request` operations over a bounded trusted
   staging worker, with 16 MiB default private RAM staging or opt-in private NPFS.
   [Shell reference](../userland/shell.md#prepared-http-requests),
   [record](../development/experiments/http-shell.md).

Each task's qualification, with its baseline, native and nested measurements and
limits, is in the linked record.

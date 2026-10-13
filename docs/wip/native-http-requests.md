# Native HTTP requests

Status: **shared milestone accepted 2026-10-11; tasks 1–4 delivered; task 5 assigned,
2026-10-13** ([accepted contract #780](https://git.internal/PyxisOS/pyxis-os/pulls/780),
implementation draft [#785](https://git.internal/PyxisOS/pyxis-os/pulls/785)).
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

**Pyxis must be able to hit good network targets overall in the future.**
End-to-end HTTP throughput should track the native TCP path. The ThinkPad xfer
measurements of 2026-10-09 (70.5 MiB/s send, 85 MiB/s receive) are the current
[reference measurement](../development/network-throughput.md), **not a contract**.
Track the HTTP/TCP gap with each relevant change. The owner accepted task 3's
native whole-GET cost (+18%, 14.3 → 17.2 ms) on 2026-10-12;
task 4 subsequently recovered that cost for a 1 MiB GET (about 28% faster,
~82 MiB/s natively). [Task 3 measurements](../development/experiments/http-prepared.md),
[task 4 A/B](../development/experiments/http-stream-adapters.md#native-ab--owner-2026-10-13) and
[revisit points](../technical-debt.md#http-provider-throughput) remain explicit.

## Inspected starting point

Task 4 started from main `2f2109fc`, userland `9f781e94`, ports `f93becb9`.
The provider already delivered prepared response streams; ordinary libc URL opens
still used complete FILE snapshots. Task 4 connects those streams to stdio and
explicit child handoff; shell transaction coordination remains task 5.

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
2026-10-12; the prerequisite is merged in #758:

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
staging CREATE/REMOVE/READ/WRITE/RESIZE/SIZE on RAM/archive/npfs. HOST/FAT32 and exported
provider forwarding are excluded. See the [completion ABI and limits](../interfaces/native-completion.md),
[admission debt](../technical-debt.md#native-completion-per-process-admission) and
[QEMU qualification/cost finding](../development/experiments/native-completion.md).
The broader completion direction stays parked. Task 3 integrates the provider,
cooperative DNS/TLS and the 205 correction; its limits remain explicit.

### Task 3 receive contract — accepted 2026-10-12

Owner option A: header-only `ENDPOINT_TRY_RECEIVE` and libpyxis helper, with
RECEIVE's owner/transport authority, packet, selection priority and retryable
collection. An empty open receiver returns `CALL_WOULD_BLOCK`; service loops
never block on stale readiness. Ordinary RECEIVE is unchanged. See
[receiver readiness](../interfaces/endpoints.md#receiver-readiness).

The provider uses cooperative native completion/TCP/TLS progress. Remaining
synchronous phases: DNS ID/port randomness (**3 s**), TLS entropy (**5 s**), clock/
UDP open/cleanup IPC, capability export/table admission, allocation and local
cryptography. The request deadline
also bounds entropy waits; STATUS/ABORT cannot run during these calls. Trust/PSA
setup precedes publication. No fabricated entropy-WANT result is included.
[Implemented facets and limits](../interfaces/http-transactions.md).

The task 3 205 correction rejects nonempty decoded Reset Content while retaining
HTTP status; empty fixed/chunked/close-delimited framing remains valid, distinct
from 204 ([RFC 9110 §15.3.6](https://www.rfc-editor.org/rfc/rfc9110.html#section-15.3.6),
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

## Task 4 accepted adapters — 2026-10-13

All three defaults accepted by the owner; task 4 implementation assigned. TLS stays solely in
the provider. Authority comes from declared namespace/resource/stream grants:
no launch-site wrapper may add grants, widen a bundle request or provision a
provider behind the caller's back. Missing POST preparation/private staging
remains a refusal. Shell transaction coordination is task 5.

1. **Explicit commit for staged stdio.** `"r"` opens a forward response;
   `"w"` and `"w+"` prepare a POST and expose only its staged writer initially.
   Native libc and Lua helpers expose prepare/commit/status/abort. Commit flushes
   this FILE's pending output and refuses a failed/uncertain producer; it never
   retries a lost submission. `"w"` commit returns a separate response FILE;
   `"w+"` adopts the response at its start. Reading `"w+"` before commit fails
   `EBUSY`, rather than submitting or reading staging. Staging may use its real
   FILE position/size; the response cannot seek, stat a size or replay bytes.
   Read, flush and close never commit. Close releases this holder's facets;
   other explicitly retained copies remain governed by the accepted lifetime.
   The ordinary URL-open deadline is one 30-second budget including staging and
   response consumption; explicit preparation accepts a longer absolute deadline.
   Slow/manual staging needs an explicit longer deadline.
2. **Paired declared standard-stream facets.** Extend launch/startup
   stream records with an optional, typed read-only observation companion, selected
   from the caller's declared grant list. HTTP response stdin carries STREAM READ
   plus HTTP STATUS-only observation, both with CALL transport; kernel/startup
   validation rejects malformed protocols/rights/aliases. Plain streams carry no
   companion. Libc owns the pair, descriptor aliases share it, and explicit child
   stream selection copies both narrowed facets. Control/body/ABORT never accompany
   a response implicitly. Update all launch, bundle and job adapters together;
   pairing is supplied by the trusted handoff, not inferred from a name or numeric
   handle. Missing/failing final observation cannot establish successful completion.
   The pair keeps its lifetime/association with the selected stream; replacing
   stdin never infers a companion or creates authority.
3. **Stream URL GET, preserve URL policy, explicitly materialize.**
   Switch libc `fopen`/`open` HTTP reads to prepared streams. Keep the existing
   anonymous redirect rules, final URL/media type and errno mapping for ordinary
   URL opens; explicit prepared APIs retain complete HTTP error bodies/status.
   Carry initial-origin/sticky-crossing and bounded header/framing context into
   PREPARE; the provider reports its trust-profile redirect restriction, so both
   outgoing and incoming custom-trust origin changes refuse.
   One deadline spans the chain, with ten hops, visited-URL detection, no downgrade
   and no redirect of credential-bearing requests or POST. There is no total-body
   allowance in the stream path. `cp` explicitly consumes HTTP into its existing
   caller-owned destination staging file and publishes only after validated EOF
   and final status. Keep native FILE-only APIs honest: a URL needing seek/SIZE
   requires explicit caller-owned materialization; no universal legacy snapshot
   fallback or implicit RAM/disk authority.

### Inspected consumer audit

Main `2f2109fc`, userland `9f781e94`, ports `f93becb9`; source inspection only.

| Consumer | Reached dependency and task 4 treatment |
| --- | --- |
| Links | `ports/links/pyxis.c` `provider_func`: sequential fread into its own cache; validate final result before cache publication and preserve final URL/status/media type. No temporary native file needed. |
| Lua I/O/loadfile | Upstream liolib/lauxlib read sequentially and check ferror; chunk reads stream, `read("*a")` explicitly allocates the whole result. Seek fails; failed input cannot yield executable code. |
| TCC | `tccpp.c` sequential refill and ferror for source/includes; final URL drives relative includes and once handling. Object/archive loaders and `tcc_load_text` seek: materialize those inputs into a native file with cp. |
| Native cp | `userspace/cp/copy.c` `cp_copy_file_handle`: SIZE and positioned reads; add explicit streamed HTTP copy into destination staging, without changing native-file copying. |
| libuv/Neovim | `ports/libuv/pyxis/fs.c`: offset -1 can stream; pread/stat need a real file. Neovim editor selection already proves native parents; download then edit, without adding remote editing. |
| Program loading | `userspace/lib/program.c` prefix/interpreter reads and kernel launcher require FILE; materialize explicitly before loading, never feed STREAM to the launcher. |
| Lua native SHA-256 | `ports/lua/pyxis.c` omits HTTP workspace and uses positioned FILE reads; HTTP hashing is not established support and is not added here. |

### Qualification and measurement

The owner released the timing hold after publishing builder `4c23a20` on
2026-10-13. [Task 4 qualification and native/nested runs](../development/experiments/http-stream-adapters.md)
record C/Lua reads, staged POST, final observation inheritance, truncation,
authenticated TLS EOF, redirect policy and explicit 17 MiB materialization.
The owner's native A/B measured about 82 MiB/s, close to the TCP reference above;
continue tracking the gap for relevant changes and HTTP-heavy workloads. No throughput optimization is assigned here.

Implemented interfaces and limits are in [stdio](../userland/stdio.md#http-streams-and-explicit-submission),
[HTTP transactions](../interfaces/http-transactions.md) and
[fetching](../userland/http-fetch.md#redirect-chains). Userland and ports precede
the parent ABI/gitlink update. Task 5's assigned proposal below settles shell
bindings and producer coordination;
implementation awaits the owner's decisions.

## Task 5 shell proposal — owner review required

Proposal only, 2026-10-13. No code or environment implementation is assigned by
this proposal. Task 4 (#774, userland #244, ports #103) is merged. These three
choices settle the user-visible surface and its required provisioning; defaults
are recommendations, not accepted decisions.

### 1. Named typed results — default: `=> name`, `@name`, `request` operations

A final stdout HTTP URI redirect requires `=> name`, reserving a fresh name
before preparation/launch. Without one, refuse with a syntax hint. There is no
implicit current/last request, numbered job, `job` builtin or printed handle.
`@name` is a typed selector only as an unquoted whole
redirection operand: output selects the prepared body, input selects the retained
response plus STATUS-only observation. It never becomes an argv path or GET.
Quoted `"@name"` or `./@name` means an ordinary filename. All ordinary commands
use the same declared stream handoff; there is no special `cat` wrapper.

```text
printf '{}' > json+https://example.test/api => reply
cat < @reply
request status reply
request drop reply
```

The inline form commits after successful production and returns after headers,
not the whole response. Manual staging uses the same value and explicit commit:

```text
request prepare upload POST json+https://example.test/api --timeout 120
cat home://payload.json > @upload
request commit upload
request copy --response kept upload
request drop upload
cat < @kept
request status kept
request drop kept
```

Prepare permits GET/POST; optional `--headers-file` reads a native file with the
caller's existing authority, never a URL or credential string on the command
line. Credential-bearing preparation keeps the native no-redirect policy.
Timeout is seconds converted once to an absolute deadline; default 30 s includes
staging, waiting at the prompt and framing. No renewal/retry.
`request abort name` requires control; `request list` shows names/types/phases,
not handles or headers.
Copy defaults to retaining the same request; `--response`/`--status` attenuate.
Copies share consumption and failure state, not replay. A second cat reads only
what remains or EOF; prefix consumers' private read-ahead is not recovered.
Existing names refuse replacement; explicit drop releases only that holder.
Shell exit aborts unfinished controlled work and closes its references; outside
holders can retain theirs, and cleanup/remote rollback is not promised.

Use one typed capability-entry table (initial policy: 16 entries and 63-byte
portable identifier names; refuse capacity/collisions, never evict).
This is an **interim shell-local adapter** beneath the future
[capability environment](capability-environment.md), not an HTTP-only global
registry: payloads hold native facets with type/ownership/attenuation, while FILEs
are I/O adapters. No text encoding of capabilities, writable environ, new startup
environment ABI, config capability values or automatic name inheritance here.
Reject names already used by string environment entries. Children receive only
explicit selected facets through launch; scripts have their own table. The
future environment must unify storage/lookup, not wrap a second request registry.

Package the pinned BusyBox `printf` as an ordinary external applet; it is absent
today. It receives stdout, not HTTP/control authority. **Alternative:**
`request send name -- COMMAND ... > URI` as a prefix instead of `=>`; the same
named values/rights apply, with more typing and
no new suffix operator. Select this syntax before implementation.

### 2. Coordinator — default: one bounded trusted staging worker

Refine the earlier direct body-stdout sketch: producer stdout is PIPE WRITE;
an internal trusted worker owns PIPE READ and staging FILE WRITE. Its declared
profile permits these streams, memory and stderr, with no namespace,
roots, network, control or descendant launcher. It latches failed/uncertain writes,
handles confirmed short progress and checks final flush/close before EXITED/0.
This avoids treating ignored exported WRITE errors as successful staging. No whole
body RAM buffer: one additional 64 KiB pipe and a 4 KiB copy buffer; FILE writes
split at 4,080 bytes. Measure the added copies/process against the baseline.

Admit binding/pipe/worker and all images before publication. One atomic batch
fits the existing eight-child limit: at most seven body-producing stages plus
worker in this profile; ordinary pipelines stay at eight. The producer profile
declares no descendant launch authority, through bundle/shebang adapters too.
A program requiring it
refuses admission; scripts needing child launch are outside this profile until
[nested containment](nested-execution-groups.md). The foreground shell observes
every producer,
worker and Ctrl+C while staging. Commit requires all EXITED/0, drained pipe EOF,
no uncertain staging and no unresolved cleanup; last-stage status is insufficient.
A failed launch/stage/write or cancellation abandons without submission, stops
and observes the direct batch, and latches failure/uncertainty in the named value:
a later manual COMMIT cannot bypass that failure.

PREPARE/COMMIT/ABORT remain synchronous IPC: Ctrl+C/status cannot be serviced
while those control calls wait, even though the provider loop is cooperative.
The deadline bounds caller waits, not native drain; worker safe-stop/cleanup can
also wait for admitted work. No generic IPC-completion extension is assigned.
A lost COMMIT reply stays uncertain and is never replayed. Fully framed non-2xx
makes submit/inline completion fail but retains the error body; reading that
body can succeed. A framing/IPC failure makes the reader fail, and HTTP-aware
reader pipelines propagate any stage's failure rather than a successful final
checksum hiding a truncated prefix. Deliberate prefix reads can succeed without
claiming completed framing. Non-HTTP pipeline status stays unchanged.

Only one HTTP body sink, at the final stage, foreground only; no HTTP stderr,
append, background transaction or concurrent body writer. `> @name` stages
without automatic commit; URI `> ... => name` is the explicit automatic
coordinator. `< http(s)://...` uses ordinary GET's STREAM/STATUS pair and redirect
policy; `< @name` always uses the held response. **Alternative:** delegate body
FILE WRITE directly and require a new reliable producer I/O-completion contract;
less overhead, but existing exit status alone misses ignored/uncertain writes.

### 3. Declared private backing — default: opt-in trusted startup, private NPFS

Reuse [declared volume/source grants](../userland/init.md#fat32-volumes-and-selected-grants),
now merged in #775; no second private-volume schema. A dedicated NPFS volume
is selected as a DIRECTORY source, absent from all URI roots. For example
(`volumes` is global; `grants` belongs to the chosen space):

```lua
volumes = { ["http-work"] = { kind = "npfs", partition = 2, volume = "http-work" } }
grants = { http_staging = { volume = "http-work", kind = "directory",
                           access = "read-write", optional = true } }
```

Trusted startup must consume this source before the interactive handoff: create
isolated per-service leaves, select each leaf through a declared provider
resource named `staging`, and publish the resulting OPEN_READ/OPEN_WRITE grants
and aliases into that space's namespace. Use the common selected-source resolver
and a provider bundle declaration, not a grant-adding launch wrapper. The provider
requests CREATE/REMOVE/READ_FILES/WRITE_FILES/FILESYSTEM_INFO on its leaf; setup
drops ancestor/provisioning copies and excludes this source from the session/
interactive-shell inventory. Existing source mounting is implemented; service
selection, leaf provisioning and consumption before handoff still need code.

The backing is outside **every** client root/cwd/alias, including system/home:
installed defaults expose both today. No inferred home/tmp directory, automatic
RAM fallback, hidden-name privacy, manifest edit or removal of HTTPS's read-only
trust-root policy. Selection only narrows declared grants. Missing/not-writable/
non-NPFS backing leaves GET available and POST explicitly unavailable. Existing
images require an explicit backing/configuration change; do not repartition or
add a volume during an update. Qualify with a separately authored disposable
installed image. This named-resource declaration is an interim current-ABI path,
not capability-valued configuration; reconcile it in that separate assignment.
**Alternative:** keep stock provisioning unchanged and require a custom trusted
initializer; functionality can be qualified, but ordinary installations cannot
use the example until separately provisioned.

### Baseline and delivery checks

Captured before proposal edits, main `26c9e9ca`, pins userland `da098fd`,
ports `d732dd4`, fs `414be0ac`, lwIP `a1aadb91`. Reused verified ordinary image
built at `24faa688` with actual userland `da098fd`; only docs/pin bookkeeping
changed since its inputs. All four bundle verifiers and ISO payload checks pass;
this is not a fresh-main build. Builder `4c23a20`, nested KVM q35/max, four CPUs,
2 GiB, HPET, VirtIO network/entropy, fresh OVMF, no HDA/disk; local fixture only.

| Workload | Median (range) |
| --- | --- |
| 1 MiB GET OPEN, five samples after warmup | 7.879 ms (7.090–9.018) |
| GET whole consumption, verified bytes/EOF | 201.982 ms (163.666–263.587), 4.951 MiB/s |
| 1 MiB RAM whole read, five samples after warmup | 0.470 ms (0.441–0.521) |
| Ordinary launch/wait, existing Lua helper, 20 samples | 4.164 ms (3.407–10.060) |

Commands: `iobench read http://10.0.2.2:19085/fixed --rounds 5`, RAM control
with `cp boot://share/iobench.bin tmp://http5-ram.bin`, and monotonic timing around
`pyxis.run{"echo","-n"}`. Shared host guests limit interpretation; no clock cost
subtracted, native gap or isolated kernel-spawn cost claimed. Raw captures and
provenance stay local. The docs branch then refreshed to main `6bc3a009`
(userland `b38b0e09`, ports `9f407742`); the measurements above remain at their
recorded pre-merge revisions, not this newer runtime. `cat URL | cksum` works;
HTTP stdin/output redirects refuse,
and `printf` is absent. Stock POST/coordinator has no measured zero-cost baseline.

After acceptance, refresh the baseline on the chosen implementation base, repeat
matched GET/launch and measure the added worker's POST
cost/storage/CPU. Qualify the example sessions, every producer's failure,
uncertainty/no replay, Ctrl+C/expiry/early close, copied/reference lifetimes,
4xx/5xx, malformed framing and TLS EOF, denied provisioning, and real private
NPFS multi-GB GET/staged POST with bounded resident buffers. Track the native
HTTP/TCP reference gap; the task 4 native result is not a worker POST measurement.
Userland/ports first, parent pins their own heads. Stop here for the three choices.

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
- [x] **3. Prepared provider transactions and responsive stream service.**
  Delivered separate body/control/response/observation exports, freeze, status,
  abort, private npfs staging and retirement; cooperative DNS/TCP/staging
  completion/TLS steps and nonblocking receiver collection. The 205 correction,
  request-scoped GET fields and read-only JSON aliases are included.
  [Implemented contract and limits](../interfaces/http-transactions.md);
  [QEMU qualification and native/nested matched costs](../development/experiments/http-prepared.md).
  One active exchange per process is shared by aliases and clients; provisioning/
  concurrency is deferred in [admission debt](../technical-debt.md#http-shared-instance-admission).
  DNS entropy (3 s), TLS entropy callbacks (5 s), capability/service handoffs
  and local cryptography remain synchronous; STATUS/ABORT cannot run during them.
  **After this task the owner can:** use native prepared grants to submit once,
  read a prefix and distinguish complete, failed and unknown outcomes.
- [x] **4. Generic libc/Lua adapters and stream propagation.** Delivered
  streamed `"r"`, staged `"w"` and explicit commit-then-response `"w+"`, with
  preparation/status/abort helpers and TLS solely in the provider. Child stdin
  receives declared reader/STATUS-only facets; final observation validates EOF.
  Seeking/SIZE refuse; `cp` materializes explicitly without a transport body cap.
  [Interfaces](../userland/stdio.md#http-streams-and-explicit-submission) and
  [QEMU qualification/cost limits](../development/experiments/http-stream-adapters.md).
  **After this task the owner can:** stage a POST and stream its response or a
  large GET using Lua file I/O, with explicit final-result/error checks.
- [ ] **5. Shell bindings and end-to-end completion — assigned 2026-10-13.** Prepare directly in the
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

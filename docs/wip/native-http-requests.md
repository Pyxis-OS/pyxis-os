# Native HTTP requests

Status: **shared milestone accepted 2026-10-11; task 1 assigned 2026-10-12.**
Extracted from [Claude tasks 1a–1c](claude-on-pyxis.md#track-1-delivery-tasks-after-approval)
and [Git's HTTP prerequisite](git-on-pyxis.md#smart-http-without-libcurl).
Their accepted one-shot transaction and credential rules carry forward. All three
defaults below are accepted, including streamed GET/POST without an artificial
response-size cap. Each implementation task needs its own owner go.

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

Main `34dfce9e`, userland `d560fbb0`: [httpfs/libhttp](../userland/http-fetch.md)
provide GET-only complete immutable snapshots, with a 16 MiB body allowance
across redirects and 64 MiB shared body storage; OPEN fetches synchronously.
[Verified TLS](../userland/https.md) has one active connection per process-local
runtime. Native FILE uses explicit offsets and zero-length reads mean EOF;
`pyxis_stdio_response` is copied open metadata, not live final status. These must
not be reinterpreted as a growing seekable FILE or a successful truncated download.

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

## Capability-valued variables and task 3 review

A prepared request is a capability. Task 5's private shell bindings are the first
form of capability-valued variables and must be designed to later become, or sit
under, entries of the cross-cutting
[capability-valued environment direction](capability-environment.md), not a
separate shell-only mechanism. Never encoding a capability as an environment
string or printed handle does not exclude that direction; the credential rule
forbids secret bytes in argv/environment/history/logs, not request capabilities.
Implementing the capability environment is outside this milestone unless
separately assigned.

**Before implementing task 3, the assigned agent must stop for owner review**
with an object/rights/lifetime/inheritance proposal: what a prepared-request
capability carries; how body/control/response/observation rights split; what
copying, child inheritance or holding it past the shell means; and how it sits
in a capability-valued variable. Accepted defaults do not waive this checkpoint.
Motivating workflow: log in, extract a token with jq, and prepare another request
with it, keeping credential bytes on the private preparation path.

## Accepted owner decisions

All three defaults accepted 2026-10-11; not implemented.

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

## Ordered delivery tasks

- [ ] **1. Small first task: incremental libhttp GET and final framing result.**
  Factor body delivery from whole-body allocation; keep a snapshot collector for
  existing callers during transition. Decode fixed/chunked/close-delimited
  bodies in bounded chunks with explicit completion/failure and no total-body
  allowance in the stream engine. Preserve TLS authenticated-EOF rules.
  **After this task the owner can:** exercise the native engine against local
  slow/truncated and over-16-MiB GET fixtures and inspect chunks/final results.
- [ ] **2. Fixed-body POST and overall deadlines.** Read a fixed-length staged
  native file in chunks; validate caller headers and retain complete HTTP error
  responses. One request deadline may exceed 30 seconds, using bounded native
  waits underneath; adapt TLS timeout handling without replaying accepted bytes.
  **After this task the owner can:** exercise local POST/error/slow-response
  fixtures with a body larger than the resident buffer, without an HTTP port.
- [ ] **3. Prepared provider transactions and responsive stream service.**
  Start with the mandatory object/rights/lifetime/inheritance review above.
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

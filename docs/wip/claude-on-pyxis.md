# Claude on Pyxis

Status: design accepted by the owner, 2026-10-09; **not implemented**. Track 1 is
an in-repo Lua Messages harness with four native tools, consuming the delivered
[HTTP resources](../userland/http-fetch.md) and shell-scripted API testing. Track 2, after the harness, investigates a Hax port.
All three decisions below are accepted.
Each implementation task still requires a separate owner go.

Source inspected: Pyxis `28051508`, userland `74f3e229`, ports `6d63971d`. No
builds, QEMU runs, credential reads or paid API requests were performed.

## Track 1: Lua harness after shared HTTP delivery

The [Messages API](https://platform.claude.com/docs/en/api/messages/create)
accepts a JSON POST at `https://api.anthropic.com/v1/messages`, with a model,
output-token limit, conversation and tool definitions. The harness supplies
`read_file`, `write_file`, `list_directory` and `run_command` schemas. On a
complete `tool_use` response, validate names/arguments/IDs before executing
tools sequentially, retain the assistant content, and return matching
`tool_result` blocks in a user message. Native failures become explicit error
results; incomplete arguments never execute. End-of-turn, refusal, token limit
and unknown stop reasons stop the loop instead of silently retrying. See
[tool-call lifecycle](https://platform.claude.com/docs/en/agents-and-tools/tool-use/handle-tool-calls).
The harness exchanges bytes through ordinary Lua file I/O on `json+https://`
URIs or prepared capabilities and uses ordinary write/flush/read/close, with
resource descriptions for failures. It
contains no HTTP client or Lua HTTPS binding. TLS stays in the provider process,
so Lua hashing and libtls do not share crypto ownership. The first harness still
assembles and validates a complete response under its consumer budget. The
provider supplies complete snapshots; SSE/model-token streaming remains later work.

### Inspected support and gaps

POST, bounded request headers/error bodies, longer overall deadlines, writable
transactions and prepared handoff are delivered by the
[shared HTTP provider](../userland/http-fetch.md). The harness must select an
explicitly writable TLS root; stock HTTPS remains read-only.
Gaps that stay with this harness:

| Need | Current behavior | Proposed task |
| --- | --- | --- |
| Files and directories | Lua has real `io` reads/writes, seek, rename/remove and `pyxis.dir`. Directory enumeration accumulates the whole table and reports changed-generation errors. | Project-relative text tools; bounded enumeration/results, explicit errors and same-parent replacement for writes. Flush/close are not durability; explicit sync needs a binding if durable writes are promised. |
| Lua/TLS boundary | The Lua port supplies file I/O and `pyxis`; TLS is already isolated in the HTTPS provider. | Keep that boundary. No Lua HTTPS binding, application HTTP library dependency or crypto-owner integration task. |
| Command output | `pyxis.run` retains live streams. `pyxis.job`/`pipeline` and `pyxis.commands` provide explicit capture/deadlines; ordinary Lua needs its declared context in a jobs-enabled space. | [Shared command execution](../userland/native-jobs.md) supplies selected streams, result/deadline observation and cleanup; harness result budgets remain consumer policy. |

Existing [TLS](../userland/https.md) verifies chain, DNS name and dates with
explicit random/clock/TCP grants and public/custom trust roots. Keep that policy.
The POST operation owns framing headers: callers cannot inject Host, duplicate
Content-Length, transfer encoding or CR/LF. Send a fixed-length body, do not
follow credential-bearing redirects, downgrade TLS or replay POST automatically.
An error after transmission can leave delivery/billing unknown; report it and
let the owner decide whether to retry. HTTP status, request ID and consumer-bounded error
body are useful diagnostics; request headers and credentials are not logs.

The owner's 2026-09-24 direction for [future writable providers](../interfaces/transactions.md)
and [prepared requests and shell handoff](capabilities-and-providers.md#variables-and-the-environment)
is a prerequisite: `cat`/read opens GET; `fopen` with `"w"`/`"w+"` stages a
POST body, explicit flush or final owning close submits once, and reads then
consume the response under [libc's rules](../interfaces/transactions.md).
`json+https` sets JSON Accept/Content-Type; it does not serialize or validate JSON.
The [shared HTTP provider](../userland/http-fetch.md) supplies the engine,
configured endpoints and generic shell/libc/Lua adapters (formerly tasks 1a–1c
here); the Lua harness consumes that file path. The owner's
2026-10-11 no-artificial-response-cap direction is shared transport work, not a
change to the harness's accepted run budgets.

Select [dkjson 2.11](https://dkolf.de/dkjson-lua/dkjson-2.11.lua), MIT, pure Lua,
without optional LPeg. Its versioned source has author-published SHA-256
`197cb50834c642f84b4cf99fe724932c50e6d9c92faec7ad89aa25e91df4d481`
([downloads](https://dkolf.de/downloads)); the inspected bytes match. Preserve
the embedded copyright/license and record the source/hash in the vendor notice.
It preserves object/array distinctions and an explicit JSON-null value; empty
tool-input objects must remain objects. Its permissive decoder is not a complete
strict-JSON validator: bound input/nesting, require complete consumption, validate
UTF-8 and every tool schema, and never evaluate response text as Lua code.

The JSON task uses the [owner's dkjson mirror](https://repo.internal/repository/raw-dkolf/dkjson-lua/dkjson-2.11.lua),
available and SHA-256 verified by the owner at the hash above. Preserve that pin
and license when vendoring; no upstream fallback. Lua, Mbed TLS and the HTTP
parser already have pinned recipes; no compiler-container rebuild is proposed.

## Three accepted owner decisions

Accepted 2026-10-09; not implemented.

1. **One shared prepared-request transaction with separate body, control and
   response grants.** The shell or harness is its coordinator. It retains
   commit/status/abort authority and gives
   body producers only WRITE; response consumers receive only READ. Copied
   controls share the transaction state, not independent submission authority.
   The first accepted commit freezes the body and starts at most one POST;
   later commits only report the retained state and never send again. The
   coordinator waits for all producers to finish successfully before committing.
   Separate grants keep ordinary FILE rights narrow. The accepted contract below
   is not an implemented protocol.

2. **A saved development key in one file, delegated read-only only to the
   harness, with the environment-specific provisioning below.** Trusted startup
   opens that file and passes a dedicated named FILE grant, not its containing
   directory. Trusted native preparation code in the harness performs a bounded
   read, closes the FILE and holds the key in process-private memory. It sends
   the key as a request-scoped `x-api-key` header in copied preparation data for
   the verified Messages endpoint under the
   [API authentication contract](https://platform.claude.com/docs/en/api/overview).
   The file is never part of the project view, shared `tmp://`, a child's roots
   or resources, the boot image or PXE staging. Do not expose the key or its FILE
   grant as a Lua global, tool input/result, environment variable, argument,
   log, transcript or request-header dump. The provider receives header bytes,
   never the key FILE or its directory. It retains those bytes only for this
   request, with no header-read operation or cross-request defaults. Subsequent
   Messages requests each prepare their own header; model-facing URI tools
   cannot obtain or reuse that credential or the harness's prepared controls.
   No key value belongs in the repository or documentation; a missing or invalid
   grant stops before any API request.

   On an **installed system**, keep `anthropic-key` in an owner-provisioned,
   dedicated NPFS credential volume, opened as `credentials://anthropic-key` by
   trusted provisioning/startup only. Do not mount that volume in ordinary
   spaces or use the shared home volume; a hidden filename or Unix mode is not
   an authority boundary. The file persists across boots, outside image updates.
   In **QEMU**, use `host://credentials/anthropic-key` in a private virtio-fs
   export outside the checkout, project and image/PXE staging directories.
   Bind that export only to trusted provisioning/startup, not the default
   Development/Remote profiles; pass only the selected FILE to the harness.

   On **live/PXE**, `home://` is RAM: bind a separate private RAM directory as
   the trusted provisioning process's `home://` and upload `anthropic-key` once
   per boot with [xfer](../userland/remote-terminal.md#explicit-file-transfer), before starting the harness.
   End the provisioning transfer, then delegate only the read-only FILE; no
   ordinary shared-home view may reach that directory. Never bake the key into
   an init script, archive or staged image. The remote transfer is not encrypted;
   this development workflow uses the owner's trusted LAN and a revocable key.
   The owner prefers revoking this development key to typing it each run: revoke
   it when exposed or no longer needed, and replace/remove the saved file.
   Provider-side revocation governs later API use; deletion does not invalidate
   copies already held in memory. This protects against ordinary child tools,
   not arbitrary compromised code inside the trusted harness process. A general
   credential store and user-permission policy remain later work.

3. **Automatic execution of the four declared tools within one explicitly
   delegated project, in a finite run.**
   Command input is a native argv array, not Bash syntax. Child tools receive
   selected project/runtime/network grants, no descendant launcher, administrative
   grants or credential. Trusted startup explicitly supplies a same-space
   CREATE_GROUP launcher to the harness, which stays outside each command group;
   ordinary foreground Lua's LAUNCH-only grant is insufficient for this role.
   Make the model and budgets explicit per-run settings. Accepted working
   defaults are 20 Messages requests, `max_tokens` 4096 per request, 64 KiB serialized request,
   1 MiB response, 16 KiB returned file/output chunks, a 300 s overall request
   deadline and a 60 s command deadline. The owner selects the model and starts
   each live run manually; keep the first live exchange small. At the end of
   every run, including a stopped or failed run, report API-reported input/output
   tokens per request and their run totals. Mark missing usage as unknown rather
   than counting it as zero; totals cover only requests with reported usage.
   These are working budgets, not exact billing limits; stop when exceeded
   without hidden retry, silent history truncation or another paid call.
   Streaming, shell-language execution and background tools require later scope.

## Prepared request contract (moved tasks 1a–1c)

The [shared HTTP provider](../userland/http-fetch.md) now delivers these
prerequisites independently of the harness: independently attenuable staging,
commit and abort, one-shot submission, request-private credentials and completed
re-readable response snapshots. Explicit flush, final owning close and clean
exit follow [libc finalization](../interfaces/transactions.md); GC/unwind
abandons. The three harness decisions above remain accepted. Request/response
and tool-result budgets, complete-JSON validation and private key-file
provisioning remain harness integration; no partial tool arguments execute.

## Authority, capture and validation

The harness gains only its space's explicitly supplied grants. Data authority
must be a project directory view, not an entire HOST/home tree selected merely
by a string prefix; supply that view through native launcher startup.
Native file tools accept project-relative components, rejecting parent traversal,
absolute native paths and NULs. Explicit URI reads/writes are file operations
through delegated scheme providers; a readable binding never grants POST. URI
writes stage a body and explicitly commit through the generic request adapter.
They do not gain the trusted loop's secret headers or prepared request controls.
Directory listing remains limited to supplied native directory views; provider
directory enumeration is not promised. Child cwd starts at the project boundary.
Auxiliary memory, clock, random, console, launch and read-only runtime roots remain
explicit. Networking can be supplied as scheme-provider authority; the provider
uses its own explicitly delegated TCP/DNS/clock/random grants. No raw TCP grant
is required just to read/write web URIs. It receives no raw-disk, mount, power or
space-factory authority. Trusted dedicated-space startup supplies the project,
private work views and supervision launcher, rather than inheriting a general
development shell's roots.
Trusted startup creates/delegates a private RAM work-volume root for capture
storage only to the harness; it is not the shared `tmp://` tree and is not
delegated to child roots, cwd or resources. Create then unlink captures there
before launch; no anonymous-file factory or shared-tmp privacy is assumed.
Project bytes included in prompts/tool results are sent to Anthropic; automatic
write/command tools can overwrite/delete project data and make connections under
delegated network authority. This is the exposure the owner chooses, not a new
destination sandbox. Keep harness code/resources outside the writable project.

The former task 1e's reusable launch/capture delivery moves to
[native command execution](../userland/native-jobs.md) on 2026-10-11; this is
not completion. Keep the accepted harness profile: it remains an ungrouped
trusted coordinator with sole CONTROL of each sealed command group, private
created-then-unlinked RAM FILE captures, absent stdin, no descendant launcher
or named console/input/display/terminal bypass grants and no credentials.
Separate stdout/stderr have no chronology guarantee. Preserve the proposed
1 MiB per-stream polled trigger (not a hard backing quota), capped results,
actual exit/fault/stop observation and cleanup before releasing captures;
published HOST work can delay cleanup without a fixed bound. Closing an observer
alone never stops a child. Ordinary `pyxis.run` remains separate.

Native pipes now have try-I/O and readiness; draining two blocking streams can
still deadlock. The shared milestone owns pipeline/event pumping. It does not
expand the first harness to pipelines, descendant tooling or shell syntax.

Qualification of the transport and prepared-request behavior belongs to the
[shared HTTP milestone](../userland/http-fetch.md). The harness tests JSON, tools
and loop transitions against local recorded responses; CI and automatic tests
never spend API credit. The owner manually starts a small real Messages/tool
exchange using the existing small credit after local qualification. No key is
requested by this proposal or captured in its artifacts. Record exact revisions,
QEMU configuration, plain-launch/session baseline for substantial changes,
response/capture memory and actual timeout/cleanup behavior; no paid benchmark.

## Track 1 delivery tasks after approval

- **Shared dependency, former 1a–1c — delivered:** consume
  [HTTP resources](../userland/http-fetch.md); no duplicate HTTP/provider/shell
  prerequisite tasks belong to this harness.
  **The owner can now:** script HTTP requests through the shell and Lua before
  implementing a Claude harness or Git helper.
- **1d: pinned pure-Lua JSON.** Vendor dkjson 2.11 from the owner's mirror above
  at the recorded SHA-256, with license/provenance, object/null handling,
  bounded parsing and schema validation.
  **After this task, the owner can:** construct and decode Messages/tool data locally without API spending.
- **Shared dependency, former 1e (moved, not completed):** consume
  [native command execution](../userland/native-jobs.md)'s trusted-coordinator
  core and private FILE capture/deadline profile. No duplicated launch/capture
  implementation task is owned by the harness; its narrow grants and budgets
  remain as above.
  **After that milestone the owner can:** capture a native command's stdout/stderr
  and real completion before implementing a Claude tool loop.
- **1f: Lua harness loop over provider files.** Implement the four tools and
  validated tool-use/result exchange using file I/O and prepared request controls,
  consuming the shared command milestone under the accepted harness profile,
  private key-file provisioning and trusted native header preparation under
  accepted decision 2, bounded context, explicit per-run settings, end-of-run per-request and total
  token usage, status reporting and deliberate stop behavior. A manual small
  live exchange follows local qualification; no automatic spending.
  **After this task, the owner can:** ask Claude to inspect/edit a small project and run a direct native command on Pyxis.

Each remaining task needs a separate owner go. Optional SSE/model-token streaming
follows separately; the first harness does not depend on curl, sockets or Hax.

## Track 2: Hax inventory, after the Lua harness

Inspected [Hax](https://github.com/OleksandrChekhovskyi/hax/tree/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0)
at `9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0`. Its
[MIT license](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/LICENSE)
and [Meson build](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/meson.build)
describe version 0.6.0, C11, required libcurl, Jansson ≥2.13 and threads, with
optional libm. Upstream does not pin those dependencies. This is a source
inventory, not a compile or runtime result.

| Area | Inspected Hax requirement | Pyxis work |
| --- | --- | --- |
| [HTTP transport](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/transport/http.c) | Curl easy API, JSON POST, headers/body callbacks, SSE, cancellation, timeouts, keepalive and content decoding. | Real curl port over shared libc networking, verified TLS/trust and streaming; completed GET FILE snapshots cannot replace it. |
| JSON | Jansson creation/parsing/serialization and reference ownership; objects/arrays and recursive updates. | A separately pinned Jansson port; the Lua JSON library does not replace this C dependency. |
| [Spawning](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/system/spawn.c) | fork/exec, pipe/dup2/fdopen, waits, signals, detached helpers and `/dev/null`. | Native argv/launcher, explicit FILE/PIPE streams and process/group supervision; no successful fork/signal stubs. |
| [Background tasks](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/tools/task_registry.c) | pthread workers, mutex/condition/timed waits, output drainers and task lifetime beyond one call. | Depend on the separately assigned [native threads/runtime plan](../interfaces/threads.md), or explicitly scope a native serial adaptation; current one-task libc is insufficient. |
| [Terminal](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/terminal/input.c) | termios raw mode, ioctl size, poll, ANSI, timed escapes/paste and threaded interrupt watcher. | Adapt to native console/readiness/resize/interrupt contracts and restore terminal ownership on exit. `isatty` exists; the other interfaces are not implemented. |
| [Files/sessions](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/session.c) | modes including 0600, locks, link counts/timestamps, symlinks, staged fsync/rename and atomic-append assumptions. | Native stat has type/size, creation supports 0666, and [append uses bounded native EOF placement](../interfaces/atomic-append.md). Specify privacy/concurrency/durability; do not fabricate Unix metadata or locking. |
| Optional integration | Git queries, host clipboard helpers/OSC52 and monotonic clock. | Explicit supported features or unavailability. Missing standard functions with real native semantics belong in libc; optional Git metadata does not authorize a Git port. |

### Separate shared libc networking milestone

A socket layer is needed so curl can build. (The [Git plan](../userland/git.md) no
longer depends on it: it uses go-git over Go networking.)
Scope this separately from the harness and Hax: outbound IPv4 streams over native
TCP/DNS, descriptor ownership and transfer/EOF/shutdown, address resolution,
nonblocking/readiness/deadline behavior, cancellation and native-error mapping.
Expose proven libc interfaces such as socket/connect/send/recv,
getaddrinfo/freeaddrinfo and the needed wait API for the selected curl build.
Do not wrap capability calls privately inside Hax or curl when a standard
function belongs in libc. Do not add POSIX-shaped socket/thread/process kernel
mechanisms or successful fake options; unsupported features must fail explicitly.
TLS and curl/Jansson remain application/port libraries, not base SDK dependencies.
Current libtls is single-task with one active connection per runtime; Hax's
threaded transport needs explicit TLS ownership and concurrency qualification.

### Track 2 tasks, not assigned

- **2a: refresh feature/configuration and upstream pins.** Select actual
  curl/Jansson versions, Meson cross-build and Hax's supported provider/tool set;
  settle native process/background/terminal and persistent-session contracts.
  **After this task, the owner can:** review an exact dependency and native-adaptation plan for Hax.
- **2b: shared libc socket/DNS milestone.** Qualify its selected curl-facing
  interface and authority/error/lifetime behavior independently of Hax.
  **After this task, the owner can:** build real socket consumers over Pyxis TCP/DNS, including a curl candidate for Git.
- **2c: curl/Jansson ports and TLS integration.** Publish dependency PRs first,
  preserve licenses, then pin reviewed heads in Pyxis; qualify POST/SSE locally.
  **After this task, the owner can:** use the selected C JSON and HTTP/TLS libraries on Pyxis.
- **2d: native Hax consumers.** Adapt launch/capture/supervision, terminal and
  session persistence to accepted native contracts; require real runtime support
  for every retained thread/background feature. No Bash-language promise by default.
  **After this task, the owner can:** build and exercise the explicitly supported Hax feature subset locally.
- **2e: integration and manual API qualification.** Verify tools, interactive and
  one-shot use, error/denial/cancel/cleanup and declared persistence/resume limits.
  Use only owner-started small live calls after local checks.
  **After this task, the owner can:** use the qualified Hax port within its documented grants and feature limits.

Before any port build, the owner must mirror the
[Hax archive at the inspected commit](https://github.com/OleksandrChekhovskyi/hax/archive/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0.tar.gz); curl/Jansson mirror
entries follow their selected upstream releases. No upstream fallback or compiler
container rebuild is assumed. This proposal starts neither track's code nor the
shared socket or thread milestone.

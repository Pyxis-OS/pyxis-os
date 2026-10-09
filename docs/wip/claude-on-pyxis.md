# Claude on Pyxis

Status: owner-authorized docs-only proposal, 2026-10-09. Track 1 is a small
in-repo Lua harness using the Anthropic Messages API and four native tools.
Track 2, after the harness, investigates a Hax port. Decisions 1 and 3 remain
proposed defaults; decision 2 records the owner's development credential policy.
None authorizes implementation.

Source inspected: Pyxis `28051508528fdd11faf848bb48d41bee80d960eb`, userland
`74f3e2294c1153ccf00a395b3a451e03d7bbbf68`, ports
`6d63971dbad8683d89d552c526b68486a53ee09d`. No builds, QEMU runs, credential
reads or paid API requests were performed for this proposal.

## Track 1: Lua Messages harness

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
First slice is non-streaming; SSE is a later optional task.

### Inspected support and gaps

| Need | Current behavior | Proposed task |
| --- | --- | --- |
| Files and directories | Lua has real `io` reads/writes, seek, rename/remove and `pyxis.dir`. Directory enumeration accumulates the whole table and reports changed-generation errors. | Project-relative text tools; bounded enumeration/results, explicit errors and same-parent replacement for writes. Flush/close are not durability; explicit sync needs a binding if durable writes are promised. |
| HTTP POST | [libhttp](../userland/http-fetch.md) sends GET with fixed headers, no request body; successful bodies are limited to 200/204. Other statuses retain no error body; generic response headers are not exposed. | Separate bounded request operation: POST, validated caller headers/body, complete response status/body including errors, selected diagnostic headers. Preserve GET/provider behavior. |
| Longer deadline | Libhttp clamps even a supplied deadline to 30 s. Native TCP calls independently reject deadlines more than 30 s ahead; TLS passes that deadline through and treats timeout as terminal. | One explicit overall deadline with bounded native-call waits underneath; adapt TLS timeout handling without replaying already sent requests or enlarging kernel TCP limits. |
| Lua HTTPS | `pyxis` exports `run`, `dir`, `sha256`; no HTTPS binding. Libhttp/libtls are userland application libraries built after ports. | Userland embedding/binding, using the exported Lua core and shared native registration, not a ports-to-userland build cycle. |
| Crypto ownership | Lua's native module and libtls each define the entropy hook and own PSA initialization, allocator hooks and teardown. | One process-local crypto owner in the embedded application; preserve existing Lua hashing and TLS behavior. Directly linking both owners is invalid. |
| Command output | `pyxis.run` inherits current streams and waits; changing Lua `io.output` does not redirect child stdout. No capture/deadline option exists. | Native launch with explicit FILE streams, result/deadline observation, capped tool results and cleanup. No `io.popen` or fake `os.execute`. |

Existing [TLS](../userland/https.md) verifies chain, DNS name and dates with
explicit random/clock/TCP grants and public/custom trust roots. Keep that policy.
The POST operation owns framing headers: callers cannot inject Host, duplicate
Content-Length, transfer encoding or CR/LF. Send a fixed-length body, do not
follow credential-bearing redirects, downgrade TLS or replay POST automatically.
An error after transmission can leave delivery/billing unknown; report it and
let the owner decide whether to retry. HTTP status, request ID and bounded error
body are useful diagnostics; request headers and credentials are not logs.

Select [dkjson 2.11](https://dkolf.de/dkjson-lua/dkjson-2.11.lua), MIT, pure Lua,
without optional LPeg. Its versioned source has author-published SHA-256
`197cb50834c642f84b4cf99fe724932c50e6d9c92faec7ad89aa25e91df4d481`
([downloads](https://dkolf.de/downloads)); the inspected bytes match. Preserve
the embedded copyright/license and record the source/hash in the vendor notice.
It preserves object/array distinctions and an explicit JSON-null value; empty
tool-input objects must remain objects. Its permissive decoder is not a complete
strict-JSON validator: bound input/nesting, require complete consumption, validate
UTF-8 and every tool schema, and never evaluate response text as Lua code.

Before vendoring or building, the owner needs a mirror/cache entry for that
versioned dkjson file. No upstream fallback. Lua, Mbed TLS and the HTTP parser
already have pinned recipes; no compiler-container rebuild is proposed.

## Three owner decisions, with defaults

1. **Where does the native binding live? Default: a first-party userland
   application embedding the exported Lua core.** The harness logic remains Lua.
   Run the agent through trusted, dedicated-space startup, with the selected
   project/work views and supervision launcher; it is not ordinary `lua script`
   inheriting a general development shell's roots.
   Share the existing native `pyxis` registration rather than copying its source;
   make its reusable export and crypto ownership explicit in coordinated
   userland/ports work. In the embedded process, libtls supplies the one crypto
   owner; hashing must borrow that state instead of reinstalling global hooks.
   This respects SDK → ports → userland ordering and keeps HTTP/TLS application
   libraries out of the base SDK. A separate HTTPS helper process would avoid
   shared crypto but add request/credential transport and lifetime machinery.

2. **Where does the API key live? Owner direction: a saved development key in
   one file, delegated read-only only to the harness.** Trusted startup opens
   that file and passes a dedicated named FILE grant, not its containing
   directory. The native HTTPS binding performs a bounded read, closes the grant
   and holds the key in process-private memory, adding it only to the verified
   Messages endpoint under the
   [API authentication contract](https://platform.claude.com/docs/en/api/overview).
   The file is never part of the project view, shared `tmp://`, a child's roots
   or resources, the boot image or PXE staging. Do not expose the key or its FILE
   grant as a Lua global, tool input/result, environment variable, argument,
   log, transcript or request-header dump. No key value belongs in the repository
   or documentation; a missing or invalid grant stops before any API request.

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
   per boot with [xfer](remote-file-transfer.md), before starting the harness.
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

3. **What may the first agent loop do? Default: automatic execution of the four
   declared tools within one explicitly delegated project, in a finite run.**
   Command input is a native argv array, not Bash syntax. Child tools receive
   selected project/runtime/network grants, no descendant launcher, administrative
   grants or credential. Trusted startup explicitly supplies a same-space
   CREATE_GROUP launcher to the harness, which stays outside each command group;
   ordinary foreground Lua's LAUNCH-only grant is insufficient for this role.
   Make the model and budgets explicit per-run settings. Proposed defaults are
   20 Messages requests, `max_tokens` 4096 per request, 64 KiB serialized request,
   1 MiB response, 16 KiB returned file/output chunks, a 300 s overall request
   deadline and a 60 s command deadline. The owner selects the model and starts
   each live run manually; keep the first live exchange small. At the end of
   every run, including a stopped or failed run, report API-reported input/output
   tokens per request and their run totals. Mark missing usage as unknown rather
   than counting it as zero; totals cover only requests with reported usage.
   These are draft working budgets, not exact billing limits; stop when exceeded
   without hidden retry, silent history truncation or another paid call.
   Streaming, shell-language execution and background tools require later scope.

## Authority, capture and validation

The harness gains only its space's explicitly supplied grants. Data authority
must be a project directory view, not an entire HOST/home tree selected merely
by a string prefix; supply that view through native launcher startup.
File tools accept only project-relative components, rejecting parent traversal,
absolute/scheme paths and NULs. Child cwd starts at the same project boundary.
Auxiliary memory, clock, random, console, launch and read-only runtime roots remain
explicit. It receives no raw-disk, mount, power or space-factory authority.
Trusted startup creates/delegates a private RAM work-volume root for capture
storage only to the harness; it is not the shared `tmp://` tree and is not
delegated to child roots, cwd or resources. Create then unlink captures there
before launch; no anonymous-file factory or shared-tmp privacy is assumed.
Project bytes included in prompts/tool results are sent to Anthropic; automatic
write/command tools can overwrite/delete project data and make connections under
delegated network authority. This is the exposure the owner chooses, not a new
destination sandbox. Keep harness code/resources outside the writable project.

First command capture uses separate private RAM-backed stdout/stderr files and
closed/absent stdin. Also omit named console input/output, keyboard/pointer,
display and terminal-service grants so the child cannot bypass captured streams.
Preserve ordinary `pyxis.run` behavior separately. Separate files avoid stream offsets overwriting
each other; chronology between streams is not promised. Observe the child while
it runs, inspect output size, stop it at a proposed 1 MiB per-stream trigger or
deadline, and return capped output with truncation and actual exit/fault/stop
status. The size trigger is polled, **not a hard backing quota**; fast output can
overshoot and physical exhaustion can still fail. No descendant launch grant
means a direct child cannot leave launched descendants holding capture files.
Launch that child through a fresh [execution group](../interfaces/execution-groups.md)
and seal its admission. The harness holds the sole CONTROL grant: its closure on
exit, fault or forced stop terminates the group even when polling has disappeared.
Closing a process observer alone would not stop a child. Termination requests do
not fake completion: observe actual process/group cleanup and release captures
afterward; published HOST work can delay cleanup without a fixed bound.

Pipes have bounded buffers but currently lack deadline/try reads and readiness
integration; draining two blocking streams can deadlock, and silent commands can
block forever. Pipe/event capture and descendant tooling are later native
contracts if tools need shell pipelines. Do not sneak them into
the first Lua harness or simulate POSIX process/signals.

Qualification uses manual local HTTP/HTTPS fixtures first: header/body framing,
error bodies, rejection/partial transfers, a response beyond 30 s, timeout without
replay, denial, allocation cleanup and unchanged GET behavior. Test JSON, tools
and loop transitions against local recorded responses; CI and automatic tests
never spend API credit. The owner manually starts a small real Messages/tool
exchange using the existing small credit after local qualification. No key is
requested by this proposal or captured in its artifacts. Record exact revisions,
QEMU configuration, plain-launch/session baseline for substantial changes,
response/capture memory and actual timeout/cleanup behavior; no paid benchmark.

## Track 1 delivery tasks after approval

- **1a, small first task: bounded libhttp POST and overall deadlines.** Add the
  request operation, response/error ownership and required TLS/native-wait deadline
  adaptation. Use local fixtures; Lua, credentials and Anthropic calls stay out.
  **After this task, the owner can:** send a bounded native C POST and inspect its response, including slow/error cases.
- **1b: shared Lua/native HTTPS integration.** Wire the userland embedding,
  reusable `pyxis` registration, one crypto owner, verified HTTPS and private
  credential-file delegation/loading with the installed, QEMU and live/PXE
  provisioning paths above. Preserve ordinary Lua `run`/`dir`/hash behavior.
  **After this task, the owner can:** make an explicitly authorized HTTPS request from the embedded Lua application.
- **1c: pinned pure-Lua JSON.** Vendor mirrored dkjson with license/provenance,
  object/null handling, bounded response parsing and schema validation.
  **After this task, the owner can:** construct and decode Messages/tool data locally without API spending.
- **1d: native command capture.** Add explicit stream/grant selection, per-command
  groups, deadlines, output triggers, result caps and retirement; exercise silent, failing,
  noisy and denied commands in QEMU/GDB, with no provider calls.
  **After this task, the owner can:** capture a native command's stdout/stderr and real completion for a tool result.
- **1e: Lua harness loop.** Implement the four tools and validated tool-use/result
  exchange, bounded context, explicit per-run settings, end-of-run per-request
  and total input/output token usage, status reporting and deliberate stop behavior.
  A manual small live exchange follows local qualification; no automatic spending.
  **After this task, the owner can:** ask Claude to inspect/edit a small project and run a direct native command on Pyxis.

Each task needs a separate owner go. Optional streaming follows as its own task;
the non-streaming harness does not depend on curl, sockets or Hax.

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
| [Background tasks](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/tools/task_registry.c) | pthread workers, mutex/condition/timed waits, output drainers and task lifetime beyond one call. | Depend on the separately assigned [native threads/runtime plan](threads.md), or explicitly scope a native serial adaptation; current one-task libc is insufficient. |
| [Terminal](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/terminal/input.c) | termios raw mode, ioctl size, poll, ANSI, timed escapes/paste and threaded interrupt watcher. | Adapt to native console/readiness/resize/interrupt contracts and restore terminal ownership on exit. `isatty` exists; the other interfaces are not implemented. |
| [Files/sessions](https://github.com/OleksandrChekhovskyi/hax/blob/9fe48c4a3a1469b63c27b9d1c83ff0539fb459f0/src/session.c) | modes including 0600, locks, link counts/timestamps, symlinks, staged fsync/rename and atomic-append assumptions. | Native stat has type/size, creation supports 0666, and append is SIZE then WRITE. Specify privacy/concurrency/durability; do not fabricate Unix metadata or locking. |
| Optional integration | Git queries, host clipboard helpers/OSC52 and monotonic clock. | Explicit supported features or unavailability. Missing standard functions with real native semantics belong in libc; optional Git metadata does not authorize a Git port. |

### Separate shared libc networking milestone

The [Git plan](git-on-pyxis.md) also needs a socket layer so curl can build.
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

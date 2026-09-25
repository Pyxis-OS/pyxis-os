# Userspace URI scheme providers

Status: agreed design direction for later work, not an implementation task or a
frozen ABI. It builds
on the [namespace direction](../vfs.md) and [named endpoints](../spaces.md#named-endpoints).

## Intended use

A userspace service could expose resources through a URI scheme, allowing:

```c
FILE *file = fopen("http://example.com/hello.txt", "r");
```

and ordinary tools to consume the same resource:

```text
cat http://example.com/hello.txt
```

The provider implements fetching; a separate download utility is unnecessary
for this simple read. This does not eliminate the HTTP client implementation
or provide every option of a tool such as curl.

Both HTTP and HTTPS clients belong entirely in userspace. HTTP parsing, headers,
redirects, TLS, certificate verification and response caching stay there. The
kernel supplies network primitives, IPC, capability enforcement and namespace
binding/routing. It must not fetch URLs or contain an HTTP/TLS implementation.

## Binding, routing and authority

An authorized application registers a provider endpoint under a scheme within
a namespace. Init can establish default bindings and delegate a namespace to a
space or process. Registration and replacement require explicit authority; there
is no unrestricted global registration table.

Opening a URI selects a binding in the caller's namespace and routes an open
request to that provider. The provider returns an opened-resource capability or
an error. Libpyxis/libc adapts that resource to the file interface, including
`FILE *`. Kernel routing only needs the generic scheme/binding contract; the
provider interprets the scheme-specific address and protocol.

Knowing a URI does not authorize access. The caller needs the applicable binding
and operation rights. The provider's own network grants limit its access, but
are not a substitute for enforcing caller policy: a shared privileged provider
must not turn a restricted caller's request into unrestricted network access.
Define how those restrictions are represented and checked before implementation,
including redirects and cache hits.

Changing a binding affects subsequent resolution, not already opened resources.
Existing handles retain their original object/provider relationship. Provider
exit must produce a defined closure/error result and wake blocked clients;
it does not silently rebind their handles to a replacement service. Registration
ownership, namespace inheritance, unregistration, restart and explicit revocation
still need concrete lifetime rules.

The current URI path helpers resolve directory roots in userspace. Kernel-managed
provider bindings would be a new facility, not a description of existing code.
Keep ordinary directory traversal and the provider-open contract distinct; an
HTTP URI need not be represented as a tree of remote directory objects.

## Media-type scheme aliases

Use `json+http://` and `json+https://` as explicit userspace provider conventions.
A read requests `Accept: application/json`; a write declares its supplied body
with `Content-Type: application/json`. The prefix selects a media type, not a
request method. It does not serialize data, infer types from leading bytes or
promise that the supplied body is valid JSON.

Accept expresses a response preference, not a guarantee of JSON. Decide whether
the provider rejects a mismatched response type when implementing this alias.
See [Accept](https://www.rfc-editor.org/rfc/rfc9110.html#section-12.5.1) and
[Content-Type](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.3).

Register each complete scheme name through the same scoped binding mechanism.
The kernel treats `json+https` as a binding name; it does not split the name into
protocol layers or interpret JSON, HTTP or TLS. The userspace provider translates
the alias to the underlying HTTP(S) URI. More aliases and request options can be
designed when needed; this is not a generic composition framework.

## Initial HTTP file policy

Support ordinary finite web downloads even when their total length is not known
upfront. The initial provider stages the complete response body under a byte
budget and an overall deadline, then returns a read-only, sized resource. The
file consumer sees stable bytes and explicit-offset reads; it never receives an
unbounded live stream. Open may block while fetching, and must report failure if
completion cannot be reached within the limits.

Finite responses can omit `Content-Length`. HTTP/1.1 chunked framing carries chunk
sizes rather than an upfront total; connection-close framing is also possible.
There is no universal streaming flag. Apply the protocol's framing rules to
determine completion, and reject conflicting framing rather than trusting a
length beside `Transfer-Encoding`. Close-delimited completion cannot itself
prove that the origin intended to send no more data.
See [HTTP/1.1 body framing](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3).

When supplied and applicable to the body, validate `Content-Length` and use it
for early size-limit rejection. Zero is valid; absence alone is not an error.
Interpret method/status semantics before length, and parse lengths with overflow
checks. A separate HEAD request is not proof of the length of a later GET.
See [Content-Length semantics](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.6).

The exposed file size describes the bytes actually retained for the caller.
Content decoding must not confuse transmitted encoded length with the resulting
file length, and any decoding needs its own output bound. A simple initial option
is to request identity encoding and reject unsupported content encodings; settle
that exact policy before implementation.

A declared length is not permission for unchecked allocation. Enforce the byte
budget as data arrives whether or not a length was declared. Malformed framing,
a declared-length mismatch, a size limit or deadline expiry fails the open and
releases staging storage; never publish a partial result as a complete file.
An indefinitely continuing response eventually fails the same limits. Exact
budgets, staging storage, HTTP versions and status/redirect mapping remain open.

## Open-resource lifetime and random access

Each successful open returns a read-only snapshot backed by the complete retained
response body. Its size and bytes stay fixed for that resource's lifetime. Reads
at any supported offset use those bytes, without another network request or a
dependency on the server's range support. A remote edit does not change an
already opened resource.

The opened resource owns the retained body. Copied or delegated handles refer to
that same snapshot; closing one handle does not invalidate the others. Release
the body after its last reference is gone and any in-flight reads have finished.
Process teardown releases its references through the normal handle lifecycle.
No HTTP-specific lifetime management is required in the kernel.

Initially, independent opens fetch independent snapshots, even for the same URI.
There is no retention for reuse after the final close. This per-open storage
provides random access; a future cache across opens is a separate feature.
Account for both downloads in progress and completed bodies pinned by live
handles. A byte budget must reject or delay new opens rather than evict storage
that an existing handle still owns.

## Future writes and shell operations

Writable resources follow the initial read-only provider. For HTTP endpoints,
writing means submitting a body for processing. Use this mapping consistently
in the shell and libc:

| Consumer operation | Proposed HTTP behavior |
| --- | --- |
| Read with `cat` or `fopen(..., "r")` | GET, returning a retained snapshot |
| Write with `>` or `fopen(..., "w")` | POST the complete staged body |
| Write/read with `fopen(..., "w+")` | POST the staged body, then read its response |
| Explicit replacement | PUT; API and shell spelling remain undecided |
| Remove with `rm` | DELETE |
| Append with `>>` or `fopen(..., "a")` | Unsupported without a defined provider append contract |

This is an intentional endpoint convention: `"w"` submits data rather than
replacing a remote file. Ordinary local-file modes and redirections retain their
existing meanings. PUT expresses creation/replacement, not append; neither PUT
nor POST supplies a universal append operation.
See [HTTP methods](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3).

Illustrative future syntax for posting a JSON body:

```sh
printf '%s' '{}' > json+https://example.com/jobs
```

The userspace provider performs the POST. The JSON alias declares the body's
media type; it does not select the method. An editor writing through the same
write mode would also submit with POST; choosing replacement requires the
explicit PUT operation rather than inferring it from an editor save.

For `"w+"`, writes build the request body and reads consume the POST response,
not a separate GET or a readback of the staged request. This is an endpoint
transaction, not an ordinary seekable update stream. Define the submission
boundary before implementing the libc adapter; an internal buffer flush must
not accidentally submit a request. Exact commit, response-status and stream
positioning interfaces remain open.

A proposed write contract stages bytes under a budget and requires an explicit
commit. Individual writes do not issue requests. Destroying an uncommitted resource
discards its staged body; closing one copied grant does not destroy a resource
still retained by another process. Shell redirection could
commit after the producing command succeeds; an editor needs a save hook that
commits and reports the result. Ordinary file writes alone do not provide these
transaction boundaries, and editor temporary-file/rename saves need an explicit
adaptation rather than an assumed HTTP rename operation.

Commit submits one complete request and reports its outcome, including access to
response status and body. A lost reply can leave the remote outcome unknown;
there is no exactly-once guarantee, and POST must not be blindly retried. Settle
commit ownership across copied handles, cancellation, conflict detection and
failure reporting before implementation. Write, submit and delete authority must
be explicit; a readable URI does not grant any of these operations.

## Prepared requests and shell handoff

A future `http` helper could prepare a request with URL, method and request-scoped
headers, then return a capability to the shell. It uses the userspace provider;
it does not contain another HTTP implementation. The shell can grant a body
writer to `echo` or another producer, and later a response reader to `cat` or jq.
This uses the same [capability-based stdio bindings](shell-streams.md) as pipes
and ordinary file redirection. The commands only read/write their given streams.

The helper must hand back a real grant through an authorized capability-transfer
path, not print its numeric handle. The shell receives a local grant that keeps
the resource alive after the helper exits. Initial copied grants are sufficient;
there is no requirement to move ownership. Define the running helper-to-shell
handoff separately from existing launch-time delegation and current request-side
endpoint copies; printing a handle or embedding one in reply bytes cannot do it.

The intended POST lifecycle is prepare, write the staged body, explicitly submit,
then read the response. The shell could retain submission authority while granting
the producer only body-write access, submit after successful producer completion,
and discard an unsubmitted request on failure. Completion/flush of an individual
write is not submission. Exact rights, response access and cancellation remain
open; header policy, including credential handling across redirects, belongs to
the provider and the specific request rather than a global default.

A motivating workflow is to submit a login body, extract a bearer token with jq,
and prepare another request with that Authorization header. Shell syntax for
holding and reusing capabilities remains open; HTTP helpers and capability-valued
variables are later ideas, not prerequisites for the first shell pipelines.

## Future response cache

A later userspace cache may retain complete responses for a limited time and use
LRU to evict entries under a byte budget. LRU selects what to discard; freshness
policy determines whether retained data may satisfy a new open. Retain only
completed, validated bodies. Cache capacity, maximum object size, lifetime caps
and accounting remain explicit policy choices, not kernel HTTP behavior.

Reuse must respect HTTP caching rules, including `Cache-Control`, validators,
`Vary`, and restrictions on authenticated/shared responses. A local time limit
must not authorize reuse forbidden by the response. Consult
[HTTP caching](https://www.rfc-editor.org/rfc/rfc9111.html#section-4) when defining
this milestone. Namespace/authority and credentials belong in the cache's
isolation policy; a URL alone is not a sufficient sharing boundary.

Eviction removes reuse eligibility; it must not invalidate storage still owned
by open handles. Account for that retained storage too. A response whose length
was initially unknown can become a cache candidate after bounded completion;
partial or still-arriving bodies cannot satisfy another open.

## Compiler experiment

A future compiler could resolve:

```c
#include "https://example.com/foo.h"
```

through its granted namespace. A motivating example is fetching a raw GitHub
header such as `stb_image.h` and including it in a TCC-built program. Use the raw
content endpoint rather than GitHub's rendered source page; a URI shaped like
`https://raw.githubusercontent.com/nothings/stb/<commit>/stb_image.h` pins the
selected revision when `<commit>` is replaced with its actual commit ID.
Neither `cat` nor the compiler should require an upfront HTTP length to consume
a response that finishes within the provider's limits.

Compiler include lookup would need URI handling,
relative includes based on the containing resource, and defined file identity.
`fopen` support alone does not guarantee that an unchanged compiler accepts or
resolves these names correctly. The caller still needs provider authority.

Direct fetching could serve interactive experiments. Repeatable builds should
pin content and use controlled dependency storage; a time-limited response cache
does not itself make a moving URL reproducible. No compiler or toolchain changes
are assigned by this idea.

## SQLite views and query results

SQLite is a second proposed provider consumer, after a native userspace library
and CLI port. Start with read-only views or published queries whose serialized
results are usable through ordinary file readers. Illustrative future syntax:

```sh
ls sqlite://catalog/
cat json+sqlite://catalog/apps
cat json+sqlite://catalog/apps | jq '.[].name'
```

Here `catalog` names a database binding and `apps` could name a table view or
published query. These names and JSON representation are proposals, not an ABI;
decide NULL/blob representation and row ordering when defining the first slice.
Enumeration also needs an explicit directory/provider contract: supporting an
HTTP open does not automatically implement `ls` for another scheme.

The userspace provider receives authority over the database through capabilities
and checks which views the caller may read. A database name does not grant access
to arbitrary backing files or every table. SQL parsing, query execution and
serialization stay in userspace; the kernel only handles generic routing, IPC
and resource authority. In this provider, `json+sqlite` selects an output format,
not HTTP headers or transport behavior.

Each successful result open would expose a bounded, fully materialized read-only
snapshot with stable size, contents and offsets until its last reference closes.
Subsequent database changes do not rewrite an existing result. Set query time,
row/output-byte and storage budgets before implementation. Read-only queries
still consume resources; do not publish a truncated result as complete. A first
slice can use fixed published views before admitting caller-supplied SQL.

## Later database sessions

A later endpoint could accept queries, parameters and mutations through an
explicit session. The open session resource identifies the interaction; there
is no need to infer a session from a PID or trust caller-supplied process identity.
Separate opens create independent sessions. If copies are allowed, they refer
to the same session, so sharing and serialization require a deliberate contract.

The initial thought was to write SQL into a transaction handle, submit with a
marker such as `go;`, then read the response from that handle. Keep the interaction
idea, but prefer structured requests for statement submission, result fetching,
commit and rollback. Writes can split or combine statements, and a textual marker
can occur inside SQL; ordinary byte writes do not define request boundaries.
Prepared statements and bound parameters should not need URI-string encoding.

Mutations need explicit authority and commit. Uncommitted work should roll back
when the session is destroyed, not when any one copied handle closes. Define
provider/client failure, pending operations, transaction lifetime and commit
outcome reporting before implementation. A lost reply must not prompt blind
re-execution of a mutation. These are later database contracts, not requirements
for the first read-only view.

## Database worksheet experiment

A future editor could open a worksheet such as:

```text
sqlite://catalog/worksheets/scratch
```

The motivating interaction is to type `SELECT * FROM apps;`, save/execute, and
see the result appear in the editor. Preserve the query, result and error as
separate state so the output does not destroy the query or a syntax error erase
the user's work. Exact worksheet lifetime, names and editor UI remain open.

Saving does not ordinarily make an editor reread a file. Investigate automatic
change detection alongside explicit reload or a small editor integration.
Neovim's current development documentation describes `autoread`, timestamp checks
and libuv filesystem watchers; unmodified buffers can reload external changes.
See [timestamp/change detection](https://github.com/neovim/neovim/blob/master/runtime/doc/editing.txt)
and [autoread](https://github.com/neovim/neovim/blob/master/runtime/doc/options.txt).
The selected Neovim port would need corresponding Pyxis metadata/notification
support or an explicit provider-aware refresh. A synchronous result replacement
during save can be mistaken for the editor's own completed write, so autoread
alone does not establish a reliable submit/result sequence. Distinct query and
result resources or an editor hook are candidates to resolve that ordering.
Existing read-only result snapshots would still remain immutable; a refresh
opens a new result. Temporary-file and
rename-based saves also need deliberate handling rather than pretending they
execute SQL. Neovim itself remains a separate future port.

Restrict the first experiment to read-only queries. Any later mutating worksheet
requires explicit execution/transaction controls: autosave must never commit a
database change. This is a possible consumer of the session protocol above, not
a command language or editor feature to implement alongside the first provider.

## Prerequisites and decisions

Before an implementation milestone, settle:

- Registration/lookup requests, namespace scope and delegation, binding lifetime,
  provider death and how policy follows a routed request.
- Reply-side capability transfer and resource ownership. Current
  [endpoints](../endpoints.md#copying-a-capability) transfer a capability with a
  request only; returning a new handle requires real transfer support, not a
  numeric handle embedded in reply bytes.
- URI/request transport beyond the current endpoint's small inline payload,
  server-side file resources or bounded materialization, timeouts, cancellation
  and resource accounting. No private pointers may cross process boundaries.
- TCP, name resolution for hostnames, the userspace HTTP implementation, HTTPS
  library and trust configuration. TLS stays out of the kernel regardless of
  library choice.
- HTTP status mapping, redirects, encoding, size/deadline limits and offset-read
  storage for the first read-only provider. Cache and compiler work come later.
- For a later SQLite consumer: the [SQLite port](application-ports.md), view
  grants and binding policy, consistent result generation, serialization and
  query budgets. Session mutations and editor worksheets follow independently.

This is a future consumer of networking, IPC and namespace work. It is not a
reason to add placeholder syscalls, provider registries or protocol adapters to
unrelated milestones.

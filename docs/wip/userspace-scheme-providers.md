# Userspace URI scheme providers

Status: future extensions to the [implemented userspace services](../interfaces/userspace-services.md).
Read-only HTTP/HTTPS, byte snapshots, scoped binding, capability transfer and export
lifetime are implemented; their contracts live in the subsystem docs. Writes,
media-type aliases and streamed HTTP are the separate
[native HTTP requests](native-http-requests.md) milestone. Richer representations,
caching, SQLite and Git remain later work. [Verified HTTPS with Mbed TLS](../userland/https.md) is implemented with
packaged public roots and optional per-instance augmentation. The URI
examples below are proposals unless identified as existing behavior.

## Implemented foundation and extension boundary

Ordinary `fopen("http://example.com/hello.txt", "r")` and
`cat http://example.com/hello.txt` use the [file-provider bridge](../interfaces/file-providers.md).
The [HTTP/HTTPS providers](../userland/http-fetch.md) stage a complete bounded body and return
an immutable snapshot. Copies retain its bytes; independent opens fetch again.
Reads use retained bytes at explicit offsets without new range requests.
Retirement governs storage release, and provider death invalidates its exports.

[Namespaces](../interfaces/namespaces.md) supply exact-name publication, atomic replacement
and explicit startup delegation. Libpyxis selects the URI scheme; the provider
interprets the full URI. Directory roots retain their existing traversal, and
ambiguous directory/provider bindings fail. Removal or replacement changes future
discovery without retargeting held grants. There is no automatic restart or
rebinding.

HTTP parsing, redirects, TLS, certificate verification and cache policy belong
in userspace. The kernel supplies network primitives, bounded IPC, capability
enforcement and namespace bindings. Existing per-space HTTP publication is not a
network-destination sandbox. A future shared provider needs an explicit caller
policy before adding redirects, credential handling or cross-caller cache reuse;
its own network grants alone do not enforce those restrictions.

## Discoverable resource representations

The working conceptual model separates the resource from the caller's grant:

- A resource has identity, lifetime, supported operations and, where useful,
  available representations.
- A capability holds a resource reference and granted rights. Handles name
  those grants in a process's table; names resolve within a namespace.

Representations describe how content or results can be consumed; they are not
required for every resource. A process-control resource may chiefly expose
operations. This is a design guide, not a universal kernel class or framework;
the current [object/capability contract](../interfaces/processes.md#objects-capabilities-and-handles)
already defines references and rights, while representation discovery is future
work.

The existing OPEN reply declares a byte representation and optional media type.
A later interface could let consumers discover alternatives and select one.
Negotiation, structured-data contracts and their authority checks remain open.

For example, `json+sqlite://catalog/apps` explicitly requests serialized JSON.
An explorer opening `sqlite://catalog/` could discover the `apps` view, then ask
for rows with column names and types when the user opens it. Ordinary readers
could use a text representation, while jq consumes JSON. Directory discovery and
result representation are distinct: listing a view does not describe its columns.

These are representations of the same authorized resource. Selecting a format
must not broaden access, execute a mutation or require the consumer to guess from
a filename. A prefix is a convenient explicit selection; discoverable metadata
lets a consumer choose without knowing the prefix convention. Complete aliases
remain scoped bindings, not a kernel parser for composable URI prefixes.

This could allow a generic explorer/table viewer to display database results
without SQLite-specific UI. Keep provider serialization and interpretation in
userspace, and define unsupported-format errors when designing the contract.
These consumer examples motivate a later contract; they do not add a general
negotiation framework to the existing OPEN interface.

## Media-type scheme aliases

For HTTP, `json+http`/`json+https` select JSON Accept/Content-Type, not a method,
JSON serialization or validation. Exact-name scoped bindings remain the kernel
boundary. Alias delivery belongs to [native HTTP requests](native-http-requests.md);
other representation/SQLite aliases remain later ideas.

## Future writes and shell operations

The owner's 2026-09-24 direction was GET for `cat`/`"r"`, staged POST for `"w"`,
and staged POST followed by response reading for `"w+"`. Accepted Claude
preparation/commit rules subsequently settled one-shot submission and explicit
body/control/response authority. On 2026-10-11 the owner moved this shared work to
[native HTTP requests](native-http-requests.md), adding no artificial response-size
cap, streamed GET/POST completion and large request staging. The streamed-response
replacement remains a proposed decision there; nothing here marks it implemented.

That milestone owns `printf '{}' > json+https://…`, explicit shell-coordinated
commit and response access. Ordinary writes, flush/close and editor autosave do
not submit; PUT/PATCH/delete and editor-specific execution remain later scope.
Reads through a newly opened URI are GET, never the previous POST response.

## Prepared requests and shell handoff

[Native HTTP requests](native-http-requests.md#carried-forward-accepted-contract)
owns the accepted real-grant handoff into private shell bindings and the
libc/Lua staging/response adapters. Printed numeric handles or byte replies cannot
transfer authority. Producers receive body WRITE, consumers response READ plus
read-only completion observation under the proposed streamed contract; only the
coordinator commits/aborts. Request-private credentials and unknown-outcome/no-replay
rules are preserved. A login/token workflow is a consumer of that shared facility,
not a second helper implementation or an ambient header store.

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

Implemented: TCC resolves `#include "https://…"` through the caller's namespace;
see [includes by URI](../userland/tcc.md#includes-by-uri). Repeatable builds pin
immutable URIs; a time-limited response cache would not make a moving URL
reproducible.

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

## Git scheme: lowest-priority parked idea

`git://pyxis` could name a repository registered in the caller's namespace,
backed initially by a local repository grant rather than a remote URL. Future
examples include `ls git://pyxis/branches`, `cat git://pyxis/HEAD`, reading
`git://pyxis/commits/main/tree/src/main.c`, inspecting
`json+git://pyxis/commits/main` with jq, and comparing two revision paths with diff.
Resolve a branch to a commit when opening a resource and retain that revision
for consistent reads. Names alone grant no repository authority; Git stays in
the userspace provider. Prepared mutation capabilities could follow much later,
with explicit execution and expected-revision checks. This has the lowest
priority among these provider ideas and must not drive current interfaces;
resource representation is the earlier design concern.

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

## Decisions for later extensions

The [implemented service contracts](../interfaces/userspace-services.md) provide discovery,
request/reply capability transfer, deadlines, immutable files and acknowledged
retirement. Future work builds on those contracts rather than reopening them:

- Namespace overlays, enumeration, search paths and automatic provider activation
  remain deferred; the implemented map uses exact lookup and explicit publication.
- Capability attachments currently copy grants. Ownership-moving transfers need
  a separate failure and lifetime contract.
- Shared provider policy must define caller isolation, credentials, redirects
  and cache reuse. A URL alone does not identify an authority boundary.
- TLS needs a userspace library and trust configuration. It stays out of the
  kernel regardless of library choice.
- HTTP writes, prepared handoff and stream completion are owned by
  [native HTTP requests](native-http-requests.md); other provider mutations still
  need their own commit/cancel, ownership and uncertain-outcome contracts.
- Representation discovery and selection need bounded metadata and resource
  protocols. Shared-memory transport needs its own mapping and lifetime rules;
  private pointers cannot cross process boundaries.
- Asynchronous provider work needs scheduling and cancellation contracts; see
  [HTTP responsiveness debt](../technical-debt.md#http-provider-responsiveness).
- A later SQLite consumer needs the [SQLite port](application-ports.md), view
  grants and binding policy, consistent result generation, serialization and
  query budgets. Session mutations and editor worksheets follow independently.

These extensions need their own bounded implementation decisions. They do not
require placeholder syscalls, registries or protocol adapters in current work.

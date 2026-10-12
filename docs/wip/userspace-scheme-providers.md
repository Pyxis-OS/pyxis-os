# Userspace URI scheme providers

Status: future extensions to the [implemented userspace services](../interfaces/userspace-services.md).
Read-only HTTP/HTTPS ([verified with Mbed TLS](../userland/https.md)), byte
snapshots, scoped binding, capability transfer and export lifetime are
implemented; their contracts live in the subsystem docs. Writes, media-type
aliases and streamed HTTP are the separate
[native HTTP requests](native-http-requests.md) milestone. Richer representations,
caching, SQLite and Git remain later work. The URI examples below are proposals.

## Extension boundary

Read-only `fopen("http://…", "r")` and `cat` use the
[file-provider bridge](../interfaces/file-providers.md) and the
[HTTP/HTTPS providers](../userland/http-fetch.md): complete bounded immutable
snapshots, with exact-name [namespace](../interfaces/namespaces.md) publication,
no automatic restart or rebinding, and the contracts in
[userspace services](../interfaces/userspace-services.md). Extensions build on
those contracts. HTTP parsing, redirects, TLS and cache policy stay in userspace.
Existing per-space HTTP publication is not a network-destination sandbox: a future
shared provider needs an explicit caller policy before adding redirects,
credential handling or cross-caller cache reuse; its own network grants alone do
not enforce those restrictions.

## Discoverable resource representations

The working conceptual model separates the resource (identity, lifetime,
supported operations and, where useful, available representations) from the
caller's grant (a capability holding a resource reference and rights). This is a
design guide, not a universal kernel class; the current
[object/capability contract](../interfaces/processes.md#objects-capabilities-and-handles)
already defines references and rights, while representation discovery is future
work. The existing OPEN reply declares a byte representation and optional media
type; a later interface could let consumers discover and select alternatives.
Negotiation, structured-data contracts and their authority checks remain open.

For example, `json+sqlite://catalog/apps` explicitly requests serialized JSON,
while an explorer opening `sqlite://catalog/` could discover the `apps` view and
ask for rows with column names and types. Directory discovery and result
representation are distinct: listing a view does not describe its columns.
Selecting a format must not broaden access, execute a mutation or require the
consumer to guess from a filename. Complete aliases remain scoped bindings, not
a kernel parser for composable URI prefixes. Keep provider serialization in
userspace and define unsupported-format errors with the contract; these examples
add no negotiation framework to OPEN.

## Media-type scheme aliases

For HTTP, `json+http`/`json+https` select JSON Accept/Content-Type, not a method,
JSON serialization or validation. Exact-name scoped bindings remain the kernel
boundary. Alias delivery belongs to [native HTTP requests](native-http-requests.md);
other representation/SQLite aliases remain later ideas.

## Future writes and shell operations

The owner's 2026-09-24 direction: GET for `cat`/`"r"`, staged POST for `"w"`,
staged POST followed by response reading for `"w+"`. On 2026-10-11 the owner
moved this shared work, with one-shot submission, no artificial response-size cap,
streamed completion and large request staging, to
[native HTTP requests](native-http-requests.md); its defaults are accepted and
none is implemented. Ordinary writes, flush/close and editor autosave do not
submit; PUT/PATCH/delete and editor-specific execution remain later scope. Reads
through a newly opened URI are GET, never the previous POST response.

## Prepared requests and shell handoff

[Native HTTP requests](native-http-requests.md#carried-forward-accepted-contract)
owns the accepted real-grant handoff into private shell bindings and the
libc/Lua staging/response adapters; printed numeric handles or byte replies
cannot transfer authority. Private shell bindings are the first form of
capability-valued variables and must fit the cross-cutting
[capability-valued environment direction](capability-environment.md). Credential
bytes remain excluded from argv/environment/history/logs. The
[accepted task 3 object/rights/lifetime/inheritance contract](native-http-requests.md#task-3-accepted-facets-and-lifetime)
records that boundary and the accepted native completion prerequisite. Capability-valued environment implementation needs a
separate owner assignment.

The shared [native command execution](native-command-execution.md) milestone
supplies selected producer/consumer streams and per-stage results/cleanup. The
HTTP redirection coordinator still owns the rule to commit only after every body
producer succeeds; last-stage pipeline status is insufficient.

## Future response cache

A later userspace cache may retain complete, validated responses for a limited
time and use LRU to evict entries under a byte budget. LRU selects what to
discard; freshness policy determines whether retained data may satisfy a new
open. Capacity, maximum object size, lifetime caps and accounting are explicit
policy, not kernel HTTP behavior. Reuse must respect HTTP caching rules
(`Cache-Control`, validators, `Vary`, authenticated/shared responses; see
[RFC 9111](https://www.rfc-editor.org/rfc/rfc9111.html#section-4)); a local time
limit must not authorize forbidden reuse. Namespace/authority and credentials
belong in the isolation policy; a URL alone is not a sharing boundary. Eviction
removes reuse eligibility but must not invalidate storage still owned by open
handles, which stays accounted. Partial or still-arriving bodies cannot satisfy
another open. TCC already resolves `#include "https://…"` through the caller's
namespace ([includes by URI](../userland/tcc.md#includes-by-uri)); repeatable
builds pin immutable URIs, which a time-limited cache would not replace.

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

- Namespace overlays, enumeration, search paths and automatic provider activation
  remain deferred; the implemented map uses exact lookup and explicit publication.
- Capability attachments copy grants. Ownership-moving transfers need a separate
  failure and lifetime contract.
- Shared provider policy must define caller isolation, credentials, redirects
  and cache reuse.
- HTTP writes, prepared handoff and stream completion are owned by
  [native HTTP requests](native-http-requests.md); other provider mutations need
  their own commit/cancel, ownership and uncertain-outcome contracts.
- Representation discovery needs bounded metadata and resource protocols.
  Shared-memory transport needs its own mapping and lifetime rules.
- Asynchronous provider work needs scheduling and cancellation contracts; see
  [HTTP responsiveness debt](../technical-debt.md#http-provider-responsiveness).
- A later SQLite consumer needs the [SQLite port](application-ports.md), view
  grants and binding policy, consistent result generation, serialization and
  query budgets. Session mutations and editor worksheets follow independently.

These extensions need their own bounded implementation decisions; they do not
require placeholder syscalls, registries or protocol adapters in current work.

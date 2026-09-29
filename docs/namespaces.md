# Service namespaces

A namespace is a kernel-owned flat map of exact names to exported service
clients. It has no process owner or implicit connection to a space. The explicit
namespace-creation service grant allows init to create one, then delegate access.
The [ABI](../include/abi/namespace.h) defines separate LOOKUP and MANAGE resource
rights; namespace handles have zero transport authority and can be copied with
reduced rights.

Each namespace has at most 64 bindings. Names contain 1–63 ASCII letters,
digits, underscores, hyphens, dots or plus signs and are case-sensitive. The
kernel interprets no URI, path, host or application protocol. Native resources,
raw endpoints, receipts, receivers and other namespaces cannot be bound in this
slice. They retain their existing explicit startup/grant routes.

## Binding and lookup

PUBLISH requires MANAGE and a previously absent name. REPLACE requires MANAGE
and an existing name; REMOVE requires MANAGE and releases that binding only.
A binding owns an exported-client reference and fixes both the resource-rights
and transport masks returned by LOOKUP. Publication cannot increase either mask
beyond the publisher's grant. LOOKUP requires LOOKUP authority and installs a new
local handle with exactly those fixed masks. A caller can attenuate it further
through ordinary COPY. Management does not itself imply lookup authority.

Replacement swaps the reference and both masks atomically. A racing lookup
captures either the old binding or the new one; capability-table growth cannot
change the captured target. Lookup retains that client and its captured authority
masks until installation completes or fails, including across a growth request
on the common BSP FIFO. It holds no namespace lock while waiting and retains no
pointer into replaceable capability-table storage. Removal/replacement affects
future lookups, never retargets old handles and never withdraws the old export.
Last namespace release releases its bindings through BSP retirement. A binding is a client reference
and can keep an otherwise unused export alive until removed.

A withdrawn export or a dead provider remains bound, and lookup reports
`CALL_ENDPOINT_CLOSED`. A manager must remove or replace it. No fallback, restart
or automatic rebinding occurs. Closure can race a successful lookup just as it
can race a direct invocation; holding the returned handle does not keep a provider
operational. See [export lifetime](endpoints.md).

Creation uses a typed request on the common
[BSP executor](smp.md#scheduling-and-ownership) FIFO. Publication lends the caller's
capability table exclusively until completion, for fixed binding allocation and
initial grant installation. After publication the caller uses only its saved wait
pointer until notification; early completion cannot enqueue a still-running task.
There is no private-VM mutation or deferred handoff, including for BSP userspace.
The executor runs with interrupts disabled and calls local allocation/installation
helpers directly, without a nested request or wait. Failed installation releases
the new namespace. Results and completion precede notification, after which the
worker makes no further request or loaned-table access.

Publish/remove use no heap allocation. The namespace lock
protects the map and reference capture, but never spans user-memory access,
capability growth, endpoint locking or parking. Final object destruction remains
BSP-owned.

## Startup and discovery

Launch supplies an optional `namespace_grant`: zero means absent, otherwise it is
one plus a grant-list index. That entry must hold a namespace with LOOKUP authority
and must not alias a standard-stream entry. The child receives the installed
handle in `startup_info.namespace`. There is no implicit inheritance, additional
reference hidden in startup metadata, or new startup version.

Ordinary shell children receive LOOKUP only. Trusted session handoff can preserve
explicitly held management authority. A restricted child receives a separately
populated namespace, because lookup authority exposes every name in its map.
Namespace creation and endpoint-service creation are distinct bootstrap grants.

Existing directory roots remain the route for ordinary paths. Launch rejects
names already bound both as a child directory root and in its namespace, even if
the export is dead. Since managers can mutate the map later, userspace resolution
also checks for ambiguity at resolution time. Neither route wins by priority.
Provider FILE/open routing uses the shared [file-provider bridge](file-providers.md);
other protocols continue to use direct named service discovery.

## Init publication

`namespace create` creates a fresh namespace for subsequent shell launches.
Existing children retain their previous namespace grants. The immutable startup
namespace grant remains borrowed until shell exit; replacing a namespace created
by the shell closes that previous owned handle.

`service start NAME IMAGE [ARG...]` requires LOOKUP and MANAGE and launches a
provider with a `publication` endpoint client. The provider
CALLs that endpoint with a setup result and, on success, one exported-client
attachment; the shell installs the binding and acknowledges before the provider
serves clients. A reported setup failure carries no grant and leaves the
namespace unchanged. `service replace`
uses the same exchange but requires an existing name. `namespace remove NAME`
removes only the binding. No numeric handle is used as a cross-process handoff.

`service start --optional NAME IMAGE [ARG...]` logs a well-formed reported setup
failure and permits script continuation. Launch errors, malformed publication,
namespace errors and cleanup failures remain errors. `--read-only`, available
for start and replace, attenuates native directory roots and working-directory
grants to traversal and file reads for that launch. Existing grants are never
expanded. Ordinary service starts retain their original delegation behavior.

The publication launch does not pass the parent's namespace or namespace-creation
service to the provider. Retaining a namespace containing its own binding could
otherwise keep the provider's export alive after external namespace users leave.
Future provider dependencies require deliberate grants, not implicit inheritance.
The command releases its process observer after publication; it does not supervise
or restart the provider. The missing wait-for-registration-or-exit facility is
recorded in [technical debt](technical-debt.md#service-startup-failure-before-publication).

The counter provider offers `--provide` for normal service and `--provide-once`
for a provider that exits after replying once. `counter --lookup NAME` reads its
value; `--add NAME` adds three. `--restrict NAME` confirms namespace management
is unavailable and uses a read-only client copy. `--hold NAME` prints readiness,
retains its looked-up grant for thirty seconds, then invokes that same grant.
Run it in the background and wait for readiness before replacing the binding to
observe the old provider independently of new lookups. The userland README has
the complete manual sequence.

# Userspace services and the first HTTP provider

Status: agreed next milestone after [libc portability](../libc-portability.md).
Tasks 1 through 5 are implemented; see [the endpoint contract](../endpoints.md). Endpoint
creation and process-owned receiver teardown moved forward from task 4 so the
first delivery slice has a real consumer. Task 6 is the next unchecked task.
This document records the selected contracts for handoff between agents; the
[broader provider ideas](userspace-scheme-providers.md) remain future directions where they exceed this scope. Work through the focused
tasks in order, updating their checkboxes in the corresponding PRs. Discuss a
newly discovered semantic conflict before changing these decisions.

## Result and boundary

A userspace process publishes a service, clients discover it through an authorized
namespace, and the service returns capabilities to exported resources. First
exercise this with a small immutable-file provider; then implement read-only HTTP
so ordinary cat, cksum, tee and libc file readers consume a fetched snapshot.

Discovery, delivery and resource protocols are separate contracts. A database
session or window need not pretend to be a file. The shared mechanism must also
support one-way messages and explicit event destinations, without implementing
SQLite, a compositor or their protocols in this milestone.

HTTP/TLS, SQL and window policy do not belong in the kernel. The kernel owns
capability enforcement, generic routing, bounded delivery and lifetime. Providers
implement their protocols and operation-specific authority checks. Do not add a
generic object framework, protocol-schema interpreter or automatic service manager.

## Creation, export and authority

Init delegates an explicit service-creation capability. It permits creating
receiving endpoints and exporting objects; it supplies no network, filesystem,
display or namespace-publication authority. Permission to create is distinct from
resource budgets and future space accounting.

An export associates a provider-owned endpoint, an opaque provider object ID,
one protocol ID, a resource-rights ceiling and a separate transport ceiling. Export returns a client capability;
export control remains separate and cannot be obtained by copying a client grant.
The opaque ID selects provider state, never client authority. Keep IDs unambiguous
through retirement acknowledgment; stale deliveries must never select a new object.

The kernel routes invocation through the held capability. Delivered metadata
includes the export ID, protocol/operation, actual granted rights, payload,
attachments, deadline where applicable and a delivery receipt. Object identity,
rights and delivery metadata come from the kernel, not client-supplied fields.
Reject a mismatched protocol before delivery. The provider checks which rights
its operation requires; the kernel needs no table of every provider operation.

Object rights and endpoint receive/reply authority are different. Possessing a
window capability must not grant access to the compositor's receiving endpoint.
Ordinary copies, launch grants and attachments can only retain or reduce each
authority mask independently. Native objects have zero transport authority; raw
endpoint clients have zero resource rights. Export protocols define resource
rights, while transport SEND and RECEIVE retain the shared delivery contract.
The owned receiver holds CONTROL resource authority and RECEIVE transport.

One process owns each receiving endpoint and its export control in this milestone.
Do not transfer that ownership to another process or let queued/client references
keep a dead provider operational. Provider exit invalidates all its exports and
wakes affected clients. No automatic restart, reconnect or rebinding exists.

## Delivery contract

Evolve the existing endpoint implementation and examples; do not leave a second
competing IPC implementation or preserve the old ABI for compatibility. Concrete
syscall numbers and C layout names belong to implementation, not this draft.

| Operation | Contract |
| --- | --- |
| call | Admit a request or fail immediately; once admitted, wait for reply, abandonment, closure or deadline expiry. |
| send | Admit a one-way message or fail immediately. Success means accepted for delivery, not performed successfully. |
| receive | Obtain one delivery or lifecycle notification, blocking when none is available. One active receiver per endpoint. |
| reply | Complete one delivered call using its single-use receipt authority. |
| finish/close receipt | Finish a one-way delivery; abandoning an unanswered call wakes its caller with an explicit abandonment result. |

Client SEND requires send authority; CALL requires send and receive authority.
Client receive authority permits only that call's response, never reading the
provider's queue. The separate process-owned receiver requires receive authority
for incoming work. There is no dedicated call right or standalone receive
operation on a client handle.

RECEIVE supplies the kernel-authenticated CALL/SEND kind. A SEND receipt has no
reply authority; a rejected REPLY leaves it live. Receipt completion uses CLOSE
through the `endpoint_finish()` helper; no separate finish operation is needed.

A provider can receive further work before completing earlier deliveries. Normal
messages are delivered in admission order; completion order is independent.
A client can grant a separate event destination to a provider, which uses send
for events. No kernel callback system or implicit reverse authority is added.

### Bounds and payloads

- Up to 4,096 application payload bytes per request, reply or one-way message.
  Protocol metadata inside the payload counts; kernel routing metadata and the
  attachment descriptors are separate. Copy only the supplied payload length.
- Up to 64 live or unacknowledged exports per endpoint, separate from delivery slots.
- Up to four explicitly attached capabilities per message, including replies.
  Transfer copies with optional resource/transport reduction; ownership moves are deferred.
- Sixteen outstanding deliveries per endpoint, shared by calls and sends.
  Count queued, delivered-but-unfinished and completed-but-uncollected calls.
  Merely receiving a message does not release its admission slot.
- Reserve request/reply payload capacity when creating the endpoint. Sixteen
  maximum request/reply pairs need 128 KiB of payload storage, plus bookkeeping.
  Keep page-sized payloads off kernel stacks. Creation can fail cleanly.

These are named implementation limits that can be adjusted deliberately, not a
permanent wire-format promise. Admission must reserve the needed storage and
retain attached references atomically. Queue-full or another admission failure
leaves no delivered message or retained transfer. No wait for queue capacity yet.
Accepted sends cannot borrow source references: the sender can close or exit.

Receive/reply transfer must publish all recipient handles or none. Preserve
existing BSP allocation and capability-table growth rules; do not hold an
endpoint lock across allocation or parking. A recoverable receive failure leaves
queued work available. Validate payloads, buffers and grant attenuation before
consuming delivery/reply authority; no partial handle installation on error.
Retain reply attachments until the caller collects them or cancellation/teardown
releases them. A successful transfer leaves the sender's original grant intact.

No private pointer or numeric handle in application bytes conveys authority in
another process. Large data will eventually use explicitly transferred memory
resources, but no new blob or shared-mapping facility is required here. Initial
open requests fit inline; large response bodies use bounded file reads.

### Receipts and failures

Every delivered message has a receipt retaining its unfinished-work state and
target lifetime. Calls additionally carry single-use reply authority. Receipts
are not copyable, transferable or inherited in this slice. One-way receipts must
be explicitly finished or closed; process teardown releases outstanding receipts.
Finishing a receipt does not close separately delivered attachment handles.

Transport failure is separate from a protocol operation's result. Preserve whether
a failed interaction was never delivered or was delivered without a confirmed
result. Delivery does not prove execution, and losing a reply does not prove that
an operation had no effects. Never automatically retry an uncertain mutation.

Invalid reply attempts do not consume a still-live call. A reply to a canceled
call cannot deliver bytes or grants to another caller; the provider must still
be able to finish/close its receipt and release the retained work. The successful
reply, timeout and closure paths must have one authoritative state transition,
not race to complete the caller twice.

### Deadlines and cancellation

Call accepts an optional absolute monotonic deadline; none means an intentional
unlimited wait. An already-expired deadline fails before admission. Pass the
effective deadline to the provider as kernel-supplied metadata. Do not reset a
call's deadline when it leaves the queue or while table growth is pending.

Task 3 implements deadlines in monotonic nanoseconds, with zero meaning
unlimited. SEND remains admission-only. Expiry is checked under the endpoint
lock before committing a reply: a reply committed
before expiry wins even if collected later; a reply after expiry fails even if
the caller has not resumed yet.

Expiry before receive removes the request and reports not delivered. Expiry
after receive wakes the caller with delivered/outcome-unknown and invalidates
reply authority. Notify the provider, but do not wait for its acknowledgment to
release the caller. Caller exit follows the same cancellation ownership rules.
No external cancel-from-another-task API or wait-set API is included yet.

A cancellation notification identifies the provider's existing receipt and
grants no new handle or authority. Finishing the receipt clears any pending
notification for it; a rejected late reply leaves it available to finish.
Expiry before receive requires no provider notification.

Today a single-task caller cannot exit while blocked in CALL, and there is no
external process-termination operation. Task 3 handles the available lifetime
transitions without introducing one. Process kill and Ctrl-C support are
[separate technical debt](../technical-debt.md#process-termination-and-ctrl-c).

Cancellation does not revoke attachments already delivered or free state still
needed by the provider. Delivered canceled work retains its slot until its receipt
is finished or the provider exits. A hung provider can exhaust its own bounded
work capacity; it cannot accumulate unlimited abandoned calls.

Cancellation and retirement notifications do not compete with normal message
capacity. Record pending notifications in delivery/export state and expose them
through receive ahead of ordinary messages. Coalesce redundant notification of
the same transition; do not allocate an unbounded control queue. Retirement must
be acknowledged before discarding its record.

## Export lifetime

Client references (including namespace bindings and transferred grants) and
accepted operations keep an exported resource logically alive. Provider control
and bookkeeping are not themselves client references. A provider must close its
local client grant after successfully handing it off if it wants client closure
to trigger retirement; an intentionally retained client grant keeps the object
live. Once the last client
reference and accepted operation disappear, notify the provider of retirement.
It frees its state and acknowledges retirement. This must still work with a full
normal message queue.

Explicit withdrawal stops new operations, removes undelivered work and wakes
affected callers. Delivered work still needs completion before its provider state
can be freed. Existing client handles remain safe to close but cannot invoke new
operations. Provider process exit closes all exports and releases pending work;
it must not depend on client reference counts falling to zero first.

Task 4 implements export/control through the existing endpoint service and owned
receiver. Exported clients use the endpoint CALL/SEND envelope with an inner
protocol and operation; there is no native file bridge yet. Retirement is ready
after accepted records drain and either all clients close or the export is
withdrawn. ACK releases the ID even if invalid withdrawn clients remain: they
retain old backing storage and never rebind to a reused ID. Cancellation notices
carry timeout/closure reasons and precede retirement, then normal FIFO work.
The [endpoint contract and counter example](../endpoints.md) describe the ABI
and implemented limits.

Do not conflate namespace removal, withdrawal and retirement. Removing a name
only prevents future discovery. Withdrawal denies further use of that export.
Natural retirement permits reclaiming an object nobody can still use.

## Namespace and startup contract

Use a kernel-owned flat map of exact names to exported client capabilities, with
distinct LOOKUP and MANAGEMENT authority. Native objects, raw endpoints and
nested namespaces retain their existing explicit grant routes. Init creates/populates it or delegates management
explicitly. A binding owns a reference and fixes both resource rights and transport authority
returned by lookup. Lookup cannot exceed either mask.

Applications receive lookup authority through a dedicated optional launch/startup
namespace capability field, with zero meaning absent and no new schema version. Child inheritance is explicit delegation, not ambient authority from
space membership. Lookup access exposes all names in that namespace; a restricted
child receives a separately populated namespace. Namespace overlays, enumeration,
search paths and automatic provider activation are deferred.

Publish/replace atomically: a racing lookup returns either the old grant or the
new one. Changes affect subsequent lookups; existing capabilities remain attached
to their original exports. A dead provider leaves an unavailable binding until
its supervisor removes or replaces it. No silent fallback to another provider.

Libpyxis selects the complete URI scheme name; the provider interprets the rest.
The kernel does not parse hosts, paths, HTTP, SQL or composable media-type prefixes.
Non-file clients can look up a plain service name such as display directly.

Existing directory-root bindings remain the route for ordinary paths. Reject an
ambiguous scheme bound both as a directory root and as a provider at resolution;
do not choose one by priority or retry the other after failure. Reject known
conflicts during launch too, but do not rely on that check alone when namespace
bindings can change later.

Initial startup sequence: init launches a provider, obtains its exported service
capability through real IPC transfer, publishes it, then launches consumers.
Update launch/session/shell forwarding and examples together. No numeric-handle
printing as a handoff mechanism, new schema version or legacy startup path.

Task 5 implements this contract through the [namespace ABI and init commands](../namespaces.md).
Bindings accept exported clients only; 64 bindings and 63-byte exact names bound
each map. PUBLISH requires absence, REPLACE requires presence, and lookup returns
the binding's fixed resource/transport masks. The shell publication launch omits
the parent namespace from provider grants. A provider that exits before
registration can leave startup waiting; this remains [technical debt](../technical-debt.md#service-startup-failure-before-publication).

## File protocol and open bridge

Make the file protocol itself safe for process-to-process delivery. Today's FILE
messages contain caller buffer addresses; those must not reach a remote provider.
Read requests carry an explicit offset and count; replies carry count and copied
bytes. Writes carry offset and copied bytes. Protocol metadata counts against the
4 KiB payload limit, so usable byte extents are slightly smaller than one page.
SIZE/RESIZE/SYNC remain protocol operations, with their existing authority and
side-effect contracts where implemented.

Migrate kernel-backed files and exported file delivery together, retaining public
libc/libpyxis function signatures. Do not add a competing HTTP-file adapter or a
generic kernel description language for marshaling pointers. Larger operations
use short transfers as appropriate. Audit native callers for partial progress;
do not assume their old requested extents still complete in one call. Preserve
stdio loops, positions, errors, uncertain-mutation handling and allocation policy.
No compatibility version or backward-compatibility implementation is required.

Provider OPEN returns an actual resource grant with a declared interface and
representation. Initially support byte-readable files with optional media-type
metadata; check that the declared interface agrees with the returned capability.
The provider interprets the full URI and requested access. Read-only providers
reject writable opens before fetching rather than silently ignoring their mode.
Shared library routing serves native file consumers, libc and shell input
redirection; do not patch cat/cksum to understand HTTP.

A snapshot supports stable SIZE and explicit-offset READ, including short reads
and EOF. Selecting or describing a representation grants no extra authority.
Structured rows, discovery of alternative representations, format negotiation and
json+http are deferred. The representation field is descriptive, not proof that
the content conforms to its media type.

## HTTP provider contract

Run one provider dedicated to a space with explicit TCP, DNS-related, clock,
randomness and memory grants as required by existing userspace networking.
Init publishes its OPEN-for-read capability only in the selected namespace.
Access permits fetching under that provider's policy; this is not a restricted
network-destination sandbox or a shared privileged proxy across spaces. Sharing
and per-caller destination policy are future work, including redirect/cache policy.
Boot without networking must remain usable; do not perform an automatic remote
fetch or make ordinary local file access depend on the service being present.

Use picohttpparser for headers and chunked decoding, pinned through ports under
its MIT license. Candidate pin for the first compile probe:
`f4d94b48b31e0abae029ebeafcfd9ca0680ede58` from
[upstream](https://github.com/h2o/picohttpparser/tree/f4d94b48b31e0abae029ebeafcfd9ca0680ede58).
Verify its SDK requirements and preserve notices before integration. It is a
parser, not a complete client; userspace owns framing policy, sockets, limits,
response staging and cleanup. Do not import its tests into this milestone.

### Requests, responses and bounds

| Area | Agreed initial behavior |
| --- | --- |
| Request | Plain HTTP GET over existing IPv4 TCP and userspace DNS. |
| URI | DNS name or numeric IPv4, optional port, path and query. Reject credentials and control characters. Do not transmit fragments. Empty path becomes /. |
| Wire request | HTTP/1.1 with Host, Connection: close and Accept-Encoding: identity. No arbitrary caller-supplied headers. |
| Response | Support HTTP/1.0 and HTTP/1.1 framing. 200 yields a complete snapshot; 204 yields an empty snapshot. Other final statuses fail OPEN. |
| Redirect | Report it; do not follow. An HTTPS Location is never downgraded to plaintext. |
| Content encoding | No decompression; reject unsupported encodings. |
| Connections | One per fetch; no pooling, pipelining or reuse. |
| URI budget | 2 KiB, leaving room for OPEN metadata within the message. |
| Headers/trailers | 32 KiB aggregate across the response, with bounded field and informational-response counts. |
| Body | At most 16 MiB per completed resource. |
| Provider storage | At most 64 MiB of body-storage reservations, including staging, retained bodies and temporary growth overlap. |
| Snapshots | At most 64 live snapshots, including empty ones. |
| Time | 30-second overall fetch budget, including DNS, connection, sending and complete response receipt; respect an earlier caller deadline. |

Use named constants for the policy limits. Account actual reserved body capacity,
not just received bytes; allocation may still fail below the budget. Never evict
bytes retained by a live resource to admit a new open. Bound informational
responses and field counts explicitly during the parser task; their exact array
sizes are implementation choices within the agreed aggregate budget.

Support Content-Length, chunked and connection-close body framing. Reject
conflicting framing, malformed messages, truncated fixed/chunked bodies and
unsupported transfer codings. Absence of Content-Length is not itself an error.
Finish chunked bodies after the terminal chunk and valid bounded trailers, not
connection close. Observe status-specific body rules; do not wait for a 204 body.
A connection-close-delimited body cannot establish that the origin intended to
send no more bytes. This limitation is accepted for the first HTTP client.
See [HTTP framing](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3).

Existing DNS retry behavior may be reused within the overall budget; no phase,
retry or informational response resets it. Reject a body over budget, deadline
expiry or malformed framing without publishing a partial resource. Close/abort
network state and release staging references on every failure. No automatic
whole-request replay after an uncertain network result.

Each OPEN fetches independently. The exported result owns immutable bytes and
size until retirement, with no cross-open cache. Preserve optional Content-Type
as descriptive metadata. Provider death invalidates its snapshots like its other
exports; retaining a client handle does not preserve the dead process's memory.

### Native results and libc errors

Keep the final HTTP status available in the native OPEN result when received,
separately from transport failure and the provider result. Success returns a
resource capability; failure returns none. Use the following libc translations
without placing HTTP status handling inside the kernel:

| Result | errno |
| --- | --- |
| Missing scheme binding; HTTP 404 or 410 | ENOENT |
| Missing authority; HTTP 401 or 403 | EACCES |
| Writable open on the read-only provider | EROFS |
| Redirect, unsupported success status such as 206, unsupported version/encoding/feature | ENOTSUP |
| Other rejected final HTTP status or malformed/truncated response | EIO |
| Fetch/call deadline expiry | ETIMEDOUT |
| URI, header or per-resource byte limit exceeded | EFBIG |
| Provider aggregate body-storage limit | EDQUOT |
| Outstanding-work or live-snapshot limit | EAGAIN |
| Allocation failure | ENOMEM |

Preserve existing native-to-libc network-error translations where applicable.
Invalid URI syntax reports EINVAL. Peer closure, abandonment and delivery/outcome
metadata remain transport results, with the shared library applying its explicit
mapping; do not erase uncertain delivery to justify an automatic retry.

### Deliberate scheduling limitation

One provider task performs synchronous fetches initially. While fetching, other
work, including reads of previously opened snapshots and lifecycle processing,
can wait. The fetch deadline bounds each interruption; caller deadlines bound
individual waits. Cancellation cannot make the provider interrupt a blocking
network call instantly. Inspect cancellation between bounded phases and discard
a result whose caller is gone.

Do not add user threads, network multiplexing or a worker-process framework to
hide this limit. Record it as debt with asynchronous service responsiveness as
the revisit point. SQLite and a compositor need their own scheduling/data contracts.

## Focused implementation tasks

Each task is a separate reviewable PR or small dependent PR set. Do not start the
next task automatically. Private scaffolding must have a working consumer in its
task; avoid publishing interfaces with fake successful operations.

- [x] **1. Bounded request/reply delivery.** Replace the one-outstanding-request
  endpoint path with the 4 KiB/four-attachment/sixteen-delivery model. Add receipts,
  deferred/out-of-order reply, atomic request/reply grant transfer and separate
  transport/operation results. Preserve BSP growth, parking and teardown rules;
  migrate existing endpoint examples. Include explicit endpoint creation authority
  and process-owned receiver teardown, brought forward from task 4. Land a
  functioning call/receive/reply slice.
- [x] **2. One-way delivery.** Add send and receipt completion using the same
  machinery. Exercise sender close/exit after admission, queue exhaustion and
  retained attachment/target lifetime without waiting for a reply.
- [x] **3. Deadlines and lifecycle delivery.** Add absolute call deadlines,
  queued/delivered cancellation distinction and reliable control notifications.
  Cover reply/expiry/exit races and abandoned receipts. Keep external cancellation
  and wait sets out of scope; later export retirement uses this notification path.
- [x] **4. Exported service objects.** Extend the creation authority from task 1
  with export control, authenticated protocol/rights delivery, withdrawal and acknowledged
  retirement. Multiple exports share a provider endpoint. Provider death must
  close exports even when clients or queued references remain.
- [x] **5. Namespace and startup delegation.** Add flat authorized binding,
  lookup, atomic replacement/removal and explicit launch/startup forwarding.
  Exercise restricted namespaces, retained old grants and provider exit. Define
  the small init-facing publication/handoff commands through existing launch/IPC;
  no supervisor framework or implicit authority.
- [ ] **6. IPC-safe file messages.** Replace pointer-bearing FILE wire payloads
  with bounded copied bytes for native files and helpers. Preserve public C
  signatures and all existing backends; audit native callers and stdio for short
  transfers. Build/boot existing file utilities and ports before adding a provider.
- [ ] **7. End-to-end userspace file provider.** Add provider OPEN and shared
  resolution/representation handling. Publish a small immutable-file service
  through init; use existing cat, cksum and redirected input to read it. Exercise
  rights, copies, retirement, withdrawal, replacement and provider exit. This is
  a small demonstrable service, not a test framework or an HTTP-specific libc path.
- [ ] **8. Pinned HTTP parser and fetch library.** Probe/package picohttpparser,
  implement userspace GET/framing/staging under the agreed budgets, and exercise
  the fetch library with a small native development consumer. Reuse the existing
  resolver; any API adjustment must preserve its current consumers and limits.
  No new general-purpose HTTP CLI or unrelated libc/network feature scope.
- [ ] **9. HTTP service integration.** Connect fetch results to exported snapshot
  files, publish per-space service authority and integrate ordinary file opens.
  Validate the examples below, the status/errno mapping, limits and cleanup.
  Keep networking opt-in and local-only boot working. Remove any temporary task-8
  consumer if it has no continuing purpose.
- [ ] **10. Milestone handoff.** Rewrite this document as implemented contracts
  and usage, move it into docs, update the index and links, and carry accepted
  limits into technical debt. Keep future HTTP writes, representations, SQLite,
  compositor, shared memory and service responsiveness in their own WIP scope.

## Validation, ownership and completion

Pyxis owns kernel objects, scheduling, public ABI and integration. Userland owns
libpyxis routing, libc adapters and the provider applications/libraries. Ports
owns the pinned parser recipe. Publish dependency commits and merge dependent
PRs before advancing parent pins. No compiler-container rebuild is anticipated;
report one if the actual implementation discovers a toolchain requirement.

Use normal builds, manual QEMU boots and debugger inspection. Exercise endpoint
saturation, short transfers, capability lifetime, deadlines and provider failure
without adding tests, fault injection, CI or boot/output automation. Distinguish
code-inspected exceptional paths from behavior observed in the guest.

Manually operated host HTTP servers provide known bytes and framing examples;
use a public plaintext HTTP endpoint for the separate real-network demonstration.
Examples, once implemented:

```text
cat http://example.com/
cksum http://host-address:8000/sample.bin
cat http://host-address:8000/sample.bin | tee home://sample.bin | cksum
```

Compare retained/copy bytes with host input; exercise fixed, chunked and
close-delimited bodies, empty 204, denied opens, rejected redirects/encoding,
time/size limits, repeated open/close and provider exit. Do not claim that public
site availability is deterministic or that a successful build validates framing.

The final result is a reusable userspace-service mechanism and a bounded HTTP
consumer. It is not HTTPS, a web server, general shared memory, concurrent HTTP,
a userspace filesystem rewrite, a SQLite port or a window server.

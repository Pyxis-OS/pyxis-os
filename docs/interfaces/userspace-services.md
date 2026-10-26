# Userspace services

Pyxis services run in ordinary userspace processes. Clients discover exported
capabilities through their process environment, then invoke the provider's
protocol. Text returns immutable FILE snapshots; ordinary HTTP(S) URL opens
return response streams with read-only final observation. `cat`, libc readers and
shell redirection consume these through their declared grants.

The kernel owns capability enforcement, bounded delivery, routing and lifetime.
Providers interpret requests and enforce operation-specific resource rights.
Discovery, transport and resource protocols are separate: a non-file service can
use the same environment and IPC without exposing a FILE interface.

## Interfaces and authority

| Interface | Implemented contract |
| --- | --- |
| [Endpoints](endpoints.md) | CALL/SEND, copied payloads and capability attachments, receipts, absolute call deadlines and lifecycle notifications |
| [Exported objects](endpoints.md) | Provider-owned receiver, authenticated object/protocol/rights metadata, separate CUT controllers and acknowledged retirement |
| [Environment](environment.md) | Owned CAP/TEXT entries, immutable captures, exported-only child selection and explicit private delegation |
| [File providers](file-providers.md) | Intent-based typed OPEN, attested grants, cached describe and immutable native error bodies |
| [HTTP fetching](../userland/http-fetch.md) | Streamed HTTP/verified HTTPS GET, prepared POST, explicit snapshots and final diagnostics |

Endpoint creation requires a service grant; it supplies no environment-mutation,
file or network authority. Each receiver belongs to one process. Receiver and
receipt handles cannot be copied or delegated; client handles can, with equal
or reduced resource and transport masks independently. Export creation returns
a revocable client and a separate CUT controller. Controllers can be copied
explicitly; receiving a client never grants management authority. Providers
retain controllers until their exports retire or the provider tears down.

SEND requires SEND transport authority. CALL requires SEND and RECEIVE, with
client RECEIVE authorizing only that call's response. It grants no access to the
provider's incoming queue; that uses the separate owned receiver. Providers
check the kernel-authenticated resource rights for each operation. Payload bytes,
embedded pointers and numeric handles do not confer authority across processes.

LOOKUP captures an exact entry from the caller's own process environment.
A restricted child receives an explicitly selected independent table; private
controllers are excluded unless a trusted handoff deliberately delegates them.
Space membership supplies no implicit environment authority.

## Delivery and lifetime

An endpoint admits at most sixteen outstanding deliveries, including unfinished
receipts and replies awaiting collection. CALL waits for a result or transport
failure; SEND returns after admission, without promising execution. Each message
carries at most 4 KiB and four capability attachments. Admission and recipient
handle installation are atomic; a sender keeps its original handles.

Each delivered message has a receipt. REPLY completes a CALL and consumes its
receipt; `endpoint_finish()` finishes SEND or canceled CALL work, or abandons an
unanswered CALL. Received attachments remain separately owned. Providers may
receive more work before replying, and completion need not follow admission order.

CALL deadlines are absolute monotonic nanoseconds, with zero meaning unlimited.
Expiry before delivery removes the queued request. Expiry after delivery releases
the caller with delivered state, invalidates reply authority and leaves a receipt
for the provider to finish. A lost response does not establish whether an
operation ran and does not justify automatic retry. Cancellation and retirement
notifications use retained control state and remain available with a full queue.

Each endpoint supports 64 live or unacknowledged exports. Object IDs remain
reserved until RETIRE acknowledgment. Three lifetime actions have distinct effects:

- Unsetting or replacing an environment publication changes future discovery;
  independently captured grants keep their ordinary lifetime. Retargeting a
  service SLOT through its controller is separate: every holder of that slot
  observes the new target, within the slot's fixed authority and representation
  ([forwarders](forwarders.md)).
- Forwarder CUT permanently denies new invocation and handle queries, and fails
  queued work. Already-delivered work keeps its receipt and may complete under
  ordinary deadline, caller-stop and receiver/provider lifetime rules. Releasing
  the last logical controller grant also cuts a revocable export automatically.
- Retirement follows drained operations and either final client closure or CUT.
  Controller storage alone does not delay natural retirement. The provider
  releases its state and controller, and acknowledges before reusing the ID.
  Stale cut handles never bind to a new object with that ID.

Receiver closure or provider exit closes every still-live export without waiting
for client closure; such operations report ENDPOINT_CLOSED. A cut lineage reports
DENIED instead. A dead binding remains unavailable until explicitly removed or
replaced. There is no automatic restart, reconnect or rebinding.

## File opens and snapshots

Libpyxis captures an exact name from the one environment table. Directory values
use native traversal, inline text becomes a sealed native FILE, and provider
values receive an opaque remainder with READ/WRITE intent and open flags. Typed
results own a kernel-attested resource and any independent immutable error body;
a failed outcome stays a failure. Cached describe never calls the provider.
See [file providers](file-providers.md).

Ordinary opened children inherit the source service's revocation lineage, so CUT
denies later use. COPY, IPC and launch preserve it. Copied error-body snapshots
remain readable after provider exit or CUT. Description fields are bounded but
untrusted and confer no authority; unknown length stays unknown.

The FILE adapter keeps transport/local validation separate from the typed
outcome. [HTTP](http-transactions.md) uses the same typed resources; its old
continuation route is removed, with no fallback for generic callers.

Native and exported FILE helpers use the same copied wire payloads and explicit
offsets, with at most 4,088 read bytes or 4,080 write bytes per call. Larger I/O
continues from confirmed short transfers. Exported standard streams retain CALL
transport authority through launch and redirection. Direct execution of an
exported binary is unsupported; the image loader requires a native file.

Text and explicit legacy HTTP/HTTPS OPEN produce immutable snapshots with stable
size and bytes. Ordinary HTTP URL opens instead use prepared STREAMs; copies
share consumption, and independent opens fetch independently.
Each provider has 63 file-export slots beside its OPEN service. Removing the
binding does not close existing files: derived child authority retains its
ancestor, and the provider serves retained exports and
exits naturally after all its exports retire.

## Boot and use

Development and read-only init publish separate `textfs` instances
as `text`. They launch `boot://session.pxe` with `--start-services SCRIPT`, which applies
configuration before running that services script, `boot://init-services` or,
installed, `system://config/init-services`. That script publishes `httpfs`
as `http` with the configured DNS server, then optionally publishes a separate
`httpfs --https` instance as `https` with read-only trust grants before handing
off to the interactive shell. Explicitly reported HTTPS setup failure leaves
that scheme unpublished and permits local/HTTP startup to continue. Idle
spaces start no provider. Publication itself performs no remote fetch, so boot
and local files remain usable without a NIC.

Build and boot with networking when HTTP access is wanted:

```sh
make -j16 image
make run CPUS=4 VIRTIO_NET=1
```

In the guest:

```text
cat text://welcome
cat < text://guide
cat http://example.com/
cat https://example.com/
cat http://example.com/ | tee home://example.html | cksum
service replace text boot://textfs.pxe --welcome "Replacement service"
env unset http
service start http boot://httpfs.pxb
```

Trusted startup declares destination roles, masks and optionality before launch.
The publication command receives the selected exported grants through IPC and
publishes their independent slot/controller pairs in one environment batch before
acknowledging readiness. Providers cannot choose additional names; omitted roles
must be explicitly optional. Provider startup omits its own publication entries.
See [publication and delegation](environment.md#publication) for the handoff
and [HTTP usage and limits](../userland/http-fetch.md) for host-server examples and error mapping.

HTTP uses the pinned picohttpparser port for header syntax and chunk decoding.
Userspace owns DNS, TCP, TLS, framing, redirects and private snapshot storage.
Typed read returns a fully framed re-readable FILE, write returns a one-shot
POST transaction and independently delegated REMOVE sends DELETE. RAM uses a
configured aggregate logical-byte budget; private npfs is the explicit large-body
option. No successful prefix or automatic backing fallback is published.
[HTTP resources](http-transactions.md) defines the authority, framing/deadline,
trust and backing limits.

## Limits and future work

Accepted limitations and their revisit points are recorded in technical debt:

- [Process termination and Ctrl-C](https://git.internal/PyxisOS/pyxis-os/issues/1360).
- [Endpoint cancellation and capacity](https://git.internal/PyxisOS/pyxis-os/issues/1445).
- [Service startup failure before publication](https://git.internal/PyxisOS/pyxis-os/issues/1446).
- [Provider calls through synchronous file helpers](https://git.internal/PyxisOS/pyxis-os/issues/1447).
- [HTTP framing compatibility](https://git.internal/PyxisOS/pyxis-os/issues/1448).
- [HTTP provider responsiveness](https://git.internal/PyxisOS/pyxis-os/issues/1453).
- [HTTPS trust and platform limits](https://git.internal/PyxisOS/pyxis-os/issues/1450).

HTTP/HTTPS uses cooperative completion/TCP/TLS progress. Synchronous entropy
waits and local work still delay control replies; see
[prepared service limits](http-transactions.md#private-backing-and-limits). Call expiry
cannot kill a provider or reclaim its unfinished native operations.

[Scheme-provider extensions](../wip/capabilities-and-providers.md) retain proposals
for writes, media-type aliases, richer representations, caches and SQLite views
and sessions. [Desktop services](../wip/desktop-graphics.md) need their own window,
shared-memory, frame-ownership and scheduling contracts. These are future designs,
not additional operations in the implemented service or FILE protocols.

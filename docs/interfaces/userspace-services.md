# Userspace services

Pyxis services run in ordinary userspace processes. Clients discover exported
capabilities through explicitly delegated namespaces, then invoke the provider's
protocol. The text, HTTP and HTTPS providers return immutable FILE snapshots that
ordinary `cat`, `cksum`, `tee`, libc readers and shell redirection can consume.

The kernel owns capability enforcement, bounded delivery, routing and lifetime.
Providers interpret requests and enforce operation-specific resource rights.
Discovery, transport and resource protocols are separate: a non-file service can
use the same namespace and IPC without exposing a FILE interface.

## Interfaces and authority

| Interface | Implemented contract |
| --- | --- |
| [Endpoints](endpoints.md) | CALL/SEND, copied payloads and capability attachments, receipts, absolute call deadlines and lifecycle notifications |
| [Exported objects](endpoints.md) | Provider-owned receiver, authenticated object/protocol/rights metadata, withdrawal and acknowledged retirement |
| [Namespaces](namespaces.md) | Flat exact-name bindings, separate LOOKUP and MANAGE rights, atomic replacement and explicit startup delegation |
| [File providers](file-providers.md) | Full-URI OPEN, validated FILE grants and byte representation, shared native/exported file helpers |
| [HTTP fetching](../userland/http-fetch.md) | Bounded HTTP/verified HTTPS GET, immutable snapshots and separate diagnostics |

Endpoint creation requires a service grant; it supplies no namespace-publication,
file or network authority. Each receiver and its export control belong to one
process. Receiver and receipt handles cannot be copied or delegated; client
handles can, with equal or reduced resource and transport masks independently.

SEND requires SEND transport authority. CALL requires SEND and RECEIVE, with
client RECEIVE authorizing only that call's response. It grants no access to the
provider's incoming queue; that uses the separate owned receiver. Providers
check the kernel-authenticated resource rights for each operation. Payload bytes,
embedded pointers and numeric handles do not confer authority across processes.

Namespace lookup exposes every binding in the delegated map. A restricted child
receives a separately populated namespace. Ordinary shell children receive LOOKUP
only; trusted session handoff can preserve held management authority. Namespace
membership is never inferred from the caller's space.

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

- Namespace removal or replacement changes future discovery; held grants keep
  their original target.
- Withdrawal stops further invocation of that export and cancels pending work;
  delivered receipts still need completion.
- Retirement follows drained operations and either final client closure or
  withdrawal. The provider releases its state and acknowledges before reusing
  the ID. Stale withdrawn handles never bind to a new object with that ID.

Receiver closure or provider exit invalidates every export without waiting for
client closure. A dead binding remains unavailable until explicitly removed or
replaced. There is no automatic restart, reconnect or rebinding.

## File opens and snapshots

Libpyxis selects the complete URI scheme in userspace. Directory roots retain
native traversal; provider schemes use OPEN with the full URI and requested
READ/WRITE rights. A scheme present in both routes is an error, with no fallback.
Successful OPEN returns an owned FILE grant with matching authority and byte
representation metadata; failed OPEN returns no resource.

Native `provider_open()` separates transport/validation status from the provider's
operation result and diagnostic status, and accepts an optional caller deadline.
HTTP diagnostics preserve the final response code when available. Media type is
descriptive metadata, not proof of content or additional authority.

Native and exported FILE helpers use the same copied wire payloads and explicit
offsets, with at most 4,088 read bytes or 4,080 write bytes per call. Larger I/O
continues from confirmed short transfers. Exported standard streams retain CALL
transport authority through launch and redirection. Direct execution of an
exported binary is unsupported; the image loader requires a native file.

Text, HTTP and HTTPS opens produce immutable snapshots with stable size and bytes.
Copies retain the same snapshot; independent HTTP/HTTPS opens fetch independently.
Each provider has 63 file-export slots beside its OPEN service. Removing the
binding does not close existing files: the provider serves retained exports and
exits naturally after all its exports retire.

## Boot and use

Development and read-only init create separate namespaces and publish `textfs`
as `text`. They launch `app://session.pxe` with `--start-services`, which applies
configuration before running `app://init-services`. That script publishes `httpfs`
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
service replace text app://textfs.pxe --welcome "Replacement service"
namespace remove http
service start http app://httpfs.pxe
```

The publication command receives an exported grant through IPC and binds it before
acknowledging readiness. It omits the parent's namespace from provider startup,
so the provider does not retain its own binding through a namespace reference.
See [publication and delegation](namespaces.md#init-publication) for the handoff
and [HTTP usage and limits](../userland/http-fetch.md) for host-server examples and error mapping.

HTTP uses the pinned picohttpparser port for header syntax and chunk decoding.
Userspace owns DNS, TCP, framing policy and storage. Each fetch has a 2 KiB URI
limit, 32 KiB aggregate header/trailer limit, 16 MiB body limit and a 30-second
budget capped by an earlier caller deadline. Each instance reserves at most
64 MiB for bodies, counting staging, retained snapshots and growth overlap.
Only completed 200/204 responses become files. HTTPS verifies chain, name and
validity against packaged public roots plus optional instance-specific roots.
Redirects, decompression, writes and response caching are unsupported.

## Limits and future work

Accepted limitations and their revisit points are recorded in technical debt:

- [Process termination and Ctrl-C](../technical-debt.md#process-termination-and-ctrl-c).
- [Endpoint cancellation and capacity](../technical-debt.md#endpoint-cancellation-and-capacity).
- [Service startup failure before publication](../technical-debt.md#service-startup-failure-before-publication).
- [Provider calls through synchronous file helpers](../technical-debt.md#provider-calls-through-synchronous-file-helpers).
- [HTTP framing compatibility](../technical-debt.md#http-framing-compatibility).
- [HTTP provider responsiveness](../technical-debt.md#http-provider-responsiveness).
- [HTTPS trust and platform limits](../technical-debt.md#https-trust-and-platform-limits).

Each HTTP/HTTPS provider fetches synchronously, delaying snapshot reads and retirement
behind network work. Ordinary file/open helpers have no IPC deadline, so the
fetch budget does not bound total client wait. Call expiry cannot kill a provider
or reclaim its unfinished receipts.

[Scheme-provider extensions](../wip/userspace-scheme-providers.md) retain proposals
for writes, media-type aliases, richer representations, caches and SQLite views
and sessions. [Desktop services](../wip/desktop-graphics.md) need their own window,
shared-memory, frame-ownership and scheduling contracts. These are future designs,
not additional operations in the implemented service or FILE protocols.

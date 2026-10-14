# Userspace file providers

A generic provider publishes an OPEN service through its process
[environment](environment.md). Opening a provider URI returns an exported FILE
capability. Ordinary libpyxis file helpers, libc, `cat`, `cksum`, `tee` and shell
input redirection use it without provider-specific code. HTTP additionally offers
[prepared transactions](http-transactions.md), used by libc URL reads for
forward-only response streams.

```text
cat text://welcome
cksum text://guide
cat < text://welcome
cat text://guide > home://guide.txt
```

## Discovery and OPEN

Libpyxis selects the complete `scheme://` prefix. Directory roots retain their
existing component-walking rules; exported provider entries use provider OPEN.
One exact-name lookup chooses the capability; there is no second binding map. Provider failure never causes a fallback to directory lookup or creation.
Relative paths still use the retained native working directory.

The [OPEN ABI](../../include/abi/provider.h) carries the complete URI as copied bytes
and a nonzero READ/WRITE mask. The provider interprets the URI without library
decoding, normalization or component walking. Generic URI bytes are bounded to 3,768;
longer requests fail with FILE_TOO_LARGE instead of being truncated. Requested access
needs the corresponding service OPEN right. Read-only services reject writable
opens before producing an exported file.

The 328-byte request prefix carries rights, URI length and optional HTTP
continuation context; an 80-byte reply prefix distinguishes ERROR, BYTES and
REDIRECT. Generic requests zero HTTP flags/origin/budgets.

BYTES returns exactly one owned exported FILE grant, exactly the
requested resource rights, and CALL transport authority. Reply metadata
declares the FILE interface and byte representation, with an optional printable
ASCII media type of at most 127 bytes. Libpyxis checks these declarations against
the kernel-authenticated handle query. Metadata grants no authority and does not
prove that bytes conform to their media type. Malformed replies release received
grants and fail; unsuccessful OPEN returns no resource. Every reply includes
the fixed OPEN prefix. Failure zeroes the interface, representation and media-type
fields, outcome and accounting, but may retain a provider-specific diagnostic status.
REDIRECT succeeds without a FILE, carrying a bounded Location and HTTP charges;
only the shared HTTP open bridge follows it. HTTP uses its final
response code; text uses zero. The kernel does not interpret this field.

`provider_open(provider, uri, rights, deadline_ns, &result, &file)` returns a
transport/local-validation status. When that is CALL_OK, `result.status` is the
provider's operation result and `result.provider_status` is its diagnostic, on
both success and failure. `result.delivery` retains IPC delivery state even on
transport or validation failure. Successful representation metadata is in
`result.metadata`; only BYTES supplies an owned file. REDIRECT has no grant,
with Location and consumed accounting in the result. The contextual helper is
`provider_open_context()`; low-level `provider_open()` performs just one hop.
`deadline_ns` is an absolute monotonic deadline; zero means unlimited. `path_resolve()` for
files and `path_open_file()` use generic provider OPEN; libc and shell redirects
share the latter's open/create routing for ordinary files. These FILE-only path
helpers refuse HTTP(S) URLs, including `json+` aliases, with BAD_OPERATION after
binding discovery rather than collecting a response implicitly. Provider
directory operations, cwd changes, removal and rename are unsupported.

## File dispatch and lifetime

`handle_query()` reports resource rights, transport rights, immutable protocol
and native/exported kind. It exposes no object identity and does not promise
continued liveness after a successful query. Querying a cut grant or its
descendants returns DENIED; a live export whose receiver has closed returns
ENDPOINT_CLOSED. Forwarded holders retain the target's attested interface.

FILE helpers select native syscall dispatch or exported endpoint CALL from that
query. Both routes use the same [copied FILE payloads](../../include/abi/file.h): at
most 4,088 read bytes or 4,080 write bytes per helper call. Providers check the
kernel-authenticated invoking rights for each operation. Clients validate counts,
reply extents and application status; unexpected attachments are closed.

`FILE_INFO` reports independently valid size, domain, object and modification
time through the same dispatch. HTTP/text private snapshots report a domain
disjoint from native backing, with no object ID or mtime. Generic exports do not
gain that domain merely by being exported; see [file metadata](file-metadata.md).

Transport and operation failures remain distinct until the helper maps them to
its native status. A delivered mutation whose transport fails reports
OUTCOME_UNKNOWN. No automatic retry, reconnection or rebinding occurs. Existing
FILE reads and generic path/libc opens still wait without a caller deadline.
Explicit legacy HTTP(S) OPEN chains use one absolute 30-second/earlier-caller
deadline, including OPEN queueing; their low-level callers supply bounded scratch
and clock READ authority. Libc URL GET uses PREPARE/COMMIT with a single deadline
through response consumption and no total-body budget. Other native OPEN callers
can supply a deadline explicitly; an unresponsive generic provider can block its
client.

Copying, transferring or inheriting a FILE grant retains the same opened object.
Each libc descriptor keeps its own offset. Namespace replacement affects future
opens; existing grants remain attached to the original provider. OPEN results
inherit their source service's revocation lineage; ordinary COPY, IPC and launch
preserve it. Cutting the source therefore denies later use of its opened files,
including handle queries. Independent reply attachments keep their own lineage.
Bytes already read remain available. CUT denies queued work; work already delivered
under the endpoint lock may complete. A live export whose receiver closes reports
ENDPOINT_CLOSED. RETIRE and its acknowledgment govern provider state reclamation
and object-ID reuse.

Launch permits exported FILE standard streams with exactly the stream's READ or
WRITE resource right and CALL transport; native streams keep zero transport.
Shell redirection forwards those masks, and libc adopts each child handle once.
Direct execution of a provider-backed binary remains unsupported: the kernel
image loader requires a native file. Script reading and ordinary byte I/O use
the shared helpers.

## Immutable text service

The `textfs` program serves a fixed collection of immutable text files, including
`welcome` and a multi-transfer `guide`. Development and read-only init scripts
publish separate instances as `text` before starting their sessions. URI schemes
use a leading ASCII letter followed by letters, digits, `+`, `-` or `.`, with
exact `welcome` or `guide` remainders; other remainders return NOT_FOUND. The
service may be published under another compatible name. `textfs --welcome MESSAGE`
selects an alternative immutable welcome message, for example:

```text
service replace text boot://textfs.pxe --welcome "Replacement service"
cat text://welcome
env unset text
```

Writable opens are denied by the published OPEN_READ grant (EACCES through libc).
Publication uses the existing service handoff and its ordinary resource grants,
omitting its own publication entries. The text service
performs no network operations.

Each OPEN creates one export. A receiver can hold 64 exports, including its OPEN
service, so a published instance supports at most 63 simultaneous file exports;
copies share an export. Creation returns a client and a separate CUT controller.
The provider drops its own client handles after successful handoff and retains
each controller until export retirement or provider teardown. A last logical
controller release cuts its revocable export automatically. Controller storage
alone does not delay natural retirement. File state remains until RETIRE, then
the provider releases its controller and acknowledges it before
reusing the ID. Canceled or timed-out calls finish their receipts without stopping
unrelated service work. Failed OPEN reply transfer releases the provisional file.

Removing or replacing the service binding can retire its OPEN export once all
lookup references and derived child authority close. The old process continues
serving already-open files
and exits after their exports retire too. There is no process-kill facility or
supervisor; this is capability-driven shutdown, not forced cancellation.

## HTTP and HTTPS snapshots

Separate [HTTP and HTTPS services](../userland/http-fetch.md) publish OPEN_READ as `http` and
`https` in configured interactive environments. Each performs one network hop.
The shared bridge follows the [bounded redirect policy](../userland/http-fetch.md#redirect-chains);
only a complete final response becomes a read-only FILE snapshot. Optional
Content-Type, status and effective URL are returned as copied metadata.
This explicit provider OPEN route remains legacy; ordinary libc URL reads use
PREPARE/COMMIT and STREAM, preserving the anonymous redirect/status policy.
FILE-only consumers must explicitly materialize a response into caller-owned
storage when they need SIZE or positioned reads.
Each open fetches independently; copies retain the same immutable body.
Retirement releases its allocation and shared storage reservation. The provider
supports 63 live snapshots alongside its OPEN export, including empty snapshots.

HTTPS additionally verifies chain, reference name and validity against its frozen
trust before issuing the GET. See [TLS authority and trust](../userland/https.md) for setup,
native integration and accepted limits.

See the [service overview](userspace-services.md) for authority and lifetime across
protocols. Format negotiation and writable providers remain
[scheme-provider extensions](../wip/capabilities-and-providers.md); shared-memory
transfers need a separate ownership contract.

[Prepared HTTP transactions](http-transactions.md) add a separate PREPARE and
forward-only STREAM contract. Existing provider OPEN returns snapshots and keeps
its redirect/budget semantics; it does not implicitly commit a POST.

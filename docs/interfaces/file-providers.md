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

**Task A3 checkpoint, incomplete.** The private HTTP wire split and native/text
request replacement are implemented here. Owned typed results, held-capability
OPEN, describe and A2 environment routing still need integration and validation;
this checkpoint does not mark task 3 complete or ready to merge.

Libpyxis selects the complete `scheme://` prefix. Directory roots retain their
existing component-walking rules; exported provider entries use provider OPEN.
One exact-name lookup chooses the capability; there is no second binding map. Provider failure never causes a fallback to directory lookup or creation.
Relative paths still use the retained native working directory.

The [generic OPEN ABI](../../include/abi/provider.h) carries READ/WRITE intent,
CREATE, EXCLUSIVE, TRUNCATE and APPEND flags, the bounded opaque remainder and
an absolute deadline. The 32-byte prefix contains no HTTP fields. Target bytes
are bounded to 4,064; longer requests fail with FILE_TOO_LARGE. The provider
interprets the remainder without decoding, normalization or component walking.
The copied deadline must match endpoint delivery; zero means unlimited.

Intent needs the matching service OPEN right. CREATE also requires OPEN_WRITE;
EXCLUSIVE requires CREATE, and TRUNCATE/APPEND require WRITE intent. Targets
refuse unsupported intents rather than emulating success. Text is read-only and
refuses creation, exclusivity, truncation and append.

The temporary FILE adapter has a 24-byte reply prefix. Success returns exactly
one owned exported FILE with the intent's exact FILE rights and CALL transport,
plus optional printable ASCII media type of at most 127 bytes. Libpyxis checks
these declarations against the kernel-authenticated handle query. Metadata grants
no authority or proof of content. Malformed replies close provisional grants;
failure zeroes the prefix and carries no resource or strings.

`provider_open(provider, target, intent, flags, deadline_ns, &result, &file)`
returns transport/local-validation status. CALL_OK makes `result.status` the
provider's operation status. `result.delivery` preserves IPC delivery state even
on failure; successful copied metadata is in `result.metadata`. No generic call
retries, rebinds or falls back to HTTP continuation.

`path_resolve()` and `path_open_file()` capture the provider through current
namespace lookup, pass the opaque URI remainder and use `workspace.deadline_ns`.
Libc and shell redirects retain their existing FILE adapter. Their URI-spelling
refusals and provider directory/cwd/mutation limitations remain until the rest of
A3 replaces them; libc returned-type dispatch and transaction close belong to A4.
The current namespace/root routing is replaced by A2 environment integration.

The [private HTTP continuation ABI](../../include/abi/provider_http_private.h)
uses operation `PROVIDER_LEGACY_HTTP_OPEN` (3), independently of generic OPEN (1)
and PREPARE (2). Its 328-byte request and 80-byte reply retain the existing
complete URI, origin, flags, budgets, ERROR/BYTES/REDIRECT results, HTTP diagnostic
status and Location semantics. `provider_legacy_http_open()` is the private
single-hop helper; `provider_http_open()` retains bounded redirect accounting.
Both remain until B6. Existing libc URL access still uses PREPARE/COMMIT and
STREAM, with its final observation checks and no new generic fallback.

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
publish separate instances as `text` before starting their sessions. OPEN accepts
exact opaque `welcome` or `guide` remainders; other remainders return NOT_FOUND.
The service may be published under another compatible routing name. `textfs --welcome MESSAGE`
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
This explicitly private continuation route remains until B6; ordinary libc URL reads use
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
forward-only STREAM contract. Private HTTP continuation returns snapshots and keeps
its redirect/budget semantics; it does not implicitly commit a POST.

## Describe and error-body contract

**Owner accepted, 2026-10-14; implementation in progress.** Issuers publish a
bounded immutable description on their owned receiver; describe reads the latest
kernel-held snapshot without provider calls. Kernel-attested protocol and both
authority masks are separate from declared outcome/detail/media/length fields.
Unknown native backing length remains unknown rather than waiting for I/O.

Error bodies are copied once into kernel-held native immutable FILE snapshots.
Publication takes bytes, never a provider-served capability. Returned READ-only
bodies remain readable after provider exit or revocation; they are independent
copied data, not newly opened children of the source. Replacing or clearing a
description releases only its ownership; returned body grants survive. This
prevents description/body reference cycles. Ordinary OPEN still preserves source
lineage. No writable body grant is published, and no subsequent write/resize is
allowed.

The [description ABI](../../include/abi/describe.h) bounds detail to 255 bytes,
media type to 127, and each copied error body to 64 KiB (a starting storage
budget). Empty bodies are distinguishable from absent bodies. Metadata-only
queries need no handle slot; including a body requires one immediately available
slot and returns LIMIT rather than waiting for allocation when full.

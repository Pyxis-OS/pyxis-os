# Userspace file providers

A generic provider publishes an OPEN service through its process
[environment](environment.md). Opening a provider URI returns an attested typed
resource. FILE helpers and current shell redirection require FILE; libc dispatches
supported returned types. HTTP implements the same typed OPEN/REMOVE and
[transaction](http-transactions.md) vocabulary, returning re-readable snapshots.

```text
cat text://welcome
cksum text://guide
cat < text://welcome
cat text://guide > home://guide.txt
```

## Discovery and OPEN

Exact `name://rest` lookup uses the process's one [environment table](environment.md).
CAP values retain their masks and lineage; TEXT values become immutable native
READ FILE snapshots of one captured publication. Directory values walk literal
components; providers receive only the opaque remainder. Relative paths copy the
complete held cwd chain. No provider failure falls back to directory creation.

The [generic OPEN ABI](../../include/abi/provider.h) carries READ/WRITE intent,
CREATE, EXCLUSIVE, TRUNCATE and APPEND flags, an opaque target remainder and an
absolute deadline. The 32-byte prefix has no HTTP fields. Targets are bounded to
4,064 bytes; longer requests fail with FILE_TOO_LARGE. The original deadline is
also the endpoint deadline; it is never restarted. CREATE needs OPEN_WRITE even
with READ-only intent, EXCLUSIVE requires CREATE, and TRUNCATE/APPEND need WRITE.
Targets refuse unsupported intents. Text remains read-only.

A 432-byte typed reply declares protocol and bounded generic description, with
one owned resource grant when a protocol is supplied. Type and both masks are
checked against the kernel; issuers may attenuate authority. A resource failure
has an outcome rather than successful empty bytes. Kernel-cached failed FILE and
STREAM outcomes reject byte READ before provider delivery. Text's failed FILEs
also refuse SIZE/INFO rather than fabricate an empty size.

`provider_open_typed()` owns the resource and any independently copied native
error body. Malformed extents, strings, counts, types or descriptions release all
provisional grants; actual query errors stay errors. The narrow `provider_open()`
FILE adapter uses this same wire for current FILE-only callers; it reports other
types as unsupported. There is no second generic ABI or legacy fallback.

`resource_open_held()` borrows a FILE, native DIRECTORY or provider capability;
empty-tail held STREAM, TRANSACTION, native PIPE and CONSOLE opens also retain
only the rights required by the consumer. TRANSACTION needs STAGE/COMMIT for a
writable libc stream and preserves separately held ABORT authority.
A held FILE requires an empty remainder; CREATE without a name is unsupported,
TRUNCATE needs WRITE, and APPEND remains consumer intent over positioned I/O.
A directory walk cannot cross the held root, follows no symlinks and resolves the
final component once through [typed native lookup](directories.md). Native lookup,
creation and resize retain their synchronous native wait contract; exported
OPEN and truncation use the supplied endpoint deadline. CREATE races return
ALREADY_EXISTS without re-lookup or replay. [Libc](../userland/stdio.md) dispatches
the attested type; the [shell](../userland/shell.md#typed-values-and-command-sequences)
passes held capabilities through typed expansion.

`provider_open(provider, target, intent, flags, deadline_ns, &result, &file)`
returns transport/local-validation status. CALL_OK makes `result.status` the
provider's operation status. `result.delivery` preserves IPC delivery state even
on failure; successful copied metadata is in `result.metadata`. No generic call
retries, rebinds or falls back to HTTP continuation.

`resource_open_path()` and `resource_open_held()` return the same owned typed
result. The name helper never falls back to private HTTP continuation. Existing
`path_resolve()`/`path_open_file()` FILE consumers adapt that result, while native
filesystem-only callers reject actual exported resources rather than a URI name.
Libc's transaction close follows the [generic contract](transactions.md);
[shell captures](../userland/shell.md) retain the returned capability.

## Remove

`PROVIDER_REMOVE` uses independently delegable `PROVIDER_RIGHT_REMOVE`, never
OPEN_WRITE. Its 16-byte prefix carries target length and the unchanged absolute
deadline, followed by an opaque target within the OPEN bound. The kernel checks
the admitted copy and required right before delivery. The reply uses the same
typed outcome/description vocabulary: success has protocol zero and no grant;
a failure may return a failed resource with its independently copied error body.
A successful resource grant is not a valid REMOVE success reply.

`provider_remove_typed()` retains delivery state; losing a delivered mutation's
reply reports OUTCOME_UNKNOWN, without retry. `path_remove()` captures a named
environment target once and dispatches native DIRECTORY or PROVIDER. Native
parent traversal is unchanged. Libc `remove`/`unlink` and `rm` share that route;
`rmdir` refuses providers, and a held FILE alone does not authorize name removal.

HTTP uses generic OPEN/REMOVE and [configured endpoints](http-transactions.md#configured-endpoints).
The old HTTP OPEN/REDIRECT wire, client-side following and private continuation
preparation/status adapter are removed; there is no generic fallback.

## File dispatch and lifetime

`handle_query()` reports resource rights, transport rights, immutable protocol
and native/exported kind. It exposes no object identity and does not promise
continued liveness after a successful query. Querying a cut grant or its
descendants returns DENIED; a live export whose receiver has closed returns
ENDPOINT_CLOSED. Forwarded holders retain the target's attested interface.

FILE helpers select native syscall dispatch or exported endpoint CALL from that
query. Ordinary operations use the same [copied FILE payloads](../../include/abi/file.h):
at most 4,088 read bytes or 4,080 write bytes per helper call. Providers check the
kernel-authenticated invoking rights for each operation. Clients validate counts,
reply extents and application status; unexpected attachments are closed.

The explicit `file_read_bulk` helper uses `FILE_READ_BULK`: offset, capacity and
one absolute deadline, with at most 1 MiB returned as raw syscall bytes. Native
RAM, archive and npfs FILEs support it; other native backings refuse. Exported
FILEs use `ENDPOINT_CALL_BULK`, with the same READ and CALL authority. Ordinary
READ, libc and Go are unchanged; there is no small-read fallback. Httpfs serves
completed snapshots through a separately owned backing bulk completion.

The initiating process has a separate 16 MiB kernel bulk-data allowance. Each
layer reserves its own result buffer before delivery/I/O: client endpoint data
and provider backing-completion data may coexist and both count. LIMIT,
NO_MEMORY and QUEUE_FULL refuse without eviction or waiting for budget. No
caller address reaches the provider. Owned buffers survive outstanding work and
copies; cancellation cannot keep them alive solely through an unused receipt.
Final user copies use short mapping leases and cooperative page-sized steps,
with no large endpoint-locked or interrupt-disabled copy. Short reads and EOF
remain valid; failures return no usable count and never write after return.

A receiver owner can attach a native RAM read backing to a complete, successful,
re-readable, READ-only FILE export through `ENDPOINT_DESCRIBE_PUBLISH`. The source
must be a raw native RAM FILE with READ|WRITE and no forwarding wrapper or
transport. Publication validates the declared length, retains the source and
seals it against WRITE/RESIZE atomically; a failed publication preserves the
previous description and leaves the source unsealed. This is a local copy
optimization, not a new grant to the client. Other backings keep provider READ.

READ then copies directly from the sealed backing with the existing 4,088-byte
bound. The endpoint header, FILE reply and bytes share one mapping lease. Export
rights, descriptions, revocation lineage, receiver closure and RETIRE/ACK remain
the authority and lifetime boundary. The local operation starts under the
endpoint guard: closure or CUT after that boundary may coexist with finishing
that admitted copy; subsequent operations fail. No native backing identity or
authority is exposed. `ENDPOINT_READ_BYTES`, on the owning CONTROL receiver,
returns cumulative successful local READ bytes for that export until RETIRE_ACK;
rereading counts again and EOF counts zero. It needs no per-READ notification.

Native `file_transfer(destination, destination_offset, source, source_offset,
size, &transferred)` transfers at most 1 MiB between explicitly supplied FILEs.
The destination call needs WRITE and the separately captured source needs READ.
RAM/initrd-to-FAT32 and FAT32-to-RAM are supported; other backing pairs and the
same object on both sides refuse before effects. Exported provider dispatch is
unsupported: caller-local handles are never sent to a provider. The global
4 KiB message limit is unchanged; only offsets, count and the source handle are
copied into this native operation.

A successful transfer reports a confirmed prefix, possibly short or empty at
source EOF. Every error stops the caller; uncertainty is never replayed. Each
range is one filesystem mutation with its existing validation and durability
barriers, using device-bounded private I/O runs. This does not confer raw-device
or mount authority. The kernel captures both references and forwarder lineage,
then reserves the memory FILE's operation ownership before acceptance, without
waiting on another reservation. BUSY leaves it unmodified. The sole filesystem worker owns one
bounded transfer buffer, preallocates all destination backing before I/O, and
holds the memory loan until actual retirement. A RAM destination changes only
for confirmed bytes; retained RAM budget exhaustion keeps its existing permanent
failed-FILE outcome. No user mapping or spinlock crosses filesystem waits.
Synchronous calls and [native completions](native-completion.md) share this path.

`FILE_INFO` reports independently valid size, domain, object and modification
time through the same dispatch. HTTP/text private snapshots report a domain
disjoint from native backing, with no object ID or mtime. Generic exports do not
gain that domain merely by being exported; see [file metadata](file-metadata.md).

Transport and operation failures remain distinct until the helper maps them to
its native status. A delivered mutation whose transport fails reports
OUTCOME_UNKNOWN. No automatic retry, reconnection or rebinding occurs. Existing
FILE reads and generic path/libc opens still wait without a caller deadline.
HTTP selects one 30-second default or the explicit earlier/later absolute OPEN
deadline, covering framing and private spooling. Its snapshots are complete before
publication and can be reread afterwards; backing storage is explicitly bounded. Other native OPEN callers
can supply a deadline explicitly; an unresponsive generic provider can block its
client.

Copying, transferring or inheriting a FILE grant retains the same opened object.
Libc aliases share an open object's cursor; separate opens have separate cursors.
Environment replacement affects future
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
and exits after their exports retire too. Retirement never terminates the
provider and nothing supervises or restarts it; this is capability-driven
shutdown, not forced cancellation. A holder of the provider's process observer
can still request [TERMINATE](processes.md#implemented-process-completion), or stop its
execution group ([group termination](execution-groups.md)), as for any process.

## HTTP and HTTPS snapshots

HTTP root exports interpret their opaque remainder as a host/path under their
configured HTTP/HTTPS and plain/JSON representation. READ returns a completed,
re-readable FILE snapshot; WRITE returns a generic one-shot POST transaction.
REMOVE issues DELETE under its own right. Redirect following belongs to the
provider. See [HTTP transactions and storage](http-transactions.md).


## Describe and error-body contract

Issuers publish a bounded immutable description on their owned receiver; describe reads the latest
kernel-held snapshot without provider calls. Kernel-attested protocol and both
authority masks are separate from declared outcome/detail/media/length fields.
A [detail-restricted grant](processes.md#handle-restrictions) receives the
description with an empty detail; its error body is still returned.
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

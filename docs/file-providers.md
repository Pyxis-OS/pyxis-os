# Userspace file providers

A provider publishes an OPEN service through an explicitly delegated
[namespace](namespaces.md). Opening a provider URI returns an exported FILE
capability. Ordinary libpyxis file helpers, libc, `cat`, `cksum`, `tee` and shell
input redirection use it without provider-specific code.

```text
cat text://welcome
cksum text://guide
cat < text://welcome
cat text://guide > home://guide.txt
```

## Discovery and OPEN

Libpyxis selects the complete `scheme://` prefix. Directory roots retain their
existing component-walking rules; namespace-only schemes use provider OPEN.
Binding a scheme in both places is an error, including when the provider has
closed. Provider failure never causes a fallback to directory lookup or creation.
Relative paths still use the retained native working directory.

The [OPEN ABI](../include/abi/provider.h) carries the complete URI as copied bytes
and a nonzero READ/WRITE mask. The provider interprets the URI without library
decoding, normalization or component walking. URI bytes are bounded to 4,080;
longer requests fail with LIMIT instead of being truncated. Requested access
needs the corresponding service OPEN right. Read-only services reject writable
opens before producing an exported file.

Successful OPEN returns exactly one owned exported FILE grant, exactly the
requested resource rights, and SEND|RECEIVE transport authority. Reply metadata
declares the FILE interface and byte representation, with an optional printable
ASCII media type of at most 127 bytes. Libpyxis checks these declarations against
the kernel-authenticated handle query. Metadata grants no authority and does not
prove that bytes conform to their media type. Malformed replies release received
grants and fail; unsuccessful OPEN returns no resource.

`provider_open()` exposes the metadata to native callers. `path_resolve()` for
files and `path_open_file()` use the same bridge; libc and shell redirects share
the latter's open/create routing. Provider directory operations, cwd changes,
removal and rename are unsupported.

## File dispatch and lifetime

`handle_query()` reports resource rights, transport rights, immutable protocol
and native/exported kind. It exposes no object identity and does not promise
liveness: querying a withdrawn export still describes its interface.

FILE helpers select native syscall dispatch or exported endpoint CALL from that
query. Both routes use the same [copied FILE payloads](../include/abi/file.h): at
most 4,088 read bytes or 4,080 write bytes per helper call. Providers check the
kernel-authenticated invoking rights for each operation. Clients validate counts,
reply extents and application status; unexpected attachments are closed.

Transport and operation failures remain distinct until the helper maps them to
its native status. A delivered mutation whose transport fails reports
OUTCOME_UNKNOWN. No automatic retry, reconnection or rebinding occurs. Existing
FILE helpers and the OPEN bridge wait without a caller deadline; bounded-wait
APIs remain future work, and an unresponsive provider can block its client.

Copying, transferring or inheriting a FILE grant retains the same opened object.
Each libc descriptor keeps its own offset. Namespace replacement affects future
opens; existing grants remain attached to the original provider. Withdrawal or
provider exit makes affected invocations fail. RETIRE and its acknowledgment
govern provider state reclamation and object-ID reuse.

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
service replace text app://textfs.pxe --welcome "Replacement service"
cat text://welcome
namespace remove text
```

Writable opens are denied by the published OPEN_READ grant (EACCES through libc).
Publication uses the existing service handoff and its ordinary resource grants,
omitting the parent namespace and namespace-creation service. The text service
performs no network operations.

Each OPEN creates one export. A receiver can hold 64 exports, including its OPEN
service, so a published instance supports at most 63 simultaneous file exports;
copies share an export. The provider drops its own client handles after successful
handoff. File state remains until RETIRE, then the provider acknowledges it before
reusing the ID. Canceled or timed-out calls finish their receipts without stopping
unrelated service work. Failed OPEN reply transfer releases the provisional file.

Removing or replacing the service binding can retire its OPEN export once all
lookup references close. The old process continues serving already-open files
and exits after their exports retire too. There is no process-kill facility or
supervisor; this is capability-driven shutdown, not forced cancellation.

HTTP fetching, format negotiation, shared-memory transfers and writable providers
remain separate [userspace-services tasks](wip/userspace-services.md).

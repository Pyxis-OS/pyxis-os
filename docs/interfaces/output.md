# Owned capture output

[`abi/output.h`](../../include/abi/output.h) defines a byte-or-value capture
pair; [`abi/retention.h`](../../include/abi/retention.h) defines its declared
session account. General pipes remain [byte streams](pipes.md); general C9
pipeline integration and shell capability substitution are separate work.

## Construction and authority

`RETENTION_CREATE_OUTPUT` on an account requires CAPTURE and returns a
creating-process collector plus a transferable native FILE producer with WRITE
only. The former unaccounted PIPE-service constructor is removed. Allocation and publication run through the BSP executor;
result slots are reserved before publication. The collector requires COLLECT.
It cannot be copied, delegated, stored in an environment, used as a mutable
forwarder target, or handed off. Siblings in the same process may use its handle.
No ordinary child gains capture construction authority implicitly.

The private byte backing is not delegated to producers. FILE WRITE, RESIZE,
SIZE, INFO and SYNC use the ordinary RAM FILE implementation. The wrapper
captures write metadata and bounded payload once before admission; it never
uses a second caller-controlled request to decide which mode was selected.
READ is denied by the producer's rights.

## Session accounts and retained backing

Trusted boot init privately delegates the native RETENTION factory through the
init chain. The root `session.pxe` reads the sole session configuration, consumes
its factory entry, and creates an account with positive immutable value/session
byte limits. The account records the calling session's originating space; there
is no limit increase, reset, reassignment or explicit debit operation. Repeated
trusted session handoff reuses the inherited account. The server, local/remote
mux and their panes/connections receive only that account, never factory rights.
Private account/factory entries are excluded from ordinary child inheritance.
Capture's child inventory refuses either protocol even through copied aliases.

Factory CREATE returns one owned CAPTURE account. Account STATUS requires
CAPTURE and samples limits/current retained bytes without allocation or I/O.
The same right constructs output pairs and bounded failed values. Factory and
account construction reserve destination slots before BSP allocation and policy
validation; late bad reply copies release exactly the returned handles. Grant
issuance follows the source operation's revocable lineage. Public result readers
receive no account authority.

Default limits are 64 MiB/value and 512 MiB/session, chosen in
[session configuration](../userland/session-configuration.md), not ABI constants.
Charge the logical FILE length, including holes and excluding frame/index
rounding. Exactly at either boundary succeeds. A retained backing reserves the
positive size delta before allocation or effects; overwrite adds no charge,
shrink returns its delta, and uncommitted allocation/admission failure refunds
the reservation. A late reply-copy error never refunds committed data.

The backing owns its originating account and charge independently of handles.
COPY, exported environment entries, namespace removal and shell exit cannot
transfer or erase that charge. Final backing destruction returns it, through
the existing deferred BSP retirement path; in-flight storage pins may delay
that release. Account closure alone neither releases backing nor resets quota.
No account guard spans allocation, frame access, grant callbacks, user copy or
waiting. Ordinary RAM FILEs are outside this capture policy. An already
re-readable handed-off value is neither copied nor charged again.

Crossing either limit permanently discards the private prefix and seals its
failure outcome as TOO_LARGE. The producer receives FILE_TOO_LARGE; subsequent
mutation, including zero writes/shrink-to-zero, cannot repair it. TAKE still
returns the failed READ-only FILE. DESCRIBE succeeds with immutable failure and
a bounded file-output hint; READ, including zero capacity, SIZE and INFO fail,
never successful empty data. Failed output is distinct from generic admitted
mutation/mixed-mode failure, whose TAKE returns IO. A successful ownership
transfer does not promise a successful resource outcome.

`RETENTION_CREATE_FAILURE` accepts a non-OK resource outcome and at most 255
counted UTF-8 detail bytes, with no NUL. It returns a zero-charge immutable
READ-only FILE retaining that exact outcome/detail, with no prefix or error
body. This supports capture-side failure publication without a provider process
or invented data. Descriptor bytes are bounded metadata, not retained content.

## Byte or value admission

The first nonempty write selects BYTES; a successful `FILE_HANDOFF` selects
VALUE. Empty output selects EMPTY and collects as an empty immutable FILE.
Zero-byte writes select neither mode. Nonzero RESIZE also selects BYTES;
RESIZE of a value is a conflict. A handoff after bytes, bytes after a value,
or a second value returns IO, preserves any rejected source handles, and
latches whole-capture failure. Collection then returns IO and publishes no
successful prefix or first value. A failed admitted WRITE or RESIZE also
latches failure and discards private backing: ignoring its error cannot turn
partial or empty output into a successful collected value. Query/pre-admission
validation errors do not constitute admitted mutations.

`FILE_HANDOFF` accepts one explicitly owned local value handle and an optional,
distinct already-owned observation handle. It first reserves the cell, then
atomically rechecks/extracts both exact source generations from the caller's
table. Success moves the original grants, including wrappers, masks and lineage;
it invalidates source generations without logical CLOSE effects. It neither
reads the result nor invents observation authority. Failed values can move too.
Receivers, receipts and collectors cannot be payloads. Pre-commit rejection
leaves the source grants intact. A concurrent admitted operation may finish
after cancel or logical close; accepted ownership is drained if publication is
no longer possible.

`FILE_OUTPUT_STATUS` is a provider-free feature query for native FILE handles.
Ordinary native FILEs answer BAD_OPERATION; console, PIPE and exported sinks
are refused by the SDK before submission. Only definite unsupported status
allows byte-copy fallback. Never retry an accepted or uncertain handoff.

## Collection and lifetime

STATUS reports mode and SEALED/FAILED/READY/TAKEN flags without waiting. The
coordinator independently observes job/subtree retirement, then SEAL closes
admission; final logical producer close also seals. READY requires sealed
admission and zero admitted operations. `wait_many` accepts COMPLETE and CLOSED;
a CLOSED-only poll does not report COMPLETE. Closure is terminal for either
interest, and readiness reserves nothing.

TAKE is nonblocking: WOULD_BLOCK until ready, one owned result (and optional
observation) on success, ENDPOINT_CLOSED after consumption, or ABANDONED after
cancel. It reserves all destination slots and validates policy before consuming
the cell; local capacity/policy failure leaves the queued result. Byte results
become READ-only immutable native FILEs, with newly issued grants derived through
the collector operation's lineage. Value results preserve their original
authority. No new retain is performed for moved values. An admitted TAKE can
complete despite a later cancel, under the ordinary started-work rule. A late
bad reply copy releases exactly the returned handles; the SDK reports uncertain
ownership rather than permitting replay.

CANCEL and final logical collector CLOSE detach queued grants and private byte
backing, then release them outside the pair guard. Storage-only CALL/readiness
references do not postpone logical drainage. In particular, a queued pipe writer
stops keeping its peer open when the collector closes. Pair storage retires on
the BSP only after both embedded endpoints and active operations release it.
The short guard spans no allocation, user access, grant callbacks or waits;
readiness saves/restores interrupt state before using it.

## Userland

[`output.h`](../../userspace/include/output.h) exposes the owned operations.
The shared libc handoff helper never consumes borrowed descriptor handles. It
accepts a fresh unread READ-only source with re-readable or held-failure
semantics, makes explicit owned copies with narrowed READ authority, and retains
any existing observation separately. Single-source `cat` uses that helper;
ordinary destinations and ineligible sources keep byte copying. Command names
have no special meaning to the shell.

The [direct capture SDK](../userland/native-jobs.md#private-capture-and-deadlines)
uses these producers. Existing general pipelines and Lua's pipeline-backed
capture remain byte-oriented until C9. These kernel prerequisites do not
complete the shell's capability-valued substitution; it must check each collected
resource outcome independently of transport success and the producer exit code.

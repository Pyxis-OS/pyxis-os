# Owned capture output

[`abi/output.h`](../../include/abi/output.h) defines a byte-or-value capture
pair. This is the A5 handoff prerequisite; general pipes remain
[byte streams](pipes.md), and session retention accounting is a separate next
prerequisite. Current byte backing has no hard session quota.

## Construction and authority

`PIPE_CREATE_OUTPUT` on the existing PIPE service requires CREATE authority and
returns a creating-process collector plus a transferable native FILE producer
with WRITE only. Allocation and publication run through the BSP executor;
result slots are reserved before publication. The collector requires COLLECT.
It cannot be copied, delegated, stored in an environment, used as a mutable
forwarder target, or handed off. Siblings in the same process may use its handle.
No ordinary child gains capture construction authority implicitly.

The private byte backing is not delegated to producers. FILE WRITE, RESIZE,
SIZE, INFO and SYNC use the ordinary RAM FILE implementation. The wrapper
captures write metadata and bounded payload once before admission; it never
uses a second caller-controlled request to decide which mode was selected.
READ is denied by the producer's rights.

## Byte or value admission

The first nonempty write selects BYTES; a successful `FILE_HANDOFF` selects
VALUE. Empty output selects EMPTY and collects as an empty immutable FILE.
Zero-byte writes select neither mode. Nonzero RESIZE also selects BYTES;
RESIZE of a value is a conflict. A handoff after bytes, bytes after a value,
or a second value returns IO, preserves any rejected source handles, and
latches whole-capture failure. Collection then returns IO and publishes no
successful prefix or first value.

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
become READ-only immutable native FILEs. Value results preserve their original
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
capture remain byte-oriented until C9; this primitive does not complete A5's
hard retention prerequisite or the shell's capability-valued substitution.

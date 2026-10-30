# Native shared-memory regions

A native region owns fixed normal-RAM backing that independent processes can
map R/NX or RW/NX. The public kernel interface is in
[shared_memory.h](../../include/abi/shared_memory.h) and
[memory.h](../../include/abi/memory.h). The SDK provides explicit-handle helpers,
bundle declarations select held creation authority, and an opt-in shared-buffer
example supplies the first application consumer. Task 4 qualification remains
pending in the [milestone](../wip/shared-memory.md#delivery-tasks).

## Authority and operations

| Operation | Captured input | Result and required authority |
| --- | --- | --- |
| MEMORY CREATE_SHARED | `memory_payload.create_shared`: nonzero size and zero reserved word | `shared_memory_created`: one owned READ\|WRITE region handle and rounded extent; MEMORY CREATE_SHARED required |
| Shared region MAP | `shared_memory_map`: READ or READ\|WRITE and zero reserved word | `memory_reservation`: process-local identity, chosen address and full extent; requested region rights required |
| MEMORY RELEASE_RESERVATION | Exact identity in the calling process | Status only; MEMORY MANAGE required; removes that view without freeing object-owned frames |
| COPY / DESCRIBE | Existing capability ABI | Normal attenuation or provider-free fixed-size metadata; no physical/object identity |

CREATE_SHARED is independent of MANAGE. The trusted stock boot-init declaration
explicitly holds both; existing application MANAGE grants gain no creation
right. Receiving or mapping a region requires no creation authority. Explicit
unmap requires the caller's own MEMORY MANAGE, including after the region handle
has closed. Raw addresses and view identities grant no cross-process authority.

Trusted boot-init/session/mux/Remote handoffs retain creation privately, without
a per-space switch. Stock shell and Lua bundles keep their required
`memory/manage` and explicitly request an optional private `memory_create_shared`
source with `memory/manage` plus `memory/create_shared`. A bundle receives
creation only when it declares it; ordinary native children and MANAGE-only
manifests keep their existing rights. Missing required authority refuses preparation. See
[bundle declarations](../userland/program-bundles.md) for source selection.

CREATE rounds up to whole pages and publishes nothing until the entire backing
is zeroed, including padding. Frames need not be contiguous. MAP chooses a
separate reservation in the caller; no fixed address, subrange, resize, sparse,
sealing, device import or execute permission exists. A separate JIT grant does
not make shared aliases executable. A read-only alias is not an immutable
snapshot while another legitimate writer exists.

Requests and reply buffers are captured/checked before effects; reply mappings
are rechecked after exclusive VM admission. Creation reserves its destination
handle slot before allocation. Zero/overflow, nonzero reserved words and
unsupported mapping masks fail BAD_REQUEST; missing authority fails DENIED,
short replies BUFFER_TOO_SMALL, allocation exhaustion NO_MEMORY and exhausted
identities/reference/handle admission LIMIT. These failures return no result
bytes or partial handle/view. Failed mapping may leave empty private page-table
ancestors, counted until process-space destruction as with ordinary private VM.
Ordinary private RELEASE/COMMIT/DECOMMIT, guarded stack/TLS pins and address
parking cannot repurpose shared views.

DESCRIBE reports the normal kernel-attested type/rights and known fixed length.
It reports neither physical addresses nor a global object identity, and claims
neither immutable bytes nor mapping authority. A stale or cut grant retains the
ordinary capability refusal behavior.

## SDK helpers

[libpyxis](../../userspace/include/shared_memory.h) provides
`shared_memory_create(memory, size, created)` and
`shared_memory_map(region, rights, view)`. They borrow explicit held grants and
return native statuses, clearing the output on failure. Success transfers an
owned region handle or process-owned view descriptor to the caller. Use native
`memory_release_reservation(memory, identity)` to unmap.

The [libc extension](../../userspace/libc/include/pyxis/shared_memory.h) provides
`pyxis_shared_memory_create`, `pyxis_shared_memory_map` and
`pyxis_shared_memory_unmap`, with the same explicit handles/descriptors and
0/-1 plus the existing errno mapping. No ambient lookup, POSIX mapping facade,
malloc change or implicit close/unmap is added. Explicit native job grant
selection accepts the new creation bit and native shared-region READ/WRITE;
it does not add them to ordinary child grants.

An untrustworthy post-effect reply is OUTCOME_UNKNOWN (libc EIO), not proof that
creation/mapping did nothing. Helpers never retry or discard a possibly
published result. Native callers retain the precise status; libc callers must
not automatically replay EIO. Ordinary definite native errors retain their
existing errno mapping.

The opt-in [shared-min example](../../userspace/examples/shared-min/README.md)
uses a fixed buffer and a READ-only worker, with endpoint acknowledgments before
buffer reuse and actual contained-worker retirement before final release.
`make SDK=/path/to/sdk shared-min` builds its bundle explicitly. Normal userland
installation ships `share/examples/shared-min.c` and
`share/examples/shared-min-manifest.json`; the compiled bundle is excluded from
the default build and installation. The example and declaration paths still
await task 4 runtime qualification.

## Source lineage and lifetime

Regions inherit their creation grant's lineage. Each mapping captures its selected
source lineage and backing; COPY, IPC, launch and environment entries retain
ordinary attenuation. Closing a region handle does not unmap its views. Creator
exit alone does not revoke independent holders or release their physical charge.
RETARGET changes future resolution, not the backing of existing views.

A persistent registration owns logical holder grants, never controller authority.
Enrollment rechecks lineage under the same guard as CUT, before any root exposes
the view. An already-started operation grants no exemption to this registration:
CUT either prevents enrollment or includes the view in withdrawal. All records
and the reference needed for uncertain retirement are reserved before exposure.

Explicit CUT success is an unmap fence for every selected mapping. It closes
each affected process's admission, joins any preceding private mutation/reply,
and drains actual roots and mapping borrowers before PTE changes. Existing
fixed RAM actors make nonblocking progress; one process mutation is owned at a
time, without waiting while holding another. Private-root departure/reload and
non-reused generation proofs apply; a shared-kernel flush is no substitute.
No per-message mapping checks or new worker are added.

One absolute one-second observation budget covers the explicit CUT, not one
budget per target. Completion and uncertainty are elected under the same guard:
completion that wins first can return OK; elected uncertainty stays
OUTCOME_UNKNOWN, including on repeated CUT. Unconfirmed withdrawal requests
termination of affected processes and quarantines uncertain backing until reboot.
A self-targeting revoker finishes stopped syscall cleanup on the kernel root;
it never reactivates an uncertain private root just to return.

Termination preserves the first terminal result. Actual CPU root/stack departure
still precedes reclamation: if that departure cannot be proved, execution records
and WAIT remain pending. After safe departure/detachment, process/group completion
can finish while independent quarantined frames, metadata and origin charges
remain. Late progress never reuses elected quarantine. Normally pending cleanup
still joins actual reclamation. Unrelated processes and cleanup continue.

Last-controller close/exit of a revocable branch queues the same pre-reserved
work without sleeping; CLOSE is not an unmap fence. Access after withdrawal is
an ordinary unmapped/protection fault. There is no implicit retry or pointer
recovery API. Plain independent grants have no controller to auto-revoke them.

## Accounting and reclamation

Physical region backing is charged once to a retained origin account with its
permanent space and non-reused process-list key. It retains neither the creator
process nor its VM. Regions hold it across creator exit. Transfer and aliases
neither refund nor transfer that physical charge. Actual PMM prefixes are charged
as allocated; discharge follows actual freeing. Allocation failure unwinds its
unpublished prefix. The origin itself is kernel-only, ungrantable metadata.

Per-process virtual view bytes/count and shared-record metadata are separate.
VM statistics separately count owned private root/lower page-table bytes; shared
kernel tables are excluded. Empty ancestors stay charged until actual table
reclamation. These tables can serve both private and shared reservations, so
there is no invented per-view partition of common ancestors. Aliases do not
multiply the region's physical backing charge.

Final reference destruction transfers a whole region to its preparation CPU's
existing RAM actor, preserving group cleanup attribution until bounded physical
and metadata release finishes. The source callback reserves the replacement
cleanup token before returning. There is no per-close work-node allocation or
permanent kernel data alias. Mappings prove their own retirement before dropping
backing references. Final-task departure queues alias removal before ordinary
process cleanup waits on its retained view records, avoiding a lifetime cycle.

Quarantine consumes the reference reserved before exposure: one independent
region reference and its origin charge survive until reboot. Repeated quarantine
does not double-charge, retain a process or add a group cleanup join. Queue
acceptance and process completion never claim those frames were freed.

Accounting is not a hard quota or RAM reservation. No general process/space RAM
isolation, FILE-retention budget reuse or JIT budget reuse is added. Kernel
snapshots distinguish physical, virtual-view, shared-record and private-table
metadata; allocator overhead is not claimed as part of record-byte gauges.

## Synchronization and limits

Normal WB CPU mappings are coherent. Initialization is visible at MAP completion;
later accesses need the program's explicit ownership and release/acquire protocol.
There is no automatic notification or transaction. Native lock-free atomics are
building blocks, not a promise that libc/C++ mutexes are process-shared. Existing
endpoints/pipes can carry notification. GPU/scanout/DMA sharing, compositor frame
reuse and device cache protocols remain outside this interface.

The [qualification record](../development/experiments/shared-memory.md) separates
source inspection, ordinary builds, QEMU behavior, debugger observations and
native existing-path controls. No lost-CPU/ACK or allocator fault injection is
claimed.

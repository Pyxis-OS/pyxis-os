# Native shared-memory regions

A native region owns fixed normal-RAM backing that independent processes can
map R/NX or RW/NX. The public kernel interface is in
[shared_memory.h](../../include/abi/shared_memory.h) and
[memory.h](../../include/abi/memory.h). SDK wrappers, general bundle/configuration
delegation and the first application consumer remain the next
[milestone task](../wip/shared-memory.md#delivery-tasks).

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

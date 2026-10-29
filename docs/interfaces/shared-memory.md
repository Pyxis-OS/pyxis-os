# Native shared-memory regions

The storage foundation supplies native fixed-region ownership and accounting.
Public CREATE/MAP and declaration adapters are not exposed by this slice; they
arrive with the connected withdrawal path in the
[shared-memory milestone](../wip/shared-memory.md). Ordinary private MEMORY
behavior and grants are unchanged.

## Backing ownership

A region owns an immutable physical-frame vector, fixed page-rounded size and
an origin-account reference. Physical frames need not be contiguous. Allocation
claims at most 64 frames per PMM acquisition, zeroes through the creating CPU's
scratch slot after unlocking, and yields between bounded batches. Nothing is
published until all backing is zeroed. Failure frees its complete provisional
prefix and metadata before returning; it never publishes partial backing.

The origin account records its permanent space and non-reused originating
process-list key, without retaining the originating process or address space.
Regions retain it across creator exit. Actual allocated frame bytes are charged
as PMM prefixes are acquired and discharged only after freeing them; metadata
bytes are distinct. The space has aggregate allocated/quarantined physical-byte
counters. These are accounting, not a hard quota or reserved RAM allowance.
FILE-retention and JIT budgets are not reused.

## Type and metadata

The native type reports `PROTOCOL_SHARED_MEMORY`. READ and WRITE are independent
attenuable resource rights; native transport rights are zero. READ is needed for
an R/NX view and READ+WRITE for RW/NX when mapping is connected. No execute right
exists, including through a separately held JIT grant. A read-only alias does
not make backing immutable while another writer exists.

DESCRIBE is a provider-free known-length snapshot of the fixed size and normal
kernel-attested type/rights. It does not report an object/physical identity,
claim immutable bytes or grant mapping authority. The origin account itself is
kernel-only metadata and cannot be installed as a public grant.

## Last reference and reclamation

Only after every backing reference and future mapping/access lease ends may
final region destruction begin. The BSP object callback transfers the whole
zero-reference region to its creation CPU's existing RAM actor. It acquires a
replacement cleanup token before the generic callback returns, so queue
acceptance is not backing reclamation. No new worker or per-call cleanup-node
allocation is used.

That actor releases bounded frame batches and metadata, then drops the origin
reference and ends cleanup attribution. Physical backing has no permanent
kernel alias in this foundation: later mappings must prove their own retirement
before dropping the final reference. Snapshot/account references alone are not
mapping or device leases.

## Mapping integration status

Kernel-internal shared-view work adds a persistent source-lineage registration,
not an ordinary started-operation record: CUT must include it even after MAP's
operation starts. Enrollment rechecks lineage under the same guard as CUT.
Records own logical holders and backing, never controller authority. The private
mutation owner reserves/maps the full view before any root exposes it.

A per-process aggregate queues withdrawal on an existing fixed RAM actor. Its
separate external VM token waits behind private mutation/reply and actual root
and borrower departure; the actor does not park while waiting. Persistent
views retain process storage until the last-task departure hook queues removal;
that hook runs before ordinary process cleanup waits on these retained records.
No task stack, user pointer or borrowed operation record is retained.

Uncertain backing consumes a reference reserved before mapping exposure. One
independent region reference and the origin charge remain until reboot; repeated
quarantine does not charge twice or enroll another process/group cleanup join.
Normal final reference releases inherit the affected process's cleanup context.
A stopped self-revoker may finish kernel-only syscall cleanup and terminate
without reactivating its private root.

This internal integration is incomplete and unqualified for use: public
CREATE/MAP dispatch and reservation-release wiring are still pending. Existing
control boots do not qualify constructor, shared-view or withdrawal behavior.
The accepted termination/completion boundaries remain in the milestone.

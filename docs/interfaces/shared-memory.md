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

Quarantine and private-view withdrawal are not implemented by the storage slice.
Their accepted termination/completion rules remain in the milestone; ordinary
successful control boots are not qualification of those future paths.

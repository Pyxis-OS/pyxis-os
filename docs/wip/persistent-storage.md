# Pyxis pool and persistent filesystem

Status: agreed design direction, 2026-09-29, with a proposed milestone sequence.
Nothing here is implemented or authorizes implementation. The exact disk format,
enforcement interfaces and individual PR tasks still need specification; remaining
proposals are identified separately below.

Related: [planning agenda](storage-and-terminal-agenda.md),
[filesystem direction](../vfs.md), and
[users and authority checkpoint](users-and-authority.md).

## Requested direction

- A custom Pyxis filesystem and storage pool, with native semantics rather than
  adopting Unix permissions or interfaces by default. Existing disk formats and
  native APIs are separable; being different from Unix alone does not require a
  custom format. Control over pool allocation, evolution and metadata motivates
  this design.
- An EFI/FAT32 boot partition can hold the bootloader, kernel and initrd. A
  separate GPT partition holds the Pyxis pool and its virtual volumes.
- Volumes grow automatically from available pool capacity, with a baseline
  reservation and an upper bound. Volumes also shrink as data is released.
- Reserve emergency/migration capacity. The user's initial candidate is 4–8 GiB,
  rather than reserving an impractical theoretical maximum file size. The exact
  size remains open; the agreed reserve is an allocator-protected capacity budget,
  not a fixed physical region.
- Use COW and design for NVMe-oriented workloads from the beginning.
- Reserve on-disk structure space for future fields. Each structure should have
  a format version that changes for incompatible layout/meaning changes, not
  merely because a backward-compatible field uses reserved bytes.
- A 4 KiB superblock with reserved space, and copies at the beginning and end,
  is the requested starting layout. Other metadata may use block-sized containers;
  exact node/entry sizes are undecided.
- Provide a Linux FUSE implementation so the filesystem can be mounted on the
  development host.

The initial migration example is `home` to `home-upgrade`: populate the new
format while freeing old storage, then remove the old volume and publish the
replacement as `home`. The names illustrate migration staging, not distinct
logical identities: migration preserves the volume and its object identities.
The exact atomic publication protocol remains to be specified.

## Explicitly agreed decisions

1. **Migration is offline.** Application access to the migrating volume is stopped.
   How to drain or reject outstanding handles/mappings remains to be designed.
2. **Migration is crash-resumable and forward-only.** Once old extents have been
   reclaimed, whole-volume rollback is not promised. Recovery resumes conversion.
3. **The baseline is a total-capacity guarantee.** The discussed baseline is 8 GiB
   per volume, including its existing allocation, not an extra 8 GiB kept free.
   A volume using 3 GiB retains a protected 5 GiB of unused guarantee. It may grow
   beyond the baseline up to its upper bound using unreserved pool space.

Persistent object identity and ownership must be considered before record
layouts are fixed. The agreed [identity and authority direction](users-and-authority.md#agreed-identity-and-authority-direction)
uses stable Pyxis principals, authentication-independent ownership, local policy
and explicit runtime capabilities. Explicit allow grants, scoped directory
capabilities and prospective policy changes are also agreed. The
[creation and move rules](users-and-authority.md#agreed-creation-and-namespace-changes)
use parent-controlled policy ownership, destination subtree exposure,
identity-preserving within-volume moves and new identities for ordinary copies.
Identifier encoding and immediate revocation mechanisms remain undecided.

## Agreed persistent identity and imported ownership

- Separate pool identity, logical volume identity and object identity within that
  volume. Names and block locations are mutable attributes, not identity. The
  bit widths and identifier-generation algorithms remain undecided.
- A format migration preserves the logical volume and its object identities,
  ownership and sharing policy. Replacement storage is a new physical generation
  of that logical volume, not an ordinary cross-volume copy. Temporary migration
  records distinguish old/new representations; only one representation is
  authoritative for each committed migration step. Do not expose both as writable
  mounts. Ordinary copy retains its agreed new-identity semantics.
- Deleting and recreating a name never revives the deleted object's identity.
  Removing an entry leaves retained object capabilities valid; reclaim storage
  after the last reference and the crash-recovery retention requirements end.
  Directory removal constraints and exact reference accounting remain open.
- Disk ownership records refer to stable principals, but possession of a pool
  does not authenticate those principals. Import requires explicit trusted
  identity mappings/admission; never match by username/email alone. Unmapped
  ownership remains recorded and requires explicit recovery/administration,
  rather than silently becoming ownership by the mounting user. A disk must not
  introduce trusted issuers or machine authority simply through its contents.
- A byte-for-byte pool clone duplicates identifiers. Initially reject concurrent
  attachment of duplicate pool identities rather than silently treating the two
  copies as independent volumes. Backup restore and deliberate clone re-identifying
  need a later explicit workflow.

## Agreed pool allocation and accounting

Sizes and transaction-level mechanisms remain open.

- Use a shared extent allocator rather than contiguous virtual partitions.
  Volume growth/shrink becomes allocation/reclamation, not boundary relocation.
- Give each volume a filesystem root, allocation accounting, capacity guarantee
  and quota alongside its agreed stable identity. Migration preserves that
  logical identity as described above.
- Charge both file data and attributable metadata to the volume. Shared pool
  metadata has separate accounting. Base capacity accounting on physical blocks,
  not apparent file lengths; sparse holes consume no data blocks. Distinguish
  live blocks, unreclaimable old blocks and bounded transaction workspace. The
  agreed COW workspace rules below separate temporary duplication from resulting
  volume allocation; exact accounting and admission algorithms remain open.
- Capacity guarantees are hard promises, not overcommit. Creating a volume or
  increasing its guarantee must fit alongside current allocations, other volumes'
  unused guarantees and pool reserves. Set the quota at least as high as the
  guarantee; growth above the guarantee is conditional on available capacity.
- Make the migration reserve an allocator-protected capacity budget, rather than
  a permanently placed scratch partition. Extents may come from anywhere in the
  pool. Ordinary volume growth cannot consume this budget.
- Treat source and replacement as one reservation during migration; temporary
  duplication uses the migration budget rather than demanding a second baseline.
- Protect a separate metadata/recovery reserve so migration and ordinary writes
  cannot consume the space needed to commit progress or free allocations.
  Its operation bounds and replenishment rules need explicit design; merely
  leaving some space unused is not a proof that recovery always has enough.

Exact reserve sizes, initial defaults for small development disks and transaction
admission algorithms remain open.

## Agreed migration data strategy

- Preserve compatible file-data extents in place. A metadata-format migration
  builds replacement metadata referencing those extents, rather than routinely
  copying file contents. Reducing migration writes is an explicit goal.
- Transactionally transfer extent ownership/reference information. Never free an
  extent still required by authoritative state or migration recovery. This needs
  an internal migration handover, not public reflink or snapshot functionality.
- Copy/transform data only when its representation is incompatible. Use bounded
  chunks rather than requiring an entire file to fit in the reserve. The migration
  budget covers replacement metadata and data that actually needs transformation.
- Reuse cannot be promised for every future format change. A change to the pool's
  own allocator/extent representation may need a different migration procedure.

## Agreed ordinary COW transaction workspace

- Protect a bounded pool-wide workspace budget for ordinary COW transactions,
  separate from migration and recovery reserves. Volume growth cannot consume
  it as permanent capacity. Reserve sizes and concurrency limits remain open.
- Admit bounded transactions only after reserving enough physical workspace for
  replacement data, metadata and the required commit/reclamation bookkeeping.
  Large writes can proceed through multiple transactions; this does not promise
  whole-write atomicity or success without partial progress.
- Check the resulting volume allocation against its quota, including data and
  attributable metadata. Account temporary old/new duplication separately as
  workspace, while continuing to count every unreclaimable block as physically
  occupied. Unlinked objects retained by live capabilities remain volume usage.
- Release workspace only when the old blocks are actually safe to reclaim,
  including recovery-root and outstanding-I/O requirements. Do not release budget
  merely because a new root has been written; transaction admission must bound
  outstanding generations and delayed reclamation.
- A quota-neutral overwrite can use workspace at the quota limit. It is not an
  unconditional success guarantee: metadata growth, fragmentation or unavailable
  workspace may still prevent admission. Permanent growth must fit the volume's
  quota and pool accounting without borrowing protected budgets indefinitely.

## Agreed durability and recovery contract

- Ordinary write success means the reported bytes were accepted and are visible
  through coherent filesystem reads, not necessarily persistent after power loss.
  It does not promise whole-write atomicity across bounded transactions. Closing
  a handle alone is not a durability barrier.
- Start with a volume checkpoint as the persistence boundary. A successful sync
  covers all modifications accepted in that volume before the operation's
  ordering point, including data, namespace changes, ownership and sharing policy.
  Concurrent later changes need not be included. File sync may initially use the
  same coarse checkpoint; exact native/libc interfaces and authority remain open.
- Commit replacement data and metadata with the required storage ordering before
  publishing a checksummed, generation-numbered root. Include allocation ownership
  in the recoverable commit state. A transaction generation is not a format version.
- Recovery selects a valid committed generation; a crash during namespace
  publication yields the old or new operation state, not a half-applied rename.
  Keep old blocks until every recovery root still eligible for selection no longer
  needs them. Superblock placement alone does not define this protocol.
- Propagate persistence failures; never report successful sync after a failed
  required write/flush. If a failure leaves commit outcome uncertain, stop normal
  mutation and require recovery rather than guessing which extents may be reused.
  No guarantee covers devices falsely reporting persistence or arbitrary media loss.

## Agreed format evolution and compatibility

- Define disk encoding independently of C layouts: fixed-width fields, explicit
  byte order and record boundaries. Metadata records have identifiable types and
  structure versions; exact headers and sizes remain open. Keep structure versions
  separate from transaction generations and enabled feature declarations.
- Initialize reserved bytes to zero and define zero as the old/default behavior
  when assigning a compatible new field. Existing writers must preserve unknown
  extension bytes when rewriting records, including COW replacements, or refuse
  writable access. Preserving bytes alone is insufficient if an old operation
  would invalidate their meaning.
- Classify format features by the knowledge required: optional for safe read/write,
  required for writing, or required for reading. Unknown write-required features
  prohibit writable access; unknown read-required features prohibit normal access.
  Read-only access must not perform recovery writes. Feature declarations have
  to be in place before metadata depending on them is published.
- Adding a compatible field in reserved space does not bump its structure version.
  Change that version when its layout or interpretation becomes incompatible.
  Use feature declarations where the existing encoding remains interpretable but
  new semantics require specific reader/writer support. No promise is made to
  keep implementations of every historical format indefinitely.
- Keep pool-format compatibility separate from volume-format compatibility.
  Unknown volume features should not alone block supported sibling volumes when
  the pool can safely account for the unsupported volume without parsing it.
  Unknown required pool features may prevent mounting the entire pool.

## Remaining migration and recovery design

The contracts above are agreed; these implementation mechanisms and failure
policies still need discussion:

- Specify a persistent migration record: create new data/metadata, commit authoritative
  ownership/progress durably, then reclaim old extents. Recovery must understand
  both participating formats and must not lose or double-allocate any chunk.
- A more expansive target format may not fit. Preflight space needs and define
  safe out-of-space suspension; no fixed reserve guarantees every future migration.
- Define a separate procedure for incompatible pool/allocator changes; volume
  conversion does not solve changes to the allocator beneath it.
- Specify root slots, storage ordering, recovery selection and reclamation under
  the agreed durability contract. A 4 KiB superblock is not an assumption of
  atomic power-failure-safe writes.

## Agreed initial implementation scope

This envelope does not settle the remaining format/interface choices.

- Apply the agreed format-evolution rules above when choosing record layouts.
- Pack small records inside metadata blocks where useful; do not allocate a whole
  page per directory entry solely for hypothetical future fields. Specify disk
  encoding explicitly rather than serializing compiler-dependent C layouts.
- Share the format/allocation/transaction implementation between Caelum and a
  Linux FUSE adapter, with narrow platform I/O/allocation glue. Initially allow
  exclusive attachment: no independent host mount, including read-only, while
  the guest writes the same pool, or vice versa. A single active mount can serve
  multiple processes normally. Linux-facing semantics and identity mapping are
  adapter policy, not the native authority model.
- Use a block interface that can support extents, batched writes and asynchronous
  requests; begin with virtio-blk, then use the same filesystem above NVMe.
- First scope: one device, without snapshots, compression, deduplication
  or encryption; no multi-device redundancy or online pool resize. Virtual volumes
  still grow/shrink within the fixed pool as agreed. COW transaction support does
  not require implementing snapshots immediately.
- Provide host formatting and inspection tools alongside the FUSE adapter. Share
  the core rather than maintaining two format/allocator implementations; repository
  ownership and build packaging remain undecided.
- Preserve migration requirements in the format/core design, but implement a
  concrete converter when there are actual source and destination formats. Do
  not claim the initial format has validated upgrade support or create an artificial
  incompatible format solely to exercise a converter.
- Separate storage milestones from implementing the entire authentication broker,
  OIDC and user-management UI. Before writable persistent ownership is introduced,
  agree the initial local-principal/bootstrap policy and enforcement subset;
  do not silently substitute a single-user bypass for the agreed authority model.

## Proposed milestone sequence

This breakdown is a proposal, not an implementation assignment or an agreed
ordering. Each milestone needs its own focused PR tasks before work starts.

1. **Block storage foundation.** First improve the shared split-queue helper as
   scoped below, then add virtio-blk discovery, bounded asynchronous block
   requests/completion, capacity and sector constraints, write/flush errors and
   GPT partition discovery. Operate only on an explicitly selected development
   disk image. Deliver usable read/write/flush support without a filesystem or
   installation UI. DMA ownership, cancellation/lifetime and device-error policy
   must be settled before implementation.
2. **Initial format and read-only core.** Specify the first pool/volume format,
   root publication/reclamation design, identifier encoding and reserve accounting.
   Settle the initial local-principal/bootstrap and storage-policy enforcement
   subset before encoding ownership or introducing persistent mutations.
   Build shared-core host formatting/inspection tools and read-only traversal of
   populated images. Define the population method in this milestone's task plan;
   do not invent a second independent format writer. Settle repository ownership
   before placing the shared core. No production data is promised safe yet.
3. **Writable core and recovery.** Add bounded COW transactions, volume allocation,
   guarantees/quotas/reserves, file/directory mutations, checkpointing and recovery.
   Implement the storage-side policy checks selected in the preceding milestone.
   Host tools exercise persistence across normal close/reopen using the shared
   core. Bootable kernel integration and FUSE are not prerequisites. Any automated
   tests or fault-injection work require separate explicit authorization.
4. **Linux FUSE adapter and ownership policy.** Mount images on Linux for ordinary
   file operations through the shared core. Adapt the selected principal/policy
   contract through explicit host identity mapping, exclusive attachment and
   sync/error behavior. This does not introduce OIDC or a complete account UI.
5. **Native persistent volumes.** Connect the shared core to Caelum's block I/O,
   allocation and capability contracts; mount a volume through trusted init and
   delegate bounded roots. Demonstrate editing, syncing, rebooting and reopening
   a document or source file, including enforced restricted access. Keep the
   bootloader/kernel/initrd on the existing boot path; no installer or NVMe yet.

Later milestones can add native NVMe, real format conversion when needed and an
installation/update workflow. The first milestone does not depend on finalizing
all filesystem record layouts or the full identity broker.

### Block-storage prerequisite: configurable split queues

Agreed addition to the proposed milestone: improve the shared queue helper before
adding another driver-specific queue implementation. This is planning scope, not
authorization to implement the helper or tune existing drivers.

The existing filesystem/entropy helper has fixed 8 KiB request/reply buffers and
one request in flight. Networking uses a separate queue implementation with 16
slots, 2 KiB packet buffers and multiple outstanding packets. Configurable buffer
sizes alone would leave the shared helper's fixed request/reply shape in place.

The intended boundary is shared descriptor allocation/chaining, ring publication,
DMA barriers, completion identification/validation and notification mechanics.
Drivers choose queue size within device limits, DMA buffer sizes/layouts and
their number of outstanding requests. Drivers retain protocol-specific validation
and request scheduling. Configuration at queue creation is enough; live resizing
is outside this slice.

Proposed focused tasks within the block-storage milestone:

- [ ] Define a narrow submission/completion contract using explicit DMA buffers
  and device access directions, with request identity and bounded descriptor
  ownership. Settle queue-full behavior, completion validation and reset/failure
  lifetime rules before implementation; timeout does not return DMA ownership.
- [ ] Implement the helper and migrate filesystem/entropy while preserving their
  existing transfer limits, concurrency, deadlines and protocol behavior. Move
  those choices out of shared queue mechanics without adding callback frameworks
  or a general driver abstraction layer.
- [ ] Use the helper for virtio-blk, choosing its transfer and outstanding-request
  limits explicitly rather than inheriting filesystem defaults.

Network consolidation is a separate follow-up after the helper serves storage;
preserve packet batching and validation if that migration is selected. Throughput
tuning is separate as well: queue configurability does not remove serialization
in a driver, worker or application interface. Exact APIs and capacities remain
decisions for the implementation proposal.

## Remaining identity and policy details

Questions to settle:

- Choose principal and object identifier encodings and implement the agreed
  identity lifetime rules. Define account deletion and authorized disk-import
  mapping procedures without accidental reassignment of old data.
- Specify rights for administering sharing and transferring policy ownership,
  separately from storage quotas and ordinary content mutation.
- Work out concrete enforcement interfaces for the agreed acquisition/delegation
  boundary and creation/move defaults. Immediate revocation, user removal and
  logout cleanup remain open.
- Decide nested namespace boundaries and each session's `home://` mapping.

No Unix UID/GID layout, mode bits, ACL format, universal administrator bypass,
per-user volume requirement or new persistent fields have been agreed. Settle
the model before choosing field sizes and record layouts.

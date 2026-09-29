# Initial filesystem format and read-only shared core

Status: agreed next storage milestone. The [block-storage foundation](../block-storage.md)
is complete. This document records the selected scope and focused tasks; it does
not start implementation. Task 1's format contract is accepted; implementation
continues through separately authorized tasks. The broader
[persistent-storage design](persistent-storage.md) and
[identity rules](users-and-authority.md) remain authoritative for later work.

Task 1's [format and host-tool specification](https://git.internal/PyxisOS/pyxis-fs/src/commit/8c4ffa67595f05eeef97b159d0af0cfb84da1ef5/docs/format.md)
is in [pyxis-fs PR #1](https://git.internal/PyxisOS/pyxis-fs/pulls/1). Follow-up review
clarified validation/accounting, compatibility, rights and reclamation. The owner
accepted a single inline extent descriptor, lower proportional reserves and ancestor
lookup checks for ID acquisition on 2026-09-29, completing task 1. The filesystem
repository contains the MPL-2.0 shared encoding layer. Task 2 implements
[local codecs and platform contracts](../../fs/docs/core.md), pinned at `fs/`;
`make fs-tools` builds the core archive and host tools. Tasks 3 and 4 provide empty
construction, opening, readonly traversal/acquisition and explicit GPT inspection.
Task 5 supplies source population and extraction. Whole-image checking is next
in task 6. The repository split and host-only scope are unchanged.

## Completion target and boundaries

Create a new populated Pyxis pool image from explicitly selected host directories,
reopen it through the shared core, enumerate volumes and directories, extract
files, and compare their contents with the sources. Inspect persistent identities,
grants and capacity accounting, and check whole-image consistency.

The host tools are `mkpyxisfs` and `pyxisfs-inspect`. Formatting builds a new image
only; it neither updates an existing pool nor promises crash-safe formatting.
An interrupted build leaves an incomplete image to recreate. The tools share
encoding and validation with the reader; there must not be a second independent
format writer.

No Caelum filesystem mount, FUSE adapter, runtime file mutations, writable COW
transactions, repair, migration converter, installer or NVMe driver is included.
No production-data safety, power-loss recovery or performance claim follows from
successfully building and reading an image. File-data checksums are deferred;
metadata checksums do not verify file contents.

## Repository and platform boundary

The owner has created [PyxisOS/pyxis-fs](https://git.internal/PyxisOS/pyxis-fs).
It will own the freestanding GNU C23 core, host tools and eventual Linux FUSE
adapter. Integration pins published revisions at `fs/` and provides the opt-in
`make fs-tools` build. During task 2 this produces `build/fs-tools/libpyxis-fs.a`;
task 3 adds the formatter and inspector executables. The specification lives in
pyxis-fs; Pyxis retains the milestone
and integration decisions. Repository and workflow references use the PyxisOS
organization.

The core owns disk encoding, checksums, compatibility, metadata traversal, file
reads and storage-policy evaluation. Later allocation, transactions and recovery
belong there too. It must not depend on host libc, kernel headers or FUSE.
Narrow platform operations supply allocation and block I/O; host adapters may
use host libc. Caelum retains capabilities, scheduling, device drivers and
namespace bindings. FUSE will translate operations and explicitly mapped host
identities, not implement a separate allocator or native policy model.

Initial core calls are synchronous: they complete or return an explicit error.
A future Caelum block callback may submit and sleep through the native block
interface without busy-waiting or blocking a CPU. This milestone does not add a
general asynchronous filesystem framework. Later transaction work can introduce
bounded batched I/O when ordering and lifetime requirements are concrete.

## Identity, names and namespace bindings

Pool, logical-volume, object and principal IDs are distinct opaque 128-bit types.
Generate new IDs using strong randomness; all-zero is invalid. Object identity
is the tuple of pool, volume and object IDs. Names and physical locations are
separate. Rename, relocation and migration preserve identity; ordinary copies
and deletion/recreation create new identities. Knowing an ID grants no authority.
The existing duplicate-pool attachment and trusted-import rules still apply.

Volumes have mutable names unique within a pool. Names are valid UTF-8, compared
bytewise and case-sensitively without Unicode normalization or locale rules.
Volume and directory-entry names are at most 255 bytes, excluding an in-memory
terminator. Reject empty names, NUL, `/`, and the exact names `.` and `..`.

Schemes such as `home://` are namespace-layer bindings to granted directories,
not disk-format fields. Name-based volume selection needs an explicitly selected
pool; configuration can use stable IDs instead. An established mount retains
identity through a rename and cannot retarget itself when a name is recreated.
Sessions may bind different volumes or subdirectories to the same scheme, or
publish several differently restricted bindings from one volume.

Initially support regular files and directories. Directory entries map unique
names to object IDs in the same volume; directory enumeration uses bytewise name
order. Directories have one parent except the root, with no cycles or cross-volume
entries. No symlinks, hard links or special objects. Parent traversal is not a
stored `..` entry and must never escape a granted root; the first core interface
can leave it to a caller's bounded traversal context.

Files have explicit byte lengths and arbitrary contents. Extents describe logical
file ranges; holes read as zero and consume no file-data blocks. Never expose
unallocated storage or bytes beyond the file's logical end.

## Ownership and acquisition policy

Each object records a policy owner and explicit allow grants. A grant names an
individual principal, object-specific rights and object-only or directory-subtree
scope. Groups wait for a trusted membership source. Exact rights, bit assignments
and acquisition interfaces are defined in the specification; file content,
directory operations and policy administration must remain distinct.

The builder requires explicit owner principal IDs and gives each initial root
an explicit owner subtree grant. Populated descendants follow the agreed
parent-controlled ownership rule; they need no duplicated per-child subtree
grants. Ownership alone grants no bypass. The principal's meaning is provisioned
outside the image; an image cannot authenticate it or appoint an administrator.
Do not infer ownership from host usernames, UID/GID or mode bits.

Policy-based acquisition requires a trusted identity context and bounded
acquisition authority. Resulting rights must fit both the applicable persistent
grant and the broker's own authority ceiling. A restricted application cannot
obtain the rest of its user's home simply by presenting that user's ID. Runtime
operations use the rights actually acquired; Caelum owns handles and delegation.

Policy edits govern future acquisitions, not immediate revocation. Existing
subtree capabilities can still acquire descendants within their scope. Child
policy cannot create a private pocket inside a broadly granted subtree. Explicit
host inspection authority is separate from policy-based access: diagnostic tools
can inspect an authorized image, without presenting that as an owner exception.
The milestone implements read/list acquisition checks; login, OIDC, a user
directory, immediate revocation and persistent policy mutation remain later work.

## Physical encoding and committed roots

Use 4 KiB filesystem blocks over supported 512-byte or 4 KiB logical sectors.
Use explicit little-endian fixed-width encoding, never serialized C structures.
Block addresses are 64-bit and relative to the pool partition, with bounds checks
against that extent. Leave any trailing partial block unused. A 4 KiB superblock
occupies each end of the usable pool extent.

Metadata records have type, incompatible-format version, length and checksum
fields. Metadata blocks identify their pool and physical block location so a
valid block at the wrong location can be rejected. Exact checksum, coverage,
headers, reserved space and reference encodings are defined in the specification.
Reserved bytes start at zero. Structure versions, feature requirements and transaction
generations are distinct; apply the agreed format-evolution rules without
bumping a version for every compatible new field.

Two superblock slots describe complete committed pool states. Pool roots include
volume roots, allocation ownership and accounting. The agreed later writable
protocol serializes pool commits:

1. Allocate replacement blocks without overwriting storage protected by either
   retained committed state.
2. Write replacement data and metadata, then flush successfully.
3. Write the newer generation into the older superblock slot, then flush again.
4. Only after that flush succeeds report the checkpoint durable.

4 KiB is not a promise of atomic storage writes. A torn slot must fail validation.
A crash can commit an operation before its caller receives success. Failed
publication or flush leaves an uncertain outcome: stop mutation and require
recovery, without blind retries. Pool-wide publication must retain the earlier
volume-checkpoint coverage contract; its concrete ordering is specified before
writable implementation.

Opening validates superblocks and their referenced root metadata, then applies:

| Slots | Result |
| --- | --- |
| Two valid, different generations | Select the newer state; preserve the older state's storage protection. |
| Two valid, same generation | Require agreement on committed state, otherwise reject as ambiguous. |
| One valid, other absent/corrupt | Degraded read-only access; writable recovery is a later explicit operation. |
| Either unsupported, I/O error, limit or allocation failure | Refuse selection; do not treat an unexamined state as corrupt. |
| Neither valid | Refuse normal access. |

The specification defines validity versus unsupported/I/O-error classification
and root-validation depth; an unreadable candidate is not automatically
proof that an older state is safe. Corruption encountered after selection returns
an error, never a silent generation switch under open objects.

The formatter initializes both slots to the same initial generation. The reader
implements selection, not runtime publication. No converter or artificial second
format is required to demonstrate the design.

## Metadata organization

| Structure | Responsibility |
| --- | --- |
| Pool root | Volume catalog, allocation state and pool accounting |
| Volume record | ID, name, guarantee/quota, policy and volume root |
| Object index | Object ID to file/directory record |
| Directory index | Name to object ID |
| File extent index | Multiple logical file ranges to physical pool blocks; one extent lives inline in the object record |

Use 4 KiB B+ tree nodes for variable-sized indexes. Pack small records in leaves;
file contents occupy separate extents. Share concrete node encoding, bounds and
traversal mechanics without hiding each index's keys or invariants in a generic
database framework. The formatter builds trees from scratch. Incremental
insertion, deletion, balancing and COW updates belong to the writable milestone.

## Allocation and capacity accounting

A pool-wide extent map distinguishes free blocks, live volume storage, live pool
metadata and retired storage. Retired extents retain accounting ownership and a
retirement generation. The allocation map itself participates in COW publication
alongside the object roots; namespace and ownership cannot commit independently.

Reclamation is conservative: both durable states must no longer require a block,
and live readers/outstanding I/O must have released it, before it can be reused.
Do not depend on an in-progress slot replacement for reuse. Recording a retired
generation alone is not proof of safety; exact bookkeeping and recovery rules
need specification before writes. This milestone records and checks ownership;
it does not reclaim storage at runtime.

Guarantees, quotas and reserves are budgets, not contiguous physical partitions.
Permanent file data and attributable metadata count toward the containing volume;
unlinked objects retained by capabilities still count. Temporary COW duplication
and retained generations consume workspace while remaining physically occupied.
Admission counts occupied blocks and unconsumed promises, without counting a
consumed reserve portion a second time as an unused reservation.

The previously discussed 4–8 GiB values were examples, not fixed minima or defaults.
The formatter derives defaults from usable capacity and the initial volume set,
allows explicit overrides and reports the resulting capacity plan before
formatting. Persist the resulting volume guarantees/quotas, ordinary COW workspace,
migration and recovery budgets. No overcommit and no silently reducing existing
guarantees to admit a later volume.

Policy defaults scale with capacity; correctness minimums must cover bounded
metadata, transaction and recovery operations. Reject a pool when those minimums
cannot fit. Numerical defaults, formulas and supported-size bounds are specified;
establish transaction/recovery costs before promising writable reserve
sufficiency. A read-only image's recorded budgets alone do not prove that claim.

## Image population and inspection

Import one or more explicitly selected quiescent source directories into named
volumes. Reject symlinks and unsupported host entries rather than following or
silently skipping them. Import host hard links as independent files with new
identities. Apply selected Pyxis ownership/grants rather than host permissions.
Fail on detected source changes and read errors; do not claim an atomic host
snapshot. Exact command syntax, destination protection, image/container handling
and extraction-path safety are defined in the specification.

`pyxisfs-inspect` lists volumes/directories, reports IDs, ownership, grants and
accounting, and extracts files using the shared reader. Its explicit whole-image
consistency operation walks metadata references and reconciles them with allocation
ownership, detecting conflicting allocations. This is a retained filesystem
diagnostic, not a new automated test framework. Define its handling of both
committed roots, unsupported volumes, resource limits and incomplete checks;
never report a full clean check after skipping unexamined required state.

## Focused PR tasks

Each task is separately reviewable. Stop for any unresolved behavior, authority
or lifetime decision before implementation; the checklist is not permission to
guess. Publish pyxis-fs dependency commits before updating a Pyxis gitlink, link
dependent PRs and update this checklist with each completed task.

- [x] **1. Specify the initial format and host-tool contract.** Document exact
  headers, checksum coverage, feature/version fields, typed IDs, root validation
  and selection, index/node/extent layouts, allocation ownership and reserve
  arithmetic. Set bounds for names, trees, files, images and validation memory;
  specify overflow, corruption and unsupported-feature errors. Set read/list and
  administration rights and the trusted acquisition boundary. Set image versus
  GPT partition handling, CLI/source/destination rules, repository license,
  submodule location and host build integration. Agree a bounded future commit
  and reclamation design sufficient to avoid an incompatible initial layout;
  do not implement writable transactions. This is a specification PR first.
  Follow-up review and owner decisions are incorporated, including inline extent
  mapping, lower reserve defaults and lookup checks for ID acquisition. Later
  allocator and writable-implementation gates remain explicit. Licensing is MPL-2.0.
- [x] **2. Establish the shared core and encoding.** Initialize pyxis-fs with
  agreed build/ownership instructions, platform allocation/I/O interfaces and
  concrete ID, checksum and record codecs. Keep the core freestanding, error
  paths explicit and adapters narrow. Publish and pin the dependency under the
  agreed integration contract; no kernel mount or unrelated workflow changes.
  Implemented in [pyxis-fs PR #2](https://git.internal/PyxisOS/pyxis-fs/pulls/2).
  Native and Pyxis-cross builds produce an archive without unresolved symbols.
  Validation is compilation and source/symbol inspection; no runtime image or
  policy/traversal claim follows. Host adapters and commands begin in task 3.
- [x] **3. Format and reopen an empty pool.** Implement initial pool/volume/root
  construction, ownership/grant records, allocation accounting and capacity plan
  using shared codecs. Write both initial superblocks and read them through the
  shared selection path. Show named empty volumes and useful diagnostics. This
  is new-image construction, not existing-pool mutation. The accepted empty layout
  places pool metadata in a contiguous prefix, two root-object/grant blocks per
  volume, then free space: N+2 allocation records and at most seven map blocks.
  `mkpyxisfs` creates sparse standalone images; `--plan` creates no file.
  `pyxisfs-inspect info` reports recorded accounting, and `volumes` validates both
  catalogs and consulted metadata ownership before publishing copied envelopes.
  Neither command claims complete checking. See the dependency's
  [host usage](../../fs/docs/host-tools.md) and
  [construction bound](../../fs/docs/empty-layout.md). Implemented in
  [pyxis-fs PR #3](https://git.internal/PyxisOS/pyxis-fs/pulls/3).
  Native and Pyxis-cross builds passed, with no unresolved core symbols.
  Manual format/reopen covered 1 and 256 volumes, maximum-length names and
  explicit capacity policy; low-capacity/memory and invalid-input refusals were
  checked. Debugger inspection confirmed root ownership and subtree grants.
  Malformed/alternate-generation and actual I/O-failure paths remain source
  review only; no damaged fixtures or QEMU validation were introduced.
- [x] **4. Implement read-only traversal and acquisition.** Traverse B+ trees,
  resolve object IDs and names, enumerate directories and read inline/tree extents
  and holes. Add the agreed explicit GPT image-selection adapter alongside these
  readonly commands; task 3 deliberately handles standalone images only.
  Validate references and bounds; implement read/list policy evaluation with
  an explicit acquisition ceiling and scope. Keep diagnostic inspection separate
  from that policy path and expose no unrestricted-by-identity shortcut.
  Implemented shared object/path lookup, directory paging, inline/tree extent and
  hole reads, trusted ID/path acquisition and opaque views with held-rights child
  derivation. Pool/volume close refuses while retained handles remain. Host
  `list`, `stat` and `access` expose diagnostic inspection and explicit policy
  simulation; GPT selection requires paired partition-entry and sector-size
  options. Implemented in
  [pyxis-fs PR #4](https://git.internal/PyxisOS/pyxis-fs/pulls/4).
  See [core contracts](../../fs/docs/core.md) and
  [host usage and validation](../../fs/docs/host-tools.md).
  Native and Pyxis-cross builds passed with no unresolved core symbols. Manual
  empty-root inspection, owner/ceiling policy cases, busy-close and memory cleanup
  checks passed. Healthy 512-byte and 4096-byte GPT images reopened without byte
  changes, including a partition start not aligned to 4 KiB. Populated namespace,
  nested acquisition and file-content runtime validation wait for task 5's
  importer; sparse/multiple extents and malformed-media paths remain source
  review only. No fixtures, test harness, CI changes or kernel mount were added.
- [x] **5. Populate and extract images.** Extend the formatter with bounded
  bulk construction from selected host directories, including multi-level trees
  and multiple volumes. Complete listing and extraction through the shared reader;
  apply source, ownership, destination-safety and partial-build rules. No second
  format writer, incremental tree mutation or host-permission import.
  One shared bulk builder now handles empty and populated volumes. It retains a
  contiguous pool-metadata prefix and one range per volume, so the allocation
  map still has N+2 records including free space. Source metadata, sorting and
  tree layouts are planned within the memory cap; file data streams into one
  inline extent per nonempty file. Descriptor-relative traversal rejects
  symlinks/special entries, detects source changes and excludes output parents
  inside imported trees. Extraction creates fresh files/subtrees through the
  shared reader, leaving reported partial output on failure. Implemented in
  [pyxis-fs PR #5](https://git.internal/PyxisOS/pyxis-fs/pulls/5).
  Native and Pyxis-cross builds passed without unresolved core symbols. A 128 MiB
  three-volume image round-tripped repository docs and kernel trees; a 64 MiB
  image covered Unicode/space names, empty files/directories, hard-link copies,
  non-block-aligned lengths and a larger binary. Extracted contents matched.
  Nested acquisition and held-rights file reads passed; missing lookup authority
  and unrelated principals were denied. GPT extraction preserved image bytes.
  Low memory/quota, source symlink, source/output overlap and existing-destination
  refusals were checked. Whole-image consistency is not established; sparse and
  multi-extent reader paths, concurrent-change and I/O-failure paths remain
  source-review coverage. See [host-tool details](../../fs/docs/host-tools.md).
- [ ] **6. Add whole-image consistency inspection.** Reconcile reachable records,
  namespace structure, extents, metadata ownership, retained roots and budgets.
  Detect conflicting allocations and report incomplete/unsupported checks
  explicitly. This operation diagnoses; it does not repair or reclaim.
- [ ] **7. Validate and close the milestone.** Build normally, format disposable
  populated images, close/reopen them, inspect and compare extracted data using
  ordinary host tools. Exercise empty/nested directories, Unicode names, multiple
  volumes, multi-node indexes, non-block-aligned file lengths and relevant policy
  allow/deny cases. Record actual configurations and measured coverage. Only
  exercise sparse or alternate-generation images through agreed tooling; do not
  invent fault injection or mutation fixtures. Move implemented behavior into
  durable docs, retain unresolved limits, and separately scope writable work.

## Validation boundaries

This host-only slice needs ordinary host builds and manual tool use, not a
gratuitous QEMU boot. Use existing CI for changed inputs and report unavailable
coverage honestly. No new tests, self-tests, fault injection, CI or boot/output
automation is authorized here. Document malformed-media and failure paths that
have only code inspection. The diagnostic checker is an agreed product tool.
Compiler-container changes, if later necessary, need an explicit owner rebuild;
none is required by this planning PR.

# Initial filesystem format and read-only shared core

The host-only filesystem milestone is complete. The MPL-2.0 shared core builds
new populated pool images, reopens them, traverses directories, reads and extracts
files, evaluates bounded read/list acquisition and checks structural consistency.
There is no Caelum mount, FUSE adapter, existing-pool mutation, writable COW
transaction, recovery, repair, converter or installer.

The filesystem repository owns the authoritative [format contract](../fs/docs/format.md),
[core interfaces and lifetimes](../fs/docs/core.md), and
[host commands and measured validation](../fs/docs/host-tools.md#validation).
The [persistent-storage design](wip/persistent-storage.md) and
[users and authority checkpoint](wip/users-and-authority.md) retain agreed future
contracts and proposals; milestone completion does not start those tracks.

## Repository and platform boundary

[PyxisOS/pyxis-fs](https://git.internal/PyxisOS/pyxis-fs) owns the freestanding GNU
C23 core, Linux host tools and eventual FUSE adapter. Pyxis pins a published
revision at `fs/`. Initialize that submodule and run `make -j16 fs-tools` to build
`build/fs-tools/libpyxis-fs.a`, `mkpyxisfs` and `pyxisfs-inspect`. The
[repository integration](sdk-and-repositories.md#filesystem-repository) records
the build boundary. Normal kernel, SDK, ports and image targets acquire no
filesystem dependency; the existing compiler container is sufficient.

The core owns encoding, checksums, compatibility, construction, metadata
traversal, allocation proof, file reads, policy evaluation and diagnostic checking.
It uses synchronous exact block-I/O and bounded allocation hooks, without host
libc, kernel headers or FUSE. Linux adapters own descriptors, source validation,
strong randomness, advisory locks and output safety. Caelum retains capabilities,
scheduling, device drivers and namespace bindings. Future adapters must share
the core rather than maintain an independent format writer or policy model.

Pools borrow their unchanged reader and memory owner until close. Volumes retain
a selected generation; views and cursors retain their volume. Close refuses with
`PFS_BUSY` while child handles remain. Instances are serial, and handles cannot
be copied. The [core contracts](../fs/docs/core.md#platform-ownership) specify
buffer ownership, partial reads, error publication and memory charging.

## Identity, names and namespace bindings

Pool, volume, object and principal IDs are distinct opaque nonzero 128-bit types.
The formatter generates pool, volume and object IDs with strong randomness;
principal IDs are supplied externally. Object identity includes its
pool and volume IDs. Names and locations are separate from identity. Future
rename, relocation and migration preserve identity; ordinary copies and
recreation receive new identities. Knowing an ID grants no authority. Trusted
import mappings and duplicate-pool attachment rules remain in the
[persistent identity design](wip/persistent-storage.md#agreed-persistent-identity-and-imported-ownership).

Names are valid UTF-8, compared bytewise and case-sensitively without normalization
or locale rules, with a 255-byte limit. Empty names, NUL, `/`, `.` and `..` are
invalid. Volume names are unique within a pool. Directory entries map unique
names to same-volume objects; directories have one parent except the root and
cannot cycle. Only regular files and directories are supported, with no symlinks,
hard links or special objects. Importing host hard links creates independent files.

Schemes such as `home://` belong to namespace bindings, not disk fields. A future
mount must retain identity across renames and cannot retarget itself when a name
is recreated. Bounded core paths are relative to a selected root and provide no
parent traversal. The namespace layer must preserve that boundary.

## Ownership and acquisition policy

Objects record policy owners and explicit individual-principal allow grants,
with object-only or directory-subtree scope. The builder requires an explicit
owner per volume, supplies an owner subtree grant at its root and applies that
owner to imported descendants. Host usernames, UID/GID, permissions and timestamps
are not imported. Ownership itself supplies no bypass or authentication.

The embedding authority supplies a trusted principal, bounded root, scope and
rights ceiling. Acquisition requires applicable grants, every requested right
and lookup authority on each intervening directory, including acquisition by
object ID. Successful views carry exactly the acquired rights and generation.
Held subtree authority can derive descendant views without reacquiring principal
policy, but cannot enlarge its scope or rights. Policy-approved mutation requests
return read-only without creating a view. See
[acquisition interfaces](../fs/docs/core.md#policy-acquisition-and-ordinary-views)
and [rights](../fs/docs/format.md#rights-and-trusted-acquisition).

Persistent policy governs future acquisitions. Existing subtree authority is not
filtered by later child policy; prospective policy edits cannot promise immediate
revocation. Policy mutation, authentication, local bootstrap admission, broker
integration, group membership and revocation remain future work. Diagnostic image
inspection is separate explicit authority; the host `access` command simulates
trusted inputs and does not authenticate a principal.

## Physical encoding and committed roots

The format uses partition-relative 4 KiB blocks over explicitly selected 512-byte
or 4 KiB sectors, little-endian fixed-width encoding and checksummed metadata
bound to pool identity and physical location. Trailing partial blocks are unused.
Two superblock slots occupy the usable extent's ends; formatting writes identical
generation-1 committed states to both. Exact layouts, feature compatibility,
reference validation and supported bounds live in the
[format specification](../fs/docs/format.md).

Opening validates both candidates and their root metadata. It selects the newer
valid generation, requires equal-generation states to agree, and permits degraded
read-only access beside an absent or corrupt peer. Unsupported meaning, resource
limits and operational failure prevent selection rather than masquerading as
corruption. Later corruption returns an error without switching generations
under existing handles. Candidate selection is narrower than whole-image checking.

The [future commit rules](../fs/docs/format.md#future-publication-and-reclamation-envelope)
requires serialized pool commits, replacement writes and flush before older-slot
publication and a second flush before durability acknowledgement. Both durable
states and runtime reader/I/O references protect storage. Uncertain publication
stops mutation and requires recovery. A retired-generation field alone cannot
prove reuse safe, and 4 KiB slots do not imply atomic storage writes. These are
agreed design constraints with explicit implementation gates, not implemented
recovery behavior.

## Allocation and capacity accounting

Pool catalogs and allocation maps and volume object/directory/extent/grant indexes
use bounded 4 KiB B+ trees. Small records are packed; file data lives in separate
extents, with one inline extent for each nonempty imported file. Reader APIs also
support extent trees and zero-filled holes, but those layouts have no runtime
validation claim. The bulk builder plans every metadata/data block before creating
output; [construction bounds](../fs/docs/empty-layout.md) account for the allocation
map itself. Incremental tree updates and reclamation are not implemented.

The allocation map distinguishes free, live volume, live pool and retired storage.
Physical ownership, permanent allocation, workspace charges, volume guarantees
and quotas are recorded and checked. Budgets are capacity promises rather than
fixed partitions, with no overcommit. Numerical defaults and accounting equations
are authoritative in [accounting and defaults](../fs/docs/format.md#accounting-and-defaults).
Recorded COW, migration and recovery reserves are prototype policy; no writable
operation-cost bound establishes their sufficiency.

## Image population and inspection

`mkpyxisfs` constructs a new standalone sparse regular image from explicitly
selected quiescent source directories or empty volumes. It never overwrites an
existing destination. It rejects symlinks, special entries and output/source
containment, detects source changes and streams file bytes through the shared
builder. This is not an atomic host snapshot or crash-safe formatting; failure
leaves reported incomplete output for explicit removal. Host holes become
allocated zero data, and sparse image sizing does not reserve host disk space.

`pyxisfs-inspect` provides `info`, `volumes`, `list`, `stat`, `access`, `extract`
and `check`. Inspection opens images read-only under advisory locks. Explicit
paired GPT entry/sector-size selectors provide a bounded partition extent; they
do not search for a pool or repair GPT. Extraction creates fresh files/subtrees
through the shared reader, refuses existing output and leaves reported partial
output on failure. Writers ignoring advisory locks are outside the consistency
contract. Exact syntax, exit statuses and destination safety are in the
[host-tool guide](../fs/docs/host-tools.md).

`check` walks both committed states, reconciles catalogs, namespaces, grants,
extents, allocation ownership, counts and reserve charges, and compares retained
storage incarnations. A clean result requires two fully checked valid states and
a complete cross-state comparison. Unsupported contents, skipped work and resource
exhaustion remain explicitly incomplete; independently proved contradictions
report corruption. It does not read or integrity-check file payloads, repair,
reclaim or prove runtime readers have released storage. Metadata checksums do
not establish file-content integrity; extraction and host comparison are separate.

## Validation and limits

[Measured host validation](../fs/docs/host-tools.md#validation) records native and
Pyxis-cross builds, disposable image construction, reopening, extraction/content
comparison, policy decisions, structural checks, explicit GPT configurations and
debugger memory/lifetime inspection. This host-only milestone requires no QEMU
boot. No kernel-mount, power-loss, production-data safety or performance claim
follows from those checks.

All runtime-checked committed pairs have identical generation-1 roots. Sparse and
multiple-extent images, differing generations, retired allocations, malformed or
unknown metadata, degraded GPT, concurrent source changes and actual I/O/flush
failures retain source-review coverage. File-data checksums are deferred. Memory
caps bound payload allocation, not I/O work or every maximum-profile input.
[Technical debt](technical-debt.md#filesystem-host-prototype-limits) records the
consequences and revisit points before writable admission, recovery or larger
imports rely on these limits.

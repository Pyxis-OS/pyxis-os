# Native read-only filesystem mounts

Status: tasks 1–6 complete, 2026-09-30. The contract, core continuation and
[bounded kernel adapter](../devices/filesystem-native-adapter.md) are implemented.
Trusted init can mount and delegate native roots; directory/file protocols and
executable capture forward to the worker. Scoped filesystem information exposes
retained identity and shared-pool capacity; Fastfetch integration is task 7. The
initial budgets cover the measured inputs, not every valid image. Update each
task in its delivery PR.

## Completion point

Create a populated Pyxis pool image with the existing host tools, attach it as a
GPT partition through virtio-blk, and have trusted init mount one volume under a
chosen namespace binding. Ordinary `ls`, `cat` and executable loading must work
through the returned directory/file capabilities. Fastfetch then reports the
accessible mount and accurately labeled capacity information.

This uses the existing bootloader, kernel and initrd. A disk is optional; an
unconfigured boot keeps the existing archive/RAM/HOST sessions. Native writes,
recovery and installation remain separate milestones.

## Existing foundation

- [Block storage](../devices/block-storage.md) supplies bounded ticketed I/O and
  waits; [GPT discovery](../devices/gpt.md) supplies a boot-time partition map.
- The [shared filesystem core](../devices/filesystem-readonly.md) opens committed
  states, acquires policy-bounded views, traverses directories and reads files.
  Its [interfaces](../../fs/docs/core.md) and [format](../../fs/docs/format.md)
  remain authoritative. Reuse this core, not a second kernel format parser.
- Existing [filesystem objects](../interfaces/filesystem-mutations.md),
  [init/session handoff](../userland/init.md) and namespace bindings provide the
  application-facing model. Separate HOST and native mount authorities select
  the HOST export or a configured disk partition and native volume.
- [BSP requests](../kernel/bsp-service-requests.md) separate request ownership from
  scheduling. The filesystem adapter must also obey [memory](../kernel/memory.md)
  and [SMP](../kernel/smp.md) rules.

## Agreed selection and authority

Partition, volume and namespace selection belong in init:

```sh
mount --partition 1 --volume system --read-only data://
```

The mount capability identifies the authorized disk. The partition number selects
an entry on that disk, not an arbitrary global device. Resolve the volume name
once when opening and retain pool/volume identities. A name being reused must not
retarget an existing mount. Namespace binding names such as `data://` are not
stored in the filesystem or derived automatically from volume names.

The kernel mount path binds a configured bootstrap principal to trusted
init's mount authority. Applications cannot nominate a different principal in a request;
there is no `--principal` argument or authority derived from a volume's owner.
The core must evaluate persistent grants within the trusted root, scope and
rights ceiling before returning a root view. Knowing principal/object IDs is
neither authentication nor access. The supplied image and trusted boot setup
must agree on the intended principal; mismatches fail acquisition.

Init chooses what to mount within its authority, then delegates the resulting
root through session handoff. Children need ordinary directory/file grants to
browse and read, not mount or raw-block authority. Closing mount authority must
not revoke independently retained roots. This introduces neither a login system
nor a user-directory/PCA implementation. The longer-term
[identity direction](users-and-authority.md) still applies.

## Read-only integration boundaries

- Bound all reads to the selected GPT partition and translate the core's 4 KiB
  filesystem blocks through the device's logical sector geometry. Use existing
  transfer limits and exact-read/error contracts; do not expose raw I/O to apps.
- Keep the underlying image unchanged while open. Read-only guest access alone
  does not prevent host writes. Development instructions must prohibit concurrent
  host mutation and use read-only QEMU attachment. No live host refresh, hotplug
  or writable co-mount is supported.
- Preserve the core's selected-generation views and degraded-read selection
  rules. A successful open validates bounded candidates/root metadata, not the
  whole image. Ordinary operations validate traversed structures; a full check
  remains a distinct host operation. Never turn a media, unsupported-format or
  resource-limit error into an empty directory or successful short backing read.
- The core is serial and its handles cannot be copied. Kernel object references
  must retain the backing pool, volume and view for outstanding operations and
  delegated copies. Cancellation/exit must release queued work and retained
  references only when ownership permits; cleanup must not race an active read.
- Synchronous core callbacks may need to wait for block completion. Run that work
  in a suitable BSP task context; do not block an IRQ handler, hold a lock across
  a wait, or park the existing interrupt-disabled service executor inside the
  core. Keep core allocation on the BSP with bounded memory accounting.
- Map native directory lookup/enumeration, file length and offset reads onto held
  core rights. Persistent acquisition and subsequent capability delegation must
  not accidentally widen each other's rights. In particular, file read authority
  does not silently grant the core's distinct metadata rights.
- Mutation attempts must fail without disk writes, whether denied by the grant
  or rejected as read-only by the backend. No write/flush path, COW allocator,
  timestamp updates, repair or format migration is introduced.
- Existing archive, RAM, HOST and user-provider behavior stays intact. Reuse the
  existing file/directory protocols where their contracts fit; no generic VFS
  redesign or new backend framework for this milestone.

## Filesystem information and Fastfetch

Expose a bounded observation associated with an authorized mounted filesystem,
not an ambient list of every disk or another space's mounts. Namespace labels
come from the caller's bindings; pool and volume names/IDs remain separate.

Volumes share pool storage, grow under guarantees and limits, and have no simple
fixed partition-sized capacity. Distinguish pool usable capacity, per-volume
charged bytes, guarantees and any configured upper limit. Do not sum shared pool
capacity once per volume or present pool-wide free space as a volume's writable
allowance. Read-only access does not imply zero free physical space.

Before exposing usage, specify which counters are verified, merely recorded in
the selected generation, or unavailable. Bounded root validation does not
establish global allocation totals. Do not run a whole-image checker on every
Fastfetch invocation or manufacture a Linux-shaped used/total percentage. The
first useful display can report the binding, filesystem type, read-only status
and pool capacity; add usage only where its evidence and meaning are explicit.

Use the existing [Fastfetch port](../userland/fastfetch.md) as the final consumer:
add the native Disk adapter and only the shared-source changes its real platform
boundary needs. Preserve upstream formatting, allocation and diagnostics. Do not
implement arbitrary other disk backends or optional upstream discovery features.
Local-IP display is a separate future follow-up using existing networking
facilities; it does not expand this storage milestone.

## Task-1 integration contract

The task-1 source review used Pyxis `79f9889`, pinned pyxis-fs `0c51185` and pinned
userland `c9ed311`. The integration contract below is agreed, including the READ
mapping, shared principal, duplicate-pool exclusion, continuation mapping and
initial runtime bounds. This design task does not start later implementation tasks.

### Trusted configuration and mount request

Make inputs `MOUNT_DISK` and `MOUNT_PRINCIPAL` generate Limine options
`mount.disk=<GPT-GUID>` and `mount.principal=<32-hex-digits>`. The GUID uses
canonical hyphenated text with explicit conversion to GPT byte order; the
principal uses the core's nonzero 128-bit ID parser. Both absent disables native
mount authority. Partial, malformed or duplicate configuration fails boot.
Values are deployment inputs, never compiled per-CPU role rules.

Every trusted workload init receives the same configured principal and ceiling.
Each can choose different root grants for its session. Ordinary applications
receive neither mount authority nor a principal-based acquisition service;
knowing the shared principal cannot recover rights withheld by a launcher.
A disk GUID selects the intended disk; it authenticates neither disk nor content.
Do not choose a first match among duplicates. The current block profile already
rejects multiple candidate devices; this task does not add multi-device discovery.

Issue a `native_mount` resource only when configured and a candidate block device
exists. An actually absent device leaves it absent. A present but unusable or
ambiguous device must retain a failing authority/setup result, rather than look
absent to `--optional`. Mount processing waits for the immutable GPT result and
checks the configured disk GUID before partition selection. A wrong GUID, invalid
GPT or unsupported device is an explicit failure, never fallback to another disk.
Implementing this distinction requires preserving the block preparation reason;
`block_get_info()` failure alone is not evidence that hardware is absent.

Keep HOST's `MOUNT_OPEN_ROOT` and access selector. Add a distinct
`MOUNT_OPEN_VOLUME` operation on the existing mount object/protocol. Its bounded
request carries a one-based GPT entry number, a counted volume name (1–255 bytes,
validated by the core) and the exact requested root-rights mask. It contains no
principal, device selector, namespace label or raw-block address. Capture the
name before publication; the worker borrows no userspace string. Success returns
one independently owned directory handle; failure installs none. Calling the
HOST operation on native authority or vice versa returns `CALL_BAD_OPERATION`.

`MOUNT_RIGHT_OPEN_ROOT` authorizes acquisition. Native authorities also carry a
separately attenuable `MOUNT_RIGHT_OBSERVE`, needed only when the requested root
includes filesystem-information authority. Neither right permits raw block I/O.
Unknown request bits are invalid; known mutation bits are rejected as read-only.
Root acquisition must include LOOKUP; zero-right or observation-only requests
are invalid. Narrower grants can be delegated from an acquired root.
The native command requires `--read-only`; a read-write attempt never opens a
writable view. The default root grant requests LOOKUP, ENUMERATE, READ_FILES and
filesystem observation when held; `--no-info` explicitly omits observation.
The library operation supports exact narrower root masks without inventing a
second general-purpose command-line rights language.

Select the partition by its GPT entry number, not its packed-array index. Check
the partition extent and volume name; do not probe other partitions on failure.
The format assigns no Pyxis GPT type GUID: as with the existing host inspector,
explicit selection accepts a used entry independently of its type and requires
a supported Pyxis superblock. Resolve the name through the core's bounded volume
catalog, open its identity and acquire its root with `pfs_view_acquire`.
The trusted context uses that volume root, SUBTREE scope, the configured principal
and the read-only ceiling below. Require the entire requested set; no partial
success or owner-based bypass.

### Exact rights mapping

| OS grant | Core rights held by the corresponding view |
| --- | --- |
| Directory LOOKUP | `PFS_DIR_LOOKUP` |
| Directory ENUMERATE | `PFS_DIR_LIST` |
| Directory READ_FILES | `PFS_FILE_READ | PFS_FILE_METADATA` |
| File READ | `PFS_FILE_READ | PFS_FILE_METADATA` |
| Directory filesystem observation | Separate kernel observation grant; no extra core object/admin rights |

The bootstrap core ceiling contains only the first three mapped sets; it has no
mutation, directory-metadata or administrative rights. Root and child directory
views use SUBTREE scope. Child file views use OBJECT scope and only the requested
file masks. Every descendant lookup checks the calling OS grant, maps both halves
of the READ bundle and invokes `pfs_view_lookup` within the held view. It never
reacquires policy under the bootstrap principal. A persistent file-read grant
without metadata fails acquisition explicitly with `CALL_DENIED`.

`FILE_READ` uses the held read interface; `FILE_SIZE` uses metadata and returns
only the byte length. This preserves the OS's existing READ-plus-size contract
without claiming that the core's two rights are interchangeable. Enumeration
returns names/kinds only. Directory metadata and policy inspection are not
required for browsing. Capability copies may share one retained core view, but
each operation and child-rights request must be checked against the particular
caller's attenuated OS rights. Sharing the underlying view never restores bits
removed by delegation.

Add a `DIRECTORY_RIGHT_FILESYSTEM_INFO` bit and a directory information operation,
not another object type. It can be copied, withheld and explicitly requested on
child directories only from a parent holding it. It grants no lookup, listing,
file access or policy inspection. Bootstrap OBSERVE is the authority for this
additional disclosure of shared-pool information; persistent file grants do not
implicitly supply it. Acquisition of an ordinary root still goes through policy.
A native root with only observation may be obtained by attenuating an already
acquired grant. Other backends retain their existing rights and behavior; they do
not gain an invented native-pool query result.

### Core continuation prerequisite and abandonment

The pinned core now provides
[`pfs_view_directory_page`](../../fs/docs/core.md#stateless-directory-continuation)
for validated continuation before native enumeration. Do not reconstruct a
listing by skipping every earlier entry on each OS call. The API supplies a
policy-view page operation with caller-owned continuation input/output and
operation-local scratch; it must
not require an open cursor or a kernel token registry between calls. Keep format
parsing and continuation validation in the core. No on-disk format change or
persistent enumeration index is required.

The OS mapping keeps the existing two-word `directory_cursor`. Each
native directory wrapper has a nonzero, boot-unique enumeration identity, shared
by copies of that object and never reused after destruction. `generation` holds
that identity, not the pool generation. `position` carries the core's opaque
64-bit continuation value, not an entry ordinal or kernel pointer. Zero means
start; a nonzero value identifies a resume point after a previously emitted
entry in the selected immutable directory. The core owns its encoding: one-based
slot choices along the root-to-leaf path, with the leaf slot in the low byte.
Eight levels of at most 195 slots fit in 64 bits. Validation follows only rooted
references, never a caller-supplied block address. The implemented representation
fits the existing two-word OS mapping without changing the disk format.

The zero pair starts a listing. Zero identity with nonzero continuation is
invalid; a different nonzero identity returns `DIRECTORY_CHANGED`. Identity
counter exhaustion returns `CALL_LIMIT`, without wrapping. A fresh lookup of the
same directory can have a different identity; callers restart rather than
transplant OS cursors. Copies of the same directory grant may independently
replay or fork a continuation without moving a shared position.

The core treats every continuation as untrusted. Check syntax/ranges, bind
interpretation to the held view's pool, volume, directory and selected generation,
and prove the resume entry is reachable from that directory's tree before using
it. This validates a position in the current view, not the token's origin: the
same numeric value may also identify a valid position in another authorized
view. Neither the token nor the OS wrapper identity authenticates a caller.
A checksum or matching ownership header alone does not prove reachability.
Validate the traversed tree/reference contexts, allocation ownership and returned
child kind/parent relations using ordinary read proofs. Caller tokens confer no
LIST, lookup, metadata or raw-I/O authority. A forged but otherwise valid resume
point may skip entries inside the authorized directory; it cannot broaden access.
Invalid/unreachable continuations fail as invalid requests, not successful END;
backing I/O, unsupported format, proof corruption and resource failures retain
their distinct operation errors. Untrusted token bytes alone do not establish
that the mounted directory is corrupt.

Seek from the validated resume key through the tree and return its successor;
do not walk the already-returned prefix to reconstruct an ordinal. Tree-path,
ancestry and allocation-proof validation costs still apply: this prerequisite
removes the added prefix-rescan cost, not every existing core scaling limit.
Do not restore caller-supplied emitted counts as trusted cursor state. A stateless
page validates consulted structures and returned entries, without claiming a
full-list count reconciliation or uniqueness check across skipped pages. Keep
child-identity duplicate detection within each returned page. Preserve the
existing diagnostic cursor's end-to-end count checks and the separate whole-image
checker; the diagnostic cursor does not establish cross-page uniqueness. The
[core interface](../../fs/docs/core.md#stateless-directory-continuation) records
this distinction.

The OS requests one candidate entry with the held LIST right. ENTRY copies a
whole name/kind and the returned continuation only after success.
BUFFER_TOO_SMALL reports the NUL-inclusive size without advancing the caller's
cursor or publishing a partial name. END leaves a repeatable terminal position;
replaying the final continuation must return END without scanning from the start.
Errors publish neither name nor next continuation. Release all core scratch and
any temporary handles before completing every success, short-buffer, end or
error path.

Abandoning an OS continuation retains no allocation, cursor registry entry or
extra volume reference. Closing the directory releases its view after in-flight
uses finish. Independent enumerations and capability copies have no shared
position. Stale continuations cannot keep a mount alive or reopen it. The core
prerequisite is published in [pyxis-fs PR #9](https://git.internal/PyxisOS/pyxis-fs/pulls/9)
at `017996b`; both dependency and parent pin update are merged. No kernel
enumeration implementation is introduced by this prerequisite.

### Worker, limits and final release

One dedicated BSP kernel worker serializes all native core calls, including final
closes. It runs with interrupts enabled; short allocation/free, queue and block
API sections obey the existing IF=0 rules. No lock spans a wait. The BSP service
executor forwards typed requests and returns to dispatch; it never sleeps inside
the core. Extend the explicit service catalog/completion states, following HOST
forwarding, rather than allowing kernel workers to make nested executor calls.

Use the existing provisioned per-user-task request area. Agreed initial bounds:

| Resource | Initial bound and failure |
| --- | --- |
| Admitted native user operations, active plus queued | 32; `CALL_BUSY` on saturation |
| Shared live core allocation payload across all pools/views | 8 MiB; `CALL_LIMIT` |
| Native wrapper/adapter payload | 1 MiB and 1,024 live wrappers; `CALL_LIMIT` |
| Backing reads in flight from this worker | One block ticket |
| Core callback transfer | At most 16 filesystem blocks, 64 KiB |
| Operation deadline | One absolute 30-second deadline from publication |

These are agreed starting bounds, subject to explicit revision with integration
measurements. Actual allocation failure below a cap is `CALL_NO_MEMORY`. Core
accounting excludes adapter objects, task/request storage,
stack, heap overhead and allocator rounding. Charge native persistent objects
and temporary adapter payload separately; check total kernel cost during the
adapter task.
Do not substitute the core's 128 MiB default or claim the 8 MiB profile can handle
every format-valid image. Inspection of `core/pool.c` shows that ordinary
operations allocate a catalog workspace containing 256 volume/name records,
256-object ancestry and 64 grants even for one selected volume. Approximate
x86-64 layout estimates are 215 KiB for this workspace and 5.5 KiB per consulted
metadata node, retained through proof closure. An otherwise empty 8 MiB budget
therefore holds only about 1,400 such nodes, fewer after other state/hash-table
costs. These are source-layout estimates, not measured peaks. Queuing 32 requests
does not allocate 32 traversal workspaces because execution is serial. Retired
wrappers remain charged against both payload and count limits until the worker
actually frees them; a cleanup backlog cannot bypass the cap. Admission storage
is already provisioned; final cleanup uses embedded retirement links and must
not require a free user-request slot.

Share an open pool by authorized device and partition identity/extent, then
retain its selected pool ID/generation. Repeated mounts of that same backing
instance reuse it. Before admitting a newly opened backing instance or publishing
any root from it, the owning worker must compare its selected pool ID with every
live or reserved pool identity. The same ID on a different partition/extent is
`CALL_ALREADY_EXISTS`, with a duplicate-pool diagnostic, even on the same disk
and even if the copies have different generations or volume names. Do not merge
their state or pick a preferred clone. This enforces the existing
[persistent identity agreement](persistent-storage.md#agreed-persistent-identity-and-imported-ownership);
rejecting multiple block devices is not sufficient.

Identity checking and reservation are serialized by the owning worker. Reserve
the identity before root publication, keep it reserved across policy acquisition,
outstanding operations and deferred retirement, and release it only after that
backing pool is actually closed. An unsuccessful candidate releases only its own
reservation/state; it cannot remove the live instance's identity. Once all uses
of the first instance have drained and it is closed, a later mount can select
another extent with that ID. This excludes concurrent duplicates without a disk
scan for unmounted copies or a permanent boot-wide claim.

Share volumes by their retained IDs within the accepted backing instance;
resolve a requested volume name against that same generation, not a new open.
Every mount request performs its own policy acquisition and receives its own
view. Multiple pools/volumes share the global budgets rather than multiplying
the allowance per mount. No live refresh or idle cache is introduced. A failed
acquisition releases only the state it acquired and cannot close another root's
backing. Publishing one root must be atomic with respect to allocation/handle
installation failures.

Each queued/active operation retains its objects and backing. Published BSP
requests keep the current uninterruptible loan contract: group stop is observed
after completion, result cleanup and loan return at the syscall boundary. Do not
free queued work on caller stop or promise immediate cancellation of an active
core call. Expired queued work completes as timed out when the worker reaches it;
it never starts new disk reads. The same deadline covers pending GPT discovery,
block admission and every backing read. Check it between core calls and inside
read callbacks; do not reset it for each transfer. It is a cooperative deadline,
not a hard CPU-time bound, because the core has no preemption/cancellation hook.

Object destruction queues work to this owning worker, including when the ordinary
BSP reaper runs while it is sleeping inside a read. Preserve deferred execution-
group cleanup attribution. Release temporary cursors and views before volumes,
volumes before pools, and pools before reader/memory owners. A BUSY core close
retains its backing and must not be followed by freeing it. Final release removes
the shared instance; closing only mount authority does not close live roots.
Worker infrastructure may remain idle for the boot; it retains no unused pool.

### Partition adapter and operation-local errors

Supply core geometry from the selected extent: `B = floor(extent_bytes / 4096)`.
Ignore trailing partial filesystem blocks. Check all arithmetic and ranges before
translating partition-relative blocks to the device's 512-byte or 4 KiB logical
sectors. Split callbacks at actual device transfer limits, even if that limit is
smaller than one filesystem block. Do not require a disk-wide 4 KiB-aligned start
on 512-byte media. No callback may access outside the selected partition.

BLOCK_FULL is an admission wait within the same deadline, with bounded short
sleeps. After a timed-out `block_wait`, the worker abandons its ticket only after
that wait returns; the block driver retains unresolved DMA ownership. Require
successful completion of every requested byte. The reader exposes no write or
flush callback. Require read-only QEMU attachment and no concurrent host mutation.

The core collapses callback failures to `PFS_IO`. Each top-level native operation
therefore owns a fresh context containing its absolute deadline and an initially
empty backing-error field. During that operation, callbacks record the first
precise backing failure. After the core unwinds, use that cause only when mapping
that operation's `PFS_IO`; an empty field maps to ordinary `CALL_IO`. Do not replace
unrelated corruption/limit/denial results with a side error. Clear/unbind the
context before completing the request, including failed mount preparation and
cleanup. Reader adapters may borrow this context only during the serial call;
no last-error field survives in a pool, volume or worker for the next operation.

| Failure source | Public result |
| --- | --- |
| Malformed request/selector syntax | `CALL_BAD_REQUEST` |
| Wrong object kind | `CALL_WRONG_TYPE` |
| Missing partition, volume or child | `CALL_NOT_FOUND` |
| Same pool ID on a different concurrently retained backing extent | `CALL_ALREADY_EXISTS` |
| Insufficient capability or persistent policy | `CALL_DENIED` |
| Valid mutation request against the native backend | `CALL_READ_ONLY`, after ordinary authority checks |
| Core profile/memory cap exhausted | `CALL_LIMIT` |
| Allocator failure below cap | `CALL_NO_MEMORY` |
| Admission/shared-state busy | `CALL_BUSY` |
| Backing wait or operation deadline expired | `CALL_TIMED_OUT` |
| Unsupported device/GPT/core format or unavailable transport | `CALL_UNAVAILABLE` |
| Wrong configured disk GUID | `CALL_NOT_FOUND` with disk-selector diagnostic |
| Absent/invalid/ambiguous GPT or filesystem on a present selected disk | `CALL_IO` |
| Corrupt metadata, media failure or successful short backing completion | `CALL_IO` |

Retain precise GPT/core/device classifications in bounded kernel diagnostics;
`CALL_UNAVAILABLE` alone does not distinguish unsupported format from transport
loss. Optional mounting never suppresses a returned operation error. `PFS_INVALID`
is BAD_REQUEST only for validated public argument errors; an adapter invariant
failure must not be mislabeled as user input. Accept healthy or degraded GPT/core
selection only under their existing rules. Report GPT degradation and filesystem
degradation separately; unsupported, resource or operational failure is never a
reason to select an otherwise unexamined fallback generation.

Stage file data in kernel-owned output. The core can report a proved prefix on
failure, but native errors carry no count/reply: publish data only on success.
Only successful EOF clamping can produce an EOF short read; do not turn a failed
backing read into successful partial data. Executable capture uses these same
rights and failures, retains the file during capture, and follows the existing
16 MiB external-image staging limit and launch cleanup contract. Capture through
the native worker and release its request reservation before submitting launch
preparation to the BSP executor; do not nest a native read inside that executor.
Executable staging is separately owned launch memory, outside the 1 MiB native
wrapper cap; its existing launch/batch bounds and failure cleanup still apply.
There is no new aggregate staging budget across callers, so 8 MiB plus 1 MiB is
not a total native-workload memory bound. Below those per-capture limits, staging
can still fail with NO_MEMORY; adapter/combined validation must account for it.

### Bindings, handoff and first information fields

The mount command validates and reserves the destination before acquisition,
then publishes it only after the complete mount succeeds. A duplicate root or
conflicting service name fails; it never replaces an existing binding. Store
`data` as the binding name from `data://`, using existing startup/path name rules.
The launcher profile supports at most 16 selected roots, including
app/home/HOST, within the existing 64 KiB startup/launch capture bound. Overflow
fails explicitly and closes a newly acquired unpublished root; it never drops
bindings. These are userspace profile limits, not on-disk name/count limits.

`--optional` permits only a missing `native_mount` resource (disabled bootstrap
configuration or actually absent hardware). A required mount fails in that case.
Every failure from a present authority stops the script, including wrong GUID,
partition, volume, principal, policy or validation. Default packaged scripts need
no disk. HOST command spelling and existing optional behavior stay intact.

Replace hard-coded root-name arrays with an explicit selected directory-binding
list. Existing root bindings plus successfully mounted roots form the ordinary
shell/session selection; query and preserve their actual rights and transport
masks, applying existing explicit read-only attenuation where requested. Forward
that selected list through trusted session/services/remote-server handoff and
ordinary child launch. Each launch builds explicit grant entries; it does not
scan the capability table or inherit every named resource. Mount authorities
remain excluded. Restricted launchers may select fewer roots or fewer rights;
working-directory grants must not recover authority omitted from that selection.
The service namespace remains a separate capability and its existing collision
rules still apply.

The directory information query requires FILESYSTEM_INFO and returns a bounded
copied record for its retained mount, without directory traversal or a full
checker run. First fields are native filesystem type, read-only flag, separate
GPT/filesystem degraded flags, pool ID, volume ID/name, selected generation and
pool allocatable bytes `(B - 2) * 4096`. The two superblock slots are excluded;
capacity includes space needed by shared metadata and reserves, so it is not
writable volume allowance. Geometry and selected root envelopes establish this
capacity; they do not establish global usage. Expose no binding name in the
kernel record: consumers take it from their own selected root bindings.

Used/free bytes, volume charged bytes, guarantee/quota and usage percentage are
unavailable in this first query, with explicit availability semantics rather than
zero values. Guarantee/quota and live/retired/free counters exist on disk but are
merely recorded and locally checked during ordinary opening, not globally
reconciled. Add them only with their evidence/units made explicit. Fastfetch can
show type, binding, read-only status and labeled shared-pool capacity; identical
pool IDs identify shared capacity and must not be summed per binding/volume.

### Validation and next implementation task

Task 1 established the contract by source inspection. Task 2 built the core and
host tools and validated continuation through interactive GDB, including pages
of 300/300/4 entries across directory leaves, replay/end, nested views, invalid
slot paths, denied LIST access, budget refusal and cleanup to zero charged bytes.
Both committed states of the populated image passed host checking; recursive
extraction matched the source headers. Exact coverage and unexercised cases are
recorded in [host validation](../../fs/docs/host-tools.md#directory-continuation-validation).
Task 3 linked that pin with kernel flags and implemented partition-bounded reads,
shared budgets, asynchronous internal jobs and serial BSP opening/final release.
[Adapter validation](../devices/filesystem-native-adapter.md#validation) records
512-byte and 4 KiB guest opening, shared instances, cloned-partition rejection,
reservation through final cleanup, later clone acceptance, queue saturation and
zero final live allocation. Guest opening peaked at 255,336 core payload bytes
and 105,808 adapter bytes. Host policy acquisition on the same image peaked at
315,816 core bytes; this does not claim guest policy/lookup validation.

Task 4 adds policy-approved root and child views, directory/file wrappers and
ordinary BSP request forwarding. READ acquisition and lookup require both core
read and metadata rights. Every operation checks the actual caller's attenuated
grant; descendants use held views rather than reacquiring bootstrap authority.
Enumeration uses stateless continuation and a wrapper identity, retaining no
per-cursor state. Final object retirement closes views on the worker before
releasing their backing references.

[Object validation](../devices/filesystem-native-adapter.md#task-4-validation)
records interactive guest lookup/read/size, continuation and denial checks,
ordinary `cat` and `ls` running on an AP, and zero live accounting after cleanup.
The combined run peaked at 312,192 core payload bytes and 106,144 adapter bytes.
The typed 4,584-byte native request fits the existing 4,928-byte task allocation.

Task 5 adds the disk-scoped mount ABI, trusted bootstrap configuration, explicit
selected-root delegation and bounded native executable capture. Present unusable
block hardware retains a failing mount authority; optional acquisition suppresses
only missing authority. [Task-5 validation](../devices/filesystem-native-adapter.md#task-5-validation)
records ordinary mount/read/launch and failure-path coverage. No compiler-container
rebuild is needed; rebuild userland against the updated SDK.

Task 6 implements scoped filesystem information and its library wrapper, separate
mount OBSERVE and directory FILESYSTEM_INFO rights, and `--no-info`. The bounded
record copies retained identity, generation, independent GPT/filesystem health
flags, volume name and shared-pool capacity without traversal or disk reads.
Unavailable usage/quota/percentage fields are absent, never reported as zero.
[Query validation](../devices/filesystem-native-adapter.md#task-6-validation)
records ordinary user syscalls, attenuation, parent-close lifetime, remote
forwarding and final cleanup. The native request remains 4,592 bytes.

Task 7 is next: adapt Fastfetch Disk to explicitly selected bindings and these
scoped observations, preserving unavailable-value and shared-capacity semantics.

Agreed scope limitations are tracked in [technical debt](../technical-debt.md#native-mount-design-limits).

## Focused PR tasks

1. [x] **Settle the native mount contract.** Inspect the pinned core and current
   block, object, init and namespace interfaces. Resolve the questions above with
   a concrete rights/lifetime/error mapping and agreed runtime bounds. Record the
   first displayable capacity fields. This is a design PR, not a broad framework
   implementation or a repeat of the filesystem-format design.
2. [x] **Add validated core directory continuation.** In pyxis-fs, provide the
   bounded policy-view continuation/seek operation described above, with no
   retained enumeration state between calls and no prefix reconstruction.
   Specify the opaque token, view/generation validation, repeat/end/error rules
   and ordinary-page versus full-list validation guarantees. Preserve diagnostic
   cursor checks and update the core interface documentation. Validate through
   ordinary builds and manual host-tool/debugger inspection of multi-page
   enumeration. Publish a focused dependency PR before updating the parent pin;
   this prerequisite does not implement kernel objects or change the disk format.
3. [x] **Link the core and implement bounded block/memory adapters.** Integrate the
   pinned freestanding library into Caelum with kernel-appropriate flags; add
   partition-bounded reads, memory accounting and the agreed BSP worker ownership.
   Document manual preparation of a disposable GPT disk using existing host tools.
   Verify opening the selected pool/volume through normal boot and debugger
   inspection without a permanent diagnostic application or automatic probe.
4. [x] **Provide read-only directory/file objects.** Implement policy-approved
   acquisition, enumeration, lookup, length and offset reads through shared-core
   views, including retained object lifetimes and failure cleanup. Enforce the
   agreed rights mapping and read-only backend errors. Keep existing backends
   working. Share internal mount preparation needed by this and the next task;
   do not publish unusable placeholder APIs.
5. [x] **Mount and delegate from init.** Add the agreed mount ABI/library/command
   support and namespace/session forwarding. Init chooses partition, volume and
   binding through disk-scoped authority. Exercise `ls`, `cat` and launching an
   executable from the disk, plus absent disk, wrong selector, policy denial and
   rejected writes. Default boot needs no development disk.
6. [x] **Expose scoped filesystem information.** Implement the settled bounded
   query and library interface. Document field units, shared-pool meaning,
   verification status, unavailable fields and observation authority. Read queries
   must not trigger a full consistency scan or acquire additional authority.
7. [ ] **Adapt Fastfetch Disk.** Add the minimal native adapter, explicit resource
   forwarding if needed, recipe/pin changes and normal image integration. Exercise
   local/remote display, redirected text/JSON and absent disk/query authority.
   Preserve unavailable-value and upstream error behavior; do not broaden the port.
8. [ ] **Validate the combined workflow and close the milestone.** Rebuild and boot
   normally with and without the disk; inspect content against the source import,
   nested traversal, executable loading, delegated restrictions, repeated opens
   and process cleanup. Use existing host checking tools before attachment and
   confirm the image is unchanged afterward. Record actual coverage and limits,
   then move this WIP to the devices references and update the index.

Use ordinary builds, interactive QEMU and debugger inspection. The existing
remote client is suitable for command work; use screenshots only for presentation
checks that need them. No new tests, self-tests, fault injection, CI or boot/output
automation are authorized by this plan. Existing CI must be checked for submitted
revisions; distinguish host-only checks, nested KVM results and owner-hardware runs.

## Repository ownership and deferred work

Pyxis owns kernel adapters, ABI, SDK and image integration; pyxis-fs owns the
format/core and its host tools. Userland owns wrappers and mount/session commands;
ports owns Fastfetch. Publish dependent commits/PRs before updating gitlinks and
state merge order. Ordinary ABI/library changes need a new SDK, not a compiler
container rebuild. Assess any actual new host/toolchain requirement separately.

Writable transactions, recovery, reclamation, live-generation semantics, unmount
or hotplug administration, Linux FUSE, NVMe, installation, on-disk boot and a full
identity broker remain outside this milestone. It does not prove power-loss
recovery, file-data integrity checksums or production-data safety. The existing
[format/host limits](../technical-debt.md#filesystem-host-prototype-limits) remain
visible; carry any newly accepted implementation limits into technical debt.

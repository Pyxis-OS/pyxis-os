# Native FAT32 mounts

The BSP filesystem worker serves supported GPT FAT32 partitions through native
FILE/directory objects. The [shared engine](../../fs/docs/fat32-read.md) validates
both GPT copies and the whole volume before admission. Subsequent I/O remains
confined to the selected physical extent. Each instance has a fixed read-only or
read-write mode; read-only instances never write, flush or change dirty flags.
There is no repair or implicit URI mount. Keep external writers excluded; raw
read handles do not provide snapshots.

## Authority and interfaces

[`DISK_OPEN_FAT32`](../../include/abi/disk.h) requires MOUNT on an explicitly selected
DISK and takes a one-based GPT entry, RO/RW access and exact root rights, including
zero. RO rights are LOOKUP/ENUMERATE/READ_FILES; RW permits DIRECTORY_CONTENT_RIGHTS.
RW additionally requires DISK WRITE and qualified write/flush support, even for
an attenuated RO root. New RW admission requires a clean, error-free volume and
full structural preflight. An RO instance cannot upgrade: an RW open returns
BUSY until it is retired. An RW instance may supply RO views to an opener with
RW disk authority, sharing its domain, records and failure state.

Trusted boot init also receives a separate mount-only DISKS inventory as
`fat_mounts`: ENUMERATE plus MOUNT_READ_ONLY/MOUNT_READ_WRITE, independently of
install mode. `DISKS_OPEN_FAT32` selects a boot-scoped device ID/GPT entry and
returns the same native pair, with no raw DISK handle, claim or raw access.
An optional expected disk GUID is captured and checked by the worker against
its selected healthy GPT, then the fresh engine validation checks that identity.
RO requires either mount right; RW requires MOUNT_READ_WRITE. The separate raw
installer inventory remains install-only. Operator delegation is opt-in through
trusted configuration, never inferred from an executable or self-declared manifest.

An authorized RW factory may deliberately admit the boot ESP partition RW
(owner-accepted default, 2026-10-13), supporting explicitly declared ESP editor
grants. The boot-origin issuer/view remains RO; fixed-mode drainage and extent
exclusion still apply. Native ESP checks require owner approval and presence.

A successful call installs a root and bound MOUNT control in one capability-table
batch. Both slots are reserved before work;
copying the reply into user memory is a separate operation. A failed reply closes
this call's new handles and drains provisional ownership; escaped sibling grants
and already published instances are preserved.

The [control protocol](../../include/abi/mount.h) grants independently attenuable
OPEN_ROOT, OBSERVE and RETIRE; RW admission also supplies WRITE and INTERVAL.
OPEN_ROOT accepts only rights within that opener's original ceiling; mutation
rights additionally require control WRITE. It returns another root for the same
instance. OBSERVE reports genuine domain, device, GPT entry, physical range,
mode, dirty ownership, retirement, active interval and retained failure/cleanup
state. Clean/error flags are observations, not integrity or durability promises.
Closing a control never retires a mount or ends its process's interval.

The 680-byte `mount_state_reply` also carries `disk_guid`, `partition_guid`,
`partition_type`, `partition_attributes`, `device_block_size` and
`device_block_count` when `MOUNT_STATE_IDENTITY_VALID` is set. These are the
retained admission facts: both GPT copies were validated, the selected entry
was checked against the inventoried GPT, and device geometry came from the
admitted whole device, not just the partition. GUID arrays use GPT/EFI on-disk
byte encoding. They name a logical target; they authenticate no hardware and
grant no authority. OBSERVE does not re-read media or attest current contents.
The evidence remains valid on failed and retired controls, alongside their
original domain and physical extent. The userland `mount_observe` helper checks
the reply size and, when the flag is set, the identity and geometry invariants
before returning it. Callers must still check lifecycle state and hold the
appropriate selected-view authority for any operation. The
[qualification record](../development/experiments/fat32-mount-evidence.md) covers
RO/RW and boot-origin views on disposable media.

FILE READ/SIZE/INFO, directory LOOKUP/ENUMERATE/INFO and selected-executable
capture use the existing native protocols. Capturing uses the common 128 MiB
serialized-image ceiling; capture backing ceases to pin the mount when its FILE
and operation references drain. Data grants never confer mounting or retirement
authority. DIRECTORY_FILESYSTEM_INFO remains BAD_OPERATION because its existing
record describes npfs pools.

The [metadata contract](../interfaces/file-metadata.md) reports genuine
domain/object identity, shared by live long-name/ASCII-alias references. Empty
files remain distinct. Full close and remount promise no identity continuity.
File size is valid; directory size and DOS-local modification time are not.
Enumeration returns valid UTF-8 names; lookup supports ASCII long names and
short aliases, case-insensitively. Dot entries confer no parent authority.

## Native mutations and publication intervals

RW instances serve FILE WRITE/RESIZE/SYNC and directory CREATE/REMOVE,
same-volume file RENAME and SYNC with ordinary native rights. CREATE reserves
returned-handle capacity and prepares its grant before media effects. FILE sync
requires WRITE; directory sync requires CREATE or REMOVE; control SYNC requires
WRITE. Sync may flush the whole instance and never clears its owned dirty mark.
Every successful mutation completes its ordered barriers and final flush before
returning; closing a handle does not synchronize it. Rename is serialized for
live readers, without a crash-atomicity or rollback promise.

New names preserve ASCII spelling in LFNs. Namespace changes require supported
ASCII names/aliases for the affected parent and every sibling; Unicode objects
elsewhere remain readable. Supported ASCII content writes do not require ASCII
siblings. The regular-file read-only attribute refuses write, resize, removal
and rename/replacement of that object; directory attributes grant no authority.
No attribute-changing operation is supplied. FAT lengths remain at most
UINT32_MAX bytes, and backing/work budgets may refuse smaller operations.

Beyond-EOF writes and resize growth zero the entire exposed gap, including old
cluster slack. Shrink discards the tail so later growth cannot expose it; the
smaller directory length becomes durable before its FAT tail is reclaimed.
Zero-byte writes validate authority and object state without extending the file.
Rename moves the source's native identity. Removal/replacement detaches a distinct
victim; surviving FILE grants retain authorized access to its own content and
chain until their last reference drains. Detached directories reject namespace
changes. Directory generations change independently of object identities.

Control BEGIN/END require INTERVAL. BEGIN returns a non-reused identifier and an
absolute 30-second lease owned by the calling process; sibling tasks share it,
children and delegated controls do not inherit it. BEGIN while an interval is
active returns BUSY and never extends the deadline. Other processes' mutations
and sync return BUSY; reads remain available and observe each completed operation.
The interval grants no data authority, rollback or atomic visibility across calls.
END must name the owner's identifier and does not synchronize or remove staging
names. Expiry or owner stop closes interval admission; submitted work drains
before another owner can begin. A live expired owner receives TIMED_OUT on further
mutations/sync until it acknowledges END or obtains a new interval, including
while another owner holds the mount.

BEGIN remains provisional until its identifier/deadline reply is delivered.
The original storage admission and actor/node references cover an allocation-free
worker finish; failed copy-out removes that exact new interval or restores the
owner's prior expired record. Shutdown drainage includes this finish. Pending
intervals block mutations and cannot be replaced by another BEGIN or END.
Each mount admits at most 16 active, pending or expired-owner interval records,
charged to the shared wrapper budget. A new owner at capacity gets LIMIT before
allocation. An expired owner renews its existing record in place; live-owner
tombstones are never evicted to admit another owner.

## Failure and clean power

Capacity, budget and authority refusals before effects leave media unchanged.
An unconfirmed write or flush returns OUTCOME_UNKNOWN and permanently freezes
the instance's data operations, including reads. OBSERVE retains the first
failure, phase, absolute sector and affected names; prepared images and held
chains remain under mount accounting and extent exclusion. Closing/reopening
cannot reset failure. There is no retry, repair or forced retirement of uncertain
effects. Known preflight reclamation refusals remain explicit CLEANUP_PENDING
records; a later explicit RETIRE may retry that cleanup.

The [clean-power sequence](../kernel/acpi.md#power-off-and-restart) holds user
tasks, closes storage admission and drains admitted operations and deferred work.
It flushes each mutated RW instance and, when no detached allocated chains remain,
clears only its owned dirty mark and flushes that clear. Views, handles and
publication intervals remain retained. Detached chains leave the mark dirty after
the flush and never refuse power merely by remaining held. Unresolved mutation
failure blocks clean power. RO instances never change dirty bits.

The worker then seals npfs pools, stops NVMe and enters S5/reset. If power fails
and programs resume, storage admission reopens and the next mutation durably
re-marks a previously sealed FAT instance before its first effect. Normal RETIRE
still requires reference drainage; power sealing does not retire a mount.

## Exclusion and retirement

One worker-owned registry tracks pending admissions, active FAT instances,
retained npfs pools and raw claims by inventoried device and checked half-open
physical range. Reserve before probing. Exact FAT opens share one instance;
other overlapping admissions return BUSY. Whole-disk claims overlap every range;
disjoint partition claims preserve existing transport/NVMe policy. A raw claim
still remains unique per device. npfs retention and duplicate-pool policy are
unchanged.

RETIRE requires its separate right and returns BUSY while data wrappers,
delegated/cwd/queued grants, admitted operations or an active interval remain.
Known pending cleanup is retried explicitly and its failure is reported.
It revokes nothing and does not wait for the caller to close references. Control
aliases and the retiring request itself are exempt. Once drained, RW retirement
durably reclaims detached chains, clears its owned dirty mark and flushes before
releasing workspace and exclusion. Controls then become inert tombstones:
OBSERVE and repeated RETIRE remain safe; OPEN_ROOT returns UNAVAILABLE. New
admission gets a new domain. A completed failed read drains normally.

Final-control close retains a published instance until an authorized opener
reacquires control and retires it, or reboot. This intentionally bounded retention
also applies when its controller faults. Unpublished failure instead releases
backing and reservations after all owned/escaped references drain. No user buffer,
AP stack or held lock crosses storage waits.

Admission settings in the [adapter](../../kernel/fs/fat32.c) are four active
instances, 32 MiB aggregate FAT workspace/chain storage, the shared 1 MiB/1024
native-wrapper budget, depth 64, 67,108,864 work steps and 16,777,216 sectors per
engine operation. The existing serial worker admits at most 32 jobs with a
cooperative 30-second deadline. Pending charges count before allocation; LIMIT
and NO_MEMORY remain distinct. Growing a workspace or retained chain charges both
old and replacement backing until the replacement succeeds. These are implementation settings, not FAT format
limits or a total-memory guarantee.

## Trusted browsing profiles

Build the default image. **Browse scratch FAT32 (native RO)** opens a displayed
disk/GPT entry explicitly through the install-only inventory authority. It closes
the broad DISKS service after selection and retains the selected read-only disk
and bound control. **Browse boot ESP (native RO)** uses only the trusted
boot-provenance issuer: owned normalized Limine disk/partition UUIDs and media
kind must identify exactly one healthy GPT ESP. UUIDs select media, not authenticate
it. Optical/PXE, missing or ambiguous provenance supplies no guessed authority or
fallback to an attached disk. Boot options/configuration cannot replace the IDs.

At `fat32-mount>` use `ls /PATH`, `cat /PATH`, `info /PATH`,
`info /PATH | /OTHER_PATH`, `read OFFSET LENGTH /PATH`, `state`, `close`, `open`,
`retire` or `quit`. Literal spaces are accepted; there is no shell quoting or
expansion. Read only known text at the console: contents can contain control bytes.
`close` releases the held root. `open` reopens the bound active instance or admits
a new one after retirement; `quit` closes data and explicitly attempts retirement.
Errors are reported without automatic retry or repair.

Direct `ls`/`cat` children receive an explicit read-only `esp` startup root,
memory and console streams, with no DISKS, MOUNT, control or descendant launcher.
This is a trusted browsing bootstrap, not an installed shell binding, generic
mount command, editor or writable ESP workflow.

For disposable scratch media only, the separate trusted bootstrap
`init=boot://init-fat32-mount-write.pxe boot.install=1` opens the explicitly
selected disk RW and starts the mounted writer. It never falls back to the real
boot ESP. Commands include `create`, `mkdir`, `remove`, `rmdir`,
`rename /FROM | /TO`, `replace /FROM | /TO`, `sync`, `begin` and `end ID`.
Eight held-file slots support `hold SLOT ro|rw /PATH`, `hinfo SLOT`,
`hread SLOT OFFSET LENGTH`, `hwrite SLOT OFFSET = LITERAL`, `resize SLOT SIZE`,
`hsync SLOT` and `drop SLOT`. `shell` receives RO roots and explicit streams,
without mounting, interval or write authority. This profile exercises native
operations; configured bindings, selected editor grants and everyday mount
commands use the [trusted workflow](#trusted-workflow).
In QEMU, attach scratch media writable with modern VirtIO block. After GPT
discovery, focus **Mounted FAT32 Scratch Writer** with Super+Right from Caelum
(HMP `sendkey meta_l-right`), then enter the displayed disk ID and one-based GPT
entry. Selection requires focus; READ_ONLY media cannot support this writer.

The [qualification record](../development/experiments/fat32-native-mount.md)
separates QEMU/debugger evidence from later owner-routed native ESP checks.
Mounted-write and clean-power evidence belongs to the
[scratch RW qualification record](../development/experiments/fat32-mounted-writes.md).
Ambient RW FAT roots and native ESP writing remain outside this workflow.

## Trusted workflow

[Boot configuration](../userland/init.md#fat32-volumes-and-selected-grants) may
supply a provenance-bound RO boot ESP view or an explicitly selected FAT volume.
Normal FAT roots are RO. Configured data/controller grants remain outside those
roots and use the common [bundle declarations](../userland/program-bundles.md#selected-external-grants).
A write profile fixes RW admission first; a later editor never upgrades an RO
instance. No shipped profile enables FAT management or RW ESP access.

The shell owns up to four FAT binding/controller records, within the shared
eight-source launch budget. `mount --fat32 --list` shows boot-scoped selections;
mounting uses explicit disk/partition/access arguments. RW mounting publishes an
RO `NAME://` root and separately retains `NAME_write` data and `NAME_mount`
controller sources. `sync --mount NAME://` synchronizes that instance. `release
NAME://` moves cwd to `home://` with a report when needed, unbinds/closes the
caller's selected data references (including original startup cwd grants), then
requests retirement. Missing/unusable home refuses release before unbinding.
Other owners are never revoked. BUSY/failure retains a controller for retry;
closing a controller alone still does not retire. Trusted session successors
preserve selected sources; ordinary children receive only declared grants.

Configured bindings use `release --control SOURCE NAME://`; controller/root
native domains must agree before closing data. Selected local source directories
in that domain drain too. Another configured space or mux ancestor may retain a
view, correctly keeping retirement BUSY until it closes its own references.
A shell-created mount has no parent-mux data view unless explicitly inherited.
Clean power sealing remains usable with held data, controls and intervals.

`fat32ctl.pxb` is a thin native utility for `list`, `state`, `sync` and `retire`.
Its manifest requests enumerating `fat_mounts`, observing/retiring `fat32_control`
and a separate optional WRITE grant for sync. It reports the actual received
rights and native errors. It cannot unbind its parent's roots; parent-held data
makes retirement BUSY. The utility grants no authority by its command spelling.

Native ESP checks require explicit owner approval and presence. The
[owner-present run sheet](fat32-volumes.md#owner-present-native-esp-run-sheet)
is RO-only. Disk GUIDs/serials stay in owner-local configuration; repository
examples use placeholders. QEMU scratch-image qualification is not native media,
unplug, torn-sector or power-loss qualification.

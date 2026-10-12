# FAT32 volumes

Status: **tasks 1–3 merged (#740, #747, #755); task 4 implemented for review.**
The directory-validation fixes (#743 / fs #33) and task 3's binding (#750) are
merged. Task 4 implements the accepted seal-at-power contract below; its
[qualification record](../development/experiments/fat32-mounted-writes.md) separates
executed scratch-image checks from source review and remaining hardware limits.
Task 5 remains unassigned. Task 4 qualifies
only disposable scratch VirtIO images, never writes the owner's real ESP.
This shared milestone extracts [NVMe task 2](nvme-coexistence.md#2-fat32-read-and-write-for-the-esp)
and [installer task 3](installer-tui.md#dependencies-and-ordered-delivery).

## Deliverable and boundaries

Mount an explicitly selected FAT32 stick or ESP, read/list files, edit authorized
files through native FILE/directory capabilities, sync and release the volume.
The engine and mounted backend must deliver independently of coexistence or the
configuration editor. Consumers are the [ESP delta editor](installer-tui.md),
[NVMe coexistence](nvme-coexistence.md), [USB storage](../devices/usb-storage.md)
and SD cards through a supported block transport. The ThinkPad's internal
RTS522A reader needs its own driver; this milestone adds no SD/USB/NVMe driver,
hotplug, formatter, partition resizing, FAT12/16, exFAT or filesystem repair.

Keep accepted consumer policy: partition-confined coexistence writes, no
foreign-file/partition changes, owned config spans and explicit adoption,
new/sync/read-verify/swap with previous-file fallback, and archive-default rescue.
Those belong to the installer/editor; a general FAT backend cannot infer ownership
from filenames or parse Lua/Limine policy. Installed ESP config loading stays
blocked until ordinary Update preserves editable and foreign files.

## Proposal foundation (before task 1)

Proposal inspection used main `d124a053`, pinned userland `a84259c0`; no FAT
runtime probe was run for that proposal. No partial FAT implementation branch was supplied.
The [ESP writer](../../userspace/installer/esp.c) creates one fixed tree with
4 KiB clusters, two FATs and contiguous allocation; it invalidates boot geometry,
flushes, replaces the tree, flushes and republishes the boot sectors. It does not
edit an arbitrary existing volume. The [recognition reader](../../userspace/installer/esp_read.c)
checks only installed boot paths, mirrored FAT pages and bounded chains on that
geometry; its limited ASCII LFN matching and FSInfo bounds checks are not a
complete FAT/name/allocation validator.

[Block I/O](../devices/block-storage.md) already provides bounded transfers and
real ordered flush; a published failure may have changed media and can latch
writes off. [Raw claims](../devices/installer-authority.md) and
[npfs mounts](../devices/filesystem-native-adapter.md) exclude conflicting use,
but at that revision their registry knew npfs only, and pools stayed retained until reboot.
NVMe raw partition claims were still refused until the coexistence
authority gate lands; engine writes qualify on scratch VirtIO/USB first.
FAT mount/release and cross-backend exclusion did not exist. The FAT mount's
trusted WRITE authority must still meet backend qualification and `nvme.write=1`;
that boot option alone grants neither a root nor a raw claim. Mounted FAT dirty
state must join [clean power operations](../devices/nvme.md#health-and-shutdown)
before storage admission seals and NVMe normal shutdown starts. Sync/drain failure
refuses clean power. Preserve the accepted local warning/fresh-activation **Force
off/Force reboot** escape; delegated POWER remains clean-only, with no new force
right or automatic retry. The ordinary
[FILE/directory](../interfaces/directories.md) protocols and libc adapters exist;
wrapping FAT paths alone does not supply their lifetime or metadata guarantees.

## Accepted contract — 2026-10-11

1. **Shared engine, kernel-owned releasable FAT mounts.** Put a freestanding
   shared engine in pyxis-fs, reusing the existing MPL-2.0 ESP algorithms with
   notices preserved; no external library is selected. It owns format/traversal/
   allocation mechanics, not capability, scheduling or disk-selection policy.
   The raw installer supplies confined I/O; a native kernel adapter serves FILE/
   directory calls on the BSP filesystem worker using the same engine.

   **Kernel placement accepted by the owner, 2026-10-11.** User-supplied media
   becomes kernel parser input; malformed or malicious volumes may hang or crash
   the kernel despite bounded validation ([accepted debt](../technical-debt.md#fat32-parser-in-the-kernel)).
   Kernel placement reuses native lifetime/claim enforcement and avoids provider
   IPC. A userspace provider offers parsing-fault isolation at an IPC cost and
   needs additional directory-export/path integration: current
   [provider OPEN](../../userspace/lib/provider.c) accepts FILE responses and
   [enumeration](userspace-scheme-providers.md#sqlite-views-and-query-results)
   still needs a directory contract.

   Trusted authority selects an exact device/extent, read-only or read-write;
   applications get only explicitly attenuated roots/files. The bootstrap ESP
   binding comes from trusted boot handoff, never editable Lua or a guessed disk.
   Use one shared volume state per extent; all aliases, cwd/namespace/queued grants,
   FILE/directory handles and operations retain it. Extend kernel claim exclusion
   to FAT: overlapping raw writers/mounts return BUSY; other partitions keep their
   existing policy. Plan-time read views make no repairs and must drain completely
   before exclusive raw publication. Leave npfs retention unchanged.

   Explicit retirement drains external data handles, cwd/namespace/queued grants
   and other operations; the retirement control/request itself is exempt. Live
   references or unfinished cleanup return BUSY, without revocation. Synchronize
   writable state before release; unresolved mutation/flush failure retains
   state/exclusion, not a silently writable raw claim. A completed failed read
   alone does not pin an otherwise drained read-only mount forever.
   Closing a handle alone neither syncs nor proves retirement. An editor's
   compare/stage/swap needs an exclusive mutation interval across calls, not just
   serial individual operations. Parent CREATE/REMOVE grants authorize a directory,
   not selected names/spans: trusted editor policy preserves foreign contents.
2. **Incremental mutation with honest durability.** Keep the earlier raw-engine
   profile additive and limited to files it created; the raw consumer must retain/
   supply and revalidate that provenance, never reconstruct it from a FAT name.
   Mounted mutation later adds
   writes/resizing, create/remove and same-volume file rename under real delegated rights.
   Adopted existing ESP files/spans still require the editor's accepted confirmation.
   Never reuse the whole-ESP replacement writer for an existing shared volume.

   Bound/preflight metadata, allocation and chain walks before effects. Use data,
   FAT and directory/FSInfo publication phases with real flush barriers at dependency
   boundaries, including mirrored FAT updates. Explicit FILE/directory/volume sync
   covers its required data and metadata and backing flush; close is not durable.
   Stop on uncertain write/flush, preserve failure and recovery names, and never
   replay automatically. FAT is [nonjournalled](https://learn.microsoft.com/en-us/windows/win32/fileio/filesystem-functionality-comparison#journaling-and-change-log):
   no crash-atomic rename, untorn-sector, multi-file transaction or rollback
   promise. Known-success live namespace changes are serialized and publish one
   coherent result/generation; that is distinct from durability across interruption.
   Preserve RAM/npfs rename contracts and specify the FAT backend
   failure/durability contract before extending their native operation; do not
   call a multi-sector FAT update an npfs journal commit. Interrupted writes
   can leave partial data, orphan clusters, inconsistent FAT copies or uncertain names; later host repair is outside Pyxis.

   **Mounted-RW policy:** mark dirty and flush before mutation, clear
   only after successful clean retirement; never clear a pre-existing dirty flag
   or repair it. Preserve the earlier raw-engine profile's no-dirty-bit-change
   rule until separately extended.
   Dirty, damaged, cross-linked, inconsistent or unsupported volumes refuse writes;
   a clean flag alone is not proof of integrity or successful publication.
3. **Bounded compatible names and real held-object lifetime.** GPT-only discovery
   is accepted by the owner (2026-10-11); MBR/bare sticks stay outside this milestone.
   First geometry: validated FAT32 partitions, 512-byte FAT/device
   sectors, valid cluster sizes, one FAT or two mirrored FATs. Unsupported
   geometry/features refuse explicitly. Validate long-name sequences,
   UTF-16→UTF-8 decoding, NT lowercase flags and aliases; list valid Unicode names.
   First lookup uses ASCII long names/short aliases, case-insensitive with
   preserved spelling; valid Unicode objects may be read via an unambiguous ASCII
   alias, but non-ASCII long-name lookup and mutation of those objects refuse.
   Namespace create/rename/remove additionally require all sibling names/aliases
   to fit the ASCII profile, so collision checks do not guess Unicode/OEM folding.
   Unrelated valid Unicode siblings do not forbid content writes to supported
   ASCII objects, but do block namespace edits in that directory. Full Unicode
   mutation and wider layouts are later qualification, not claimed as delivered. Enforce native name
   bounds and FAT file-length limits, with errors rather than truncation/mangling.

   Aliases and rename retain one mount-lifetime object identity. First cluster or
   reusable directory slot alone cannot identify an empty/replaced file. Old handles
   keep old content after unlink/replacement; do not free/reuse their chains until
   references drain. Growth zero-fills; detached directories reject new children.
   Expose [metadata](../interfaces/file-metadata.md) only with genuine validity;
   no fabricated inode/time, Unix permissions, npfs journal fields or FAT volume IDs.

## Accepted task 1 details — 2026-10-11

- The general inspector strictly refuses corrupt metadata. The installer's
  separate lenient recovery reader keeps its existing foreign-binding safeguards;
  a strict-open failure must never become permission to rebuild an ESP.
- A trusted inspection bootstrap opens an explicitly selected disk read-only,
  delegates INFO|READ only, and confines engine reads to the selected GPT extent.
  It delegates neither the DISKS service nor MOUNT, writes or child launch.
  Selected media must be quiescent: current read-only disk access supplies no
  snapshot or exclusion. Shared mount/claim exclusion remains task 3.
- Each list/read validates all metadata and chains it uses. An explicit whole-volume
  check supplies broader validation; a successful individual read does not prove
  unrelated metadata sound. Workspace is caller-owned and bounded, data is streamed,
  and LIMIT/NO_MEMORY never weaken validation.

## Task 1 implementation and qualification

The [shared engine](../../fs/docs/fat32-read.md) exports `libfat32-read.a` and
`pyxis_fs/fat32.h`; the SDK exports the target archive and MPL notices. The
[host inspector](../../fs/docs/fat32-inspect.md) and
[native inspection profile](../devices/fat32-inspection.md) support info/check/list/read.
The live menu selects a trusted bootstrap, explicit disk/GPT selection and an
INFO|READ-only child. No mount or write API was added, and the separate installer
reader was byte-identical during task 1 qualification; this task does not
replace or modify it.

[Qualification](../development/experiments/fat32-read-only.md) records exact
baseline/dependency revisions, disposable-media preparation, ordinary builds,
host corruption/budget checks and interactive QEMU/GDB authority and retirement.
This is nested-VM evidence, not native ESP/hardware qualification. Read-only
inspection still requires quiescent media; writer exclusion remains task 3.

## Task 2 mutation contract — accepted 2026-10-12

1. **Creation provenance lifetime: writer-session receipts.** The trusted
   caller provides bounded receipt storage. Only a successful create issues a
   receipt, bound to that session/volume, parent, entry and complete chain;
   append/replacement revalidate it. Existing files are never adopted by name or
   marker. Closing/restarting loses permission to update those objects. This
   keeps the first raw slice useful for create→extend→replace in one session,
   without inventing persistent FAT object identity or on-disk ownership metadata.
2. **Operation result and failure state: synchronous durable operations.**
   Preflight whole-volume integrity, namespace/allocation and finite workspace/work
   budgets before effects. Create, directory creation, append and staged replacement
   succeed only after their final flush, following the accepted data→FAT→directory/
   FSInfo barriers. Keep staging/recovery information after failure; any uncertain
   mutation permanently stops writes in that session, with no rollback, automatic
   replay or cleanup. Preserve the raw profile's dirty bits. These outcomes do not
   promise crash atomicity, untorn sectors or preservation of every old byte after
   an uncertain in-place append. Persistent adoption/recovery remains outside this
   slice; mounted-RW dirty/retirement policy remains task 4.
3. **Native raw-write entry point: separate trusted scratch-write profile.**
   Explicit disk/GPT selection, initially disposable VirtIO and qualified USB;
   keep the read-only inspector and installer separate. Existing RW open first
   claims the whole disk; successfully release it, claim the selected partition,
   then revalidate GPT/volume/provenance before effects. The child receives disk
   INFO|READ|WRITE|RELEASE, memory/console streams and any explicitly selected
   read-only payload FILEs, with no DISKS/MOUNT/launcher or directory roots.
   This remains trusted code: WRITE also permits CLAIM, so the current capability
   can be used to release/reclaim the whole disk. Code and the active claim confine
   ordinary writes, not a permanently attenuated partition capability. RELEASE
   ends the claim even on failure; report this honestly, relying on the existing
   device failure latch rather than promising retained raw exclusion. No native ESP
   write qualification or NVMe coexistence authority is added here.

The [shared raw writer](../../fs/docs/fat32-write.md) and separate trusted
[scratch profile](../devices/fat32-scratch-writing.md) implement this contract.
[Qualification](../development/experiments/fat32-raw-writes.md) records baseline
revisions, host/mtools checks, actual capacity failure and interactive QEMU/GDB.
Directory-tail and volume-label fixes are included; embedded short-name spaces
remain read compatibility, never creation provenance. No mount/write adapter,
formatting or native ESP qualification is added.

## Task 3 native read-only binding — accepted 2026-10-12

The [pre-task baseline](../development/experiments/fat32-mount-baseline.md)
records main `173f1321`. The [native mount reference](../devices/fat32-mounts.md)
defines the implementation's protocols, browsing profiles, limits and lifetime.
Configured npfs MOUNT authority and boot-long pool retention remain unchanged.
The following accepted contract governs task 3; task 4 is assigned below and
task 5 remains unassigned.

### Three accepted owner decisions — 2026-10-12

1. **Binding and retirement: separate root and bound control.** Add
   `DISK_OPEN_FAT32`, requiring the selected disk's existing MOUNT right, with
   a one-based GPT entry and exact read-only directory rights. Successful admission
   returns a root and a control for that mounted instance in one capability-table
   batch; there is no half-installed pair. Reserve both slots before work. This is
   not an atomic transaction with copying the reply into user memory. No new control
   comes from ordinary FILE/lookup rights; READ alone cannot allocate a mount.
   Configured npfs MOUNT remains unchanged.

   Use a bound backend of the existing MOUNT protocol: OPEN_ROOT reopens only that
   instance under its recorded read-only ceiling; OBSERVE supplies actual state;
   a separate RETIRE right authorizes retirement. No caller-supplied raw range,
   alternate device or writable upgrade is accepted. Actual data grants are LOOKUP,
   ENUMERATE and READ_FILES, independently narrowed as today.

   RETIRE does not wait for references: BUSY leaves the instance active when any data object,
   admitted operation, unpublished returned grant or deferred cleanup remains.
   Copied/delegated roots and files, cwd chains, queued transfers and launch grants
   all count through their retained objects. Control aliases and this retirement
   request do not themselves prevent retirement. Check and close new data admission
   on the sole worker before destroying backing and removing exclusion; report OK
   only after completion. Retired controls remain inert tombstones: observe/retire
   is safe and idempotent, OPEN_ROOT fails, and remount creates a new instance/domain.
   A completed failed read drains normally and cannot pin retirement forever.

   Closing the final control does **not** implicitly retire or revoke data handles.
   State/exclusion remains until an authorized opener reacquires control and retires,
   or reboot. This preserves explicit retirement, at the cost of retained bounded
   state if its controller dies; the trusted browser keeps selection authority/control
   while children run. Failed admission releases this call's reserved slots, grants
   and operation references. A failed reply copy closes its newly installed handles;
   it cannot revoke references a sibling already copied/delegated. Release a new
   provisional instance's backing/exclusion only after all its references drain.
   Never discard a previously published or shared instance on another opener's failure.
2. **Admission and exclusion: one worker-owned physical-extent registry.**
   Key entries by inventoried device and checked half-open ranges in that device's
   logical blocks. Track pending admissions, FAT instances, retained npfs pools and
   raw claims. Reserve before format probing and workspace/store backing allocation, validate both GPT copies and the
   complete FAT volume, then publish. Failed admission removes its reservation.
   Repeated exact FAT opens with matching media/geometry share state, independent
   of GUID/name spelling; another backend or an overlapping different extent is BUSY.

   A whole-disk raw claim overlaps every registered extent. A partition claim
   conflicts with that physical extent, not just its GPT ordinal. Preserve the
   existing one-raw-claim-per-device restriction, NVMe blank/partition gates and
   npfs retention/duplicate-pool policy. A retired FAT interval does not make
   whole-disk RW OPEN possible while another npfs partition remains mounted;
   partition publication still needs an already authorized eligible disk object.
   Raw read handles do not become mount leases or immutable snapshots.

   Read-only FAT admission can retain observed dirty/error flags when full structural
   validation succeeds; it never clears them, repairs or replays anything. Corrupt,
   unsupported or out-of-budget media refuses without weaker checks. No writes or
   flush are issued by this read-only mount/retire path. External media mutation is
   unsupported; device loss/I/O errors fail operations without permitting stale reuse.

   Initial implementation budgets: four FAT instances, 32 MiB aggregate FAT
   workspace/retained state, the existing shared 1 MiB/1024 native-wrapper budget,
   depth 64, 67,108,864 work steps and 16,777,216 sectors per engine operation.
   Reuse the serial worker's 32-job admission and cooperative 30-second deadline.
   Charge pending and retained storage before allocation; LIMIT and NO_MEMORY are
   distinct, with no partial root publication. These are implementation settings,
   not FAT format limits or a total-kernel-memory guarantee.
3. **Trusted ESP selection: boot provenance, explicit root delegation.**
   Copy valid GPT disk/partition UUIDs and media kind from the executable's Limine
   file into owned boot metadata; normalize UUID fields in the adapter and retain
   no bootloader pointer. Match exactly one healthy inventoried GPT disk/ESP by both
   identifiers. UUIDs select media, not authenticate contents. Missing/zero,
   ambiguous, unsupported or inaccessible identity supplies no guessed ESP authority;
   optical/PXE boot does not select an attached ESP merely because one exists.

   The initial trusted bootstrap alone receives a boot-only MOUNT selection backend,
   bound to that disk/partition. Its OPEN_FAT32 operation takes only read-only
   root rights and uses the same admission path to return the first root/control pair;
   it accepts no device/partition selector and grants no generic DISK MOUNT authority.
   The returned control is the instance backend from decision 1, not this issuer.
   A trusted inspection bootstrap retains issuer/control and delegates an `esp`
   startup root to direct `ls`/`cat` readers, without disk, reacquisition, RETIRE or
   descendant launch authority. Editable Lua, names and callers cannot change the
   selected disk/partition. Ordinary URI reads never mount automatically.
   For disposable-media qualification, a separate trusted install-mode selector
   uses explicit disk/GPT selection and DISK MOUNT; it is not boot-ESP provenance.
   A required unavailable root fails that request; it does not fall back to another
   disk or a global `esp://`. Installed shell bindings/config loading/editor workflows
   remain task 5; task 3 introduces no descendant-launch policy.

### Adapter and lifetime details within task 3

Extend the shared read engine with confined entry locators, single-component
lookup/enumeration and offset reads. Intern canonical objects across long-name,
short-alias and repeated capability paths. A locator is internal validation state,
not identity: assign genuine native domain/object IDs using the existing
[metadata allocator](../../include/kernel/fs/metadata.h). The domain lasts for
this mounted instance; IDs do not wrap/recycle and comparison follows the existing
[held-reference contract](../interfaces/file-metadata.md). Empty files remain distinct.
No after-final-close or remount identity promise is added. Task 3 has no rename,
unlink or replacement; the accepted held-victim semantics still gate task 4.

FILE READ/SIZE/INFO and directory LOOKUP/ENUMERATE/INFO use ordinary native
interfaces with actual rights and staged replies. Enumerate valid UTF-8 LFNs;
lookup remains ASCII long name/alias with preserved spelling. Dot entries grant
no parent authority. Cursors remain opaque, shared by canonical directory identity,
with unchanged short-buffer/END rules. Report real SIZE/DOMAIN/OBJECT validity;
FAT's local DOS timestamps have no known UTC offset, so MTIME remains invalid.
DIRECTORY_FILESYSTEM_INFO stays BAD_OPERATION in this slice: its existing record
is npfs-specific, and no pool UUID/journal generation is fabricated.

Use the existing 128 MiB selected-image capture path; no whole-volume/file snapshot
or eager file-buffer wrapper. Captured executable bytes cease to pin the volume
once the input operation/FILE reference drains; actual delegated resources still
pin it. FILE ranges publish no bytes on operational failure. No provider directory,
service-namespace extension, POSIX unmount, new worker or allocator lock is introduced.
Startup roots/cwd are directory grants; the endpoint service namespace is separate.

The BSP filesystem worker owns mutable registry, mount, engine and identity state
with IF=1. Existing BSP allocation/VM helpers and short IF=0 handoffs own allocation,
publication and allocation-free destructor transfer. No user pointer/AP stack,
capability entry or held lock crosses disk waits. Worker cleanup returns backing,
identity records and admission charges before releasing exclusion. Admission and
retirement must also unwind correctly during process/group stop and failed replies.

### Task 3 implementation and qualification

The shared reader exports confined locators, single-component lookup/enumeration
and staged offset reads. The BSP adapter, extent registry, bound control and
trusted browsing profiles implement the accepted binding. See the
[native reference](../devices/fat32-mounts.md) for use and interfaces.

[Qualification](../development/experiments/fat32-native-mount.md) records matched
npfs/host-reader costs, actual held identity and drainage, overlap/coexistence,
controller reacquisition, process-stop cleanup, corrupt/budget refusal and
boot-bound/absent/ambiguous ESP selection. It distinguishes source-reviewed
failed-reply/group ordering from executed cases. This is QEMU/debugger evidence;
owner-routed native ESP checks remain later, read-only. No writes, new test
harness, fault injection, generic mount CLI or task 4 work.

## Task 4 mounted-write binding — accepted, 2026-10-12

The [exact-main baseline](../development/experiments/fat32-mounted-write-baseline.md)
uses `36bcd7e9`, fs `30f625a2` and userland `3daa2461`. This section settles the
remaining task 4 binding. Decisions 1–2 are accepted unchanged; decision 3 is
accepted with the owner's replacement clean-power contract below. Implementation
is authorized; no new session-termination phase is required.
The accepted ASCII profile, retained-object semantics, dirty marking, ordered
flushes and explicit retirement above remain requirements.

### Three accepted owner decisions — 2026-10-12

1. **RW admission and authority: fixed instance mode, no upgrade in place.**
   Add explicit RO/RW selection to `DISK_OPEN_FAT32`, independent of the
   exact returned root rights. RW requires both DISK MOUNT and WRITE, qualified
   write/flush support. Creating a new RW instance requires a clean/error-free
   volume and full structural preflight. Exact RW reacquisition accepts its own
   dirty mark and tracked held chains, preserving the existing domain/state; it
   cannot reset failure or treat unexplained orphans as valid.
   Requesting an RO root from an RW instance still requires RW admission authority;
   it does not create an upgrade route. An existing RO instance makes an RW open
   BUSY until explicitly retired. An existing RW instance may supply attenuated
   RO views, all sharing its identity/state. No root or FILE authority upgrades a
   mount, and no boot-ESP issuer gains write authority.

   Bound controls have independently attenuable OPEN_ROOT, OBSERVE, WRITE, RETIRE
   and publication-interval rights; their recorded root ceiling cannot widen.
   OPEN_ROOT additionally requires control WRITE for mutation-capable root rights.
   RO admission supplies neither WRITE nor interval rights and records an RO ceiling.
   Native FILE WRITE and directory CREATE/REMOVE/WRITE_FILES retain their ordinary
   checks. FAT's read-only regular-file attribute additionally refuses content
   changes, resize, removal and rename/replacement of that object; this slice adds
   no attribute-changing operation. Directory attribute bits confer no authority.
   The scratch bootstrap uses an eligible RW disk opened before mounting, releases
   its raw claim, then selects GPT/RW and delegates exact native roots/control.
   It does not bypass whole-disk claim, disjoint npfs or NVMe admission policy.

   The boot browser stays RO; writable installed bindings and the real ESP
   remain outside this task.
2. **Cross-call publication interval: mount-wide, owned by one process.**
   A separate control right permits BEGIN/END. BEGIN returns a
   non-reused interval identifier; another owner receives BUSY immediately.
   The owner retains its ordinary data grants; the interval neither grants access
   nor bypasses FILE/directory checks. All its sibling tasks share ownership;
   children and delegated controls do not inherit it. Reads remain available and
   observe each coherent completed operation. Other processes' mutations and sync
   return BUSY. This serializes compare/stage/swap against other mutations, not
   readers, and promises neither rollback nor multi-call atomic visibility.

   Use an absolute, initially 30-second bounded lease, separate from the worker's
   per-operation deadline. Repeated BEGIN does not extend it. END must name the
   owner's identifier. Expiry or owner stop closes admission under that interval;
   effects already submitted drain before another owner can begin. A mutation
   admitted under an expired interval cannot silently execute outside it. A live
   expired owner receives TIMED_OUT on further mutations until it explicitly
   acknowledges END or obtains a new interval. Successful END permits subsequent
   ordinary mutations; it neither removes staging names nor clears dirty state.
   Closing the final control does not retire the mount; an abandoned interval ends
   through its deadline/owner-stop rule, without revoking data grants.

   Retain the existing storage-independent
   [process control](../../include/kernel/object/process.h), not a raw process/VM
   pointer, as owner identity. The BSP worker owns interval state, samples stop
   through the existing short IF=0 handoff, and bounds retained lease records with
   the mount's admission accounting. This avoids pinning execution storage until
   a deadline. Ownership requires shared-process cooperation and explicit expiry
   handling; it is not transferable.
3. **Sync, uncertainty and clean power: durable calls, failed mount retained.**
   Each successful mutation completes its ordered barriers and final
   flush before returning. FILE/DIRECTORY sync may conservatively flush the whole
   instance; control SYNC requires WRITE. Ordinary sync never clears the owned
   dirty mark. Preflight capacity/budget/authority refusal has no media effects;
   an unconfirmed write or flush returns OUTCOME_UNKNOWN and permanently freezes
   that instance's data operations, including reads whose media may be inconsistent.
   OBSERVE/recovery diagnostics remain available. Retain the first failure, phase,
   sector, affected names, prepared images and held chains with extent exclusion.
   There is no retry, repair, forced retirement or recovery by closing/reopening.

   RETIRE remains BUSY with data references, an active interval or deferred cleanup.
   After drainage, reclaim detached chains durably, clear only this mount's owned
   dirty mark, flush that clear, and release state/exclusion only on known success.
   Failure during dirty marking, reclamation or clean marking follows the same
   terminal rule. A pre-existing dirty/error state is never cleared or admitted RW.

   **Clean power: seal, do not retire — accepted 2026-10-12.** Hold user tasks,
   drain admitted FAT operations and deferred work, then flush each mutated RW
   mount. When it has no detached held chains, clear its owned dirty mark and
   flush that clear without revoking views, handles or publication intervals.
   Follow this with the existing npfs seal, NVMe shutdown and S5/reset. If power
   fails and programs resume, the first subsequent mutation durably re-marks the
   volume dirty before its first effect. When detached held chains remain, flush
   but leave the dirty mark set for later mount/fsck reclamation; that condition
   never refuses power. Only unresolved mutation failure blocks clean power under
   this FAT policy. Force off/reboot and clean-only delegated POWER are unchanged.
   RO mounts never change dirty bits, and no pre-existing dirty mark is cleared.
   Normal-operation RETIRE retains its BUSY/drain/clean-retirement rule above.

### Existing power sequencing and task 4 integration

Inspected main `36bcd7e9`, userland `3daa2461`, 2026-10-12. The
[power overlay](../kernel/power-overlay.md#shut-down-and-reboot) and
[ACPI reference](../kernel/acpi.md#power-off-and-restart) match the code:
[`acpi_power_run`](../../kernel/acpi/power.c) holds user execution with
[`task_user_hold`](../../kernel/task.c); queued filesystem work and available
cleanup drain; the [filesystem worker](../../kernel/fs/npfs.c) flushes/checkpoints
and seals npfs; NVMe shuts down; then S5/reset follows. Failure unseals pools and
releases held tasks; NVMe retirement is not undone. User holding does not terminate
processes or close capabilities. Boot init starts spaces and exits; shell POWER,
local overlay and power-button requests converge on the same ACPI worker.

Task 4 inserts FAT drain/flush/owned-dirty handling before the existing npfs seal,
using retained mount state for failure/resumption. The owner replaced the earlier
stop-spaces amendment with this sequence; no session-termination redesign remains.

### Implementation obligations

The [raw writer](../../fs/include/pyxis_fs/fat32_write.h) still uses session-only
creation receipts, cannot shrink and immediately reclaims replaced chains. Do not
weaken that profile or use its receipts to authorize existing native objects.
Add mounted-engine records with attached/detached state, current location, size
and retained chains; the kernel owns genuine native IDs and references. Rename
moves the source identity and replacement detaches a distinct victim. Held FILEs
continue authorized reads/writes against their retained object; removed directories
reject namespace mutation. Separate mutable directory generations from object IDs.

Whole-volume mutation preflight must account for explicitly tracked mount-owned
held chains as additional roots, without accepting unexplained orphan allocation
or weakening the public checker. Free a detached chain only after its last data
reference drains. Preflight backing/work/sector budgets before effects, charging
prepared images and detached records to the existing aggregate mount limits;
return LIMIT/NO_MEMORY rather than allocating unbounded plans. No per-file image
copy or new journal is implied. Preserve real FAT length limits and genuine
metadata validity; DOS-local timestamps still do not supply a valid UTC MTIME.

Beyond-EOF writes and resize growth zero the entire exposed gap, including old
slack; shrink discards the tail so later regrowth cannot expose it. Publish a
smaller directory length before cutting/reclaiming a shrink tail. Growth and
namespace edits retain the appropriate data/FAT/directory/FSInfo dependencies;
complete multi-sector rename is hidden from live readers by the sole worker,
without a crash-atomicity claim. Success updates records/generations only after
confirmed publication. Preserve the shared reader, npfs/RAM semantics, BSP
allocation/VM ownership, queued grant drainage and no caller pointer or held lock
across disk waits.

Implementation qualification repeats the baseline with preserved control binaries/
images interleaved. Use scratch VirtIO media, mtools/fsck.fat cross-checks and
interactive QEMU/GDB for held victims, zero gaps, aliases, detached directories,
interval contention/expiry, sync, clean retirement and retained failed states.
Daybreak's adversarial pass follows the implementation. No writes to the owner's
ESP, new test framework, fault hook, klog line or task 5 work.

## Parked cross-cutting directions

[Nested execution groups](nested-execution-groups.md) are not a prerequisite for
the engine or kernel filesystem worker: mounts are retained native objects, not
descendant processes. Trusted init can delegate selected roots using existing
launch grants. A first helper can inspect/sync its own mount or delegate a root
to a direct child without descendant launch authority; it cannot pretend to
change its parent shell's bindings. Group/process completion does not substitute
for mount reference drainage, data sync or hardware retirement. Any later
helper/repair workflow that launches descendants must follow the parked required
subgroup direction rather than inventing a sibling-group service here.

[Capability-valued environment](capability-environment.md) matters if later shells
or configuration store a mounted root as a variable. This first milestone uses
existing explicitly transferred roots/namespace and launch grants; `esp://`
names an authorized root, not authority encoded in an environment string. Do not
invent a FAT-only capability-variable store or printed numeric handles. Before
task 5 adds live shell bindings or configuration-selected capability entries,
return to that parked design for owner review; early trusted `esp` delegation
and mounted file I/O do not require its implementation.

## Ordered delivery tasks

- [x] **1. Small first task: shared read-only engine/inspector — assigned 2026-10-11.** Extract/generalize
  the bounded reader, admit geometry and validate chains/FAT/LFN/FSInfo with explicit
  memory bounds. List/read arbitrary supported files through confined raw reads,
  refusing unsupported/corrupt metadata; no writes or mount API yet.
  **After this task the owner can:** inspect supported scratch FAT media and read
  the real ESP read-only without running coexistence or the editor.
- [x] **2. Incremental raw engine writes — assigned 2026-10-12; mutation contract accepted.** Allocation, directories, long names
  within the chosen profile, create/extend/staged replacement of created files,
  ordered flush/sync and failure preservation. Keep explicit ownership policy and
  partition claims; qualify on disposable images, without whole-tree formatting.
  **After this task the owner can:** add and replace Pyxis-created files on a
  scratch FAT volume that Linux reads, without rebuilding that volume.
- [x] **3. Native read-only mount and release — merged #755.** Kernel FILE/directory wrappers,
  shared extent registry, alias/object identity, claim exclusion, reference drainage
  and trusted ESP bootstrap/root delegation. Design the native mount/retire binding
  before code; no userspace scheme proxy or implicit mount authority.
  **After this task the owner can:** mount supported FAT32 and use ordinary `ls`/
  `cat`, then release all views so an authorized raw publisher can claim the extent.
- [x] **4. Mounted read-write operations and sync — assigned 2026-10-12; implemented for review.** Deliver real native mutations,
  held-handle/zero-fill semantics, selected profile, exclusive publication interval,
  failed-state retention and the accepted dirty-bit/clean-retirement policy.
  **After this task the owner can:** edit authorized files, explicitly sync and
  retire a scratch FAT mount; simultaneous conflicting raw writers are refused.
- [ ] **5. Trusted mount workflow and qualification.** Thin mount/sync/release
  utility and configured binding integration; selected read-only normal ESP views
  and explicit writable editor grants. Qualify scratch VirtIO first, then supported
  USB and owner-approved native ESP steps; SD depends on its block driver. Revalidate
  consumer handoff/release and preserving unrelated files, not installer completion.
  **After this task the owner can:** mount a GPT-partitioned stick matching the
  first profile or a selected ESP view, read/edit/sync files through native
  programs and release it with reported errors. Typical retail sticks using MBR
  or a bare FAT volume are not covered; MBR/bare-volume discovery is later work,
  outside this milestone.

Future assigned tasks use ordinary builds and interactive QEMU/GDB, existing
`mtools`, `fsck.fat` and Linux mounts: cross-read, compare untouched files/sectors,
short/denied/full writes, alias collisions, corrupt chains, live-handle replacement,
claim/mount races, sync errors and reference drainage. Capture matched baselines
where existing mount/I/O paths change. No new test harness or fault injection;
QEMU does not establish hardware power-loss durability. Native ESP writing needs
its own explicit qualification go.

# Explicit durability for namespace and FAT32 mutations

Status: tasks 1, 2, 2b and 3 are merged. Task 4 is owner-assigned (2026-10-19):
finish matched measurements and the scheduled native check, then fold this plan
into the implemented filesystem contracts. The decisions below remain accepted.

## Goal

Finish the durability model the owner chose on 2026-10-03: `fsync` and `sync`
are the explicit durability points and block until the data is synchronized,
close only releases the handle, a background task flushes old dirty state (30
seconds by default), memory pressure writes back early, and power-off and
restart flush everything. Journal commits happen as required to keep metadata
recovery guaranteed.

npfs file contents already follow this model
([filesystem adapter](../devices/filesystem-native-adapter.md#writeback-recovery-and-errors)).
Two paths formerly made every call durable before it returned:

- **npfs namespace and size operations** (changed by task 2). Create, rename,
  remove and shrink each committed a pool transaction to durable COMMITTED. The
  [#348 review](../development/experiments/npfs-namespace-writeback/README.md)
  kept them immediately durable while removing unrelated file flushes. Removing an
  old program revision (about 2,400 entries) cost about 19,000 flushes.
- **The mounted FAT32 writer** (changed by task 3). Mutations formerly ran ordered
  DATA, FAT, DIRECTORY and FSINFO phases with a flush after each before returning
  ([FAT32 mounts](../devices/fat32-mounts.md)). An Update stages its boot archive
  in 1 MiB growing writes, about 1 MiB/s on a USB stick.

The remaining costs are recorded in [technical debt](../technical-debt.md).

## Accepted decisions (owner, 2026-10-17)

1. **npfs first, then FAT32.** npfs namespace operations join the pool's running
   journal transaction, which commits at `sync`/`fsync`, on the background
   interval, under pressure and at power-off or restart. The journal keeps
   recovery at a consistent point. FAT32 follows as its own task: it has no
   journal, so it keeps its phase order but pays it per batch (all data, flush;
   FAT, flush; directories, flush; FSInfo, flush), and clusters freed in a batch
   are not reused until that batch is durable.
2. **Replacing renames keep the safe-save flush.** A rename that replaces an
   existing name still flushes the moved file's contents first, so a crash cannot
   turn a saved file into an empty or partial one. Only replacement pays this.
3. **Callers that need durability ask for it.** Paths that relied on per-call
   durability (the installer's publication points, saved shell history and
   similar) gain an explicit sync. A crash may lose operations since the last
   checkpoint, nominally the background interval. The npfs journal restores a
   consistent metadata point; FAT32 keeps its existing dirty-volume and external
   host-repair limits, without atomic multi-sector publication or replay.

## Tasks

- [x] **1. Audit and baseline.** List every in-tree caller that depends on a
  namespace or FAT32 mutation being durable on return, with the sync each one
  needs. Capture matched baselines before any change: old revision cleanup and a
  hosted payload extraction on npfs, and Update staging on FAT32, in QEMU and on
  a stick, including how fragmented the resulting files are (npfs already
  delays data allocation to writeback; FAT32 allocates at each call).
  Done 2026-10-18: [audit](#task-1-audit) below and the
  [QEMU baseline](../development/experiments/explicit-durability-baseline.md).
  The stick baseline needs an owner run; task 4 repeats both.
- [x] **2. npfs namespace operations in the running transaction.** Implement
  decision 1 for npfs with decision 2's replacement flush, add the syncs from
  task 1, and update the npfs, adapter and mutation contracts. The proposal
  settles journal-capacity commits during large operations and how crash
  recovery is checked.
  Done 2026-10-18 as decided below; the history save syncs its file. Behavior is
  in the [adapter](../devices/filesystem-native-adapter.md#writeback-recovery-and-errors)
  and [mutation](../interfaces/filesystem-mutations.md) contracts; QEMU results
  and crash checks are in the
  [record](../development/experiments/explicit-durability-baseline.md#after-task-2).
  File writeback still committed per file until task 2b.
- [x] **2b. File writeback in the running transaction.** Implement the
  [accepted decisions](#writeback-in-the-running-transaction): writeback stages
  instead of committing, replacing rename and shrink rely on ordered commits,
  and allocation protects durable frees and staged metadata homes.
- [x] **3. Batched FAT32 ordering.** Implement decision 1 for the mounted FAT32
  writer: batches applied phase by phase at a checkpoint, no reuse of clusters
  freed in an unfinished batch, and the installer syncing at publication. Update
  the FAT32 mount and writer contracts. Allocation stays at mutation time, as
  accepted below; per-file delayed allocation is deferred. Qualification is in
  the [record](../development/experiments/fat32-batched-ordering.md).
- [ ] **4. Measurements.** Repeat task 1's workloads after each change and record
  them, distinguishing QEMU from native runs.

## Task 4 measurement scope

Keep the recorded adjacent comparisons distinct: the earlier task-2/2b runs used
VM-local Btrfs; task 3 used shared FUSE. Their wall times are not one matched
four-state table. Existing Update pairs have three samples; earlier extraction
controls have only one. The recorded task-2b samples predate the later staged-home
reuse correction and must keep their actual revision label.

Recover published artifacts for pre-task-2 `a29372f8`, task 2 `7ffc03f6`, and
corrected task 2b `89c86f74`. Use coherent archive/SDK groups and fixed payload
bytes within each comparison. The pre-task-3/after-task-3 code pair is `19c3cfc8`
and `3c3738af`; its saved kernels and common archive are retained. Repeat fresh
Update and hosted-extraction fixtures, three samples per state, serially on the
same declared backing. Record host load, completion plus explicit sync, device
counts, checked contents and stopped-image layout. No profiler during timing.
Preparation, source identities and the native capture profile are in the
[task-4 record](../development/experiments/explicit-durability-measurements.md).
Older host results remain labelled with their original configuration.

The original extraction archive/VM fixtures are unavailable here. A replacement
hosted archive must be identified by hash/counts and used unchanged on both sides;
its times cannot be substituted into the earlier payload's table. No new
benchmark infrastructure or kernel probe is needed.

**Native choices accepted by the owner, 2026-10-19:**

1. Use the disposable 32 GB SanDisk (owner substitution/erase consent,
   2026-10-19; supersedes the green 64 GB choice), for matched native comparisons with a legacy-compatible
   restored fixture. Never send historical writers to an installed pool whose
   hard-links feature state is unknown.
2. Use labelled, never-merged installer timing builds. Stock Install init does
   not configure IPv4, while ordinary UDP logs need an activated interface with
   an address. Reuse the installer's existing phase clock readings, retain
   bounded results and print a summary after completion. Apply the same patch
   to compared callers; no kernel probe, new network grants or per-write output.

**Native plan superseded, 2026-10-19:** the owner requests a fully hands-free
ThinkPad/Fedora loop, controlled off-target, instead of the held hands-on sheets.
The owner accepted the [harness](../development/experiments/durability-native-harness.md)
decisions 1–3: four states/two samples with early regression stops, private plan
replay and durable scratch-ESP results. Build/QEMU qualification is in progress;
native chunks run only when scheduled around the higher-priority Wi-Fi work.
The prior vela suggestion is superseded. Task 4 remains incomplete.

## Task 3 accepted scope

Both details accepted by the owner, 2026-10-19. Pre-code inspection baseline:
Pyxis `19c3cfc8`, fs `d2fb97c5`. The matched fixture was rebuilt locally because
opus2's VM-local fixtures are unavailable. Baseline and implementation results
are in the [record](../development/experiments/fat32-batched-ordering.md).

**Implementation scope.** Retain bounded prepared sector images across calls,
with one coherent pending view for reads, lookup and validation. Copy producer
bytes before returning; pending state never borrows a syscall payload or reusable
operation workspace. Keep the sole BSP filesystem worker and existing mount
rights, intervals, failure retention and clean-power ownership. Publish fs changes
before the parent pin; no public capability ABI change is expected.

Use a provisional 4 MiB per-mount pending-image target, charged with indexes and
retained backing under the mount budget; this is tunable implementation policy.
Reserve a whole operation before logical publication. If it cannot fit, checkpoint
first; an individually larger operation uses a single ordered checkpoint under
its existing operation budget, without splitting it or imposing a new file limit.
Checkpoint at explicit sync, the existing background interval, pressure, retirement
and clean power. Close remains release. A distinct-victim replacing rename first
checkpoints the source's contents; a mount-wide checkpoint suffices.

Preserve durable MARK and pending-mirror repair before user effects, followed by
DATA, FAT additions/links, DIRECTORY, **RECLAIM**, FSINFO, with a flush after each
nonempty phase. RECLAIM remains after DIRECTORY so shrink cannot free a tail still
named by the durable length. Batch-freed clusters stay unavailable until the
whole checkpoint succeeds. Merge phase images from cumulative changes, or
checkpoint before a conflicting edit: an earlier RECLAIM image must not overwrite
a later allocation/link in the same FAT sector. Uncertain I/O permanently stops
the instance and retains complete batch backing and owned diagnostics.

**Owner decisions accepted, 2026-10-19:**

1. **Allocation timing: reserve clusters during each mutation; defer
   per-file delayed allocation.** Existing chains map live bytes immediately.
   Delayed allocation would also need unmapped dirty ranges, checkpoint-time
   allocation failure and new read/resize rules. This task batches barriers and
   images while preserving allocation failures at the requesting call. It makes
   no promise to improve interleaving/fragmentation between growing files.
2. **FAT32 crash guarantee: preserve the mounted writer's existing
   dirty-volume/external-repair limits.** The milestone's blanket statement that
   a crash never leaves inconsistent metadata is too strong for unjournaled
   FAT32: interruption between FAT and DIRECTORY can leave lost/overlong chains,
   and rename spans sectors. Task 3 preserves ordering and reuse safety, not
   atomic multi-sector publication or replay. Qualify selected interrupted
   checkpoints with host inspection/fsck diagnostics, and verify complete bytes
   after completed sync; do not claim every crash image is clean or replayable.
   Stronger recovery would require separately approved scope.

Validation: repeated matched staging/grow/overwrite workloads on matched disk-image
backing, ordinary fs/kernel/SDK builds, interactive one-/four-CPU QEMU and GDB
inspection of phase boundaries, shrink/reuse, replacement and close lifetimes.
Task 4 repeats the broader measurements automatically after task 3 merges.
No native staging or media access is authorized by this scope note.

## Task 1 audit

Audited at main `82f9387d` (userland `3677edd2`, ports `33661036`, fs
`25337999`). Paths start at the pyxis-os root, submodules included. Every
FILE or DIRECTORY SYNC on npfs is pool-wide today: it flushes every dirty file
in the pool and commits (`kernel/fs/npfs.c:756` calls the store's `npfs_store_sync`). MOUNT SYNC
covers the configured disk's pools or the FAT32 instance.

**Needs a change.** Only one caller relies on per-call durability without
syncing:

| Caller | Mutations | Relies on | Needed |
| --- | --- | --- | --- |
| Saved shell history, `userspace/shell/history.c:149-213` | create `.history.HEX`, write, close, replacing rename | the rename being durable on return; the [shell guide](../userland/shell.md) promises a crash loses at most the line being saved, with no sync | a sync after the rename (decision 3, [C](#task-2-decisions)); done in task 2 |

**Already explicit; unchanged by tasks 2 and 3.** These sync at each
publication point, so they stay correct, and get cheaper, once mutations wait
for a checkpoint:

| Caller | Mutations | Sync points |
| --- | --- | --- |
| Installer program stage, `userspace/installer/programs.c` | `bin` volume create (752), revision directory (778), rerun clear (440-500), per-file create/write (511) | `directory_sync` after copying (808) and after the manifest (843); relies on directory sync flushing every copied file, so sync must stay pool-wide for directories |
| Installer old revision cleanup, `userspace/installer/programs.c:886-935` | removes about 2,400 entries per old revision | one `directory_sync(root)` at the end (933) |
| Installer home volume, `userspace/installer/executor.c:283` | `disk_create_volume` | covered by the program stage's pool-wide sync, which precedes the ESP switch |
| Installer classic ESP replacement, `userspace/installer/esp_activate.c:208-229` | fixed-path files grown in 1 MiB `file_transfer` calls, resize | `file_sync` per file, `directory_sync` on five parents, `mount_sync` |
| Installer revision and `limine.conf`, `userspace/installer/esp_activate.c:330-442` | create/write/resize two files | `file_sync` each (so the revision is durable before `limine.conf` is written), two `directory_sync`, `mount_sync` |
| Installer coexist attempts, `userspace/installer/esp_payload.c:171-455` | attempt directories, payload files, manifest | `file_sync` per file (446), `mount_sync` (455) |
| Configuration editor, `userspace/config-editor/storage.c` | staged creates (355), replacing renames (408), removes (668) on npfs and the ESP | `file_sync` per staged file, `mount_sync` after each publication phase (533, 601, 618, 636, 676) |
| `xfer receive`, `userspace/xfer/main.c:294-417` | stage create, replace/no-replace rename | `file_sync` before the rename, `directory_sync` after it |
| Hosted payload publication, [native Clang](../development/hosted-tools.md#manual-immutable-publication) | `mkdir`, `tar xf` (about 9,200 creates), `mv` | `sync` of `.staging`, then of the revision and `toolchain://` |
| Machine settings, [convention](../userland/machine-settings.md) | ordinary file writes | the documented `sync PATH` before reboot |
| Ports saving by replacement: BusyBox vi `ports/busybox/busybox_pyxis.c:585-591`, Quake `ports/quake/sys_pyxis.c:203-209`, Neovim through `ports/libuv/pyxis/fs.c:220` | temporary file, `fsync`, rename (Neovim truncates and writes) | `fsync` before the rename; the rename itself becomes durable at the next checkpoint, as on other systems |

**No durability requirement.** `cp` (`userspace/cp/copy.c`,
`userspace/cp/tree.c`) and `screenshot` (`userspace/screenshot/main.c`) replace through a temporary name
and already document no crash durability; decision 2 keeps their contents safe.
Libc `rename`, `remove`, `mkdir` and `O_TRUNC` opens
(`userspace/libc/file_open.c`, `userspace/libc/descriptor.c`) keep ordinary
semantics with `fsync` available. Kilo's create/truncate/write save documents no atomic
replacement. Shell redirection truncation (`userspace/shell/launch.c:1994`,
`userspace/lib/resource.c:137`), `iobench`, `acpidump`, `hello`, `mkdir` and
BusyBox `tar` need no promise of their own. Boot-init HTTP staging, job work
directories and capture files use RAM volumes.

**FAT32 users outside the installer.** Shell RW mounts
(`userspace/shell/fat32.c`) and `fat32ctl` already offer `sync --mount` and retirement, and the scratch
writer (`userspace/init-fat32-mount`) has `sync`/`hsync`. Today every copy to
such a mount is durable on return; after task 3 it is durable at sync,
retirement or power-off. The [FAT32 mount guide](../devices/fat32-mounts.md)
must then tell users to sync or release before removing media.

**fs repository.** No guest callers. `fs/fat32/mounted.c:1179` is the per-call
DATA/FAT/DIRECTORY/FSINFO phase loop that task 3 batches; the host tools are
separate writers.

**Contracts to rewrite.** Task 2:
[filesystem mutations](../interfaces/filesystem-mutations.md) ("reach durable
COMMITTED before returning"), [directories](../interfaces/directories.md)
(namespace mutations and replacing rename), the
[adapter](../devices/filesystem-native-adapter.md#writeback-recovery-and-errors),
the [shell guide](../userland/shell.md) history bullets and the
[technical-debt entry](../technical-debt.md). Task 3:
[FAT32 mounts](../devices/fat32-mounts.md) and `fs/docs/fat32-mounted.md`.
Kernel power-off/restart already flushes and checkpoints every pool
(`kernel/fs/npfs.c:880`); task 2 keeps that covering the running transaction.

## Task 2 decisions

Accepted by the owner 2026-10-18. Fixed by the decisions above: create, remove,
rename and shrink stage their metadata images in the pool's running transaction
and return; a replacing rename still flushes the moved file first; the
transaction commits at `sync`/`fsync`, on the 30-second background interval,
under pressure and at power-off or restart; the history save gains a sync.

- **A. Commits forced by journal capacity.** A transaction holds at most 128
  metadata images (one descriptor block and the pool's scratch buffer). Before
  staging, an operation reserves its worst-case image count; if the running
  transaction cannot take it, the worker commits and checkpoints that
  transaction first, inside the call. Operations are never split across commits,
  so recovery always lands between whole operations. Allocating operations also
  reserve their worst-case block count, committing and checkpointing first if
  the conservative allocatable count is short while the transaction holds frees.
  Previously durable allocations and staged metadata homes await durable EMPTY. Reclamation of removed files (`cleanup_one`) joins the running
  transaction with its current per-step bounds. The cap stays 128.
- **B. Crash recovery checks.** Abrupt `SIGKILL` of the QEMU VM at about three
  points during each of the cleanup and extraction workloads, plus right after
  an unsynced replacing rename and right after an explicit sync. Each stopped
  disk passes host `fsck.npfs --replay` and `npfs-inspect` checks: every name
  belongs to a whole operation, synced files match their source, and a replaced
  file holds the old or the new contents, never an empty or partial file. No
  per-flush harness.
- **C. File sync scope.** FILE SYNC flushes that file's data and then commits the
  running transaction, leaving other files' cached data for writeback.
  DIRECTORY and MOUNT sync stay pool-wide. The history save syncs its file after
  the rename.

## Writeback in the running transaction

Proposed after task 2 and accepted by the owner 2026-10-18 with decisions A-C
below; implemented as task 2b.
Task 2 left one commit per written file: when the 4 MiB data cache fills, every
dirty file is written back and each `flush_inode` step commits its mapping and
size on its own, then the next step checkpoints it (about four flushes per
file). That is now nearly all of the remaining cost
([after task 2](../development/experiments/explicit-durability-baseline.md#after-task-2)):
about 36,950 flushes to extract the hosted payload and 9,060 to copy a program
revision.

**What changes.** A writeback step still allocates the dirty blocks, writes the
data to their homes and stages the inode's mapping images and new size, but it
no longer commits. The running transaction then commits at the existing points:
`sync`/`fsync`, the 30-second interval, memory pressure, power-off or restart,
and when the next operation does not fit. FILE sync keeps its task 2 meaning
(write back that file, then commit). Nothing else changes in the format, the
cache or the sync contracts.

**Ordering rules.**

- *Data before the metadata that references it.* Each data write completes
  before its step returns (`transport` waits), and every commit flushes the
  device before it writes the COMMITTED control. Data reaching its home before
  the commit is therefore durable no later than the mapping and size that
  expose it. A staged size only ever covers written blocks, as today.
- *No stale data exposure.* A mapping can only name a block that was free when
  the step allocated it and that the step wrote before staging. If the step
  fails, its undo drops the mapping and allocation, so the written block is free
  again. If the transaction never commits, the committed bitmap still shows the
  block free and no committed mapping names it.
- *Allocation after a free.* A block allocated in the checkpointed bitmap
  cannot be refilled before durable EMPTY, so a crash cannot expose another
  file's data under its durable old owner. A staged metadata home is also
  excluded, including one allocated and freed within this transaction: its
  retained journal image must not overwrite another owner's data.
- *Overwrites in place.* A block that is already mapped is rewritten in place,
  as today; staging changes nothing about torn single-block updates.

**Bounds.** No new memory: steps keep the 128-image transaction, the
ten-image-per-entry reservation and task 2's undo pages. A small file costs one
shared inode block plus shared bitmap and directory images, so a transaction
holds hundreds of files; a large file needs one indirect image per 512 blocks
(2 MiB), so a commit covers roughly 200 MiB of one file. The data cache and its
4 MiB bound are unchanged.

**Expected effect, by inspection.** Commits become bounded by capacity and the
interval rather than by file count. For the payload (an estimated 1,300
distinct metadata blocks over the run) that is a few dozen capacity commits plus one per
30 seconds: a few hundred flushes instead of about 36,950, and metadata writes
drop with them because each commit writes its images twice. Copying a program
revision should fall to a similar few hundred. Data writes are unchanged. To be
measured, not promised.

**Risks.**

- Data written back in the background becomes durable at the next commit, up to
  the 30-second interval later, instead of right after writeback. The accepted
  model already promises only `sync` and the interval; `close` promises nothing.
- With decision B's default, freed space becomes allocatable only after the
  next checkpoint, so a nearly full pool may commit early to reclaim it.
- A single `sync` may now commit more staged work at once, bounded by one
  transaction.

**Decisions (accepted 2026-10-18).**

- **A. Replacing rename and shrink.** They write back the target and
  stage its mapping before staging the rename or smaller size, without a
  separate commit. Commits are ordered, so the new contents are durable no later
  than the name (decision 2 still holds) and a replacing save stops paying its
  own commit.
- **B. Freed blocks.** Allocate only blocks free in both the checkpointed
  bitmap and staged bitmap pages, and exclude homes with staged metadata images.
  Before staging, reserve the operation's worst-case allocation. If the
  conservative free-block count, excluding this transaction's frees, cannot
  cover that reservation, commit and checkpoint first, then begin the operation.
  There is no retry of a partially staged allocation.
- **C. Validation.** Task 2's QEMU workloads and crash checks, plus a
  stale-data check: remove a synced file with known contents, write new files
  without syncing, kill, and confirm no file shows the removed contents.

## Related

- [Technical debt: npfs namespace flushes and USB write throughput](../technical-debt.md#usb-stick-write-throughput)
- The owner's wish to revisit "immediate committed removal" (2026-10-17) is
  covered by task 2.

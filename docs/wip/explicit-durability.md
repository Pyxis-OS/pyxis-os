# Explicit durability for namespace and FAT32 mutations

Status: **planned by the owner 2026-10-17, started 2026-10-18.** Task 1 is
done; the [task 2 proposal](#task-2-proposal) awaits the owner. The three
decisions below are accepted.
Each task still starts with a short proposal of its open details; nothing here
authorizes code beyond the task being worked on.

## Goal

Finish the durability model the owner chose on 2026-10-03: `fsync` and `sync`
are the explicit durability points and block until the data is synchronized,
close only releases the handle, a background task flushes old dirty state (30
seconds by default), memory pressure writes back early, and power-off and
restart flush everything. Journal commits happen as required to keep metadata
recovery guaranteed.

npfs file contents already follow this model
([filesystem adapter](../devices/filesystem-native-adapter.md#writeback-recovery-and-errors)).
Two paths still make every call durable before it returns:

- **npfs namespace and size operations.** Create, rename, remove and shrink each
  commit a pool transaction to durable COMMITTED: four device flushes per call.
  The [#348 review](../development/experiments/npfs-namespace-writeback/README.md)
  kept them immediately durable while removing unrelated file flushes. Removing an
  old program revision (about 3,000 entries) costs about 12,000 flushes.
- **The mounted FAT32 writer.** Every successful mutation runs its ordered DATA,
  FAT, DIRECTORY and FSINFO phases with a flush after each before returning
  ([FAT32 mounts](../devices/fat32-mounts.md)). An Update stages its boot archive
  in 1 MiB growing writes, about 1 MiB/s on a USB stick.

Both costs are recorded in [technical debt](../technical-debt.md).

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
   checkpoint, nominally the background interval, as on other systems; it never
   leaves inconsistent metadata.

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
- [ ] **2. npfs namespace operations in the running transaction.** Implement
  decision 1 for npfs with decision 2's replacement flush, add the syncs from
  task 1, and update the npfs, adapter and mutation contracts. The proposal
  settles journal-capacity commits during large operations and how crash
  recovery is checked.
- [ ] **3. Batched FAT32 ordering.** Implement decision 1 for the mounted FAT32
  writer: batches applied phase by phase at a checkpoint, no reuse of clusters
  freed in an unfinished batch, and the installer syncing at publication. Update
  the FAT32 mount and writer contracts. The proposal also considers delayed,
  per-file cluster allocation at the checkpoint, so concurrent growing files are
  not interleaved (an open detail, not a decision).
- [ ] **4. Measurements.** Repeat task 1's workloads after each change and record
  them, distinguishing QEMU from native runs.

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
| Saved shell history, `userspace/shell/history.c:149-213` | create `.history.HEX`, write, close, replacing rename | the rename being durable on return; the [shell guide](../userland/shell.md) promises a crash loses at most the line being saved, with no sync | a sync after the rename (decision 3); see [proposal decision C](#task-2-proposal) for its cost |

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

## Task 2 proposal

Proposed 2026-10-18, awaiting the owner. Fixed by the accepted decisions:
create, remove, rename and shrink stage their metadata images in the pool's
running transaction and return; a replacing rename still flushes the moved file
first; the transaction commits at `sync`/`fsync`, on the 30-second background
interval, under pressure and at power-off or restart; the history save gains a
sync. The [baseline](../development/experiments/explicit-durability-baseline.md)
shows why the per-call path is costly: about 8 flushes per removed entry,
because each removal commits and each removed file's reclamation commits again.

**A. Commits forced by journal capacity.** A transaction holds at most 128
metadata images (one descriptor block, and the pool's scratch buffer). Default:
before staging, an operation reserves its worst-case image count; if the running
transaction cannot take it, the worker commits and checkpoints that transaction
first, inside the call. Operations are never split across commits, so recovery
always lands between whole operations. An allocation after a free in the same
transaction does the same, keeping the existing rule that freed blocks are not
reused before durable EMPTY. Reclamation of removed files (`cleanup_one`) also
joins the running transaction, keeping its current per-step bounds. The cap
stays 128; raising it is a separate memory and format question. Removing an old
revision should fall from about 19,000 flushes to a few hundred. Alternative:
also raise the cap now.

**B. How crash recovery is checked.** Default: manual abrupt stops in QEMU
(`SIGKILL` of the VM) at about three points during each of the cleanup and
extraction workloads, plus right after an unsynced replacing rename and right
after an explicit sync. Each stopped disk then passes host `fsck.npfs --replay`
and `npfs-inspect` checks: every name belongs to a whole operation, synced files
match their source, and a replaced file holds the old or the new contents, never
an empty or partial file. No new test infrastructure. Alternative, needing your
explicit authorization: a host harness that cuts the disk at every flush
boundary of a recorded run and checks each cut, which covers far more crash
points but is new test tooling.

**C. What the history sync flushes.** FILE and DIRECTORY sync are pool-wide
today, so a sync per saved line would also write back every unrelated dirty
file, so the next command would also wait for whatever an earlier large copy
still has cached. Today's history save flushes only the history file and
commits. Default: FILE SYNC flushes that file's data and then commits the
running transaction (every namespace operation so far), leaving other files'
cached data for background writeback; DIRECTORY and MOUNT sync stay pool-wide,
because the installer relies on a directory sync flushing all copied programs.
The history save keeps its handle, renames, then syncs that file: about today's
cost per line. No audited caller relies on FILE SYNC flushing other files.
Alternatives: keep sync pool-wide and accept the wait, or leave history unsynced
and document losing lines since the last checkpoint.

## Related

- [Technical debt: npfs namespace flushes and USB write throughput](../technical-debt.md#usb-stick-write-throughput)
- The owner's wish to revisit "immediate committed removal" (2026-10-17) is
  covered by task 2.

# Explicit durability for namespace and FAT32 mutations

Status: **planned by the owner, 2026-10-17; starts after the
[clangd milestone](clangd-neovim.md).** The three decisions below are accepted.
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

- [ ] **1. Audit and baseline.** List every in-tree caller that depends on a
  namespace or FAT32 mutation being durable on return, with the sync each one
  needs. Capture matched baselines before any change: old revision cleanup and a
  hosted payload extraction on npfs, and Update staging on FAT32, in QEMU and on
  a stick.
- [ ] **2. npfs namespace operations in the running transaction.** Implement
  decision 1 for npfs with decision 2's replacement flush, add the syncs from
  task 1, and update the npfs, adapter and mutation contracts. The proposal
  settles journal-capacity commits during large operations and how crash
  recovery is checked.
- [ ] **3. Batched FAT32 ordering.** Implement decision 1 for the mounted FAT32
  writer: batches applied phase by phase at a checkpoint, no reuse of clusters
  freed in an unfinished batch, and the installer syncing at publication. Update
  the FAT32 mount and writer contracts.
- [ ] **4. Measurements.** Repeat task 1's workloads after each change and record
  them, distinguishing QEMU from native runs.

## Related

- [Technical debt: npfs namespace flushes and USB write throughput](../technical-debt.md#usb-stick-write-throughput)
- The owner's wish to revisit "immediate committed removal" (2026-10-17) is
  covered by task 2.

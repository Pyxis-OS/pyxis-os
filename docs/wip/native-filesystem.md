# Simple native filesystem

Status: **owner direction, 2026-10-02.** This restarts writable filesystem
work. It supersedes the pyxis-fs writer plans listed under
[what stops](#what-stops).

- **Accepted:** [owner decisions](#owner-decisions), [v1 answers](#v1-answers),
  [installation and authority](#installation-and-authority),
  [code, file layout and order](#code-file-layout-and-order) and
  [focused tasks](#focused-tasks). The completed
  [format decisions](native-filesystem-format.md#decision-status) record accepted
  policies and the implemented task-2 format/tool boundary (2026-10-03).
- **Accepted task-3 policies:** [writeback details](#accepted-writeback-details).
- **Proposal awaiting the owner:** the [working method](#proposed-working-method).
- **Not requirements:** [later ideas](#later-ideas).

## Owner decisions

1. **Start again with a deliberately simple filesystem.** Aim for ext2-class
   structure, not a complete design. Pyxis has no users yet, and it doesn't need
   to know what it will look like in a week. Simple structures also keep writes
   and latency low. New capability is added later through reserved bytes and
   feature flags in the on-disk structures, which the format must carry from the
   start.
2. **Native to Caelum.** The kernel owns mount state, caching and the writer,
   and uses Caelum's memory management. Persistent in-memory state between calls
   is expected, not avoided.
3. **The shared library covers the format only.** It holds on-disk structure
   definitions, encoding and decoding, and perhaps small allocation helpers. It is
   used by host tools (`mkfs`, `fsck`, inspection) and by Caelum. Whatever the
   library needs from its environment is an external symbol that the host or
   Caelum defines at link time. There are no function-pointer callback tables.
4. **No users or permissions in the filesystem for now.** Access control stays
   with capabilities. A permission model waits until Pyxis boots from its own
   filesystem and there is a concrete need.
5. **Wear budget: no faster than one SSD every six months.** For the ThinkPad's
   256 GB SK hynix NVMe, SMART reports 17.38 TB written at 2% used. Because that
   percentage is a whole number, it implies roughly 580–870 TB of endurance, so
   six months allows at least about 3.2 TB per day. Recompute from the device's
   own SMART data rather than treating this figure as fixed. See the
   [T14 inventory](../targets/t14-gen1-amd/notes.md).
6. **Latency and bytes written are measured from the first implementation,**
   because the previous design failed on latency first.

## Why

The pyxis-fs writer was designed as a portable, stateless library. Each
publication reloads and validates the whole allocation map, then regenerates the
complete candidate. In the instrumented RAM matrix, measured workload time per
logical operation was 13–80 ms, against 0.12–0.16 ms for Btrfs, and grew 3–5x from
1 MiB to 20 MiB of stored data. That figure is window time divided by operations,
including the host failure simulator, not a per-call latency distribution.
That is the [overflow-split record](https://git.internal/PyxisOS/pyxis-fs/src/commit/810d2af66d0281e2d8a3e8a396a041f4232f2ce9/docs/overflow-split-measurements.md)
at filesystem `a250731`. Write volume was already below Btrfs, but at that
latency every synchronous write would stall the system.

The accumulated plans also no longer reliably record the owner's intent. Some
were accepted in bulk during long decision sessions. Rescoping them would cost
about as much as starting over.

## What stops

- No further portable pyxis-fs writer work: no write-efficiency, latency or
  map-planner tasks, and no further measurement matrices. The six superseded
  planning documents were removed with task 3; Git retains their history.
  The obsolete filesystem docs and measurement records were also removed;
  Git retains the [evidence for the restart](https://git.internal/PyxisOS/pyxis-fs/src/commit/810d2af66d0281e2d8a3e8a396a041f4232f2ce9/docs/overflow-split-measurements.md).
- The pyxis-fs writer is never run against a real disk.
- The [native mounts](../devices/native-readonly-filesystem.md) now use the new
  format and kernel writer; the portable core and obsolete tools are retired.

## V1 answers

Accepted 2026-10-02; durability revised by the owner on 2026-10-03.

1. **Durability is explicit.** `fsync` (one file) and `sync` (everything) are the
   durability points: the caller blocks until the data is on the disk. A plain
   `close` only releases the handle and promises nothing. Otherwise, data is
   written back from the cache:
   - **periodically,** by a kernel background task that flushes sufficiently old
     dirty data at an interval set by a constant (30 s or 60 s);
   - **under memory pressure,** by writing dirty cached data early to reclaim RAM;
   - **on clean shutdown, restart and sleep,** by flushing everything dirty, once
     those exist (Pyxis has none of them yet).

   Journal commits happen as needed to keep metadata recoverable. After a crash,
   journal recovery restores metadata consistency; data not yet written back may
   be lost.
2. **Crashes are handled by a metadata journal with ordered data,** as in
   ext3/ext4.
3. **V1 is a native Pyxis installer.** It runs from a live image holding
   Limine, Caelum, its boot archive and a RAM-backed scratch filesystem. The
   image can be an ISO (burned or written to a disk), a thumbdrive or a QEMU
   image; v1 does not depend on any one of them. Limine loads everything into RAM,
   so the installer reads its source files from the boot archive and needs no
   driver for the live medium. It contains or launches tools that:
   - create a GPT on an empty target disk;
   - create the Pyxis partition or partitions;
   - install the files needed to boot Pyxis from that disk.

   Afterwards the target boots on its own.
4. **Storage is a pool with growable volumes.** A partition holds one pool, and
   the allocation bitmap (or whatever replaces it) covers the whole pool. Volumes
   are virtual and grow inside the pool; they are not partitions sized up front.

## Accepted writer policies

The owner accepted these task-3 policies during the
[format review](https://git.internal/PyxisOS/pyxis-os/pulls/338), 2026-10-03:

- Build at least an in-memory free-inode list at mount and maintain it as inodes
  are allocated/reclaimed. Do not scan the whole inode file on each creation.
- `fsync`/`sync` return once all required data and the covering COMMITTED records
  are durable. Checkpointing and EMPTY publication continue in the background,
  before the next commit. A request spanning batches waits for all required
  batches. The whole-current-transaction policy was subsequently accepted below.

These policies are implemented by the kernel writer, alongside the codecs and host tools.

## Accepted writeback details

Accepted by the owner for task 3 on 2026-10-03:

1. **Periodic full flush.** One background task flushes all dirty data every T
   seconds, without per-page age tracking. T defaults to **30 seconds** and is
   configurable through **menuconfig**. T is nominal, not a bound on crash loss;
   a pass can take longer under load. Only `fsync` and `sync` guarantee durability.
2. **Whole-current-transaction `fsync`.** Flush the requested file's dirty data
   and commit the pool's current metadata transaction, satisfying every ordered
   data dependency. File contents/size, mappings and the directory entry become
   durable together. A small save can wait for unrelated I/O from other files.
3. **Delayed allocation.** Assign physical disk blocks when cached data is written
   back. This improves batching and reduces coupling between files, but cannot
   remove dependencies already in a transaction. A cached write can succeed and
   later encounter insufficient disk space during writeback or synchronization.

Memory-pressure writeback arrives with the cache and its memory-management hook.
The installer must call `sync` before reporting success; Kilo's save should later
call `fsync`. The kernel writer implements these policies; clean shutdown,
restart and sleep flushing remain deferred until those operations exist.

Accepted mount authority and lifetime policies, 2026-10-03:

- Native mount configuration selects a GPT disk GUID without an on-disk
  principal. `MOUNT_RIGHT_WRITE` separately authorizes requesting mutation rights
  on a root; read-only file and directory grants remain attenuated.
- `MOUNT_SYNC` requires mount WRITE and synchronizes every mounted pool on the
  capability's configured disk. Existing file and directory sync calls remain
  available through their own capabilities.
- Last-handle close releases the process's cleanup charge and makes no durability
  promise. The mounted pool retains dirty data and writeback errors for later
  synchronization. Failed writeback does not discard cached contents.
- FILE_SIZE accepts READ or WRITE, including write-only append/end-relative seek.
- Sync reports and acknowledges each retained recoverable writeback error. Later
  sync can succeed after dirty data is durable; ongoing failures still fail each
  attempt. Uncertain disk I/O stops mutation and remains visible until reboot.

## Pool format and host tools

The [format decisions](native-filesystem-format.md) record the accepted shape:
one bitmap, 64 volumes with growable inode files, block-pointer mappings, simple
directories and one metadata journal. The [implemented encoding](../../fs/docs/native-format.md)
and [host tools](../../fs/docs/native-host-tools.md) live in pyxis-fs; the
old core and tools are retired. V1 checksums only headers and journal; read-only opening refuses a
committed journal and writable fsck replays it. Journal capacity is chosen per
pool, starting at at least 128 MiB for the 256 GB target. Volume starvation remains
deferred: working first, space policy later. Caelum's native mounts use the new
format through its own inode/cache/writer engine.

The native installer makes Pyxis userspace the third link-time symbol provider
for the format library, alongside the host and Caelum.

## Installation and authority

Accepted 2026-10-02.

1. **Raw-disk authority.** The kernel grants a new whole-disk write capability
   only to trusted init. Init passes it only to the installer, and only when the
   installer is launched to install.
2. **Target disks.** V1 supports whatever writable disk driver Caelum has. Today
   that is a QEMU virtio-blk disk. [USB mass storage](usb-installation.md) is
   developed in parallel, and a USB target follows when it lands; it is not a
   prerequisite. NVMe comes later.
3. **Installer steps.** On an empty target disk the installer:
   1. creates a GPT;
   2. creates the boot partition (a FAT32 EFI system partition, written fresh,
      with no general FAT driver), because firmware boots from FAT and Limine never
      reads a Pyxis pool;
   3. copies Limine to it and writes its boot configuration;
   4. copies Caelum and the boot archive to it;
   5. creates the pool partition, formats the pool and creates its volumes.

   V1 ships a fixed set of standard inits. Programs still run from the boot
   archive; the pool holds persistent volumes mounted after boot.

## Code, file layout and order

Accepted 2026-10-02.

1. **Repository.** Reuse the pyxis-fs repository. The new format library starts
   beside the old core, and the old core is deleted once the native read-only
   mounts move to the new format. Git keeps the history.
2. **Files use block pointers,** with ext2-style direct and indirect pointers.
   Each inode carries a mapping-type field whose only defined value in v1 is
   "block pointers". Extents can later be added behind a feature flag, for example
   for large copied files, without changing the format. Supporting both mappings,
   and converting between them, is not v1 work.
3. **Build order** is the [focused task](#focused-tasks) sequence below.

## Focused tasks

1. [x] **Format proposal.** A short design of the on-disk format: pool header,
   bitmap, volume table, inodes, directories and journal, with reserved bytes and
   feature flags. Owner decisions go at most three per round. The
   [format decisions](native-filesystem-format.md) retain accepted choices;
   task 2 implements the format details.
2. [x] **Format library and host tools.** Structure definitions, encoding and
   decoding with link-time symbols, plus host `mkfs`, `fsck` and inspection.
   Implemented in [pyxis-fs #26](https://git.internal/PyxisOS/pyxis-fs/pulls/26);
   [tool guide and validation](../../fs/docs/native-host-tools.md). Committed replay
   has an independent [post-merge review](../development/native-filesystem-replay-review.md)
   covering host and kernel recovery; the task-3 kernel writer produces normal
   COMMITTED/checkpoint transactions. Broader crash qualification remains separate.
3. [x] **Native read/write in Caelum** on virtio-blk, with persistent in-memory
   state and caching. Implemented in
   [parent #348](https://git.internal/PyxisOS/pyxis-os/pulls/348);
   [measurements](../development/experiments/native-filesystem-task3/README.md)
   record latency, bytes and ordinary validation from the start. Final owner
   policies are implemented; crash injection and physical-media qualification
   were not assigned.
4. [ ] **Installer tools:** GPT creation, the FAT32 boot partition, pool
   formatting and copying files from the boot archive.
5. [ ] **End to end:** install and boot in QEMU, then on the ThinkPad.

## Task-3 delivery

The task branch is `fs/native-writer`, based on main `132aef1`, with
[parent #348](https://git.internal/PyxisOS/pyxis-os/pulls/348). Main `d04c6a6` was
merged during delivery, followed by main `e7f389e`, to preserve concurrent
completed work. It uses published
[filesystem #27](https://git.internal/PyxisOS/pyxis-fs/pulls/27) at `caf8edc`
and [userland #104](https://git.internal/PyxisOS/pyxis-userland/pulls/104) at `d15d782`,
which preserves current userland main alongside the native changes.
Merge dependencies before the parent PR. The kernel owns cached data, delayed
allocation, journal commit/replay/checkpoint and bounded cleanup; the old shared
core and tools are retired. No compiler-container rebuild is needed.

The owner accepted both final refinements: FILE_SIZE through READ or WRITE, and
one-time acknowledgment of retained recoverable errors by sync. The measurement
record holds builds, QEMU/GDB observations, persistence and host checking results.
The final ordinary build and QEMU boot, sync, writable INFO, host fsck and file
comparison passed. Exact submitted-revision CI is reported with the PR. Runtime
crash injection and physical-media qualification were not assigned. All task
validation clients, QEMU and debugger processes have been stopped.

PR review found per-candidate disk bitmap reads that caused writeback timeout on
a populated 1 GiB pool. The kernel now retains the full bitmap with staged journal
overlays, scans free words and folds changes only after durable EMPTY. The
[populated-pool review](../development/experiments/native-filesystem-task3/populated-pool-review.md)
records the reproduction, ordinary corrected workloads and memory cost. The six
superseded portable-writer plans and their active references were removed as
directed by the owner. The obsolete pyxis-fs docs and measurement JSON were
also removed; the current task-3 experiment directory remains unchanged.
Allocation now asserts that its transaction has freed no blocks. The guard
clears only after durable EMPTY or healthy transaction discard. An ordinary
four-CPU, 256 MiB nested-KVM boot verified repeated truncate/write/sync rounds,
unlink cleanup and subsequent allocation. GDB observed the flag set with a
COMMITTED cleanup transaction and cleared after successful checkpoint/EMPTY.
The stopped pool passed host fsck, and the extracted new file matched the
installed 1 MiB fixture. Healthy abort and uncertain-failure handling were
source-reviewed; no failure injection was added.

## Later ideas

Not requirements, and not v1. Recorded so they are not designed out:

- **An interactive installer or launcher** that lets the user choose which inits
  start which spaces.
- **Filesystem overlays,** for example a volume overlaid on the boot archive.
  "Root filesystem" was only an illustration; it is not a Pyxis name or design.

## Proposed working method

- The owner states requirements first, in short form. Agents work from them and
  never expand them on their own initiative.
- Decisions go at most three per round, each with a default, and any can be
  deferred. Agent proposals are never "agreed" by default. A decision accepted in
  bulk can be reopened at any time.
- Design documents stay short. A design that needs thousands of lines of
  justification is a signal to simplify it.

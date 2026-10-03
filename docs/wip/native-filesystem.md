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
- **Proposals awaiting the owner:** [writeback details](#proposed-writeback-details) and the
  [working method](#proposed-working-method).
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
That is the [overflow-split record](../../fs/docs/overflow-split-measurements.md)
at filesystem `a250731`. Write volume was already below Btrfs, but at that
latency every synchronous write would stall the system.

The accumulated plans also no longer reliably record the owner's intent. Some
were accepted in bulk during long decision sessions. Rescoping them would cost
about as much as starting over.

## What stops

- No further pyxis-fs writer work: no write-efficiency, latency or map-planner
  tasks, and no further measurement matrices. These plans are superseded and kept
  only as history:
  - [writable filesystem core](writable-filesystem-core.md);
  - [write efficiency](filesystem-write-efficiency.md);
  - [retirement debt](filesystem-retirement-debt.md);
  - [overflow split](filesystem-overflow-split.md);
  - [native persistent volumes](native-persistent-volumes.md);
  - [pool and persistent filesystem](persistent-storage.md).

  The owner decides separately whether to delete them now or when the
  replacement lands.
- The pyxis-fs writer is never run against a real disk.
- The [native read-only mounts](../devices/native-readonly-filesystem.md) keep
  working on the current format until the new filesystem replaces them.

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
  batches; this does not accept the separate whole-current-transaction policy.

These remain requirements for task 3, beyond the implemented codecs and host tools.

## Proposed writeback details

Proposals for the owner, refining the accepted durability model above.

1. **One dumb flush task.** Every T seconds, write all dirty data and commit the
   journal, with no per-page age tracking. T is the nominal writeback interval,
   not a bound on crash loss. Data written just after a pass waits for the next
   one, plus that pass's I/O and commit time, and longer under load. Only `fsync`
   and `sync` guarantee durability. Proposed T: 30 s, a named constant.
2. **`fsync` commits the whole current transaction.** It writes that file's
   dirty data, then commits the pool's single journal transaction. The file's
   size, block pointers and directory entry then become durable together. That is
   simpler for programs than POSIX, where a new file's name needs a separate
   `fsync` of its directory. The commit must satisfy every ordered-data
   dependency in the transaction. If another file's newly allocated blocks are in
   it, that data must reach the disk first, so a small `fsync` can wait for
   unrelated I/O.
3. **Delayed allocation.** Cached data gets disk blocks only when it is written
   back. In ordered mode, a commit must first write the data its new metadata
   points to. Allocating at write time puts every cached write into the
   transaction, so a small `fsync` waits for unrelated large writes (ext3's fsync
   stalls). Delayed allocation reduces that coupling, but cannot remove
   dependencies already in the transaction. Allocating at writeback also places
   files contiguously, which helps block pointers. If it complicates the first
   version, it can be deferred.

Notes for implementation: memory-pressure writeback needs a reclaim hook in
Caelum's memory management, so it arrives with the page cache. The installer must
call `sync` before reporting success. Kilo's save should later call `fsync`.

## Pool format and host tools

The [format decisions](native-filesystem-format.md) record the accepted shape:
one bitmap, 64 volumes with growable inode files, block-pointer mappings, simple
directories and one metadata journal. The [implemented encoding](../../fs/docs/native-format.md)
and [host tools](../../fs/docs/native-host-tools.md) live in pyxis-fs beside the
old core. V1 checksums only headers and journal; read-only opening refuses a
committed journal and writable fsck replays it. Journal capacity is chosen per
pool, starting at at least 128 MiB for the 256 GB target. Volume starvation remains
deferred: working first, space policy later. Caelum's old native mounts continue
using the old format; the new writer and mounts belong to task 3.

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
   [format decisions](native-filesystem-format.md) retain accepted choices and
   unresolved writer policies; task 2 implements the format details.
2. [x] **Format library and host tools.** Structure definitions, encoding and
   decoding with link-time symbols, plus host `mkfs`, `fsck` and inspection.
   Implemented in [pyxis-fs #26](https://git.internal/PyxisOS/pyxis-fs/pulls/26);
   [tool guide and validation](../../fs/docs/native-host-tools.md). Committed replay
   is source-reviewed, not runtime exercised; no writer exists yet.
3. [ ] **Native read/write in Caelum** on virtio-blk, with persistent in-memory
   state and caching. Latency and bytes written are measured from the start.
4. [ ] **Installer tools:** GPT creation, the FAT32 boot partition, pool
   formatting and copying files from the boot archive.
5. [ ] **End to end:** install and boot in QEMU, then on the ThinkPad.

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

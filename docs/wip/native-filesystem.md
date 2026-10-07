# npfs native filesystem

Status: **owner direction, 2026-10-02.** This restarts writable filesystem
work. It supersedes the pyxis-fs writer plans listed under
[what stops](#what-stops).

- **Accepted:** [owner decisions](#owner-decisions), [v1 answers](#v1-answers),
  [installation and authority](#installation-and-authority) with its
  [installer decisions](#installer-decisions) and [target consent](#target-consent),
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
   - **on clean shutdown, restart and sleep,** by flushing everything dirty.
     Power-off and restart do this since the ACPI milestone's task 2; there is
     no sleep.

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
   - create a GPT on a target disk the owner prepared (see
     [target consent](#target-consent));
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
call `fsync`. The kernel writer implements these policies, including the
power-off and restart flush; sleep does not exist.

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
directories and one metadata journal. The [implemented encoding](../../fs/docs/npfs-format.md)
and [host tools](../../fs/docs/npfs-host-tools.md) live in pyxis-fs; the
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
   that is virtio-blk and qualified [USB mass storage](../devices/usb-installation.md).
   NVMe comes later.
3. **Installer steps.** On a [consenting target](#target-consent) the installer:
   1. creates a GPT;
   2. creates the boot partition (a FAT32 EFI system partition, written fresh,
      with no general FAT driver), because firmware boots from FAT and Limine never
      reads a Pyxis pool;
   3. copies Limine to it and writes its boot configuration;
   4. copies Caelum and the boot archive to it;
   5. creates the pool partition, formats the pool and creates its volumes.

   V1 ships a fixed set of standard inits. Programs still run from the boot
   archive; the pool holds persistent volumes mounted after boot.

### Installer decisions

Accepted 2026-10-03.

1. **Name.** The format is **npfs** (next Pyxis filesystem). Code, tools, docs
   and messages use it: the API prefix is `npfs_`, and the host tools are
   `mkfs.npfs`, `fsck.npfs` and `npfs-inspect`. The pyxis-fs repository keeps its
   name.
2. **Source files.** The kernel exposes the files Limine loaded (Caelum and the
   boot archive) read-only to the installer. The Limine EFI binary and the
   `limine.conf` template ship in the boot archive.
3. **Layout.** A 512 MiB ESP, then the rest of the disk is the pool. The journal
   is at least 128 MiB on the 256 GB target, scaled down for small QEMU disks.
   The installer creates one volume, `system`. The owner subsequently accepted
   an editable installation prompt: prefill ceil(pool bytes / 128), rounded up
   to MiB, with an 8 MiB default minimum and **1 GiB default cap**. Explicit
   choices can be 1–1024 MiB if metadata/bootstrap files fit; a 256 GB disk
   defaults to 1 GiB.
4. **Starting it, for now.** A second Limine entry, "Install Pyxis", selects an
   install init. That init starts the installer in the first user space and is
   the only init that passes it the disk authority; the normal entry never
   receives it. This is a deliberate simplification. The preferred later flow is
   in [later ideas](#later-ideas).
5. **Interactive, with no command-line options.** The flow is:
   1. First choice, with no further description: **Proceed with installation**
      or **Read the room**. Read the room widens which disks are eligible (see
      [target consent](#target-consent)).
   2. A list of every disk, and why each one does or doesn't qualify.
   3. With one eligible disk, its size, GUID and the volumes that will be
      destroyed. With several, the user picks one
      by number first.
   4. An editable journal size prefilled with the standard value, then typed
      `wipe` after displaying the proposed layout.
   5. Installation, then a read-back check: the ESP files are compared byte for
      byte with their sources, the pool is reopened read-only through the normal
      mount path, and every volume is checked for its marker. Only then does it
      report "installed".
6. **The installed disk's boot menu** (accepted 2026-10-04). The installer fills
   both placeholders in `share/installer/limine.conf.template` with timeout `0`
   and the normal command line, and leaves out the "Install Pyxis" entry. An
   installed system boots straight into Pyxis; install media are the only way
   into install mode. Fixed `init-installed` mounts partition 2's `system`
   read-write at `system://`, then starts the ordinary local session. Home
   remains RAM-backed.

### Kernel authority choices

Task-4.2 authority choices accepted 2026-10-03:

- Discover multiple disks now. Boot inventory IDs select physical devices;
  GPT GUIDs remain metadata and normal mount selection must be unique.
- A writable raw open requires exclusive access: any retained pool or existing
  claim refuses it. The claim blocks new mounts, and release flushes and rescans
  GPT before relinquishing access. Pools remain mounted until reboot.
- The initial five-second menu choice is superseded by the owner-accepted PR
  review refinement: `BOOT_MENU_TIMEOUT` is a build setting, default `0`.
  Set it to `5` manually for install media; both entries remain generated.

The [implemented authority](../devices/installer-authority.md) keeps consent in
the trusted installer. Consent inspection must use raw reads and the format
codecs; opening a pool through the normal mount path retains it and prevents
formatting in the same boot. The owner accepted this current direction during
PR review. The installer looks up each root marker through a committed journal
overlay in memory, without writes before consent. Use the normal mount path
for verification after release.

### Target consent

Accepted 2026-10-03. A disk qualifies only when its owner has marked it as
disposable:

- It has a validated protective GPT and at least one recognized npfs pool.
  Every recognized pool has at least one live volume.
- **Every** live volume has a regular file named `SAFE_TO_WIPE` in its root.
- No partition of the disk is mounted, which also excludes the stick Pyxis
  booted from.
- A committed journal is not damage: the installer reads metadata through its
  validated journal images as an in-memory overlay, then checks the markers.
  This owner-accepted review refinement replaces persisted replay before
  consent. Rejected consent leaves the disk unchanged and recoverable by a
  read-write mount; accepted targets are wiped, so persisted replay is unnecessary.

A qualifying disk is rebuilt from scratch: a new GPT, ESP and pool, using the
layout above. The existing layout is not reused. The installer creates
`SAFE_TO_WIPE` in each volume it makes, so development reinstalls repeat with no
host step. **Deleting those files marks an install as final.**

The first disk is prepared on the host: `make usb-image` (which also places the
markers), then `dd` to the stick. Host tools work on image files, not block
devices.

**Read the room** widens consent eligibility while keeping the operational and
raw-access exclusions. It refuses npfs pools that are verifiably final (nonempty, all volumes readable, no regular
markers). **Any final pool vetoes the entire disk**, even if another pool is
marked or damaged; accepted by the owner for task 4.3. This
covers:
- blank disks;
- foreign layouts, such as a store-bought FAT32 stick;
- damaged npfs pools, where the checksummed header is valid but the volumes or
  markers can't be read. These are listed as "npfs pool, damaged: consent
  unreadable", so a broken test install is recovered without another computer.

Every eligible disk is listed with what it is, and still needs the typed `wipe`.
On the normal path, a damaged pool is never eligible: a final install that is
later damaged is the one whose data fsck should get a chance to recover. Once
Caelum can write NVMe, the ThinkPad's Fedora disk will appear under Read the room
as a foreign disk. This is accepted.

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
   [tool guide and validation](../../fs/docs/npfs-host-tools.md). Committed replay
   has been exercised at runtime in host and kernel; see the
   [adapter](../devices/filesystem-native-adapter.md#task-3-validation).
   The task-3 kernel writer produces normal COMMITTED/checkpoint transactions. Broader crash qualification remains separate.
3. [x] **Native read/write in Caelum** on virtio-blk, with persistent in-memory
   state and caching. Implemented in
   [parent #348](https://git.internal/PyxisOS/pyxis-os/pulls/348);
   [measurements](../development/experiments/native-filesystem-task3/README.md)
   record latency, bytes and ordinary validation from the start. Final owner
   policies are implemented; crash injection and physical-media qualification
   were not assigned.
4. [x] **Installer.** Follows the [installer decisions](#installer-decisions)
   and [target consent](#target-consent), as three focused PRs:
   1. [x] **Rename to npfs**: a mechanical rename across pyxis-fs and the parent,
      with no behavior change. `make usb-image` places the `SAFE_TO_WIPE`
      markers. The namespace/header/tool rename preserves the disk encoding
      and numeric ABI values; current consumers use the new names together.
      The USB builder places one empty regular marker in its sole `usb-test`
      volume.
   2. [x] **Kernel authority**: whole-disk write authority for trusted init, the
      read-only Limine-loaded files, and the "Install Pyxis" boot entry with its
      native init. Multiple VirtIO disks, exclusive raw claims and release-time
      GPT rescans are implemented; see the [authority reference](../devices/installer-authority.md)
      and [validation/probe](../development/experiments/native-filesystem-task4.2/README.md).
      Task 4.3 removes the temporary probe; its historical source remains linked
      from the validation record.
   3. [x] **Installer program**: the interactive flow, GPT, a fresh FAT32 ESP,
      npfs formatting and the read-back check, with userspace as the third
      link-time symbol provider. See the [implemented installer](../userland/installer.md)
      and [task-4.3 validation](../development/experiments/native-filesystem-task4.3/README.md).
5. [ ] **End to end:**
   - [x] Install and boot in QEMU on merged main, including USB-backed live
     media, CPU entropy without VirtIO RNG and synchronized persistent files.
     See [task-5 qualification](../development/experiments/native-filesystem-task5/README.md).
   - [x] Native ThinkPad installation/boot. The owner deferred it on 2026-10-04
     until writable USB storage existed. It was completed on 2026-10-05 by the
     [USB installation](../devices/usb-installation.md#validation), which installed
     onto and booted a USB stick natively, with persistence across a synced
     power-off and one Update round trip. No NVMe backend exists.

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
- **A live-system install flow (owner preference).** Boot a usable system in
  which you can inspect disks and the network, then enter the installer when
  ready, as Linux live images do, rather than rebooting into a separate entry.
  This likely needs runtime space creation (the new-space flow). The second Limine entry is the
  v1 simplification. Pool retirement is a prerequisite: the current retained
  normal mounts prevent raw installation in the same boot. Define safe pool
  retirement and ownership before adding that flow.
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

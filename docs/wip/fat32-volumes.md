# FAT32 volumes

Status: **shared milestone, decisions 1–3 accepted by the owner on 2026-10-11;
not implemented.** Extract [NVMe task 2](nvme-coexistence.md#2-fat32-read-and-write-for-the-esp)
and [installer task 3](installer-tui.md#dependencies-and-ordered-delivery) into one
end-to-end deliverable. No implementation is assigned; every FAT32 task needs
its own later owner go. Moving work does not complete it.

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

## Inspected foundation

Main `d124a053`, pinned userland `a84259c0`; source inspection only, no new
FAT probe or runtime result. No partial FAT implementation branch was supplied.
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
but their registry currently knows npfs, and pools stay retained until reboot.
Current NVMe raw partition claims are still refused until the coexistence
authority gate lands; engine writes qualify on scratch VirtIO/USB first.
FAT mount/release and cross-backend exclusion do not exist. The FAT mount's
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

- [ ] **1. Small first task: shared read-only engine/inspector.** Extract/generalize
  the bounded reader, admit geometry and validate chains/FAT/LFN/FSInfo with explicit
  memory bounds. List/read arbitrary supported files through confined raw reads,
  refusing unsupported/corrupt metadata; no writes or mount API yet.
  **After this task the owner can:** inspect supported scratch FAT media and read
  the real ESP read-only without running coexistence or the editor.
- [ ] **2. Incremental raw engine writes.** Allocation, directories, long names
  within the chosen profile, create/extend/staged replacement of created files,
  ordered flush/sync and failure preservation. Keep explicit ownership policy and
  partition claims; qualify on disposable images, without whole-tree formatting.
  **After this task the owner can:** add and replace Pyxis-created files on a
  scratch FAT volume that Linux reads, without rebuilding that volume.
- [ ] **3. Native read-only mount and release.** Kernel FILE/directory wrappers,
  shared extent registry, alias/object identity, claim exclusion, reference drainage
  and trusted ESP bootstrap/root delegation. Design the native mount/retire binding
  before code; no userspace scheme proxy or implicit mount authority.
  **After this task the owner can:** mount supported FAT32 and use ordinary `ls`/
  `cat`, then release all views so an authorized raw publisher can claim the extent.
- [ ] **4. Mounted read-write operations and sync.** Deliver real native mutations,
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

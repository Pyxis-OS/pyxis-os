# Pyxis beside Fedora on an internal NVMe

Status: **tasks 1–2 delivered; task 3 assigned to Codex epsilon for this scope
note and implementation, 2026-10-15.** All three task-3 decisions below were
accepted by the owner on 2026-10-15; implementation proceeds authority first.
Native NVMe writes remain disabled; native qualification is a separate owner go.
Decisions 1 and 3 were accepted on 2026-10-10; decision 2's owner-run Limine
migration and the later owned-entry editor direction remain as recorded below.
Both the ThinkPad and [vela](../targets/ideapad-slim5-16iph11/notes.md) are targets.
This builds on the [native installer](../userland/installer.md),
[system updates](../userland/system-updates.md), [installer authority](../devices/installer-authority.md)
and [shared FAT32 mounts](../devices/fat32-mounts.md).

## What the inventories show

From the [undocked Linux inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt)
of 2026-10-02:

- **Disk:** one SK hynix PC611 (`1c5c:1639`, NVMe 1.3) at `01:00.0`, firmware
  `11710C10`, one namespace of 256,060,514,304 bytes with 512-byte blocks,
  MDTS 64 pages, five power states, Secure Boot disabled.
- **Partitions:** three, 600 MiB (the ESP, starting at LBA 2048), 2 GiB (Fedora's
  `/boot`) and 235.9 GiB. Their sizes add up to the whole 238.5 GiB disk, so
  **that snapshot had no free space.** The inventory does not record filesystem types,
  the ESP's FAT geometry or its dirty flag. The owner states that the root is not
  encrypted (owner-stated, not captured by the inventory).
- **Firmware entries:** Fedora's `shimx64.efi` on that ESP is `Boot0001` and first
  in the boot order. A `Windows Boot Manager` entry also names that ESP (the
  inventory does not say whether the files exist), so it may hold Microsoft files,
  which Pyxis never touches.
- **Health (SMART):** passed, no critical warnings or media errors, 2% used,
  100% spare, 17.3 TB written and 11.8 TB read, 7,868 power-on hours, 86 unsafe
  shutdowns. The power-cycle count (854,277) is an unexplained counter.
- **Card reader:** a Realtek RTS522A (`10ec:522a`) at `04:00.0`, class `ff00`,
  unbound by Pyxis.

### Vela: a second target, not the ThinkPad layout

The [2026-10-14 inventory](../targets/ideapad-slim5-16iph11/ideapad-inventory.txt)
records a SanDisk SN7100S (`15b7:5044`, NVMe 2.0), a 954 MiB vfat ESP at
`/boot/efi`, a 1.9 GiB ext4 `/boot` and a 475.9 GiB btrfs root/home. The target
notes and owner describe about 510 GB already unallocated; no shrink is needed
if fresh GPT inspection confirms a suitable gap. Exact partition LBAs, unused
GPT slots, ESP FAT32 geometry/clean state, free capacity and directory contents
are not established by `lsblk` and need read-only capture before a layout-faithful
rehearsal. Sizes here are observations, not installer admission constants.

The recorded firmware entries target Fedora's `shimx64.efi`/`shim.efi`, not a
proven Fedora-through-Limine setup. Booting Pyxis through Limine from USB does
not establish that migration. Step 0 remains owner-run and must be proved for
each native target; task 3 changes neither Fedora's loader nor firmware entries.
The owner's [read-only vela result on #853](https://git.internal/PyxisOS/pyxis-os/pulls/853#issuecomment-11129)
reports the correct namespace capacity and healthy three-partition GPT with
writes disabled. It qualifies no write or coexistence operation.

## Decisions

1. **Where the free space comes from (accepted).** Pyxis never resizes anything.
   The ThinkPad snapshot had no gap: **if still necessary before the installer
   runs, the owner shrinks
   Fedora's 235.9 GiB partition and its filesystem from Linux with Fedora's own
   tools, leaving at least 64 GiB unallocated at the end of the disk, after a
   verified backup.** The installer only ever uses space that is already
   unallocated and refuses otherwise. This shrink is the one step in the whole
   plan that can lose Fedora data, and it is the owner's, outside Pyxis. Vela
   already has an owner-reported gap; fresh GPT admission must verify it. The
   alternative is another disk or a USB drive for Pyxis, which this milestone
   does not cover.
2. **Limine replaces GRUB (changed by the owner).** Limine becomes the ESP's boot
   manager. It boots Fedora directly through its Linux protocol, with Fedora's
   kernel, initramfs and command line, and boots Pyxis as another entry. The
   owner dislikes GRUB and multiboot2 and does the swap from Linux, with the old
   GRUB entry kept as a tested fallback until Limine is proven
   ([the swap](#limine-as-fedoras-boot-manager)). **Owner reversal, 2026-10-11:**
   the [configuration delta editor](installer-tui.md#accepted-direction--2026-10-11)
   may edit explicitly owned Pyxis `limine.conf` entries after plan confirmation,
   preserving all foreign/unowned bytes. Its publication and ownership contract
   was accepted on 2026-10-11; installed activation is alpha's gated task 5.
   Until delivered,
   the installer prints the entry, including the pool's disk GUID, for the owner to paste; that fallback stays. Firmware-variable
   edits remain excluded.
3. **Write authority and the internal disk (accepted).** **The kernel, not the
   installer, enforces the boundary.** NVMe writes are off until an explicit boot
   option enables them for qualification. When enabled:
   - the installer gets writes only through partition-confined claims on the new pool and on the ESP (the
     interfaces exist on other transports; NVMe admission is still task 3);
   - the GPT is changed by one new kernel operation that only appends one entry
     into already-unallocated space and verifies that every existing entry,
     both headers and the arrays are otherwise byte-identical, so the installer
     never holds a whole-disk write on a disk with foreign partitions;
   - ordinary whole-disk claims refuse an ESP or any non-native-pool partition.
     Only classic installation may obtain the separately reviewed, single-use
     [erase grant](#whole-disk-erase-authority--accepted-by-the-owner-2026-10-15).
     Coexistence never receives that exception; Fedora and Windows partitions
     and boot files remain outside its authority.

   The alternative, a whole-disk claim and an installer that is careful, is
   simpler and leaves one userland bug between Pyxis and the Fedora partition.

[Shared FAT32 volumes](../devices/fat32-volumes.md) deliver the freestanding
read/validation and raw-write engines, native RO/RW views and the trusted workflow.
The editor and removable media can use them without completing coexistence.
The raw creation-receipt profile preserves dirty flags; mounted RW has its own
implemented dirty-mark, durable mutation and clean-power seal contract. Editable
ESP config ownership and publication remain in [the configuration editor](installer-tui.md).
This dependency completes no coexistence installation or native NVMe write check.

## Invariants

- Every write is additive or inside the Pyxis partition, and reads are verified
  before and after.
- Refuse on anything uncertain. For the GPT: degraded, ambiguous or unsupported
  metadata, a reserved attribute bit, no unused entry or no aligned free extent of
  the requested size. For the ESP: not FAT32 with 512-byte sectors, the dirty
  flag set, structural corruption in FAT1, too little room, or `EFI/pyxis` holding
  unowned files. Use the delivered mounted FAT policy: invalid FSInfo hints are
  unknown and rebuilt, and a valid FAT1 with stale FAT2 is admitted and reported.
  RO planning never repairs; only the first effective RW mutation repairs FAT2.
  These findings never authorize formatting or rebuilding the ESP. The installer
  cannot see Secure Boot; it stays off by the owner's choice (see below).
- Pyxis payload names live under one ESP directory, `EFI/pyxis`. The only planned
  exception to that write boundary is the owned-entry `limine.conf` reconciliation
  above; it never authorizes changes to Fedora entries or other ESP payloads.
- No fault injection, no fake success, no resize, no format of the ESP.

## Limine as Fedora's boot manager

Step 0, the owner's, from Linux, needs no Pyxis code and comes before any Pyxis
native write, so Fedora booting through Limine is proven alone first.

**Layout.** Pyxis's pinned Limine (`boot://share/installer/BOOTX64.EFI`, so the
loader and its `limine.conf` syntax match the Pyxis entry) goes in its own ESP
directory, with a `limine.conf` where that Limine looks for it (the existing
images use `boot/limine/limine.conf` on the ESP; the search path is unverified
here, so rehearse it). A new firmware entry puts Limine first in the boot order.
Fedora's `\EFI\fedora` files, `\EFI\BOOT` fallback and its firmware entry are
not modified.

**Fedora entries.** Each is `protocol: linux` with `path` and `module_path` on the
partition that holds the kernels, selected by GPT GUID (`guid(...)` rather than
`boot()`, which is the ESP), the initramfs, and `cmdline` taken from Fedora's own
boot entry, so `root=` and `rootflags=` come from Fedora rather than from copies.
On a default Fedora install `/boot/efi` is the FAT32 ESP, which is what UEFI
requires, while `/boot` is usually a separate ext4 partition holding the kernels,
initramfs and boot entries; the 2 GiB partition fits that. Limine reads ext2, ext3,
ext4 and FAT, so the Linux-protocol default works either way and nothing is
copied to the 600 MiB ESP. The owner confirms which it is with
`findmnt /boot /boot/efi` from Fedora. Loading the kernel through its EFI stub
(`protocol: efi`) is the alternative; it needs the initramfs handed over by the
loader, which is unverified, so the Linux protocol is the default. Path syntax and
keys are checked against the pinned Limine's (12.9.3) documentation, which this repository does
not carry, before anything is written.

**Kernel updates.** Fedora's versioned kernel names change with every update and
there is no stable `vmlinuz` path, so the default is a small `kernel-install`
plugin, written once, in `/etc/kernel/install.d/`. On every add or remove it
regenerates only the Fedora block of `limine.conf`, between marker comments, from
the boot entries in `/boot/loader/entries`: newest first as the default, the
older kernels kept, rescue included. It writes a temporary file and renames it on
the ESP, and writes nothing when it would produce no entries, so a failing run
leaves the previous file. The Pyxis block, the timeout and the global options
belong to the owner and are never touched. A stable-symlink scheme is the
alternative, but symlink following in Limine's ext4 driver is unverified. Command
line changes made with `grubby` are not kernel-install events, so the same script
is run by hand after them. Whether Fedora 44's kernel packages call
`kernel-install` this way is confirmed on a Fedora VM before the plugin is
trusted, with a real kernel update and removal. The script is owner-run host
tooling; it joins the repository under `scripts/` only if the owner asks.

**Secure Boot and the TPM.** Limine is unsigned, so Secure Boot stays off. The
2026-10-02 inventory records `SecureBoot disabled`; the owner reruns
`mokutil --sb-state` before the swap and after any firmware update or reset,
since the firmware setting can change. Without Secure Boot Fedora's shim is not
needed to boot. The machine has a TPM 2.0; with the root not encrypted (owner-stated), no
unlock is bound to boot-loader measurements, so replacing the loader does not
affect Fedora's start.

**Tested fallback.** The old shim and GRUB entry (`Boot0001`) and its files stay
until Limine has proven itself, and the fallback is exercised before it is
needed: after adding Limine, the owner boots Fedora from the firmware boot menu's
GRUB entry once, and again after each kernel update. Limine is proven by several
cold boots into Fedora and Pyxis, at least two kernel updates and one old-kernel
removal through the plugin, a Fedora rescue boot, and a clean `fsck.fat` of the
ESP. Only then does the owner remove the GRUB firmware entry and, separately,
its files. Removing GRUB packages changes what Fedora's own update scripts expect,
so it is rehearsed on a Fedora VM first. A Fedora live USB stays regardless.

**Rehearsal.** The same swap and plugin run first on a Fedora VM in QEMU with
UEFI, a GPT of the same shape and ext4 `/boot`, before the ThinkPad.

**After this step the owner can:** boot Fedora through Limine with a menu entry
for every installed kernel, keep updating Fedora, and fall back to GRUB from the
firmware menu.

## Tasks

Each task ends with a PR and, where it can, QEMU qualification; the owner goes
ahead task by task.

### 1. NVMe driver — implemented ([#710](https://git.internal/PyxisOS/pyxis-os/pulls/710))

- [x] Driver implemented and QEMU-qualified (assigned and delivered 2026-10-11).
  Behavior, the `nvme.write=1` gate, raw-claim refusals and the clean-shutdown and
  force-off path are in the [NVMe reference](../devices/nvme.md); measurements and
  exact native staging steps are in the
  [qualification record](../development/experiments/nvme-task1/README.md).
- [x] Native read-only qualification on the ThinkPad passed (2026-10-11): the
  disk read at its expected capacity, the GPT with three partitions matched
  Linux's view, SMART was read, and the unsafe-shutdown count did not change
  across a Pyxis reboot, so the shutdown notification works. No native writes and
  no later task are authorized by this delivery.

Owner decisions of 2026-10-11 that still bind later tasks: one served namespace
per controller, never silently namespace 1 (since 2026-10-15 the first eligible
active namespace, with the others reported; [NVMe](../devices/nvme.md#namespaces));
whole-disk raw claims on NVMe need a fresh full-namespace zero-data verification
until task 3, and partition claims on NVMe stay refused until then.

### 2. FAT32 read and write for the ESP

**Shared dependency delivered, 2026-10-13.**
[Shared FAT32 volumes](../devices/fat32-volumes.md) provide read/validation and
incremental raw-write engines, native RO/RW files and trusted sync/release. No
duplicate FAT implementation task remains in coexistence. Keep partition claims,
Pyxis-owned additive publication, no whole-ESP rewrite and no inferred adoption
of externally created files. The editor separately admits confirmed owned spans
of shared `limine.conf` under its accepted publication/fallback contract.

Data/FAT/directory publication requires durable dependency barriers, not only
submission order. FAT copies and LFN/name changes can span sectors; no atomic
rename, untorn-sector or corruption-free crash guarantee is claimed.
Interrupted updates may need host FAT repair and the existing live-image recovery
path. Multi-file publication remains non-atomic. The raw engine does not change
the dirty bit; mounted RW durably marks dirty before mutation and clears only
its owned mark under the [retirement/seal contract](../devices/fat32-mounts.md#failure-and-clean-power).

**With this delivered dependency the owner can:** inspect supported FAT32 media,
then edit/sync authorized files through native capabilities without first
delivering coexistence installation. The [owner-present native ESP check](../devices/fat32-volumes.md#owner-present-native-esp-run-sheet)
remains pending and authorizes no real ESP write.

### 3. Installer coexistence mode

**Accepted scope, 2026-10-15; implementation assigned.** Inspected main
`cfa02e41`, userland `f239d479`, fs `101e1f68`. Decisions 1 and 3 remain binding.
Task 3 covers coexistence Install/Update, not resize, ESP format, boot-manager
replacement, firmware writes or native qualification.

#### What current main changes

- [`disk_access.c`](../../kernel/storage/disk_access.c) still zero-scans NVMe
  whole-disk write admission and refuses partition claims. [`disk.h`](../../include/abi/disk.h)
  has no append or partition-first writable opener. Ordinary READ_WRITE OPEN
  is not a substitute. Existing npfs OPEN/CREATE_VOLUME needs a published GPT
  entry, so the old GPT-last order would require provisional-pool machinery.
- Native FAT views, allocation-map/CHECK and mirror repair are delivered. Retire
  drained RO planning views before writes; keep issuer facts, CHECK immediately
  after BEGIN, the 30-second non-extending interval and stopped-writer rules.
- [#862](https://git.internal/PyxisOS/pyxis-os/pulls/862)'s two-entry classic menu
  is no foreign-menu reconciliation path. Share [#845](https://git.internal/PyxisOS/pyxis-os/pulls/845)'s
  quiet progress/Review/Print and explicit diagnostic grant.
  [#853](https://git.internal/PyxisOS/pyxis-os/pulls/853)'s newer-controller
  admission changes neither write authority nor partition access.

#### Agreed boundary with Codex alpha

Alpha owns [task 5](installer-tui.md#buildable-scope-and-activation-gates),
[#797](https://git.internal/PyxisOS/pyxis-os/pulls/797) /
[userland #253](https://git.internal/PyxisOS/pyxis-userland/pulls/253):
`installer/{configuration.*,publication*,boot_entry*}` and
`config-editor/{record,readiness,storage}*`. Epsilon owns GPT/partition authority
and coexistence selection. Shared `installer/{plan*,executor,main,installer.h,programs}`
edits are serialized and agreed before code.

The draft `install_publication_publish` couples payload staging to configuration
activation. After owner approval alpha factors reusable staging/verification
with retained attempt/hash evidence; task 3 consumes it, without a duplicate
stager. This dependency may land before alpha's installed activation, which
still waits for coexistence authority. It does not authorize activation itself.

One frozen plan binds geometry, disk/ESP/pool GUIDs, GPT slots/extents, volume
names, consumed target bytes and sources. Render/validate supported archive
configuration with the actual pool entry, not text substitution of `partition = 2`;
`bind_bin` already follows the system partition. Keep classic two-partition
eligibility separate. The largest-gap/1 MiB alignment/64 GiB default remains
installer policy, with an explicit size choice, never permission to resize.

#### Task-3 decisions — accepted by the owner, 2026-10-15

1. **Partition-first authority:** new, explicitly delegated kernel
   append/partition-open rights on the trusted installer profile; no executable-name
   exception or ordinary writable whole-disk OPEN. Opening an existing pool for
   Update binds disk/partition GUIDs, type, slot, extent and geometry from the
   reviewed plan. A pool DISK remains partition-bound across RELEASE, aliases,
   later CLAIM and volume operations; it cannot widen to partition zero or a
   neighbour. The ESP uses explicitly delegated native FAT RW view authority,
   not raw format/write authority; compare bound OBSERVE facts before mutation.
   Install and Update use the same selected-view path. Kernel whole-disk claims
   must refuse foreign partitions, including Read the room; the blank-only NVMe
   path is not a bypass. Disjoint pool/ESP operations share extent exclusion;
   no overlapping raw claim/mount or implicit grant from a filename.
2. **Append GPT before pool format/program staging:** after read-only
   checks and confirmed Review, a bounded worker-owned append transaction
   revalidates both GPT copies and the unused slot/gap, prepares resources,
   appends only the native-pool entry, flushes/read-verifies and returns its
   confined authority. Only that slot and required CRC fields change; preserve
   existing entries, padding, disk identity and protective MBR. Then format only
   the new pool, stage/sync/verify programs and stage the ESP payload. This
   revises the old GPT-last order and avoids provisional npfs lifetime machinery.
   Two-copy GPT updates are not crash-atomic: failure may leave a visible partial
   installation or ambiguous table. Stop with phase/identity evidence; no
   automatic rollback/delete/retry/second append. Uncertain effects retain
   exclusion and refuse further mutation; recovery needs fresh review or owner
   tools. Update opens its existing partition, never appends another.
3. **Immutable payload attempts:** reuse alpha's
   `EFI/pyxis/<revision>/<attempt>/` layout for kernel, filtered archive, revision,
   entry text and ownership evidence. Print/save the entry only after verified
   sync/hash, naming exact staged paths. Until alpha's owned-entry activation
   lands, the owner pastes it; task 3 changes no Limine/global/Fedora config and
   declares no configuration active. Preserve older attempts: an unowned/manual
   entry may reference them. No automatic cleanup or adoption of unknown files.
   Review extra space and refuse insufficient capacity. This costs more ESP
   backing than the old proposed flat paths, but preserves older boot payloads.

#### Whole-disk erase authority — accepted by the owner, 2026-10-15

Ordinary kernel whole-disk write admission refuses any GPT containing an ESP
or a non-native-pool partition. It never infers ownership from partition types,
names or ESP payloads. Blank devices remain admitted under the existing rules.

Classic Install and Read the room instead require an explicit whole-disk erase
grant: trusted installer's typed consent authorizes one reviewed plan, bound to
the disk GUID and exact GPT snapshot (both copies, entries and device geometry).
The kernel revalidates it at admission, refuses any difference and consumes it
once. A changed table requires fresh Review and consent. Coexistence receives
no erase authority and continues through append/partition-confined grants only.

Before consent, Review shows every partition and each ESP's top-level `EFI/`
directories, including `EFI/fedora`, `EFI/Microsoft`, `EFI/pyxis` and `EFI/BOOT`
when present. Windows partitions are foreign just like Fedora's: coexistence
never touches Microsoft EFI files, MSR or NTFS. Only the explicit erase grant
permits classic installation to destroy such a disk; that destructive effect
must be visible before consent.

The [owned ESP attempt cleanup limit](../technical-debt.md#owned-esp-attempt-cleanup)
is accepted. Implement authority first, after the current-main baseline.
Alpha's qualified shared payload stager is available in userland #272 / #880;
executor integration consumes it without duplicating the publisher.

#### Classic Update recovery — accepted by the owner, 2026-10-15

Classic Update preserves recovery from a damaged or missing ESP through an
explicitly reviewed, single-use, ESP-only rebuild grant. It binds the reviewed
disk, exact GPT snapshot and selected ESP extent; any difference refuses
admission. Exclude overlapping views before admission. The returned writer can
initialize only that extent, with no whole-disk access or later reclaim. Release
it before opening the fresh native FAT view and using the shared stager.
Coexistence never receives this authority: a shared Fedora or Windows ESP is
never rebuilt. A healthy ESP uses native FAT views; normal Update never formats
it. Fresh classic Install initialization stays inside its accepted whole-disk
erase authority.

This is the explicit, reviewed recovery route alongside the
[configuration editor's preservation contract](installer-tui.md):
damaged FAT or an unavailable writer still refuses normal preservation Update;
it never triggers an automatic destructive fallback. Review must name recovery
and its exact target before separate consent. The shared payload stager in
[userland #272](https://git.internal/PyxisOS/pyxis-userland/pulls/272) /
[#880](https://git.internal/PyxisOS/pyxis-os/pulls/880) supplies payload staging,
not rebuild authority or activation.

#### Classic sector compatibility — accepted by the owner, 2026-10-15

Preserve classic Install/Update support for 512- and 4096-byte logical sectors.
Before switching the executor to native ESP views, extend the shared FAT engine
and kernel mount adapter to 4096-byte device/FAT sectors, retaining every
validation and admission bound. A healthy ESP must never be formatted to bypass
a native-view restriction. Coexistence keeps its accepted 512-byte-sector
profile. The shared extension is implemented in [fs #40](https://git.internal/PyxisOS/pyxis-fs/pulls/40)
and the kernel adapter in task-3 #878; [disposable QEMU/host qualification](../development/experiments/fat32-4kn.md)
covers both sizes. The installer executor switch remains pending. The reviewed
ESP-only recovery grant remains independently accepted.

#### Classic activation integration — accepted by the owner, 2026-10-15

Classic Install/Update keeps today's fixed boot paths, written through native
FAT files: `boot/caelum.elf`, `boot/initrd.cpio`, `boot/revision`,
`boot/limine/limine.conf` and the EFI loader. No classic attempt directories or
new ownership record are introduced by task 3. Classic moves to immutable
attempts with task 5's validated committed boot-binding evidence. This keeps
task 3 independently mergeable without changing classic activation behaviour.

Coexistence uses `EFI/pyxis/<revision>/<attempt>/` from the start. Its
ESP-GUID-qualified saved/printed entry remains manual text, never automatic
activation or ownership evidence. The shared stager's `ownership.record`
describes staged payloads; task 5's committed `config.record` remains the sole
authority for an activated immutable boot binding.

#### Implementation and qualification

The authority checkpoint is in draft [userland #270](https://git.internal/PyxisOS/pyxis-userland/pulls/270):
reviewed erase/append, confined pool handles, trusted profile selection and
read-only candidate helpers build and pass disposable QEMU/GDB checks. The
[experiment record](../development/experiments/nvme-coexistence-task3.md) separates
these checks from the baseline. Complete Review, literal archive rebinding, immutable-attempt plan fields and
classic native activation helpers are implemented as integration modules. The
frontend now selects coexistence targets and checks its delegated profile;
the common plan/executor does not yet consume the modules. The qualified shared
ESP stager is available, and the sector prerequisite is complete. Task 3 stays
open and this checkpoint is not ready to merge.

Implement authority first, then consume alpha's factored stager and integrate
the common plan/executor. Capture baseline/after costs, write bytes and backing
with existing observers. Planning, Cancel/dry-run, stale/gap/foreign-file
refusals make no writes; strict rejection never permits rebuilding the ESP.

Use sparse NVMe fixtures for both target layouts, with three foreign partitions,
a verified gap and copied Fedora ESP trees. Exact read-only GPT/ESP captures
supply LBAs/geometry; approximate `lsblk` sizes do not. If using a disposable
Fedora VM's tree instead, label that provenance, not the target's actual ESP.
Compare foreign partition bytes and unowned ESP files/namespaces before/after
Install and Update. For GPT allow only the chosen slot/CRCs, not an entire-table
exception; account for specific required FAT/FSInfo/directory changes, including
pending mirror repair. Run independent GPT/FAT checks and boot staged Pyxis.

Also perform coexistence Install **and Update on a bootable Fedora VM disk**,
then boot Fedora from each resulting disk; an untouched VM is no proof. The
copied-tree geometry fixture alone cannot establish a Fedora boot. A separate
read-only Limine test loader may select staged Pyxis files while leaving Fedora's
boot manager and firmware unchanged. Native step 0 remains owner-run per target.
QEMU qualifies geometry/software, not SN7100S firmware/NVMe-2.0 write behaviour.
`nvme.write=1` is confined to disposable QEMU media; no native task 4 or plugin
implementation is assigned.

**After this task the owner can:** rehearse coexistence Install/Update and boot
in QEMU with preservation evidence, without authorizing writes to either laptop.

### 4. Native qualification on each target

Not assigned by task 3. Step 0 is proved for the selected machine first; the
ThinkPad and vela need separate records. Then the preparation, all the owner's:

- Full backups on external media: `dd` of the GPT and protective MBR regions, of
  the ESP and of `/boot`, `sgdisk --backup`, the output of `efibootmgr -v`,
  `lsblk -f`, the ESP's `fsck.fat -n`, and a copy of the root filesystem's
  contents that matter. The ESP and `/boot` copies include Limine's files and the
  plugin. A Fedora live USB with the restore commands written down:
  `sgdisk --load-backup`, `dd` back of the ESP, `efibootmgr --create` for Fedora.
- Run `fsck.fat` on the ESP and record cluster size, FATs and dirty flag, which
  the inventory lacks, `findmnt /boot /boot/efi`, and the root partition's
  filesystem, which sets the tool for the shrink.
- If no suitable gap already exists, shrink the root partition per decision 1
  and boot Fedora once. Never repeat a shrink merely because the ThinkPad
  inventory once needed one.

Then in order, each a separate owner-run step with results recorded in an
experiment record: read-only Pyxis boot and SMART numbers; one conservative write
phase inside the new partition only (a megabyte pattern, flush, read back,
compare); the pool format; the ESP additions; Limine's Pyxis entry; Fedora booting
through Limine again; the update path; a final Fedora boot, `fsck.fat` and a
`btrfs`/`ext4` check. The GRUB entry stays as the fallback until step 0's proof is
complete, and the owner removes it, not Pyxis. The owner rechecks the boot order
after Fedora updates; whether shim or firmware updates rewrite it is unverified.

**After this task the owner can:** use Pyxis on the internal NVMe beside Fedora,
choosing in Limine's menu, with Fedora booting directly and a rehearsed way back
to Fedora alone.

## Wear budget

The owner's budget is no faster than one SSD per six months. The drive's own
2026-10-02 ThinkPad numbers give a loose upper bound: 17.3 TB written at 2% used implies a rated life
of about 870 TB (700 TB to 1.15 PB with rounding of the 2%), so one drive per six
months is 3.8 to 6.3 TB a day. That is the vendor's estimate, not a verified
rating and not a vela/SN7100S estimate; fresh per-device evidence is required. Pyxis needs a tiny fraction of
it: the QEMU update wrote about 46 MB to the ESP
([record](../development/experiments/system-updates-task2/README.md)), and a
whole install is in the tens to low hundreds of MB. So the budget is not binding;
what is worth controlling is repetition. The proposal:

- Writes start conservative: off by default, then the single-megabyte check, then
  the pool, then the ESP, as task 4 orders.
- Count bytes written per boot in the driver and log them at power-off, and read
  SMART before and after each native session, recording the change in the
  experiment record.
- No write caps in code; the budget lives with the owner's records.
- Matched QEMU install runs measure the install's write volume before any native
  run, as the update record did.

## Alternative: the microSD card reader

Lower priority, recorded for later. The RTS522A is a PCIe card reader with a
vendor-specific register interface, so using it takes a driver for that chip and
an SD stack above it (initialization at low clock, voltage and bus width
switching, SDHC and SDXC addressing, read and write commands, card detect).
No Realtek datasheet is in the repository (public availability is unverified), and Linux's reader driver is under
the GPL, so its use as a reference depends on the project's licensing policy
([LICENSING.md](../../LICENSING.md)). The firmware probably cannot boot from the
slot, which would leave Limine and the kernel on the internal ESP or a USB stick
anyway, so the card could hold only data. Consumer microSD cards give no wear
reporting and are slow, so they suit a read-mostly pool poorly. The NVMe path
needs none of this and stays first.

## What this does not decide

- Resizing or moving Fedora's partitions from Pyxis, or any reuse of Fedora's
  space.
- Mounting Fedora's data filesystems and Windows. Mounted FAT32 for ESP
  configuration uses the delivered [FAT32 backend](../devices/fat32-volumes.md);
  consumer ownership/publication and native write qualification remain separate.
- Secure Boot, a signed Limine or kernel and TPM measurement: it stays off.
- Hibernate and suspend interaction with the shared disk.
- Keeping Pyxis's pool partition number stable if Fedora tools renumber the table.

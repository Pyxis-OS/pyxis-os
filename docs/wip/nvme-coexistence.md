# Pyxis beside Fedora on the internal NVMe

Status: **proposal; nothing is implemented and no task is assigned.** It needs the
owner's answers to the three decisions below. The direction is the owner's
(2026-10-10): Fedora stays on the ThinkPad's internal NVMe, Pyxis gets a pool
beside it, and the installer learns a coexistence mode that never destroys
anything. It builds on the NVMe and FAT32 items in
[later OS directions](later-os-directions.md), and the
[native installer](../userland/installer.md), [system updates](../userland/system-updates.md)
and [installer authority](../devices/installer-authority.md) as they are today.

## What the inventories show

From the [undocked Linux inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt)
of 2026-10-02:

- **Disk:** one SK hynix PC611 (`1c5c:1639`, NVMe 1.3) at `01:00.0`, firmware
  `11710C10`, one namespace of 256,060,514,304 bytes with 512-byte blocks,
  MDTS 64 pages, five power states, Secure Boot disabled.
- **Partitions:** three, 600 MiB (the ESP, starting at LBA 2048), 2 GiB (Fedora's
  `/boot`) and 235.9 GiB. Their sizes add up to the whole 238.5 GiB disk, so
  **there is no free space today.** The inventory does not record filesystem types,
  encryption, the ESP's FAT geometry or its dirty flag.
- **Firmware entries:** Fedora's `shimx64.efi` on that ESP is `Boot0001` and first
  in the boot order. A `Windows Boot Manager` entry also names that ESP (the
  inventory does not say whether the files exist), so it may hold Microsoft files,
  which Pyxis never touches.
- **Health (SMART):** passed, no critical warnings or media errors, 2% used,
  100% spare, 17.3 TB written and 11.8 TB read, 7,868 power-on hours, 86 unsafe
  shutdowns. The power-cycle count (854,277) is an unexplained counter.
- **Card reader:** a Realtek RTS522A (`10ec:522a`) at `04:00.0`, class `ff00`,
  unbound by Pyxis.

## Decisions for the owner

1. **Where the free space comes from.** The disk has none, and Pyxis must not
   resize anything. Default: **before the installer runs, the owner shrinks
   Fedora's 235.9 GiB partition and its filesystem from Linux with Fedora's own
   tools, leaving at least 64 GiB unallocated at the end of the disk, after a
   verified backup.** The installer only ever uses space that is already
   unallocated and refuses otherwise. This shrink is the one step in the whole
   plan that can lose Fedora data, and it is the owner's, outside Pyxis. The
   alternative is another disk or a USB drive for Pyxis, which this milestone
   does not cover.
2. **How Limine and Fedora coexist.** Default: **Limine is added, not swapped
   in.** The owner copies the pinned Limine (`boot://share/installer/BOOTX64.EFI`,
   so loader and `limine.conf` syntax match) to its own ESP directory, writes its
   `limine.conf` with a Pyxis entry and a Fedora entry that chainloads Fedora's
   existing `shimx64.efi`, and adds a firmware entry first in the boot order.
   Fedora's files and its firmware entry stay, so the firmware boot menu still
   reaches Fedora when Limine is broken, and Fedora updates keep working because
   nothing of theirs moved. The alternative, Limine loading Fedora's kernels
   itself, needs the owner to keep `limine.conf` in step with every kernel update.
   **The installer never edits `limine.conf` or firmware variables.** It writes the
   Pyxis files and prints the entry text, including the pool's disk GUID, for the
   owner to paste. Limine updates stay the owner's.
3. **Write authority and the internal disk.** Default: **the kernel, not the
   installer, enforces the boundary.** NVMe writes are off until an explicit boot
   option enables them for qualification. When enabled:
   - the installer gets writes only through partition-confined claims (which
     exist today) on the new pool and on the ESP;
   - the GPT is changed by one new kernel operation that only appends one entry
     into already-unallocated space and verifies that every existing entry,
     both headers and the arrays are otherwise byte-identical, so the installer
     never holds a whole-disk write on a disk with foreign partitions;
   - whole-disk claims are refused on any disk with a foreign partition. This
     narrows what the owner accepted earlier: Read the room could otherwise
     offer to wipe the Fedora disk, so under this default it cannot, and
     reversing that is an explicit later decision.

   The alternative, a whole-disk claim and an installer that is careful, is
   simpler and leaves one userland bug between Pyxis and the Fedora partition.

Assumption to confirm: FAT32 is a freestanding engine library first, used by the
installer through a partition claim (task 2). A mounted FAT32 volume, a
directory-capability backend for the shell, waits for the
[userspace scheme-provider direction](userspace-scheme-providers.md) and is not
in this milestone.

## Invariants

- Every write is additive or inside the Pyxis partition, and reads are verified
  before and after.
- Refuse on anything uncertain. For the GPT: degraded, ambiguous or unsupported
  metadata, a reserved attribute bit, no unused entry or no aligned free extent of
  the requested size. For the ESP: not FAT32 with 512-byte sectors, the dirty
  flag set, a damaged chain, FSInfo that disagrees with the table, too little
  room, or `EFI/pyxis` holding files the manifest does not name. Also Secure Boot
  enabled, since the kernel is unsigned.
- Pyxis names live under one ESP directory, `EFI/pyxis`, which is also the only
  place an update may write or remove.
- No fault injection, no fake success, no resize, no format of the ESP.

## Tasks

Each task ends with a PR and, where it can, QEMU qualification; the owner goes
ahead task by task.

### 1. NVMe driver

Controller initialization on the PCI discovery already in the kernel: map BAR0,
check CAP (page size, timeout, doorbell stride, command sets), disable and reset
the controller, build the admin queue pair, enable, identify controller and the
namespace, create one I/O queue pair, MSI-X completion like the VirtIO queues, a
bounded request path under the existing block interface, PRP lists for transfers
beyond two pages (npfs uses 64 KiB), and a normal shutdown notification before
power-off or restart, so Pyxis does not add to the unsafe-shutdown count. Reads
first; writes land last in this task, behind the boot option of decision 3, with
the block interface's flush mapped to the NVMe flush command. The admin Get Log
Page for SMART (data units, percentage used, temperature, spare) is read-only and
lets the owner measure wear natively.

QEMU's NVMe emulation (`-device nvme`) is the development target: 512-byte
blocks, MDTS set to the ThinkPad's 64 pages, one and several queues. It does not
reproduce the SK hynix controller's timing, power states or counters. Natively
the first boot reads only: enumerate, identify, read the GPT and sectors, log
page.

**After this task the owner can:** boot a QEMU image installed on an NVMe disk,
and on the ThinkPad see the internal disk, its GPT and its SMART numbers in
Pyxis, and compare read bytes with Linux, writing nothing.

### 2. FAT32 read and write for the ESP

A FAT32 engine scoped to what the ESP needs, built from the installer's existing
ESP writer and bounded reader (`esp.c`, `esp_read.c`) but reading any valid
volume instead of one the installer made:

- **Read:** any cluster size, one or two FATs, directories, long names including
  the NT lowercase flags, checks for chain loops and bounds, the dirty flag and
  FSInfo, and refusal rather than repair.
- **Write, additive only:** create directories and files, extend and replace
  Pyxis-owned files, allocate by searching the FAT, never touch an entry it did
  not create. Pyxis chooses 8.3-compatible names where it can, so the long-name
  writer covers only what is left.
- **Crash order:** data clusters first, the chain next, both FATs in a fixed
  order, FSInfo adjusted or marked unknown, the directory entry last, so a crash
  leaves unreferenced clusters or a missing file, never one that names garbage.
  There is no journal; leaked clusters are for Fedora's `fsck.fat` to reclaim,
  and the engine never sets or clears the volume's dirty flag.
- **Replacing a file:** write under a temporary name in the same directory,
  then one 32-byte directory entry changes name, a single-sector update. A set of
  three files is not atomic; an interrupted update leaves Pyxis unbootable and
  Fedora unharmed, repaired by booting the live image and choosing Update, the
  recovery the update already has.

Validated on disposable FAT images in QEMU and on a USB stick, with host
`fsck.fat`, `mtools` and Linux mounts reading what Pyxis wrote and Pyxis reading
what they wrote, and byte comparison of everything outside the expected sectors.
Reading the real ESP happens natively and read-only. No new test harness.

**After this task the owner can:** list and read files on any FAT32 volume or the
ThinkPad's real ESP from Pyxis, long names included, and have Pyxis add files to a
scratch FAT image that Linux checks clean.

### 3. Installer coexistence mode

A third installer action beside Install and Update, offered only on a disk that
decision 3's checks accept. Its eligibility is its own; the exact two-partition
layout check stays for the existing modes.

- **Plan and confirm:** show the disk's partitions, the free extent chosen (the
  largest gap, 1 MiB aligned, 64 GiB by default, up to the owner's size), the new
  entry, the ESP and exactly which files will be created. Typed consent, as
  today, and no writes before it.
- **Order:** format the pool inside the still-unannounced free extent, write
  `bin` programs, write the Pyxis files into `EFI/pyxis` (kernel, archive,
  revision and an entry text file), flush, then add the GPT entry through the
  kernel operation, rescan, and verify by reading back everything including that
  existing partitions are byte-identical.
- **Boot configuration:** `installed.lua` names the pool as partition 2; the
  installer rewrites that number in the in-memory archive it already filters. A
  renumbered table fails closed because the mount does not find a pool.
- **Limine entry:** the installer prints and saves the entry (`boot():/EFI/pyxis/caelum.elf`
  and the archive, with the pool disk's GUID on the command line) and does not
  edit `limine.conf`. Verify the chainload key for Fedora's entry against the
  pinned Limine's documentation; this proposal has not.
- **Update:** the program stage writes the pool as now, the ESP stage touches only
  `EFI/pyxis`, and nothing is formatted or removed elsewhere. The coexistence
  update reports when the shipped Limine differs from the one on the ESP but
  does not change it.

Qualified in QEMU on an NVMe image shaped like the ThinkPad's disk: the same
three partitions with the gap, a copy of a real Fedora ESP's directory tree, and
byte comparison of every sector outside the new entry, the GPT, the FAT tables,
`EFI/pyxis` and the pool. Limine in OVMF boots Pyxis and chainloads a stand-in for
the Fedora entry.

**After this task the owner can:** rehearse the whole coexistence install and
update in QEMU against a faithful copy of the disk layout, boot both entries, and
inspect the byte-level proof that nothing existing changed.

### 4. Native qualification on the ThinkPad

Preparation first, all the owner's, with nothing from Pyxis yet:

- Full backups on external media: `dd` of the GPT and protective MBR regions, of
  the ESP and of `/boot`, `sgdisk --backup`, the output of `efibootmgr -v`,
  `lsblk -f`, the ESP's `fsck.fat -n`, and a copy of the root filesystem's
  contents that matter. A Fedora live USB with the restore commands written down:
  `sgdisk --load-backup`, `dd` back of the ESP, `efibootmgr --create` for Fedora.
- Run `fsck.fat` on the ESP and record cluster size, FATs and dirty flag, which
  the inventory lacks, and the root partition's filesystem and encryption.
- Shrink the root partition per decision 1 and boot Fedora once.

Then in order, each a separate owner-run step with results recorded in an
experiment record: read-only Pyxis boot and SMART numbers; one conservative write
phase inside the new partition only (a megabyte pattern, flush, read back,
compare); the pool format; the ESP additions; a firmware boot of Limine's Pyxis
entry; the Fedora entry; the update path; a final Fedora boot, `fsck.fat` and a
`btrfs`/`ext4` check. Fedora's GRUB and firmware entry stay until the owner
removes them. After the BootOrder change, the owner rechecks it after Fedora
updates; whether shim or firmware updates rewrite it is unverified.

**After this task the owner can:** use Pyxis on the internal NVMe beside Fedora,
choosing at boot, with a rehearsed way back to Fedora alone.

## Wear budget

The owner's budget is no faster than one SSD per six months. The drive's own
numbers give a loose upper bound: 17.3 TB written at 2% used implies a rated life
of about 870 TB (700 TB to 1.15 PB with rounding of the 2%), so one drive per six
months is 3.8 to 6.3 TB a day. That is the vendor's estimate, not a verified
rating, and the SMART snapshot is from 2026-10-02. Pyxis needs a tiny fraction of
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
- Mounting Fedora's filesystems, a mountable FAT32 backend, and Windows.
- Secure Boot, a signed kernel and TPM measurement.
- Hibernate and suspend interaction with the shared disk.
- Keeping Pyxis's pool partition number stable if Fedora tools renumber the table.

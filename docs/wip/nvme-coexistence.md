# Pyxis beside Fedora on the internal NVMe

Status: **proposal; nothing is implemented and no task is assigned.** The owner
accepted decisions 1 and 3 and the FAT32 assumption on 2026-10-10 and changed
decision 2: Limine replaces GRUB. The direction is the owner's: Fedora stays on the
ThinkPad's internal NVMe, Pyxis gets a pool beside it, and the installer learns a
coexistence mode that never destroys anything. It builds on the NVMe and FAT32 items in
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

## Decisions

1. **Where the free space comes from (accepted).** The disk has none, and Pyxis must not
   resize anything. **Before the installer runs, the owner shrinks
   Fedora's 235.9 GiB partition and its filesystem from Linux with Fedora's own
   tools, leaving at least 64 GiB unallocated at the end of the disk, after a
   verified backup.** The installer only ever uses space that is already
   unallocated and refuses otherwise. This shrink is the one step in the whole
   plan that can lose Fedora data, and it is the owner's, outside Pyxis. The
   alternative is another disk or a USB drive for Pyxis, which this milestone
   does not cover.
2. **Limine replaces GRUB (changed by the owner).** Limine becomes the ESP's boot
   manager. It boots Fedora directly through its Linux protocol, with Fedora's
   kernel, initramfs and command line, and boots Pyxis as another entry. The
   owner dislikes GRUB and multiboot2 and does the swap from Linux, with the old
   GRUB entry kept as a tested fallback until Limine is proven
   ([the swap](#limine-as-fedoras-boot-manager)). The installer's part is unchanged:
   **it never edits `limine.conf` or firmware variables.** It writes the Pyxis
   files and prints the entry text, including the pool's disk GUID, for the owner
   to paste.
3. **Write authority and the internal disk (accepted).** **The kernel, not the
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
     offer to wipe the Fedora disk, so it cannot, and
     reversing that is an explicit later decision.

   The alternative, a whole-disk claim and an installer that is careful, is
   simpler and leaves one userland bug between Pyxis and the Fedora partition.

Accepted assumption: FAT32 is a freestanding engine library first, used by the
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
  room, or `EFI/pyxis` holding files the manifest does not name. The installer
  cannot see Secure Boot; it stays off by the owner's choice (see below).
- Pyxis names live under one ESP directory, `EFI/pyxis`, which is also the only
  place an update may write or remove.
- No fault injection, no fake success, no resize, no format of the ESP.

## Limine as Fedora's boot manager

Step 0, the owner's, from Linux, needs no Pyxis code and comes before any Pyxis
write, so Fedora booting through Limine is proven alone first.

**Layout.** Pyxis's pinned Limine (`boot://share/installer/BOOTX64.EFI`, so the
loader and its `limine.conf` syntax match the Pyxis entry) goes in its own ESP
directory, with a `limine.conf` where that Limine looks for it (the existing
images use `boot/limine/limine.conf` on the ESP; the search path is unverified
here, so rehearse it). A new firmware entry puts Limine first in the boot order.
Fedora's `\EFI\fedora` files, `\EFI\BOOT` fallback and its firmware entry are
not modified.

**Fedora entries.** Each is `protocol: linux` with `path` and `module_path` on the
`/boot` partition by GPT GUID (`guid(...)` rather than `boot()`, which is the ESP),
the initramfs, and `cmdline` taken from Fedora's own boot entry, so `root=`,
`rootflags=` and any `rd.luks` options come from Fedora rather than from copies.
Limine reads ext2, ext3, ext4 and FAT only. If `/boot` is ext4, which is Fedora's
default but unrecorded in the inventory, nothing is copied; on any other type the
hook would have to copy kernels onto the 600 MiB ESP, which is a different and
costlier plan. Loading the kernel through its EFI stub (`protocol: efi`) is the
alternative; it needs the initramfs handed over by the loader, which is
unverified, so the Linux protocol is the default. Path syntax and keys are
checked against Limine 12.9.0's documentation, which this repository does not
carry, before anything is written.

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
needed to boot. The machine has a TPM 2.0 and the inventory does not say whether
the root is encrypted or unlocked from it; a binding to boot-loader measurements
would stop unlocking when the loader changes, so the passphrase is kept at hand.

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
  edit `limine.conf`, whose Fedora block belongs to the step-0 plugin. The owner
  pastes the Pyxis block outside the plugin's markers. A kernel update and a
  Pyxis update never share a file.
- **Update:** the program stage writes the pool as now, the ESP stage touches only
  `EFI/pyxis`, and nothing is formatted or removed elsewhere. The coexistence
  update reports when the shipped Limine differs from the one on the ESP but
  does not change it; the owner updates Limine and re-proves Fedora's entries.

Qualified in QEMU on an NVMe image shaped like the ThinkPad's disk: the same
three partitions with the gap, a copy of a real Fedora ESP's directory tree, and
byte comparison of every sector outside the new entry, the GPT, the FAT tables,
`EFI/pyxis` and the pool. Limine in OVMF boots Pyxis and, on the Fedora VM image
of step 0, its kernel, with the plugin's block left byte-identical by a Pyxis
update.

**After this task the owner can:** rehearse the whole coexistence install and
update in QEMU against a faithful copy of the disk layout, boot both entries, and
inspect the byte-level proof that nothing existing changed.

### 4. Native qualification on the ThinkPad

Step 0 is done first. Then the preparation, all the owner's, with nothing from
Pyxis yet:

- Full backups on external media: `dd` of the GPT and protective MBR regions, of
  the ESP and of `/boot`, `sgdisk --backup`, the output of `efibootmgr -v`,
  `lsblk -f`, the ESP's `fsck.fat -n`, and a copy of the root filesystem's
  contents that matter. The ESP and `/boot` copies include Limine's files and the
  plugin. A Fedora live USB with the restore commands written down:
  `sgdisk --load-backup`, `dd` back of the ESP, `efibootmgr --create` for Fedora.
- Run `fsck.fat` on the ESP and record cluster size, FATs and dirty flag, which
  the inventory lacks, and the root partition's filesystem and encryption.
- Shrink the root partition per decision 1 and boot Fedora once.

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
- Secure Boot, a signed Limine or kernel and TPM measurement: it stays off.
- Hibernate and suspend interaction with the shared disk.
- Keeping Pyxis's pool partition number stable if Fedora tools renumber the table.

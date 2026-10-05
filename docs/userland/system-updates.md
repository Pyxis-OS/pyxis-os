# System updates through the installer

Boot the newer live image, select **Install Pyxis** in Limine, then choose
**Update** in the [native installer](installer.md). The updater replaces the
installed boot files while preserving the GPT, partition identities and bounds,
and the npfs pool containing `system://` data. It uses the same trusted
[raw-disk and boot-source authority](../devices/installer-authority.md) as install.

## Eligibility and confirmation

The installer lists disks with an eligibility reason, selects a sole eligible
disk or asks for its displayed number, and shows installed and live kernel
revisions. A missing, empty, structurally damaged or nonprintable `boot/revision` displays
`unknown`; this optional record does not authorize an update. Retained VirtIO
and USB candidates use the same selection and typed-consent rules. USB write
claims require known write-protection-clear media and successful real blocking
cache synchronization, with no latched write failure or mounted/claimed device.
Inventory waits for sealed discovery; partial USB coverage may conceal other
eligible disks, while lost registry records or incomplete VirtIO bookkeeping
refuse inventory.

An eligible disk has healthy matching GPT copies with the exact installer
layout: the 512 MiB ESP at 1 MiB and an npfs partition extending to the aligned
end before backup GPT metadata. Its pool must pass the live format code's
writable-mount metadata admission, have a live `system` volume and an empty
selected journal. A valid npfs header/control copy may survive a damaged peer,
following kernel admission. This is metadata admission, not whole-pool fsck.

A selected committed journal is refused: boot the installed system once to
recover it, then update. The updater never mounts during eligibility inspection,
replays, migrates or writes the pool. Finalized and unfinalized installations
both qualify; Update needs no `SAFE_TO_WIPE` marker and has no Read the room mode.

ESP inspection distinguishes valid contents, rebuildable contents and refusal.
Missing or structurally damaged FAT/boot contents are rebuildable using the
healthy GPT and compatible empty-journal pool as the recovery anchor. When the
installed configuration is readable, it must not carry a foreign or invalid disk
binding. Readable configuration naming another disk refuses even when other
ESP metadata or the optional revision record is damaged. Raw I/O and allocation
failures, or unsupported partition extents, fail closed rather than becoming
permission to rebuild. The bounded reader follows installed configuration paths
and traversed FAT copies/chains; it is not a general FAT service.

Type the exact word `update` to confirm. Cancellation makes no writes. The
installer acquires exclusive raw access, repeats GPT, pool and ESP eligibility
checks, and compares both partitions' bounds and all three GPT GUIDs with the
selected layout before the first write. It also plans the new boot tree and
checks that the sources fit before mutation.

## Replacement and verification

All installed software is in the boot archive on the ESP. Update creates a
fresh FAT32 filesystem inside the existing ESP and writes:

- `EFI/BOOT/BOOTX64.EFI`;
- `boot/caelum.elf`;
- `boot/initrd.cpio`;
- `boot/limine/limine.conf`;
- `boot/revision`.

The kernel and archive come from the live boot's original immutable source
files. The configuration comes from the packaged template, using the existing
disk GUID, timeout zero and installed init, with no Install entry. The revision
record comes from the running live kernel's SYSTEM_INFO. Install and Update
share the fresh-ESP writer; Update invokes neither GPT writing nor pool formatting.
The writer clears and flushes the reserved area, including both FAT boot sectors,
before replacing metadata or file data. It keeps boot geometry invalid until
the complete replacement tree has been flushed, then writes both boot sectors.
The final sync below persists those sectors. Stale configuration and revision
bytes therefore cannot classify a partly rewritten tree as a valid installation,
even when the new files occupy different clusters. Once boot geometry is
published, the new tree is already durable, subject to the device's flush contract.
Replacing the whole ESP removes any other ESP files. There are no side-by-side
kernels or fallback entry.

Success requires a disk flush, explicit raw release and GPT rescan, FAT directory
traversal with byte-for-byte comparison against the boot sources, then a normal
read-only reopen of partition 2's `system` root. Only then does the installer
report `updated`. Remove the live medium and boot the target. Verification
retains the pool until reboot, so a second update needs another live-media boot.
The reopen establishes that the system root can be opened; it does not compare
ordinary user files or replace whole-pool fsck.

## Recovery and qualification

A failure after mutation begins reports that the EFI files may be partially
rebuilt and directs the user to boot the live image again and choose Update.
The GPT and pool remain the recovery anchor, and the same eligibility checks
apply on the next run. There is no automatic retry, rollback, pool repair or
power-loss durability guarantee.

The [recognition record](../development/experiments/system-updates-task1/README.md)
covers the initial action menu, revision recording and candidate/refusal cases.
The [ESP-update qualification record](../development/experiments/system-updates-task2/README.md)
records the finalized installation update, target-only reboot, preserved data,
host structural checking and recovery of an interrupted ESP replacement.
These records qualify the original VirtIO target path. Installer USB raw-disk
authority is now implemented, using the same Update workflow; its integration
checks are in the [C.3 USB-target
record](../development/experiments/usb-installer-c3/README.md). C.4 of the
[USB plan](../wip/usb-installation.md) installed natively on the ThinkPad; the
owner deferred its physical Update round trip to the next real update. Power
loss and uncertain I/O remain [deferred](../technical-debt.md#installer-inspection-and-recovery-limits).
Updates from inside a running installed system, image downloading and npfs
format migration are outside this interface.

# System updates through the installer

Boot the newer live image, select **Install Pyxis** in Limine, then choose
**Update** in the [native installer](installer.md). Update has two stages. It
first writes the new revision's programs into the pool's `bin` volume, then
replaces the installed boot files. It preserves the GPT, partition identities
and bounds, and every existing volume, including `system://` and `home://`
data. It uses the
same trusted [raw-disk and boot-source authority](../devices/installer-authority.md)
as install.

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
recover it, then update. Eligibility inspection never mounts, replays or
writes the pool; only the confirmed program stage mounts it. Finalized and
unfinalized installations both qualify; Update needs no `SAFE_TO_WIPE` marker
and has no Read the room mode.

ESP inspection distinguishes valid contents, rebuildable contents and refusal.
A valid installed configuration has exactly two command lines: one with only
`init=boot://boot-init.pxe` and this disk's `mount.disk`, and the rescue entry's,
which adds `boot.default_config=1`. Any other configuration is rebuildable.
Installations from before boot init, including 0.0.2 and the `boot://` rename,
have a single `space.pyxis=` command line. Update offers them as rebuildable,
reporting damaged or missing boot files and an unknown installed revision, and
replaces the ESP as usual. Existing volumes, including any
`system://config/boot.lua`, are untouched; the program stage adds `bin`.
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

## Program stage

The installed boot archive keeps only the rescue set: the executables named in
`boot://share/installer/rescue.list` and every non-executable entry. The other
`.pxe` executables move to the pool's `bin` volume, one directory per kernel
revision. The installer releases its whole-disk claim and mounts the pool
through the disk handle. It creates an empty `home` volume when the pool has
none, without opening it, and stops there if that fails. It creates the `bin`
volume when it is missing, writes
every moved executable into `bin/REVISION`, syncs the pool and compares each
copy with its source. A rerun replaces the files of an earlier partial
`bin/REVISION`. Builds without a revision use `bin/unknown`.

Writing the programs before the ESP is the commit rule. Until the ESP holds the
new kernel, the disk boots its old kernel, and boot init binds that kernel's
own `bin/REVISION`. An Update interrupted in the program stage therefore leaves
the previous revision running.

When the new revision equals the running one, as on a rerun, the program stage
rewrites the directory `bin://` is bound to. An interruption can then leave it
mixed until an Update completes. Builds of the same commit have the same files,
so this matters mainly for builds without a revision, which share `bin/unknown`.

## Replacement and verification

The installer then claims only the ESP partition, leaving the pool mounted.
Update creates a fresh FAT32 filesystem inside the existing ESP and writes:

- `EFI/BOOT/BOOTX64.EFI`;
- `boot/caelum.elf`;
- `boot/initrd.cpio`;
- `boot/limine/limine.conf`;
- `boot/revision`.

The kernel comes from the live boot's original immutable source file. The
installed archive is the live archive without the moved executables, filtered
in memory. The configuration comes from the packaged template, using the
existing disk GUID, the three-second menu and the normal and rescue entries,
with no Install entry. The revision
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

Success requires a disk flush, release of the partition claim without a GPT
rescan, FAT directory traversal with byte-for-byte comparison against the boot
sources, then a normal read-only reopen of partition 2's `system` root. The
installer then removes every `bin` revision directory except the new one and
the one the disk booted until now; when that one is unknown, as after damaged
boot files, it removes none. Only then does the installer report `updated` and [offers a restart](installer.md#restart). Remove the live medium and boot the target. Verification
retains the pool until reboot, so a second update needs another live-media boot.
The reopen establishes that the system root can be opened; it does not compare
ordinary user files or replace whole-pool fsck.

## Recovery and qualification

A failure in the program stage reports that the disk still boots its previous
revision. A failure after the ESP claim reports that the EFI files may be
partially rebuilt. Both direct the user to boot the live image again and choose
Update.
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
record](../development/experiments/usb-installer-c3/README.md). The first
[native USB installation](../devices/usb-installation.md#validation) on the
ThinkPad passed one physical Update round trip, preserving a synced file. Power
loss and uncertain I/O remain [deferred](../technical-debt.md#installer-inspection-and-recovery-limits).
Updates from inside a running installed system, image downloading and npfs
format migration are outside this interface.

# Native installation

Build live media with `make -j16 image BOOT_MENU_TIMEOUT=5`, then select
**Install Pyxis** in Limine. The native installer takes no command-line options.
It runs through trusted install init with the explicit
[disk and boot-file grants](../devices/installer-authority.md). Source files are
already in RAM; installing does not require a driver for the live medium.

Choose **Install** first, then **Proceed with installation** for marked development disks or **Read the
room** to include blank, foreign, partially marked and damaged contents. The
installer waits for sealed boot discovery, lists retained VirtIO and USB disk
candidates and explains eligibility. Partial USB coverage can hide additional
disks; it does not prevent listing the observed candidates. Lost registry records
or incomplete VirtIO bookkeeping refuse inventory. It selects a sole eligible
disk or asks for its displayed number when several qualify, then shows size,
GUID and escaped labels of the volumes that will be destroyed.
A zeroed first sector with neither GPT header present is listed as blank;
this describes partition metadata, not a scan of every disk byte.

Normal installation requires a validated protective GPT, at least one recognized
npfs pool, a nonempty set of live volumes in every such pool and a regular root
`SAFE_TO_WIPE` in every live volume. Consent inspection reads raw metadata with
validated COMMITTED journal images overlaid in memory; it never persists replay.
Any individually readable nonempty pool with no regular markers vetoes the
entire disk in both modes. A marked pool cannot override another final pool.
Mounted/retained pools, raw claims and unsupported, failed, read-only or
flush-incapable devices are excluded. USB targets must have known
write-protection-clear media and a successful blocking cache-synchronization
qualification; reads alone do not qualify a target. Unsupported GPT features/capacity and
inspection I/O/allocation failures also fail closed.

The journal prompt starts with ceil(pool bytes / 128), rounded up to MiB,
with an 8 MiB default minimum and 1 GiB cap. A 256 GB disk defaults to 1 GiB.
Edit the field or press Enter; explicit choices may be 1–1024 MiB when metadata
and bootstrap files fit. The final layout is displayed before requiring the
exact word `wipe`. Cancellation makes no target writes. Consent is inspected
again under exclusive raw access before the first write.

Installation rebuilds the whole selected disk: a fresh GPT, a 512 MiB FAT32 ESP
starting at 1 MiB, then an npfs pool extending to the aligned end before backup
GPT metadata. The pool has a `system` volume with an empty regular root marker,
a `bin` volume and an empty `home` volume. The installer formats the pool and
writes the GPT, releases its whole-disk claim, then creates `home` through the
disk handle, mounts the pool and writes the executables outside the archive's rescue list into `bin/REVISION`, as
[Update's program stage](system-updates.md#program-stage) does. Only then does
it claim the ESP partition and write it. Limine is at `EFI/BOOT/BOOTX64.EFI`;
the original kernel, the rescue boot archive (the live archive without the
moved executables), the configuration and a newline-terminated kernel
`revision` record are under `boot`. The installed configuration fills the packaged
template with a three-second timeout and the new disk GUID, and omits the
installer entry and any global `default_entry`. It has two entries:
`Pyxis OS (Caelum)`, booted on timeout, with
`init=boot://boot-init.pxe mount.disk=<GUID>`, and `Pyxis OS (rescue)`, which
adds `boot.default_config=1` to ignore the pool's boot configuration. Boot init
then starts the [installed spaces](init.md#boot-configuration): by default the
`pyxis` space, whose `init-installed` receives partition 2's system and home
volumes read-write as `system://` and `home://`, starts in `home://` and starts
the ordinary local session. `tmp://` stays RAM-backed.

Success requires the program copies to match their sources, then ESP flush and
release, FAT directory traversal and byte-for-byte source comparisons, then
normal read-only pool reopening and marker verification. Only then does the program report `installed`. Remove the
live medium and boot from the target. Reinstall requires another live-media
boot because verification retains the pool until reboot. Delete
`system://SAFE_TO_WIPE` and sync that directory to mark the installation final.

The first screen also offers **Update**. It lists eligible installations and
installed/live revisions, then requires the exact word `update`. Update preserves
the GPT and existing volumes, rechecks eligibility under exclusive raw access,
writes the new revision's programs into `bin`, then replaces the whole ESP,
flushes and releases the claim, byte-verifies the boot files, reopens `system`
read-only and removes older program revisions before reporting `updated`. Healthy installer-layout
GPT and a compatible empty-journal pool also permit rebuilding a damaged or
missing ESP; readable foreign disk bindings and raw I/O/allocation failures
refuse. A selected committed journal must first be recovered by booting the
installed system. No wipe marker is needed. See
[system updates](system-updates.md) for admission, recovery and qualification.

## Restart

After the final `installed` or `updated` message the installer asks to restart,
reminding the user to remove the medium first:

```text
Remove the install medium before the machine restarts.
Press Enter to restart, or type stay to remain here:
```

Only an empty line restarts. Any other text, Ctrl+C or a failed read leaves the
installer with status 0, as before the offer. Failures and cancellations never
offer a restart. Restart is the kernel's [power restart](../kernel/acpi.md#power-off-and-restart):
it holds user tasks, flushes and checkpoints every mounted native pool, including
the one the installer retains, then resets. If the call returns, the installer
prints its status, says the installation or update is complete and to restart by
hand, and exits 0. Without the medium removed, the restart boots the live image
again.

`init-install` gives the installer its `power` resource narrowed to the RESTART
right, so the installer cannot power off. A boot without `power` offers nothing.

Installation writes allocated metadata and boot-file storage, without secure
erasure of free space. A failed installation may leave a partial disk; the failure message
directs you to run the installer again and choose **Read the room**. Boot the
live image again first; the same consent vetoes still apply. There is no automatic
retry, rollback or repair. V1 uses existing writable disk drivers and supports logical
sector sizes 512 and 4096, matching kernel GPT discovery/rescan. It adds no general FAT driver or
firmware-variable updater. See the
[validation record](../development/experiments/native-filesystem-task4.3/README.md)
and [remaining limits](../technical-debt.md#installer-inspection-and-recovery-limits).
Merged-main [QEMU end-to-end qualification](../development/experiments/native-filesystem-task5/README.md)
includes USB-backed live-media loading and CPU entropy. The separate
[C.3 USB-target record](../development/experiments/usb-installer-c3/README.md)
documents installer raw-authority integration checks. The first
[native USB installation](../devices/usb-installation.md#validation) installed
onto a USB stick on the ThinkPad from PXE live media. The stick then booted
alone, kept a synced file across a synced power-off and passed one Update round
trip; see the
[owner-reported record](../targets/t14-gen1-amd/usb-bringup.md#2026-10-05-first-native-installation-c4).
The [program reference](../../userspace/installer/README.md) describes its SDK
boundary; the [format and host tools](../../fs/docs/npfs-host-tools.md) remain
owned by pyxis-fs.

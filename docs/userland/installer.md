# Native installation

Build live media with `make -j16 image BOOT_MENU_TIMEOUT=5`, then select
**Install Pyxis** in Limine. The native installer takes no command-line options.
It runs through trusted install init with the explicit
[disk and boot-file grants](../devices/installer-authority.md). Source files are
already in RAM; installing does not require a driver for the live medium.

Choose **Install** first, then **Proceed with installation** for marked development disks or **Read the
room** to include blank, foreign, partially marked and damaged contents. The
installer lists every disk and explains eligibility. It selects a sole eligible
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
flush-incapable devices are excluded. Unsupported GPT features/capacity and
inspection I/O/allocation failures also fail closed.

The journal prompt starts with ceil(pool bytes / 128), rounded up to MiB,
with an 8 MiB default minimum and 1 GiB cap. A 256 GB disk defaults to 1 GiB.
Edit the field or press Enter; explicit choices may be 1–1024 MiB when metadata
and bootstrap files fit. The final layout is displayed before requiring the
exact word `wipe`. Cancellation makes no target writes. Consent is inspected
again under exclusive raw access before the first write.

Installation rebuilds the whole selected disk: a fresh GPT, a 512 MiB FAT32 ESP
starting at 1 MiB, then an npfs pool extending to the aligned end before backup
GPT metadata. The pool has one `system` volume with an empty regular root marker.
Limine is at `EFI/BOOT/BOOTX64.EFI`; the original kernel, whole boot archive and
configuration and a newline-terminated kernel `revision` record are under `boot`. The installed configuration fills the packaged
template with timeout zero and the new disk GUID, and omits the installer entry
and any global `default_entry`.
Fixed `init-installed` mounts partition 2's system volume read-write as
`system://` and starts the ordinary local session. Home stays RAM-backed.

Success requires flush, explicit raw release/GPT rescan, FAT directory traversal
and byte-for-byte source comparisons, then normal read-only pool reopening and
marker verification. Only then does the program report `installed`. Remove the
live medium and boot from the target. Reinstall requires another live-media
boot because verification retains the pool until reboot. Delete
`system://SAFE_TO_WIPE` and sync that directory to mark the installation final.

The first screen also offers **Update**. This first implementation lists and
selects existing installations, then reports that inspection completed without
writing. ESP replacement and typed `update` confirmation remain
[task 2](../wip/system-updates.md). Update requires healthy matching GPT copies
with the exact installer layout, a writable-mount-compatible pool with an empty
selected journal and a live `system` volume, and FAT32 boot configuration naming
this disk GUID and installed init. A valid pool header/control copy can survive
a damaged peer, following kernel admission. A committed journal is refused:
boot the installed system once to recover it, then update. No wipe marker is
needed, and inspection never mounts, replays or writes the pool.

Update shows both the live kernel revision and the installed `boot/revision`;
older installations without this record show `unknown`. The bounded FAT reader
checks the required directory/file chains and mirrored FAT entries, without
requiring a pristine fresh-writer byte layout or rejecting unrelated files.
See the [task-1 qualification](../development/experiments/system-updates-task1/README.md).

This writes allocated metadata and boot-file storage, without secure erasure of
free space. A failed mutation may leave a partial disk; the failure message
directs you to run the installer again and choose **Read the room**. Boot the
live image again first; the same consent vetoes still apply. There is no automatic
retry, rollback or repair. V1 uses existing writable disk drivers and supports logical
sector sizes 512 and 4096, matching kernel GPT discovery/rescan. It adds no general FAT driver or
firmware-variable updater. See the
[validation record](../development/experiments/native-filesystem-task4.3/README.md)
and [remaining limits](../technical-debt.md#installer-inspection-and-recovery-limits).
Merged-main [QEMU end-to-end qualification](../development/experiments/native-filesystem-task5/README.md)
includes USB-backed live-media loading and CPU entropy. Native ThinkPad
installation is deferred until writable USB storage is available.
The [program reference](../../userspace/installer/README.md) describes its SDK
boundary; the [format and host tools](../../fs/docs/npfs-host-tools.md) remain
owned by pyxis-fs.

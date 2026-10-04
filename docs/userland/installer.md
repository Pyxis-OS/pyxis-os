# Native installation

Build live media with `make -j16 image BOOT_MENU_TIMEOUT=5`, then select
**Install Pyxis** in Limine. The native installer takes no command-line options.
It runs through trusted install init with the explicit
[disk and boot-file grants](../devices/installer-authority.md). Source files are
already in RAM; installing does not require a driver for the live medium.

Choose **Proceed with installation** for marked development disks or **Read the
room** to include blank, foreign, partially marked and damaged contents. The
installer lists every disk and explains eligibility. It selects a sole eligible
disk or asks for its displayed number when several qualify, then shows size,
GUID and escaped labels of the volumes that will be destroyed.

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
configuration are under `boot`. The installed configuration fills the packaged
template with timeout zero and the new disk GUID, and omits the installer entry.
Fixed `init-installed` mounts partition 2's system volume read-write as
`system://` and starts the ordinary local session. Home stays RAM-backed.

Success requires flush, explicit raw release/GPT rescan, FAT directory traversal
and byte-for-byte source comparisons, then normal read-only pool reopening and
marker verification. Only then does the program report `installed`. Remove the
live medium and boot from the target. Reinstall requires another live-media
boot because verification retains the pool until reboot. Delete
`system://SAFE_TO_WIPE` and sync that directory to mark the installation final.

This writes allocated metadata and boot-file storage, without secure erasure of
free space. A failed mutation may leave a partial disk; there is no retry,
rollback or repair. V1 uses existing writable disk drivers and supports logical
sector sizes 512, 1024, 2048 and 4096. It adds no general FAT driver or
firmware-variable updater. See the
[validation record](../development/experiments/native-filesystem-task4.3/README.md)
and [remaining limits](../technical-debt.md#installer-inspection-and-recovery-limits).
The [program reference](../../userspace/installer/README.md) describes its SDK
boundary; the [format and host tools](../../fs/docs/npfs-host-tools.md) remain
owned by pyxis-fs.

# Native installation

Build live media with `make -j16 image BOOT_MENU_TIMEOUT=5`, then select
**Install Pyxis** in Limine. The stock entry starts the interactive planner. Trusted launchers may pass
`--plan`, `--dry-run` or `--apply` with explicit consent as described below.
It runs through trusted install init with the explicit
[disk and boot-file grants](../devices/installer-authority.md). Source files are
already in RAM; installing does not require a driver for the live medium.

The separate [read-only FAT32 inspector](../devices/fat32-inspection.md) lists
and reads a selected GPT volume without invoking Install/Update. Strict inspector
refusal never authorizes an ESP rebuild.

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
flush-incapable devices are excluded. USB targets must have known write-protection-clear media and the delivered
[flush/write-through qualification](../devices/usb-storage.md); reads alone do
not qualify a target. Unsupported GPT features/capacity and
inspection I/O/allocation failures also fail closed.

The journal prompt starts with ceil(pool bytes / 128), rounded up to MiB,
with an 8 MiB default minimum and 1 GiB cap. A 256 GB disk defaults to 1 GiB.
Edit the field or press Enter; explicit choices may be 1–1024 MiB when metadata
and bootstrap files fit. The final layout is displayed before requiring the
exact word `wipe`. Cancellation before Apply makes no target writes. Consent is
inspected again through fresh read-only inspection; the single-use erase grant
also revalidates the exact reviewed GPT/geometry before write admission.

Installation rebuilds the whole selected disk: a fresh GPT, a 512 MiB FAT32 ESP
starting at 1 MiB, then an npfs pool extending to the aligned end before backup
GPT metadata. The pool has a `system` volume with an empty regular root marker,
a `bin` volume and an empty `home` volume. The installer formats the pool and
writes the GPT and initializes the ESP through its reviewed erase writer,
releases it, opens the explicitly bound pool and releases that raw claim before
creating `home`, mounting volumes and copying non-rescue root `.pxe` files and complete
root `.pxb` trees into `bin/REVISION`, as
[Update's program stage](system-updates.md#program-stage) does. Only then does
it open the ESP through native FAT files and copy/verify the fixed boot files.
Limine is at `EFI/BOOT/BOOTX64.EFI`;
the original kernel, the rescue boot archive (the live archive without the
moved program roots and bundle descendants), the configuration and a newline-terminated kernel
`revision` record are under `boot`. The installer generates its own configuration
with a three-second timeout and the new disk GUID. Its explicit allowlist contains
exactly two entries; live-media installer and FAT32 tool entries are never copied:
`Pyxis OS (Caelum)`, booted on timeout, with
`init=boot://boot-init.pxe mount.disk=<GUID>`, and `Pyxis OS (rescue)`, which
adds `boot.default_config=1` to ignore the pool's boot configuration. Boot init
then starts the [installed spaces](init.md#boot-configuration): by default the
`pyxis` space, whose `init-installed` receives partition 2's system and home
volumes read-write as `system://` and `home://`, starts in `home://` and starts
the ordinary local session. `tmp://` stays RAM-backed.

Success requires the program copies to match their sources, then native ESP
sync, canonical traversal, held-object identity and full size/hash verification,
explicit view retirement and read-only pool/marker verification. Only then does the program report `installed`. Remove the
live medium and boot from the target. Reinstall requires another live-media
boot because verification retains the pool until reboot. Delete
`system://SAFE_TO_WIPE` and sync that directory to mark the installation final.

The first screen also offers **Update**. It lists eligible installations and
installed/live revisions, then requires the exact word `update`. Update preserves
the GPT and existing volumes, rechecks eligibility under exclusive raw access,
writes the new revision's programs into `bin`, then replaces fixed boot files
through native FAT without formatting a healthy ESP, verifies them, retires
the view, reopens `system`
read-only and removes older program revisions before reporting `updated`. Healthy installer-layout
GPT and a compatible empty-journal pool also permit rebuilding a damaged or
missing ESP through an explicitly reviewed, one-use ESP-only rebuild grant;
readable foreign disk bindings and raw I/O/allocation failures
refuse. A selected committed journal must first be recovered by booting the
installed system. No wipe marker is needed. See
[system updates](system-updates.md) for admission, recovery and qualification.

## Plans and Programs report

Preparation is read-only: temporary native RO ESP views are retired after
inspection; no RW mount, persistent journal replay, formatting,
cleanup or writes. Normal Review shows the target, journal/policy choices,
selected payload, aggregate program changes and destruction/recovery limits on
one screen at ordinary console sizes. `-v` / `--verbose`, `--plan` and the Print
plan action add per-program old/new sizes, partition extents and volume details.
Its Apply/Print/Dry-run choice edits no disk. Only the exact word
`wipe` or `update` authorizes the prepared plan; exclusive revalidation precedes
the first write and changed inputs refuse without automatic replanning.

Programs are selected non-rescue root `.pxe` files and complete `.pxb` trees.
Compare paths, kinds, empty directories, sizes and SHA-256 per root, showing new,
updated, removed and unchanged. Removed means absent from the new revision;
current/known previous retention still applies. Install lists its selected roots
as new. A known previous tree without a valid inventory requires explicit `scan`
consent for one-time raw read/hash inventory, repeated at exclusive revalidation.
Unknown prior revision/ancestry stays unknown. A valid manifest describes the
recorded revision; it is not a full live audit of subsequent payload changes.

`-v` / `--verbose` also prints detailed phase records locally. `--plan` prints
and exits; `--dry-run` reviews and exits. `--apply` reads one
`pyxis-install-plan 1` frame from stdin, ending with `end` plus LF, rebuilds its
read-only inspection and displays Review. `--apply --confirm=wipe` or
`--apply --confirm=update` provides separate non-interactive consent and skips
restart prompting. Human output/progress uses stderr; stdout carries the data-only
plan. The stock menu also offers replay. A caller may save printed bytes with
its own grants; the installer obtains no writable root to save them. Plans are
not commands and carry no authority. Only trusted install-mode launches have
the [required grants](../devices/installer-authority.md).

Replay requires a uniquely observed validated existing GPT GUID, matching geometry
and consumed state, never an enumeration number. Duplicate identities refuse;
this binds a logical disk, not authenticated physical identity. Blank/damaged
unidentified targets support in-process printing/dry-run and interactive apply;
their serialized plans cannot select a target for apply. Recorded Install IDs
and journal choices are never regenerated during replay. Hashes bind the choices,
all immutable source assets and consumed raw target reads. Missing, malformed or
stale input refuses. Standard input is framed; following bytes cannot amend or
execute that plan.

## Revision inventory

Every installed revision has `bin/REVISION/.pyxis-revision-manifest`: a canonical
text tag, hex-encoded revision, sorted relative-path records, directory records
including empty directories, and file records with byte size/SHA-256. It excludes
itself; its installed file counts toward the 65,536-entry bound. The manifest and
serialized plan each have a 16 MiB cap, alongside 32 directory levels and 255-byte
component names. Long trees admitted by older entry/depth limits may exceed this
metadata cap. Raw inspection also bounds decoded records (including free slots)
at 65,536 per directory. This is consumed-path validation, not whole-pool fsck.
The [source format and ownership](../../userspace/installer/README.md) define
canonical decimals/hex, ancestor validation and stream lifetime.

The executor copies/syncs programs, verifies exact tree structure and source
bytes and hashes destination bytes against the inventory. It then writes, syncs
and byte-verifies the manifest before switching the ESP. Unsigned hashes neither
authenticate a revision nor prove later payload integrity. This is ordinary
filesystem-view verification and may use copied pages; no stronger device-read
contract is introduced. Planning uses raw codecs because a read-only mount would
retain the pool until reboot and prevent later write admission. Staging space is
conservatively checked without credit for cleanup; it is not a promise against
all allocation limits. Classic keeps its fixed boot paths and allowlisted menu
until task 5 adds immutable attempts with committed boot-binding evidence. Native
FAT replaces whole-ESP rewriting; owned configuration publication remains task 5.

[QEMU record](../development/experiments/installer-plan/README.md) separates
baseline costs, measurements, inspected behavior and qualification limits.

## Shared mounted ESP payload stager

The reusable [payload helper](../../userspace/installer/ESP-PAYLOADS.md) stages
kernel, filtered archive and revision bytes, plus optional printed entry text,
in a fresh `EFI/pyxis/<revision>/<attempt>/` directory. It checks reviewed hashes,
synchronizes and reopens the canonical names to compare retained object identity
and content. `ownership.record` describes that exact attempt; paths and hashes
alone confer no authority. Partial attempts are retained on error, without
retry, overwrite or deletion. The caller holds the result through its final
verification and closes it before retiring the mount.

The caller supplies explicitly granted native root/control and bound identity
facts, owns Review/admission, CHECK and interval lifetime, activation and later
cleanup. The helper has no raw-disk, configuration, Limine, firmware or active
boot selection operation and requires no REMOVE grant. Its optional `entry.txt`
is saved evidence, not installed automatically. This extraction changes no
shipped Install/Update execution; coexistence and task 5 integrate it separately.

Traversal requires ENUMERATE as well as LOOKUP: reused `EFI`, `pyxis`, revision
and attempt parents must have the requested canonical name under ASCII FAT case
rules. Short-name aliases of other canonical names refuse. This is checked when
opening staging parents and again during every final verification.

Cancel is available before confirming Apply. The current synchronous executor
does not poll cancellation input during payload hashing, copying or sync; progress
is not a cancellation control. Interrupting confirmed work can leave an
unreferenced partial attempt and never promises rollback of confirmed writes.

[Scratch qualification](../development/experiments/esp-payload-stager/README.md)
records QEMU staging/verification, capacity refusal and retained failure evidence.
It does not qualify configuration activation or publication-interval headroom.

## Progress

Install and Update show one bounded live line for long work: phase, a bar and
completed/total files or bytes when known, otherwise an activity indicator.
Major inspection, copy, verification, EFI publication and cleanup phases leave
one finished line. Short checks stay silent unless they fail. Selected disk
identity is shown again before journal/legacy-inventory questions and in Review.

Copy and program verification count completed regular files and payload bytes,
including bundle descendants. Verification bytes count successful comparisons
once; directories are checked separately. Totals come from the existing archive
pass, without another scan. Removal counts deleted files and directories; raw
stages count successful disk-command bytes, including sector/cluster padding.
Measured byte rates use the read-only clock. Counters describe observed work,
not durability or whole-operation success; a phase ending does not mean success.

Program verification explicitly says **filesystem view; may be cached**. Pool
sync precedes it, but ordinary npfs reads can return retained copied pages. It
is not guaranteed device read-back. ESP verification syncs and reopens native
FAT files, checks held-object identity and hashes complete contents. It does not
prove cache bypass, physical-media behaviour or power-loss safety.

Shared phase/counter records also send detailed boundaries and coalesced updates
to Caelum's log through the trusted install launch's WRITE-only
`installer_diagnostics` console grant. Ordinary programs do not receive it.
`-v` prints these records locally; normal output remains quiet. The standalone
configuration editor keeps its local detailed records and has no diagnostic grant.

Console rows reserve the final column, adapt to current width and redraw at most
four times per second. Minor work becomes visible after one second. Non-console
output leaves final major/long-operation lines, without carriage-return animation.
Clock checks are coalesced after 64 KiB or 32 items. A blocked syscall can show
only a static activity marker. Prompts/errors close the current row. Clock or
output failure disables the corresponding observer without changing confirmed
I/O, consent, checks or publication order; failure to print Review still refuses
apply. The future curses TUI can consume the same records.
[QEMU qualification and earlier costs](../development/experiments/installer-progress/README.md)
record the runtime evidence and limits.

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

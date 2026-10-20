# System updates through the installer

Boot the newer live image, select **Install Pyxis** in Limine, then choose
**Update** in the [native installer](installer.md). Update has two stages. It
first writes the new revision's programs into the pool's `bin` volume, then
replaces the installed boot files. It preserves the GPT, partition identities
and bounds, and every existing volume, including `system://` and `home://`
data. It uses the
same trusted [reviewed disk and boot-source authority](../devices/installer-authority.md)
as install.

## Eligibility and confirmation

The installer lists disks with an eligibility reason, selects a sole eligible
disk or asks for its displayed number, and shows installed and live kernel
revisions. A missing, empty, structurally damaged or nonprintable `boot/revision` displays
`unknown`; this optional record does not authorize an update. Retained VirtIO
and USB candidates use the same selection and typed-consent rules. USB write
claims require known write-protection-clear media and the delivered
[flush/write-through qualification](../devices/usb-storage.md), with no latched
write failure or mounted/claimed device.
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

ESP inspection distinguishes preserved native filesystems, explicitly rebuildable
filesystems and refusal. The installed menu has exactly two command lines: one
with `init=boot://boot-init.pxe` and this disk's `mount.disk`, and the rescue entry's, which adds `boot.default_config=1`. Missing or older boot files on an admitted,
clean native FAT view are replaced without formatting the filesystem.
Installations from before boot init, including 0.0.2 and the `boot://` rename,
have a single `space.pyxis=` command line. Update offers them as rebuildable,
reporting damaged or missing boot files and an unknown installed revision, and
replaces fixed boot files, using explicit reviewed recovery only when the
filesystem itself needs initialization. Existing volumes, including any
`system://config/boot.lua`, are untouched; the program stage adds `bin`, and `packages`
and `http-work` when an older installation lacks them.
Missing or structurally damaged FAT/boot contents are rebuildable using the
healthy GPT and compatible empty-journal pool as the recovery anchor. When the
installed configuration is readable, it must not carry a foreign or invalid disk
binding. Readable configuration naming another disk refuses even when other
ESP metadata or the optional revision record is damaged. Raw I/O and allocation
failures, or unsupported partition extents, fail closed rather than becoming
permission to rebuild. Raw recovery recognition remains separate from native
admission. The native view also checks any readable fixed menu for a foreign or
invalid disk binding before it may be replaced.

Review the [plan and Programs report](installer.md#plans-and-programs-report),
then type the exact word `update` to confirm. Cancellation/dry-run makes no writes. The
installer repeats read-only eligibility and consumed-input checks, then opens
the exact reviewed pool. Any explicit recovery also consumes its separately
reviewed GPT/geometry-bound ESP grant. Changed bounds, GUIDs or consumed state
refuse without automatic replanning. It also plans the new boot tree and
checks that the sources fit before mutation.

## Program stage

The installed boot archive keeps the executables named in
`boot://share/installer/rescue.list` and shared data outside selected programs.
Other root-level `.pxe` files and complete root-level `.pxb` directories move to
the pool's `bin` volume, one directory per kernel revision. The installer
opens the exact reviewed pool partition, releases its raw claim and mounts the
pool through that partition-bound handle. It creates an empty `home` volume when the pool has
none, without opening it, and stops there if that fails. It likewise creates the
`packages` and `http-work` volumes when missing and seeds `system://config/packages.lua`
from the boot archive only when that file is absent; an existing file is never
replaced. It creates the `bin`
volume when it is missing, copies every selected program into `bin/REVISION`,
syncs the pool, then verifies names, kinds, structure, sizes and bytes against
the immutable boot source. Bundle copy preserves empty directories; verification
detects missing and extra entries at every level. Destination bytes also match
the new revision's SHA-256 inventory. Only after this check does the installer
write, sync and byte-verify its reserved revision manifest before switching the
ESP. A valid old manifest supplies the recorded Programs report without a full
old-payload audit; missing/invalid manifests need explicit read/hash inventory
consent. These ordinary npfs reads may
use retained copied pages after sync: program verification checks the filesystem
view, without guaranteeing fresh device read-back. Its counters are compared
bytes/files, rather than device-read counts; see [progress](installer.md#progress).
A rerun recursively clears and
rewrites an earlier partial `bin/REVISION`. Builds without a revision use
`bin/unknown`.

Traversal uses bounded heap storage, one directory pair per level, and bounded
file buffers. Its current installer settings admit at most 32 directory levels
below the revision root, 65,536 selected entries in total, and native destination
names up to 255 bytes. Program staging that exceeds these bounds fails before
the ESP switch.

This is an offline target update: pool-partition admission refuses a retained
npfs instance on the selected extent. Installed programs cannot retain app/bin handles while
the target is admitted for update. Held development directories are not snapshots;
do not modify or delete published development trees while programs use them.
Online or program-only updates need a separate revision lifetime contract.

Writing the programs before the ESP is the commit rule. Until the ESP holds the
new kernel, the disk boots its old kernel, and boot init binds that kernel's
own `bin/REVISION`. An Update interrupted in the program stage therefore leaves
a different previous revision bootable.

When the new revision equals the installed one, as on a rerun, the program stage
rewrites the directory that installed boot binds as `bin://`. An interruption can
leave that tree incomplete until an Update completes. An unknown previous
revision also prevents a guarantee that the rewritten directory is different.
There is no atomic same-revision replacement.

## Replacement and verification

The installer opens the reviewed ESP as a native RW FAT view, leaving the pool
mounted. A healthy filesystem is preserved; these classic fixed files are
written through native FILEs:

- `EFI/BOOT/BOOTX64.EFI`;
- `boot/caelum.elf`;
- `boot/initrd.cpio`;
- `boot/limine/limine.conf`;
- `boot/revision`.

A damaged or missing classic ESP instead needs the separately reviewed,
single-use ESP-only rebuild grant bound to the exact GPT/geometry. That route
initializes only its reviewed extent and releases the raw writer before native
copies. Admission/capacity failures never trigger formatting or a fallback.
Only absent or damaged FAT metadata, with native admission reporting corruption
or I/O, qualifies. A valid FAT12/FAT16 ESP, or FAT32 using features native
admission does not support, refuses Update instead.
Coexistence receives no rebuild authority.

The kernel comes from the live boot's original immutable source file. The
installed archive is the live archive without the moved program roots and bundle
descendants, filtered in memory. Descendant matching respects path components:
removing `nvim.pxb/` leaves an unrelated name such as `nvim.pxb-extra` intact.
The installer generates the configuration from an explicit allowlist: exactly the
normal and rescue entries, a three-second timeout and the existing disk GUID.
Live-media installer and FAT32 tool entries are never imported. Update rewrites
the ESP menu even when the program revision is unchanged, removing extra entries
from older installations. The revision record comes from the running live kernel's SYSTEM_INFO.
Classic retains these fixed paths until installer task 5 introduces immutable
attempts with committed boot-binding evidence. Coexistence already uses immutable
attempts and manual entry text; it changes no boot menu or firmware entries.

Native copies are bounded, grow sequentially and truncate only after a matching
copy hash. Sync precedes canonical reopening, held-object identity and full
size/hash verification. After staging, BEGIN followed immediately by CHECK
starts the existing non-extending 30-second final interval; verification and
classic menu/revision publication must finish within it. There is no cross-file
atomicity or old boot-pair preservation after an interrupted fixed-path write.
Unrelated ESP files stay. Existing file and directory names must match under
ASCII FAT case rules; an 8.3 alias of a different name is refused. Successful
cleanup closes all data references, ends
the interval and explicitly retires the native view.

The selected pool's `system` root is reopened read-only. The installer then
removes older `bin` revisions except the new one and the known previous one;
unknown previous ownership skips cleanup. Cleanup is best effort and reports a
warning on failure. Only after successful boot-file verification and view
retirement does classic Update report `updated` and
[offer a restart](installer.md#restart). Remove the live medium and boot the
target. Pool verification retains the pool until reboot, so another Update
needs another live-media boot.
The reopen establishes that the system root can be opened; it does not compare
ordinary user files or replace whole-pool fsck.

## Recovery and qualification

A failure in the program stage reports that a different known previous revision
remains bootable. For a same-revision rewrite or unknown previous revision it
reports that the program revision may be partially rewritten. A failure during
native ESP copies/publication can leave fixed boot files partially replaced. A reviewed recovery failure can leave an incomplete ESP.
Inspect the reported phase before booting live media and reviewing another Update.
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

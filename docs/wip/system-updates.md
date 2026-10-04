# System updates through the installer

Status: **accepted, 2026-10-04.** The owner accepted the shape below and the
defaults for all three [decisions](#owner-decisions). Any decision can be
revised later by the owner. Each task starts only when the owner says so.

## Goal

An installed Pyxis system can be brought up to a newer build without losing its
data. Boot the new live image, choose **Update**, confirm, and the next boot of
the installed disk runs the new system with its `system://` contents intact.

**Completion:** in QEMU, a finalized installation whose `system://` holds files
is updated from a newer live image. After reboot, the installed disk runs the
new build, the files are unchanged, and host `fsck.npfs` passes.

## Starting point

- **Everything that changes lives on the ESP.** Programs ship inside the boot
  archive, so an installed system's software and its revision record occupy five files on the
  FAT32 ESP:
  - Limine's `EFI/BOOT/BOOTX64.EFI`;
  - `boot/caelum.elf`;
  - `boot/initrd.cpio`;
  - `boot/limine/limine.conf`;
  - `boot/revision` (new installations; older installations may omit it).

  The npfs pool holds only the user's data, in its `system` volume.
- **The [native installer](../userland/installer.md) already does the hard parts:**
  - it runs from live media with the [installer disk authority](../devices/installer-authority.md);
  - it inspects disks through raw reads with committed journal images overlaid in memory;
  - it writes a fresh FAT32 ESP and the boot files;
  - it verifies by FAT traversal and byte-for-byte comparison.

  It deliberately has no general FAT driver; it only writes a fresh FAT.
- **The installed `limine.conf`** comes from the packaged template, with
  timeout zero and the disk's GPT GUID, which `init-installed` uses to mount
  `system://`.

## Owner decisions

Accepted 2026-10-04, with the defaults below.

1. **Rebuild the whole ESP.**
   - Keep the GPT and both partitions' bounds and identities.
   - Re-create the ESP's FAT32 with the new boot files and a regenerated `limine.conf`.
   - There is no fallback entry and no side-by-side kernels. File-level editing of an existing FAT would need the FAT driver the installer doesn't have.
   - An update always runs from live media in the user's hands, so a failure or power cut part-way through is recovered by booting the live media again and re-running Update.
   - Fallback entries can come later, with updates from inside a running system.
2. **Proceed only if the live system can read the pool, and its journal is empty.**
   - The running live kernel is the one that will mount the pool after reboot. If its npfs format code accepts the pool's headers and features, the pool is compatible.
   - Use the installer's existing raw inspection, not a mount: a mounted pool would block raw access until reboot.
   - A committed journal is refused with a message: boot the installed system once to recover it, then update.
   - The updater never replays, migrates or writes the pool.
3. **Confirm by typing `update`,** mirroring `wipe`.
   - No `SAFE_TO_WIPE` marker is needed, since the update doesn't touch user data. Finalized and unfinalized installations both qualify.
   - Several candidates are chosen by number.
   - Read the room doesn't apply: a damaged or foreign disk isn't offered.

## Tasks

- [x] **1. Recognize installations and choose an action.**
  - **The first screen** offers **1 Install** and **2 Update**. Install leads to today's Proceed / Read the room screen, unchanged.
  - **A disk is an installation** when it has:
    - a validated protective GPT with the installer's layout;
    - a FAT32 partition 1 whose `boot/limine/limine.conf` names this disk's GPT GUID;
    - an npfs partition 2 with a `system` volume.
  - **Ineligible disks** are listed with the reason, as install does. The checks from decision 2 apply here, so an incompatible or replay-pending pool is listed with its reason and not offered.
  - **Revisions.** Record the kernel build revision on the ESP at install and update time. Show the installed revision (or "unknown" for older installs) next to the live one.
  - **Finish when:** in QEMU, Update lists a fresh install and a finalized install with `system://` files as candidates; refuses a blank disk, a foreign GPT and a pool with a committed journal, each with a clear reason; and cancelling makes no writes.

- [ ] **2. Rewrite the ESP and verify.**
  - **Confirmation:** after `update`, check eligibility again under exclusive raw access before the first write.
  - **Writes:** re-create the ESP's FAT32 inside the existing partition, write the live system's boot files and a `limine.conf` generated from the template with the disk's existing GUID (timeout zero, no Install entry), then flush.
  - **Untouched:** the pool and the GPT.
  - **Verification:** the same checks as install (FAT traversal and byte comparison), then a read-only reopen of the pool showing its `system` volume is intact. Report `updated`.
  - **Failure:** a failure after the first write says to boot the live media and run Update again.
  - **Finish when:**
    - in QEMU, install an older build, finalize it, write files to `system://`, then update from a newer live image;
    - the target-only boot runs the new revision, the files are unchanged and host `fsck.npfs` passes;
    - an update interrupted mid-write (QEMU killed) is recovered by re-running Update from the live media.

  The native run waits for writable USB storage ([USB C.1](usb-installation.md)).

## Implemented task 1

The action menu and read-only Update inspection are implemented. Selecting a
candidate reports inspection complete and nothing written; task 2 remains
unimplemented. New installs write and byte-verify `boot/revision`, using the
running kernel's SYSTEM_INFO revision. Missing/empty/nonprintable revision text
shows `unknown`; structural or I/O failures refuse inspection.

Accepted 2026-10-04: one valid npfs header with a damaged peer is eligible when
writable mount accepts it. Inspection also follows kernel control selection and
writable feature/journal-capacity admission; the shared mount ABI minimum is
18 journal images. Damaged or conflicting GPT copies are refused. Recognition
requires exactly the current ESP/pool geometry, a live `system` volume, and
complete boot command-line tokens naming installed init and this disk GUID.
Pool validation follows writable opener metadata checks rather than whole-pool
fsck. FAT recognition accepts extra unrelated files and changed incidental
formatting, while validating required chains and mirrored FAT entries.

[QEMU qualification](../development/experiments/system-updates-task1/README.md)
covers both candidate types, the three required refusal cases, revision recording
and cancellation without writes. Physical media and interrupted ESP rewriting
are not qualified by task 1.

## Out of scope

- updates from inside a running installed system;
- downloading images;
- side-by-side kernels with a fallback entry;
- npfs format migration;
- updating anything in the pool.

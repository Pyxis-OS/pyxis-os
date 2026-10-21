# ThinkPad native chainload integration — review plan

Task 2 of the [accepted plan](grub-limine-chainload.md), prepared 2026-10-21.
**Execution choices accepted (chronium, 2026-10-21); packet preparation is
assigned. Native commands, staging and writes await a separate packet go.**
The [QEMU rehearsal](../development/experiments/grub-limine-chainload.md) is
merged. Vela is outside this round. The executable packet follows review and
must use the numbered-checklist and single-command checks in the shared
`RUNSHEETS.md`; the steps below specify its scope and acceptance criteria.
Install/Update stay on the local trusted installer console, with chronium present
and outside a work call (chronium, 2026-10-21). The packet marks each console wait
and keeps his inputs to a short numbered list; surrounding Fedora and boot checks
run over SSH/Remote. The private installed Remote profile is qualification-only;
it does not change the shipped installed configuration.

## Starting state

Orchestrator-reported completed preparation (chronium, 2026-10-21): Fedora's
Btrfs p3 was shrunk online to exactly 64 GiB, with its start unchanged;
`sgdisk --verify` passed, 171.9 GiB is unallocated afterward, and Fedora booted
successfully. GPT backups before/after are retained privately on the ThinkPad
and horse. **Do not repeat or extend the shrink.**

The redacted `/shared/present/grub-limine-inventory/thinkpad-raw.txt` confirms
512-byte logical sectors, a 600 MiB FAT32 ESP, ext4 `/boot`, BLS with a rescue
entry, the active `41_custom` hook and absent `/boot/grub2/custom.cfg`.
It records 606,912,512 free ESP bytes, Secure Boot off, no `EFI/Microsoft`,
and a stale Windows firmware entry `Boot0000`. Its partition capture predates
the shrink: execution must compare fresh geometry with the **post-shrink**
backup, not reuse the capture's old p3 end. No identifier is copied into this plan.
Windows-entry cleanup remains a separate chronium-approved task.

Unlike the cloud fixture, ext4 `/boot` permits testing normal GRUB environment
updates and the actual rescue BLS entry. Neither is yet natively qualified.
The recorded firmware order starts with PXE, then Fedora: unchanged GRUB defaults
do not imply that an ordinary reboot returns to Fedora.

## Run sequence — packet go pending

1. **Freeze the packet and target.** Build a clean, pinned main revision without
   personal game data; include kernel configuration, SHA256SUMS, the pinned
   Limine and exact source/dependency revisions. Smoke-boot that packet in QEMU.
   Use the known ThinkPad wired Remote management profile. Privately recheck disk
   identity, post-shrink GPT copies/geometry, ESP identity/health/free capacity,
   no existing conflicting Pyxis files, GRUB/BLS hooks and Secure Boot. Check FAT
   health on an unmounted ESP or consistent captured copy, not a changing mount.
   Check capacity using all actual rounded bootstrap, rescue and attempt sizes;
   never credit future cleanup.
   Packet preflight prints one PASS/refusal line and saves details; changed or
   ambiguous evidence stops before writes. No new shrink or target substitution.

2. **Keep diagnostic evidence before bootstrap.** Retain readable copies on horse
   of the post-shrink GPT/protective MBR, ESP, Fedora `/boot` and boot-entry
   inventory; preserve original `grubenv` and the fact `custom.cfg` is absent.
   Save foreign EFI/GRUB/BLS hashes. These are diagnostic copies, not a restore
   promise. ThinkPad Fedora is disposable with zero personal data: no root/home
   backup or recovery medium is required (chronium, 2026-10-21). The older larger
   GPT is historical; never restore it automatically.

3. **Bootstrap and test the loader with NVMe writes disabled in Pyxis.** From
   Fedora, add only the pinned `EFI/pyxis/BOOTX64.EFI`, clean test kernel/archive
   in an identified Pyxis-owned directory, the ESP primary/fallback configs and
   the one accepted GRUB custom entry. Refuse existing conflicting content.
   Keep both EFI config-shadow locations absent. The first entry selects its kernel/archive by the verified ESP GUID and mounts
   **no pool**, with neither `mount.disk` nor `nvme.write=1`. Selecting the disk
   before its pool exists would start local-only archive recovery, without the
   Remote service required for automated checks. The ordinary clean live profile
   therefore supplies the read-only loader check; no other pool can be selected. Boot GRUB → Limine
   from the NVMe ESP → archive shell, collect runtime/boot-origin evidence over
   Remote and return to Fedora. Check normal and rescue Fedora boot paths before
   proceeding to the installer phase. No generated GRUB, BLS, Fedora EFI or
   removable-media fallback file is replaced.

4. **Review and apply Coexist Install.** Stage the same frozen clean packet on
   PXE with `boot.install=1` and `nvme.write=1` only for this approved round.
   The staging helper must verify payload hashes and restore the original menu
   byte-for-byte afterward. Select Install → Coexist, the reviewed internal disk
   and ESP, and allocate the **whole verified 171.9 GiB gap** to the Pyxis pool;
   Fedora keeps its 64 GiB. Use the exact aligned available extent from fresh
   inspection, not the rounded report or the installer's 64 GiB default.
   Freeze the displayed journal choice, new GPT slot and exact extent in Review.
   Do not reuse the fixture's ESP/pool slot numbers. Save the plan before typing
   `append`. The existing executor appends GPT first, formats only the new pool,
   then stages/verifies the immutable ESP attempt. Require exit 0 and
   `Coexistence payload staged, synchronized, and verified. Existing boot menu unchanged.`
   Any failure stops with its phase and retained evidence: no automatic retry,
   rollback, ESP rebuild or alternate target.

5. **Activate manually and boot the installed pair.** Back in Fedora, verify the
   saved entry and read back its exact kernel/archive hashes before replacing
   only the Pyxis primary configuration. Keep a separate verified rescue pair.
   The installed normal entry needs the explicitly approved `nvme.write=1` to
   mount its pool RW; add it deliberately to the printed entry, never infer that
   the installer flag persists into installed boot. Keep rescue NVMe-read-only.
   Boot through the unchanged GRUB target. Require the expected runtime and bin
   revision, selected pool and shell; record the archive-default configuration
   fallback honestly, since Coexist committed publication is not delivered.
   Verify clean poweroff, installed rescue, and return to Fedora.

6. **Update and check preservation.** Reboot the frozen PXE installer, select
   Update's discovered Coexist target, check its exact identity and type `update`
   only after Review. Require exit 0. Compare GPT/MBR and every pre-existing ESP,
   GRUB and BLS file before manual activation: only the new immutable attempt may
   be added on the ESP. Explicitly activate its printed entry as in step 5 and
   boot it; GRUB's stable EFI target stays unchanged. The packet prints one
   preservation result and records the compared paths, hashes and allowed changes.

7. **Finish in Fedora.** Recheck GPT, FAT health and foreign-file hashes after
   clean shutdown; exercise Fedora normal/rescue and confirm GRUB's saved Fedora
   default and account for expected one-shot `grubenv` changes. Restore PXE
   byte-for-byte and stop capture
   helpers by recorded PID. Report PASS/refusal with artifact paths. Keep the new
   pool and its attempts unless chronium separately approves removal.

## Accepted execution decisions (chronium, 2026-10-21)

1. **Pool and first writer:** use the entire verified post-p3 gap for Pyxis,
   leaving Fedora's 64 GiB unchanged. The existing reviewed Coexist executor,
   GPT append then pool format, is the first Pyxis writer; no separate raw
   megabyte probe, new raw writer or broader authority is required.

2. **Boot selection:** Fedora-side one-shot `BootNext` plus `grub2-reboot` is
   allowed. Use only the freshly verified existing Fedora entry, keep BootOrder
   unchanged, create no firmware entries and delegate no firmware authority to
   Pyxis. PXE is first, so a return reboot can reach the restored PXE entry;
   identify that transition explicitly rather than promise a Fedora return.
   Verify consumption of GRUB's one-shot selection on ext4; never change the
   persistent saved default automatically.

3. **Disposable Fedora/evidence:** no root/home backup or recovery medium is
   required. Keep the cheap GPT/ESP/`/boot` copies on horse as diagnostic evidence
   only. Do not describe them as a restore guarantee or introduce a recovery gate.

These decisions authorize packet preparation, not execution. Chronium's go on
the executable packet remains required before any native step.

Rollback of the menu choice removes only the newly added custom entry and
restores its saved environment where appropriate. Fedora's original EFI path
remains available. Do not recursively delete `EFI/pyxis`, delete the pool, or
restore GPT/ESP images automatically after partial effects. Such recovery needs
fresh inspection and explicit approval. This plan performs no Windows cleanup,
Limine-first migration, automatic Coexist publication or native failure injection.

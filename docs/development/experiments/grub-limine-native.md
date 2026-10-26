# ThinkPad native GRUB → Limine qualification — 2026-10-21

**Task 2 complete: chronium/orchestrator-reported native results.** This closes
both tasks of the [chainload milestone](../../userland/grub-limine-chainload.md),
after the [disposable QEMU rehearsal](grub-limine-chainload.md). This record uses
the reported native results and frozen packet provenance; the recording agent
did not access the laptop or independently read the private horse logs.

## Frozen inputs and target

- Pyxis runtime `3e09ea15ea4c8328983afcd733c48d09df108971`.
- Userland `cf731efffa376e7de840023413daefc69e208482`,
  fs `f3f22b6e07e7b38078ae86b414e430ffb0b4f9c2`,
  ports `e0356ea6404f67f3ad543609adcf56a4baeb17ca`,
  lwIP `a1aadb91a50360ff5b52864f7cec810b8162ee85`.
- Unchanged pinned Limine 12.9.3. Clean non-game build in the existing
  `pyxis-llvm23.1.3-3d69e83` builder. The private archive adds the accepted
  installed Remote/power profile, starts Remote only, and retains diagnostic
  commands in rescue; kernel and program binaries are unchanged.
- Final packet manifest SHA-256:
  `f3d425aea72dd97267d5fd6161dd058f86cae852f3275a95bfb4a61edbd2d62b`.
- ThinkPad T14 Gen 1 AMD, internal SK hynix PC611 NVMe, 512-byte logical sectors,
  600 MiB FAT32 ESP at partition 1, ext4 `/boot`, Fedora GRUB/BLS and Secure Boot
  off. The separately authorized Linux shrink had already left Fedora p3 at
  exactly 64 GiB, start unchanged, with a successful Fedora reboot. The native
  installer used the entire aligned gap: **176013 MiB**, new pool partition 4,
  **1024 MiB** journal. No shrink was repeated by this round.

Fedora is disposable under the accepted execution decision. GPT/ESP/`/boot`
copies are diagnostic evidence, not a restoration promise. Install/Update used
the local trusted console with chronium present; surrounding checks used
SSH/Remote. Firmware entries and BootOrder were retained; selection used approved
one-shot state only. No real GUID, serial, MAC or address is reproduced here.

## Checks and observed results

| Check | Reported native result |
| --- | --- |
| Fresh inspection and evidence | PASS: post-shrink geometry/identity and both GPT copies checked against private evidence; ESP and Fedora boot files captured |
| Fedora-side bootstrap | PASS: stable Pyxis EFI loader, primary/fallback, independent rescue pair and the custom GRUB entry installed without replacing Fedora's files |
| Initial read-only Pyxis boot | PASS: GRUB → Limine loads kernel/archive from the NVMe ESP; bootstrap Remote checks pass without a mounted pool or NVMe writes |
| Fedora rescue and normal returns | PASS: actual BLS paths, fresh boot checks and one-shot consumption on ext4 `/boot`; persistent saved default retained |
| Coexist Install | PASS: reviewed whole-gap append, program copy and immutable staging; `check-install` verifies GPT append, foreign-file preservation, entry/ownership and source/read-back payloads |
| Installed normal boot | PASS: explicit activation, expected runtime, NVMe write mode and mounted bin checks over Remote |
| Independent Pyxis rescue | PASS: `rescue-boot` and rescue Remote checks |
| Same-source Coexist Update | PASS: `check-update` verifies unchanged GPT/MBR, pre-existing files/attempts and new immutable attempt; explicit activation and installed Remote checks pass |
| Final health and shutdown | Health, installed boot and Remote checks PASS; PXE restored byte-for-byte; power requested, helper outcome unknown, chronium visually confirmed the ThinkPad off |

Chronium reported **67 MB/s** on Install's copy display. This is one reported
phase-rate observation: no repeated samples, matched baseline or end-to-end
NVMe write-throughput claim is made. The Update used the same frozen source;
a cross-revision migration was not exercised.

## Preservation and helper limits

Comparisons cover the reviewed GPT regions and existing ESP/Fedora `/boot`
records, including file hashes, symlink targets and directories. Install permits
only the chosen GPT append/CRCs and reviewed Pyxis additions; Update preserves
GPT/MBR and previous attempts. Activation changes only the selected Pyxis primary.
This is not a byte-for-byte comparison of unused FAT sectors, Fedora root state
or power-loss durability.

The first return exposed two host-helper assumptions: whole-disk PTUUID lookup
through `blkid -t` did not work on this Fedora, and Fedora created ESP
`loader/random-seed`. The corrected packet enumerates whole disks for GUID
uniqueness, keeps UUID/PARTUUID checks, and permits only the bounded regular seed
and its parent directory to appear/change. GRUB accounting/consumed one-shot
fields are compared semantically; the saved default and other settings stay
fixed. A shell logging pipeline initially masked a STOP exit status; the helper
itself returned nonzero. The supplied timestamp does not identify the writer of
an earlier grubenv change, so no process attribution is claimed.

The temporary return entry uses Limine's existing `Fedora` firmware label;
no native GUID is published in the shared PXE menu. Its QEMU control observed
reset, Fedora login, unchanged Fedora entry bytes and consumed BootNext. That
control used VirtIO and a disposable cloud fixture; native evidence above comes
from the ThinkPad instead.

The private power helper waits for the old Remote connection to close. An abrupt
power-off can leave that connection silent until its watchdog reports an unknown
outcome. The final helper result was **not** a confirmed transport close or a
successful automatic S5 verdict. Chronium's visual observation establishes the
machine was off; the
[observation limit](https://git.internal/PyxisOS/pyxis-os/issues/1322)
remains. No second power request, Force off or automatic recovery is claimed.

## Evidence and scope

Raw native logs are private on horse at
`~/private/grub-limine-native/step-*.log`, alongside retained state, manifests
and diagnostic copies. Shared clean packet/provenance and QEMU evidence remain
under `/shared/present/grub-limine-native/`; raw native identifiers do not belong
there or in repository examples. The orchestrator reports final PXE restoration;
no further native action was performed to write this record.

The milestone's native ThinkPad gate is complete. Vela, Secure Boot, a later
Limine-first migration, interrupted-write recovery and automatic Coexist
publisher activation remain separate. The test-only Remote/power profile remains
inside the installed qualification attempts; no shipped-default grant changed.

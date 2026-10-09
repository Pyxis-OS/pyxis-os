# USB installer authority qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Manual QEMU qualification on 2026-10-05 for C.3 of the
[USB milestone](../../../devices/usb-installation.md). Physical installation is the
separately assigned C.4 task.

## Sources and scope

Baseline main `102432b` with fs `d352c7e`, ports `36d952e`, userland `a3eb8d2` and lwIP
`a1aadb9`; pins unchanged. Raw authority sources were `33a0aee`, and `91346a8` additionally
fixes a shadowing warning in the mount guard (the first image reports `33a0aee6bd4`, newer
live media `91346a8d80a8`). Installer inventory now waits for boot disk discovery to seal,
then exposes all retained observed VirtIO/USB candidates. The owner accepted partial USB
topology coverage, including unsupported EHCI, while lost registry records and incomplete
VirtIO bookkeeping still refuse inventory; unseen devices cannot be listed. Existing
eligibility, typed consent, per-device write qualification, raw claims, bounded I/O, flush,
release/rescan and mount exclusion are reused, with no installer, driver, ABI or
selection-policy replacement.

Builds were `make -j16 image fs-tools BOOT_MENU_TIMEOUT=60` for the baseline and
`make -j16 image BOOT_MENU_TIMEOUT=600` for manual menu selection, with no new warnings
(existing Quake vendor warnings remain) and no compiler-container, pin or CI change.
QEMU 10.2.2 with the AHCI fix: q35, nested KVM, `-cpu max`, four vCPUs, 2 GiB, UTC RTC,
fresh OVMF variables per process, xHCI enabled, 30-second npfs flush interval, disposable
raw files with 512-byte sectors and writeback cache, VirtIO RNG; live media was a read-only
CD-ROM and target-only boots had no ISO or VirtIO disk. No physical device or host export
was attached. Limine/terminal input, screenshots and GDB were manual; no tests, hooks or
automation were added. The interrupted-Update case uses the real writer and a killed
disposable VM, as C.3 requires; it does not model physical power-loss durability.

## Matched candidate inspection

Three baseline and three changed-source boots used the same healthy finalized older 2 GiB
VirtIO fixture, a separate blank 2 GiB USB target, `qemu-xhci,p2=1,p3=1` on root port 1 and
unsupported EHCI. The Update choice inspected candidates and stopped at typed confirmation
(none confirmed). GDB read VirtIO counters before candidate inspection and at confirmation:

| Sources | VirtIO inspection requests (three samples) | Additional USB inspection |
| --- | --- | --- |
| `102432b` | 49, 49, 49 | USB candidate not exposed |
| `33a0aee` | 49, 49, 49 | 3 reads / 1536 bytes per inspection |

Each group had zero range. The changed installer listed USB as `ineligible: blank disk: no
installation` and kept the eligible VirtIO installation; USB had zero writes and one
synchronization (from boot write qualification, not inspection), and VirtIO image bytes
were unchanged. Accidental normal boots and incomplete debugger attempts were excluded
before the valid baseline samples. These are virtual request counts, not latency or
physical performance.

## USB installation and target-only boot

With live `33a0aee` media, Install / Read the room listed the blank sparse 2 GiB USB target
as the sole eligible target (default 12 MiB journal for that capacity). After typed `wipe`
the installer wrote the ESP and pool, flushed/released/rescanned, byte-verified the boot
files, reopened `system` read-only and reported `installed`, exit 0. Layout: ESP entry 1 at
LBA 2048 for 1048576 sectors, pool entry 2 at 1050624 for 3143640 sectors (fixture
observations, not driver constraints). BOT was READY, writable with flush, no latched
failure, 11657 writes / 47714816 bytes, three synchronizations including boot
qualification, and the pool was EMPTY at journal sequence 1.

A fresh target-only boot (`usb-storage,bootindex=1`, no live media) reached installed init;
`fastfetch` showed Caelum `33a0aee6bd4` and npfs `system://`, and GDB confirmed a writable,
healthy pool. Local commands copied and synchronized data:

```text
cat app://share/hello.txt > system://kept.txt
sync system://kept.txt
rm system://SAFE_TO_WIPE
sync system://
sha256sum app://share/hello.txt system://kept.txt
installer
```

The two 661-byte files matched (SHA-256 `aa947fd83f8021231c34df87f225c5d5e2db615b5041af7b84a7800dfb09c86d`).
The installer launched from the ordinary session refused for missing install-mode authority
(exit 1), and GDB saw an EMPTY journal at sequence 9 with no writeback error. The finalized
saved-file image was kept as the baseline for the update and interruption cases.

## USB Update and raw-claim refusal

Live `91346a8` media listed the installed `33a0aee6bd4` and the same disk GUID as eligible.
After typed `update` the normal flow (exclusive revalidation, ESP replacement,
flush/release/rescan, byte verification, read-only reopen) reported `updated`, exit 0; BOT
READY, no latched failure, 11620 writes / 47574016 bytes, three synchronizations, pool EMPTY at
sequence 9. After closing the VM, `cmp` against the saved baseline passed for every byte before
the ESP (1048576) and from its end (537919488) to EOF, so GPT copies, the whole pool and
outside slack were unchanged. The ESP revision was `91346a8d80a8`, `fsck.npfs` passed without
replay and `kept.txt` still matched.

Mounted exclusion was checked after the updater closed its root handle but retained the
pool: the installer still held its trusted `disks` grant, the raw-claim list was empty and the
USB device writable. In its own CPU 1 userspace context (CS `23h`) the existing `disks_open`
wrapper was invoked for the same device with READ_WRITE and returned `CALL_BUSY`; writes stayed
11620, synchronizations 3 and no claim appeared. The call used the executable call instruction
at a debugger-chosen address with a hardware stop at its return, valid output storage and the
actual startup handle, restoring registers afterward (addresses are specific to that ELF, not
contracts). An earlier inferior-call attempt used a nonexecutable stack trampoline and faulted
the task; that run was discarded and the baseline restored. Stack execution was never enabled
and no production hook or test program was added.

A fresh protected (write-protected) attachment was listed `ineligible: read-only device`; the
installer reported no eligible disks (exit 1) with zero writes or synchronizations and an
unchanged raw-file hash. Unknown optional capabilities and real transport failures remain the
C.1 source-reviewed/physical-qualification limits.

## Interrupted ESP replacement

On an independent copy of the saved baseline, with the newer media and normal Update
confirmation, GDB stopped the real writer before NPFS_RAW_WRITE at byte offset 1056768 for
4096 bytes (two earlier writes, 8192 bytes, and boot qualification as the sole sync). With
the VM paused, QEMU was killed with SIGKILL before that third request was published. The
primary and backup FAT boot sectors were found cleared by the writer and mtools rejected the
ESP as non-DOS media; outside-ESP comparisons still passed and no control bytes were
injected. (An earlier batch-debugger attempt that detached and resumed was excluded.)

Fresh live media recognized the disk as eligible with damaged FAT32 geometry and reported
`Boot files damaged or missing; Update will rebuild them.` Typed `update` completed with exit
0 (BOT READY, 11620 writes / 47574016 bytes, three syncs, pool EMPTY at sequence 9), outside-ESP
comparisons passed, `fsck.npfs` passed without replay and `kept.txt` matched. Target-only boots
of both the normally updated and the recovered disk reached installed init, reported Caelum
`91346a8d80a8` and npfs `system://`, and returned the original `kept.txt` hash.

## Limits

At this qualification `.config` still had `CONFIG_XHCI=n` (an ordinary `make -j16 image` with
that default also passed); the owner later enabled it (see below). Only disposable images were
written; physical installation, PXE live boot, a real power-off and the `0.0.1` tag remain C.4.
These results qualify the installer path for the observed QEMU USB medium, not physical write
durability, optional-command failure recovery, unseen topology or other logical-sector sizes.
Partial topology through unsupported EHCI was intentional in every live boot. No topology,
capacity, GUID or debugger address is part of the implementation.

## Review follow-up: durable ESP publication and enabled xHCI

The [review on #407](https://git.internal/PyxisOS/pyxis-os/pulls/407) found that old readable
configuration/revision clusters could classify a partly rewritten kernel/archive as a
recognized installation. The owner requested an immediate fix and separately made xHCI
enabled by default on 2026-10-05. [Userland #124](https://git.internal/PyxisOS/pyxis-userland/pulls/124)
(`a3ea1b0`) changes the shared ESP writer: clear and flush the reserved area before replacing
FAT/directory/file data, keep both boot sectors invalid, flush the complete new tree, then
publish the boot sectors (two more synchronizations, no reliance on old and new clusters
coinciding; flush failures stop without retry). The parent rebased onto `a12aa3b`, and `52c177b`
pins the published fix and enables xHCI in Kconfig and `.config`.

With the same configuration and baseline, GDB stopped the real writer before reading archive
offset 5242880 for 4096 bytes (live image `bbe595f87ea4`: 2409 writes / 9852928 bytes, two
flushes) and QEMU was killed there. Host inspection showed both FAT boot sectors entirely zero,
the kernel matching the new source, the first 5 MiB of the archive matching the new archive
while the whole archive still differed, and every byte outside the GPT-derived ESP unchanged.

Fresh `52c177b` media listed the disk as eligible with damaged FAT32 geometry, installed
revision `unknown` and `Boot files damaged or missing; Update will rebuild them.` After typed
`update`, a second GDB stop immediately before primary boot-sector publication saw three flushes
(qualification, invalidation, complete-tree sync) with the archive complete and both boot
sectors still zero. Execution resumed, verified the files, reopened the pool and reported
`updated`, exit 0 (11621 writes / 47578112 bytes, five flushes, healthy transport, no retries).
Outside-ESP comparisons passed, host fsck passed without replay and `kept.txt` kept its hash.
A separate blank 2 GiB target with the same media passed Install / Read the room, typed `wipe`,
formatting, GPT rescan and verification (`installed`, exit 0), and the repaired original disk
booted alone from USB, reported `52c177be45b3` and npfs `system://` and returned the unchanged
`kept.txt` hash.

Full image builds passed without overrides, and Kconfiglib confirmed `XHCI=y` both before
loading any `.config` and after loading the checked-in one. Existing third-party warnings
remain; no tests, hooks or automation were added. Userland had no standalone CI tasks and
`fj pr status 124` could not parse Forgejo's empty status (`unknown variant`), so dependency CI
was unavailable rather than passed; parent CI builds the pinned dependency. Physical
flush/power-loss qualification remains C.4.

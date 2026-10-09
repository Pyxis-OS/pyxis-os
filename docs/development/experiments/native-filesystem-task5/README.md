# Native filesystem task 5: QEMU end to end

Qualification on 2026-10-04, based on merged main `7955f59` with userland `06812bc`, filesystem `d352c7e`, ports `bf7667c`
and lwIP `a1aadb9`. Installer PRs parent #369 and userland #109 were confirmed merged first. The owner accepted finishing
QEMU and deferring native ThinkPad installation until writable USB storage is available; no storage driver or ABI change
belongs to this qualification.

## Build inputs

The kernel and host tools were built from current main. The unchanged SDK, userland and ports products were reused from the
task-4.3 build with their bundle manifests and payload verification: SDK provenance records parent `119ad0e` (modified),
clean userland `06812bc` and filesystem `d352c7e`, and comparing `0b04ea8` to current main showed no change to public
ABI/P1F headers, SDK export or codec rules, shared shebang source or dependency pins. This preserved matching application
inputs while rebuilding the changed kernel; it does not claim a fresh SDK/application source build at `7955f59`. The build was
`make -j16 image usb-image PREBUILT="sdk userspace ports" PYTHON=/usr/bin/python3 BOOT_MENU_TIMEOUT=5` with GCC 16.2.0 and
Lua 5.4 tools, no container rebuild, and the effective configuration kept the 30 s npfs flush, 120 HPET maintenance ticks and
disabled native xHCI (USB firmware boot does not need native xHCI I/O). The ordinary ISO and 1 GiB USB image both built,
and main CI run 782 passed build and filesystem for `7955f59`.

## Manual installation

QEMU 10.2.2 under nested KVM: q35, four CPUs, 2 GiB, OVMF with a fresh variables copy per run, no CD-ROM. The live medium was
attached as a read-only `usb-storage` on `qemu-xhci` (bootindex 1) and the target as `virtio-blk-pci` with
`cache=writeback`: a new blank 2 GiB raw image with 512-byte sectors. VirtIO net used the user backend and no VirtIO RNG was
attached; the log reported CPU RDSEED with RDRAND fallback ready and its boot self-test passed (the CPU entropy path in
nested KVM, not native firmware). Install Pyxis was selected manually in the five-second Limine menu, then Super+Right for
its terminal. Read the room admitted the blank target, displayed its size and unavailable old GUID, prefilled a 12 MiB
journal and required typed `wipe`. The installer reported `installed` after flush/release/rescan, ESP traversal and byte
comparisons and normal read-only pool/marker verification, the live USB image's SHA-256 stayed unchanged, and kernel GPT logs
showed a healthy map with ESP LBA 2048/count 1048576 and pool LBA 1050624/count 3143640.

This mixed-device launch deliberately uses a raw QEMU command because the `run-usb` launcher rejects a VirtIO disk so that
its native-USB qualification cannot be masked by another backend (that contract is unchanged). The live medium is
firmware-loaded and the target explicitly VirtIO, so the run does not establish native USB block access.

## Installed boot and persistence

The target booted alone with fresh variables, no USB medium and no ISO: Limine used timeout zero and no installer entry, the
read-back configuration selected `init-installed` and new GUID `8bfb16a7-116f-445b-a7d4-7e32a0e7ebb8`, and the init reached
the `home://` prompt, configured net0 and mounted writable `system://` with CPU entropy still available. Ordinary shell
commands copied data and a native program to the volume and synchronized it:

```text
cat app://share/hello.txt > system://hello.txt
cat app://cat.pxe > system://cat.pxe
sync system://
```

The extracted pool passed `fsck.npfs` and listed `SAFE_TO_WIPE`, `hello.txt` (661 bytes) and `cat.pxe` (51,576 bytes), both
files matching their sources with `cmp`. A second target-only boot with fresh variables ran
`system://cat.pxe system://hello.txt`, printing the complete text and returning to the prompt (persisted executable capture
and file reads after reboot), and the pool passed structural checking again.

## Host inspection and limits

The installed GPT passed `sgdisk --verify` (advisory only: the pool's 4 KiB aligned end is not a 2048-sector boundary), the
extracted ESP passed `fsck.fat -n`, the pools passed structural checks, and the EFI, kernel and archive matched their sources
with `cmp`. These are success and consistency observations, not crash or uncertain-I/O qualification; broader 4 KiB-sector,
multi-pool veto, cancellation and reinstall cases remain in the [task-4.3 record](../native-filesystem-task4.3/README.md).
Native ThinkPad installation is deferred by owner choice: Caelum has writable VirtIO storage but neither USB mass-storage
reads, writes and flush nor NVMe, and firmware USB boot with descriptor enumeration cannot substitute for a writable native
target. Physical validation resumes after the USB read/block-integration and write/cache-synchronization work in the
[USB milestone](../../../devices/usb-installation.md), with an expendable target chosen explicitly (the internal Fedora disk
was not touched). No physical-media, power-loss or performance claim follows; no tests, fault injection, CI or automation
were added.

## Installer review follow-ups

Parent #371 also pins userland `d730e4f` ([userland #111](https://git.internal/PyxisOS/pyxis-userland/pulls/111)), addressing
the blank-disk wording and template note on #371 and partial-failure guidance left from #369. Consent eligibility and veto
rules are unchanged, and the recovery hint requires booting the live image again before choosing Read the room (source review
checked its attempted-write guard; no write failure was induced, so this is not runtime recovery qualification).

Full source image and USB builds passed again. A temporary local template line `default_entry: 2` selected Install Pyxis for a
manual QEMU run (q35, nested KVM, four CPUs, 2 GiB, no VirtIO RNG) with read-only USB live media and two 2 GiB 512-byte-sector
VirtIO targets: the zeroed target was listed as `blank disk: no partition table`, the other (nonzero unpartitioned contents)
kept the unrecognized-layout reason, and installing the blank target with the 12 MiB default succeeded while the other
target's whole-disk SHA-256 stayed unchanged. Stopped-target GPT, FAT and npfs checks passed with the same alignment advisory;
the read-back configuration had timeout zero, the new GUID and `init-installed` with neither `default_entry` nor the Install
entry; and a target-only boot reached the local prompt. The temporary template edit was reverted. These follow-ups do not
replace the earlier persistence measurements or the deferred physical validation.

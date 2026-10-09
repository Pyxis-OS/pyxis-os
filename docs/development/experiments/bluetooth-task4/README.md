# Bluetooth warm-firmware verification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Task 4 completed on 2026-10-08 for the owner's accepted warm-host scope: accept already operational firmware, skip loading
and verify the running version remains unchanged. The owner explicitly deferred cold bootloader/upload support before the
scan investigation, so this is a qualified skip decision, not an implemented or qualified firmware uploader.

## Review follow-up and policy

The reviewer on [merged #526](https://git.internal/PyxisOS/pyxis-os/pulls/526) asked task 4 to account for warm reboot from
another OS and prepared-host passthrough, both of which may retain firmware selected by that OS. USB port reset and HCI Reset
do not establish cold bootloader state. The investigation accepts the observed operational build without comparison against a
Pyxis firmware pin; future version-comparison policy is undecided.

The temporary consumer first completes Reset and Intel Read Version through the
[checked task 3b transport](../bluetooth-task3b/README.md#bounded-probe-behavior). For the known AX200 profile with firmware
variant `0x23` it records the skip decision and issues Read Version again, and both ten-byte return records must match for
verification to succeed. A command or transport failure or a changed version fails verification, and a bootloader or unknown
state stays explicitly unsupported by this probe. It sends no Intel secure-download, DDC-write or firmware-boot command and
does not read bootloader-only boot parameters on operational firmware.

## Revision, build and guest configuration

Main `cdc096eda3b0c1288cda5a9e885177a9ed0e442c`, including #526, with verified SDK, userland and ports bundles from successful
[CI run 1220](https://git.internal/PyxisOS/pyxis-os/actions/runs/1220). The unmerged
[probe revision](https://git.internal/PyxisOS/pyxis-os/commit/e8391e011e3f6c1b256d38f18ae28c61d7ca8bc1) on
`probe/bluetooth-warm-firmware` carries the task 3b consumer plus the skip/confirmation step; do not merge it. It was built
from a clean checkout with local Clang/LLD 23.1.3 (fork `41ab6043cc4fd63e0a358d1e60bba249a751d8ee`) using
`make -j16 image PREBUILT="sdk userspace ports"`, without warnings. Pins: userspace `21457b0d88ca101aec471d78b39b99d12afca4ee`,
ports `e85d307336af66d6d5171c5f943ffe312fd086d5`, filesystem `b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. The physical ThinkPad ran Fedora/KVM (not nested) with QEMU `10.2.2-1.fc44`, four
CPUs, 2 GiB and the [task 3b command configuration](../bluetooth-task3b/README.md#revisions-and-execution) (Q35, fresh Fedora
OVMF variables, ISO boot, serial capture, display disabled, VirtIO RNG and network, `8087:0029` passthrough on `qemu-xhci`,
GDB on port 1234); Bluetooth stayed inactive/disabled, rfkill unblocked and the invoking user had device access.

## Measured warm state

The serial capture recorded all three HCI events:

| Command | Sequence | Event bytes | Status / command allowance |
| --- | ---: | --- | --- |
| Reset | 1 | `0e 04 02 03 0c 00` | 0 / 2 |
| Intel Read Version | 2 | `0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00` | 0 / 1 |
| Intel Read Version after skip | 3 | `0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00` | 0 / 1 |

The two version records were identical: platform/variant/revision `0x37/0x14/0x01`, firmware variant/revision `0x23/0x03`,
build 193, week 33, year 24, patch 0, with `0x23` read as operational firmware per the
[Linux legacy protocol](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.c). GDB showed
`firmware_load_skipped=true`, `firmware_verified=true`, `finished=true` and `result=USB_OK`, with the original and confirmation
arrays matching, sequence 3, an empty copied queue, both receives `INTERRUPT_POSTED`, stream `USB_OK` and the controller
running. Guest `lsusb -n` exited 0 with complete AX200 inventory and both interfaces, and the remote shell exited with a
complete drain.

The host initialization evidence is an excerpt from Fedora's kernel journal for this boot: a bootloader initially, then
loading `intel/ibt-20-1-3.sfi`, applying `intel/ibt-20-1-3.ddc` and running build 193/week 33/year 2024. With the unchanged
guest replies this supports the reviewer's explanation that Fedora's loaded firmware survived attachment and HCI Reset; Pyxis
did not perform that upload.

## Deferred cold upload and asset provenance

Linux's legacy AX200 naming uses decimal hardware variant, hardware revision and firmware revision. Today's fields identify
`intel/ibt-20-1-3.sfi` and `.ddc`, while build/week/year do not select those names, and cold bootloader facts must be recaptured
before selecting future assets. Read-only inspection of the installed Fedora `linux-firmware-20260309-1.fc44` found these aliases:

| Requested file | Installed target | Decompressed bytes | SHA-256 of decompressed bytes |
| --- | --- | ---: | --- |
| `ibt-20-1-3.sfi.xz` | `ibt-20-0-3.sfi.xz` | 801016 | `fd51628c307a1b03fbd7156f0d2a7f5af67b7202d4f358f5d4d807911982c3cc` |
| `ibt-20-1-3.ddc.xz` | `ibt-0040-0041.ddc.xz` | 9 | `fe272982577efdc289cfe3e8bedafca607c0b7d6c9df1aab1880e22ef930d077` |

These are host observations, not selected Pyxis assets: no firmware file entered the tree or build, and no owner mirror or pin was
selected or consumed. A future mirror needs the exact upstream [linux-firmware](https://gitlab.com/kernel-firmware/linux-firmware)
revision, requested aliases and targets, WHENCE provenance and `LICENCE.ibt_firmware`. Cold loading also needs an owned strategy
for bootloader events on bulk IN as well as interrupt IN, secure-send bulk OUT and the real Intel boot notification (the
[Linux USB path](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btusb.c) documents that transport). Pyxis's
synchronous bulk-IN timeout quarantines the controller and so cannot serve as an idle channel poll, and a reset that
re-enumerates USB meets the accepted removal/quarantine policy; the
[cold-upload debt](../../../technical-debt.md#bluetooth-cold-firmware-upload-and-running-version-policy) records these
consequences and the revisit point before native startup claims.

## Limits

Source review covered confirmation failures and unknown/bootloader handling; those paths were not forced in the guest. Cold
startup, secure upload, DDC application and pinned-build comparison remain unqualified, the accepted warm skip does not show that
every operational build will work with a future Bluetooth stack, and no LE scan ran in this task. The mergeable PR holds the
report, captures, accepted scope, checklist update and deferred debt; the ordinary image has no HCI-probe symbols, and pins and
compiler/container inputs did not change.

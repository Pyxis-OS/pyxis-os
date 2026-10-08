# Bluetooth warm-firmware verification

Task 4 completed on 2026-10-08 for the owner's accepted warm-host scope:
accept already operational firmware, skip loading, and verify the running version
remains unchanged. The owner explicitly deferred cold bootloader/upload support
before the scan investigation. This is a qualified skip decision, not an
implemented or qualified firmware uploader.

## Review follow-up and policy

The reviewer on [merged #526](https://git.internal/PyxisOS/pyxis-os/pulls/526)
asked task 4 to account for warm reboot from another OS and prepared-host
passthrough. Both may retain firmware selected by that OS. USB port reset and
HCI Reset do not establish cold bootloader state. This investigation accepts
the observed operational build without comparison against a Pyxis firmware pin;
future version-comparison policy remains undecided.

The temporary consumer first completes Reset and Intel Read Version using the
[checked task 3b transport](../bluetooth-task3b/README.md#bounded-probe-behavior).
For the known AX200 profile with firmware variant `0x23`, it records the skip
decision and issues Read Version again. Both ten-byte return records must match
before verification succeeds. A command/transport failure or changed version
fails verification. A bootloader or unknown state remains explicitly unsupported
by this probe. It sends no Intel secure-download, DDC-write or firmware-boot
command and does not read bootloader-only boot parameters on operational firmware.

## Revision, build and guest configuration

Main was `cdc096eda3b0c1288cda5a9e885177a9ed0e442c`, including #526. Verified SDK,
userland and ports bundles came from its successful
[CI run 1220](https://git.internal/PyxisOS/pyxis-os/actions/runs/1220).
The unmerged
[probe revision](https://git.internal/PyxisOS/pyxis-os/commit/e8391e011e3f6c1b256d38f18ae28c61d7ca8bc1)
on `probe/bluetooth-warm-firmware` is based on that main, carries the task 3b
consumer and adds the skip/confirmation step. Do not merge the probe branch.

The image was built from a clean checkout of that revision with local Clang/LLD
23.1.3, fork `41ab6043cc4fd63e0a358d1e60bba249a751d8ee`:

```sh
PATH="$HOME/opt/pyxis-llvm/bin:$PATH" make -j16 image PREBUILT="sdk userspace ports"
```

The build passed without compiler warnings. Pins were userspace
`21457b0d88ca101aec471d78b39b99d12afca4ee`, ports
`e85d307336af66d6d5171c5f943ffe312fd086d5`, filesystem
`b427df29f865bc361b8da92bcd74e114581e9a32` and lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

The physical ThinkPad ran Fedora/KVM, not nested virtualization, using QEMU
`10.2.2-1.fc44`, four CPUs, 2 GiB RAM and the
[task 3b command configuration](../bluetooth-task3b/README.md#revisions-and-execution):
Q35, fresh Fedora raw OVMF variables, ISO boot, serial capture, standard VGA
with display disabled, VirtIO RNG/network and `8087:0029` passthrough on
`qemu-xhci`. GDB inspected the matching ELF through port 1234. Bluetooth remained
inactive/disabled and rfkill unblocked; the invoking user had device-node access.

## Measured warm state

The [serial capture](serial.txt) records all three HCI events:

| Command | Sequence | Event bytes | Status / command allowance |
| --- | ---: | --- | --- |
| Reset | 1 | `0e 04 02 03 0c 00` | 0 / 2 |
| Intel Read Version | 2 | `0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00` | 0 / 1 |
| Intel Read Version after skip | 3 | `0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00` | 0 / 1 |

The two version records were identical: platform/variant/revision
`0x37/0x14/0x01`, firmware variant/revision `0x23/0x03`, build 193, week 33,
year 24, patch 0. Variant
`0x23` is interpreted as operational firmware using the
[Linux legacy protocol](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.c).

[GDB](gdb.txt), with entered expressions retained, showed
`firmware_load_skipped=true`, `firmware_verified=true`, `finished=true` and
`result=USB_OK`. The original and confirmation arrays matched. The sequence was
3, copied queue empty, both receives still `INTERRUPT_POSTED`, stream `USB_OK`
and controller running. Guest `lsusb -n` exited 0 with complete AX200 inventory
and both interfaces; the remote shell exited with a complete final drain.

The [host initialization excerpt](host-initialization.txt) comes from Fedora's
kernel journal for this host boot. It reports a bootloader initially, then
loading `intel/ibt-20-1-3.sfi`, applying `intel/ibt-20-1-3.ddc`, and running build
193/week 33/year 2024. Together with the unchanged guest replies, this supports
the reviewer's explanation that Fedora's loaded firmware survived attachment
and HCI Reset. Pyxis did not perform that host upload.

## Deferred cold upload and asset provenance

Linux's legacy AX200 naming uses decimal hardware variant, hardware revision
and firmware revision. Today's fields identify `intel/ibt-20-1-3.sfi` and `.ddc`;
the build/week/year do not select those names. Cold bootloader facts must be
recaptured before selecting future assets.

Read-only inspection of the installed Fedora `linux-firmware-20260309-1.fc44`
files found these aliases and decompressed identities:

| Requested file | Installed target | Decompressed bytes | SHA-256 of decompressed bytes |
| --- | --- | ---: | --- |
| `ibt-20-1-3.sfi.xz` | `ibt-20-0-3.sfi.xz` | 801016 | `fd51628c307a1b03fbd7156f0d2a7f5af67b7202d4f358f5d4d807911982c3cc` |
| `ibt-20-1-3.ddc.xz` | `ibt-0040-0041.ddc.xz` | 9 | `fe272982577efdc289cfe3e8bedafca607c0b7d6c9df1aab1880e22ef930d077` |

These are host observations, not selected Pyxis assets. No firmware file entered
the source tree or build. A future owner mirror needs the exact upstream
[linux-firmware](https://gitlab.com/kernel-firmware/linux-firmware) revision,
requested aliases and target files, WHENCE provenance and `LICENCE.ibt_firmware`.
No owner mirror or pin was selected or consumed in this task.

Cold loading also needs an owned strategy for bootloader events on bulk IN as
well as interrupt IN, secure-send bulk OUT, and the real Intel boot notification.
The [Linux USB path](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btusb.c)
documents that transport. Pyxis's synchronous bulk-IN timeout quarantines the
controller, so it cannot serve as an idle channel poll. A reset that re-enumerates
USB also meets the accepted removal/quarantine policy. The
[cold-upload debt](../../../technical-debt.md#bluetooth-cold-firmware-upload-and-running-version-policy)
records these consequences and the revisit point before native startup claims.

## Delivery and limits

Independent source review covered confirmation failures and unknown/bootloader
handling; those paths were not forced in the guest. Cold startup, secure upload,
DDC application and pinned-build comparison remain unqualified. The accepted warm
skip does not establish that every operational build will work with a future
Bluetooth stack. No LE scan ran in this task.

The mergeable PR contains the report, captures, accepted scope, checklist update
and deferred debt. The ordinary image was rebuilt after returning to this branch
and has no HCI-probe symbols. QEMU/GDB jobs exited, `btusb` rebound and Bluetooth
stayed inactive/disabled. Dependency pins and compiler/container inputs did not
change; no container rebuild is required.

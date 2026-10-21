# Limine reading NVMe — 2026-10-15

The earlier synthetic coexistence boot stopped at Limine's kernel-loading
message: 441,344 target bytes / 55 reads, then about 183 seconds without another
read or Caelum output. That establishes a bounded no-progress observation,
not a permanent hang or its cause. The same staged files booted via VirtIO.

## Controlled checks

Unmodified Limine 12.9.3, commit `e3aa138d383c`, vendored EFI SHA-256
`94308b4e9ee1783a65cfd60570240a80595baf125c5b18e729d3dd977194ad51`.
QEMU 10.2.2, q35/nested KVM, host CPU, four vCPUs, 1 GiB, OVMF, SCSI boot CD,
no NIC; target disks opened read-only. All captured target write counters are
zero. Original payloads are kernel `d52f3529` plus its approximately 40 MiB
archive; the newer attempt is `2f471d54`. The 4Kn control uses the independently
qualified classic recovery image. No Limine source or binary change.

| Variant | Observation | Last startup UART write after launch |
| --- | --- | ---: |
| Explicit namespace 1, 512-byte blocks, MDTS 7; newer payload | Caelum/rescue | 3.86 s |
| Legacy device, MDTS 7; original payload, reversed PCI placement | Caelum | 13.01 s |
| Original SCSI-before-NVMe order, MDTS 7; original payload | Installed Pyxis | 2.41 s |
| Original payload, MDTS 4 (64 KiB) | Installed Pyxis | 2.21 s |
| Original kernel, valid 146,944-byte archive on a diagnostic NVMe copy | Caelum/init | 1.79 s |
| One 4096-byte namespace, MDTS 7; classic kernel/archive | Caelum/echo | 2.21 s |
| Original payload/order and preserved original UEFI variables, MDTS 7 | Installed Pyxis; observed through 188.37 s | 2.10 s |
| Original order including legacy RNG, original variables, MDTS 6 (256 KiB) | Installed Pyxis | 1.95 s |

The UART timestamps bound Caelum entry from above; they include firmware and
guest startup, not just loading. They are filesystem write timestamps against
the launch timestamp, not a matched throughput or precise handoff measurement.
Shared-host builds ran during these functional checks. Even the 13-second case
completes. Both tested native transfer-limit settings, 6 and 7, boot; this does
not emulate either laptop's controller or firmware.

MDTS 2 (16 KiB) instead reaches an explicit Limine failed-open panic and its
key-to-menu prompt. That is a distinct failure, not the original no-progress
observation. Its counters stop at 441,344 bytes / 66 reads. The relationship
between firmware request sizing and this small limit is a lead, not a proven
cause: no failing command or firmware call arguments were captured.

The original stall is **not reproduced or explained**. Later media preserves
the original ESP files, but its pool has subsequently booted, so this is not
an exact replay of every original disk byte. Do not describe it as fixed.
Arguments, UART, QMP/register samples and screenshots remain in the private
qualification artifacts. All diagnostic QEMU jobs are stopped.

## Subsequent firmware assertion

The first pre-install boot of the 600 MiB Fedora derivative changes the
diagnosis: fresh-variable OVMF boots with explicit 512-byte namespaces at
MDTS 6 and 7 stop in the NVMe driver; a legacy device, read-only backing, and a local tmpfs
backing reproduce the assertion too. No guest disk writes complete. These
checks do not establish that the derivative is bootable.

The MDTS-7 capture remains stopped after 133.7 seconds. Memory inspection
finds `ASSERT MemoryAllocationLib.c(821)` inside OVMF's `NvmExpressDxe`, at
relocated RIP `0x7eb5443e`; the earlier 1-GiB Limine capture had
`0x3eb5443e`. The matching relative address is a strong lead, rather than
proof that the original stack was identical. The new stack identifies an
NVMe FLUSH timeout, controller reinitialization, failed Identify Controller,
and a failed pool free (`EFI_INVALID_PARAMETER`), followed by the deliberate
assertion loop. That status describes the free, not the first command failure.
The [pinned firmware cleanup](https://github.com/tianocore/edk2/blob/2970e5699ba6/MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressHci.c#L878-L896)
and [timeout path](https://github.com/tianocore/edk2/blob/2970e5699ba6/MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressPassthru.c#L798-L865)
match the captured instructions and return addresses.

This is a firmware assertion, not evidence of slow executable reads. The
original cause of the command timeout and invalid free remains unproved; no
firmware or Limine patch is proposed as a demonstrated fix. The owner-configuration comparison below separates the original and derived
fixtures. The successful
read-only Limine controls above remain separate from this failing Fedora boot.

## Owner firmware and device comparison

The owner's copied `edk2-ovmf-20260812-8.fc44` pair is byte-identical to the
installed files used above: CODE SHA-256
`904bfa3e0d966372b43b804c4fe323ae63751566687c2bfdf52ca947f47eb13a`,
VARS `6ed987af3a3c155be71665f510eae3e007eda9b8b94afd59d45e91c4a11565cc`.
Package verification is clean. An older OVMF RPM is also registered locally,
but its files are not the active pair. QEMU is the same Fedora build,
`10.2.2-1.fc44`.

Using fresh copies of the owner's VARS and `-machine q35,accel=kvm`,
`-drive if=none,id=d0,format=raw,file=...`, and exactly
`-device nvme,drive=d0,serial=fixture` (no explicit MDTS, namespace, block size
or boot index), with four host-CPU vCPUs, 2 GiB and no NIC:

| Input | Result |
| --- | --- |
| Copy of the original Fedora cloud fixture, 100 MiB FAT16 ESP | Reaches `multi-user.target` and serial login; QMP powerdown completes cleanly. |
| Previously unmodified 600 MiB FAT32 derivative copy | No Fedora startup; captured stack still contains the pool-free assertion after 385.5 seconds; zero target writes/flushes. |
| Existing synthetic coexistence attempt, target read-only; SCSI CD supplies Limine's manual GUID entry | Kernel and approximately 40 MiB archive load from NVMe, Caelum enters and rescue shell starts; zero target writes/flushes. |

The copied firmware is therefore not a different build. Both original Fedora
and the Limine NVMe-file path boot with the owner's simple controller options;
the derivative's failure is fixture-dependent in these checks. This does not
identify which layout/filesystem property or firmware defect initiates it, nor
prove the original Limine observation had the same cause. The 600 MiB fixture
remains unqualified; no firmware or Limine change has been made. The original
Fedora boot used the scratch copy previously used for Review, so no post-Review
whole-image hash was retained before Linux changed that copy. The original
supplied fixture stays pristine.

## Adopted Fedora qualification input

The owner supplied a separately boot-tested 600 MiB FAT32 fixture,
`fedora44-coexist-fat32.raw`, SHA-256
`b463c5359b79640907971459f91912b3e32c89dde4b968483a37ff1cd622afde`.
It deletes/wipes the old FAT16 ESP, leaves that 100 MiB gap, preserves Fedora's
root, and creates ESP entry 4 after the root with unchanged EFI files and the
original volume serial. The owner boots it through `EFI/BOOT` to Fedora login
using the same OVMF pair. Task 3 uses copies of this input; it does not require
a fix or further diagnosis of the failed experimental derivative.

Current-source coexistence Install and Update pass on copies of that fixture
with strict independent preservation/source checks. Fedora reaches login and
clean shutdown on the actual post-Install disk under nested KVM and the actual
post-Update disk under TCG, both attached as NVMe. Repeated post-Update
nested-KVM firmware boots time out; a fresh pre-Update copy also times out and
asserts after reset. A diagnostic repeat observes FAT `DiskError=1` despite
unchanged on-disk root data. MDTS 6 does not avoid the failure; TCG with `max`
CPU does. A 40-bit KVM control succeeds only on the subsequently Linux-booted
disk; a fresh frozen copy still fails, so changing address width is no proven
workaround. This separates observations without establishing
whether acceleration, CPU exposure or another firmware interaction initiates
it. See the [coexistence results](nvme-coexistence-task3.md#bootable-fedora-coexistence--current-source-functional-checks).

Successful controlled kernel/archive loading from NVMe removes the earlier
observation as a demonstrated Limine-wide failure. The initiating cause remains
unproved: the firmware binaries compared above are identical, so a different
OVMF build is not established. Read-only native firmware loading on both vela
and the ThinkPad remains a separate question, under the prerequisites below.

## Upstream inspection

[Version 12.9.3](https://github.com/Limine-Bootloader/Limine/releases/tag/v12.9.3)
fixes a memory-map omission and licensing documentation, without an NVMe or
BlockIo fix. [Issue 633](https://github.com/Limine-Bootloader/Limine/issues/633)
concerns pre-configuration GPT discovery and was fixed in 12.6.1. Historical
EFI discovery hangs and an Apple Silicon NVMe write problem also predate this
pin. [Issue 530](https://github.com/Limine-Bootloader/Limine/issues/530) concerns
module loading on older Dell firmware with a greater-than-4-GiB mapping problem;
that diagnosis is not established for this 1-GiB OVMF guest. No matching report
was found; the search is not exhaustive.

The [pinned UEFI reader](https://github.com/Limine-Bootloader/Limine/blob/e3aa138d383ca4fe061f05931b5a54497cd502f2/common/drivers/disk.s2.c#L342-L353)
calls firmware `ReadBlocks()` without its own timeout wrapper. Its cache uses
firmware transfer granularity, normally 64 sectors and at most 512. The loading
message precedes subsequent processing too; it alone does not locate a stalled
PC inside `ReadBlocks()`. No Limine replacement or patch is justified by the
current observations.

## Read-only native prerequisites

**Later evidence, 2026-10-21:** the
[approved ThinkPad integration](grub-limine-native.md) successfully loads the
kernel/archive from its NVMe ESP through Fedora GRUB and Limine, then completes
Coexist Install/Update and normal/rescue checks. The prerequisites below remain
for vela; this dated proposal does not mean the ThinkPad check is still pending.


Both laptops need separate checks: ThinkPad's SK hynix PC611 (`1c5c:1639`,
512-byte namespace, reported MDTS 6) and vela's SanDisk SN7100S (`15b7:5044`,
512-byte namespace, reported MDTS 7). Fresh read-only inventory must establish
each ESP's partition GUID and an existing regular EFI file's canonical path,
size and hash. Keep identifiers in local configuration, never repository
examples. QEMU results do not qualify either firmware/controller pair.

A first firmware-read diagnostic can load the pinned kernel and ordinary
archive from USB/PXE, plus an existing NVMe EFI file as a second Limine module.
Limine loads that file but does not execute it. Current Caelum intentionally
stops at `expected exactly one initrd module`, before `arch_init` and storage
driver startup. This would qualify that file's read only, not a full Pyxis NVMe
boot or a large archive. This exact diagnostic still needs its QEMU check
before advertising a native packet as ready.

A full kernel/archive check requires those files already on the NVMe ESP. If
absent, it requires separately authorized staging writes; it cannot be called
a no-write test. Neither check changes foreign menus or firmware boot entries.

Follow the owner's run-sheet rules: numbered one-command checks, `stick-deploy`
for USB kernel/archive delivery, no manual installed `limine.conf` edits, and
no OLED blanking option for attended checks. A request-specific module URI
requires an orchestrator-approved one-off entry; no native entry or packet has
been staged. ThinkPad may use PXE; vela's USB staging route must be settled
before asking the owner to run it. Native writes remain unauthorized.

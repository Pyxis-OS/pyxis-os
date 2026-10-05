# USB installation

Pyxis boots from a USB drive, reads and writes a Pyxis pool on USB mass storage,
and installs onto and updates a USB drive with its native installer. The first
native installation, onto a ThinkPad USB stick, was tagged `0.0.1` on 2026-10-05.
This page summarizes that path and the contracts that span its layers. The
subsystem references hold the details.

| Part | Implemented behavior | Reference |
| --- | --- | --- |
| Raw boot image | `make usb-image` builds a GPT image with a FAT32 ESP (Limine, kernel, boot archive) and an npfs sample pool; QEMU can boot it through emulated USB | [Raw USB boot image](../development/usb-image.md) |
| Controllers | Every discovered PCI xHCI function is prepared independently; `CONFIG_XHCI=y` is the default | [xHCI controllers](usb-xhci.md) |
| Enumeration | Boot-present root devices and USB 2/3 hub descendants form an immutable inventory | [USB enumeration](usb-enumeration.md), [hubs](usb-hubs.md), [lsusb](../userland/lsusb.md) |
| Storage | Bulk-Only/SCSI disks register as kernel block devices with GPT discovery; qualified disks accept writes and flushes | [USB BOT/SCSI storage](usb-storage.md) |
| Mounts | Configured GUID authority opens USB npfs volumes read-only or, when qualified, writable | [Configured mount authority](usb-storage.md#configured-mount-authority) |
| Development loop | Source, objects and executables persist on a delegated USB volume | [Persistent USB development](../development/edit-build-run.md#persistent-usb-development) |
| Installer authority | Trusted Install init gets exclusive raw claims on qualified USB disks, as on VirtIO | [Installer disk authority](installer-authority.md) |
| Install and Update | The installer formats a target, or replaces only its ESP while keeping the pool | [Native installation](../userland/installer.md), [system updates](../userland/system-updates.md) |

Limine loads the kernel and archive into RAM, so booting packaged programs needs
no Caelum USB driver. Native USB access is needed only for the pool after firmware
boot services end.

## Layers and ownership

- **xHCI** owns controller registers, rings, DMA, endpoint scheduling and
  completions. It does not interpret storage commands.
- **USB core** owns descriptors, enumeration, inventory and control requests.
  Class matching uses interface descriptors.
- **Mass storage** owns Bulk-Only command/data/status exchanges, SCSI commands
  and BOT reset recovery, and serves the block contract.
- **Block, GPT and filesystem** layers do not know about endpoints or rings.

Unknown classes stay unbound. Unsupported interfaces, devices and controllers
produce explicit per-device or per-controller results without entering storage
code. State belongs to the controller, device, interface or request that owns
it; no global makes every transfer belong to one disk. There is no generic
driver framework or plugin registry.

## Disk selection and authority

Each physical device contributes at most one storage candidate. Its first
supported configuration and alternate in descriptor order is selected: one
non-composite interface, SCSI transparent command set over Bulk-Only
(`08/06/50`), one Bulk-In and one Bulk-Out endpoint, no streams and LUN 0 only.
A device that also offers UAS is used through its BOT alternate. No vendor ID,
controller, port number, route or sampled capacity selects a disk, and Linux
device names or observations are not matching rules.

Incomplete inspection prevents binding that device. A failed media probe keeps
its candidate and never substitutes another disk. Boot discovery seals once every
controller reaches a terminal outcome. Partial coverage, such as an unsupported
EHCI controller or an uninspected hub branch, stays explicit and does not stop
individually ready disks from serving I/O.

- **Configured mounts** wait for sealed discovery and terminal GPT scans, then
  select the sole observed matching GUID. Duplicate observed matches fail. With
  no match, complete discovery reports NOT_FOUND and partial discovery reports
  UNAVAILABLE.
- **Installer inventory** waits for sealed discovery and lists every observed
  VirtIO and USB candidate with its eligibility. It refuses lost registry records
  or incomplete VirtIO bookkeeping. Sole-eligible selection and typed consent
  apply only to observed disks; unseen disks may exist.
- **Ordinary programs** receive directory and file grants, never raw USB access.
  USB addresses, routes, GUIDs and volume names grant nothing. No reset or
  recovery substitutes a new disk into existing authority.

## Read-only and writable qualification

A disk is readable once its probe succeeds: INQUIRY, readiness, capacity with
512- or 4096-byte logical blocks, and reads of the first 64 KiB and the final
block. A disk is writable and flushable only when MODE SENSE reports write
protection clear and a real blocking whole-medium SYNCHRONIZE CACHE (10) succeeds.
Unknown protection or a clean rejection keeps a healthy disk read-only.
Qualification writes no data and changes no cache mode.

Writable mounts need mount WRITE authority, an explicit writable request,
both qualified capabilities and no latched write failure. Installer write claims
need the same qualification and exclude mounted or already claimed devices.
A failed or abandoned published write or flush latches write failure until
reboot; no mutation is replayed.

## Controller, request and failure ownership

PCI claiming is staged: firmware command state is preserved for BIOS handoff,
halt and reset before bus mastering is cleared and BARs are sized. Controller
records, rings, contexts, device pools and I/O buffers are allocated on the BSP
before AP startup. One worker per controller runs after scheduler setup; its IRQ
path only acknowledges and wakes it. All claims, mappings and DMA backing stay
retained until reboot.

Requests capture outbound bytes and keep no caller read pointer. A caller collects
or abandons a ticket exactly once; a wait timeout neither cancels nor consumes it.
Published work keeps its request, ring span and DMA until checked completion or
confirmed safe retirement, even after abandonment. Endpoint stall recovery belongs
to USB; BOT reset ordering belongs to mass storage. Recovery never turns a failed
command into success. Timeout, removal during active work, corrupt events or
unsafe recovery stop that controller for the boot; others continue.

Deadlines are one second for firmware handoff, halt and reset; five seconds for
a controller command, control transfer or BOT exchange; five seconds for
recovery; and thirty seconds for boot enumeration and media setup. Retries never
restart a deadline. These values and the resource budgets live in
`kernel/usb/settings.h` and are implementation choices, not contracts.

## Validation

QEMU qualification covers [read-only probing, block reads and mounts, qualified
writes and synchronization](../development/usb-storage-bringup.md), the
[persistent development loop](../development/usb-storage-bringup.md#persistent-usb-development-loop-c2)
and [installer USB Install, Update and interrupted-Update recovery](../development/experiments/usb-installer-c3/README.md).

Native evidence comes from the owner's ThinkPad T14 Gen 1 AMD and is
[owner-reported](../targets/t14-gen1-amd/usb-bringup.md):

- **2026-10-03:** inventory of all three xHCI controllers, including nested
  high-speed dock hubs.
- **2026-10-04:** read-only probes of a root-port stick and a stick behind the
  dock's USB 3 hub, with capacities matching Linux.
- **2026-10-05, installation:** PXE live media from `4dc804a` installed onto a
  SanDisk 128 GB stick (`0781:55a9`) in a built-in port in about 15 s. The stick
  qualified for writes. Booted alone, it ran `4dc804a` with writable `system://`.
  A synced `keep.bin` kept its SHA-256 across a synced power-off. The owner
  tagged `4dc804a` as `0.0.1`.
- **2026-10-05, Update:** Update from SMP task 2a live media replaced the stick's
  ESP in 14 s. The SHA-256 of `keep.bin` was unchanged afterwards.

Pyxis has no orderly shutdown. A synced power-off is `sync` of the written files,
an idle shell, then holding the power button. It covers only data synced before
power was removed.

## Limits

- Only xHCI and Bulk-Only storage are supported; see
  [USB controller and transport coverage](../technical-debt.md#usb-controller-and-transport-coverage).
- Devices are found once at boot. Hotplug, power management, the controller
  profile and startup settling are listed under
  [xHCI hardware profile](../technical-debt.md#xhci-hardware-profile-and-runtime-retention).
- Topology bounds, multiple LUNs, composite devices and recovery paths that have
  not run on hardware are listed under
  [descriptor bounds](../technical-debt.md#usb-descriptor-bounds-and-per-port-preparation).
- Write qualification watchpoints and flush deadlines are under
  [writable-media qualification](../technical-debt.md#usb-writable-media-qualification-limits).
- Power loss during writes, uncertain I/O, other sticks, ports, the dock path
  and the internal NVMe are not qualified; see
  [installer inspection and recovery](../technical-debt.md#installer-inspection-and-recovery-limits).
- The [discovery latency measurement](../technical-debt.md#configured-mount-discovery-latency)
  and [GUID and boot-device identity](../technical-debt.md#configured-guid-and-boot-device-identity)
  review remain deferred.
- The raw image builder replaces its pool on each build and has an unexplained
  emulated firmware failure; see
  [image updates and firmware](../technical-debt.md#usb-image-updates-and-firmware-qualification).

A replaceable stick limits the cost of a failure. It proves no endurance or
durability guarantee.

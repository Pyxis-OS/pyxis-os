# Steam Deck as the second hardware target

Status: **accepted plan; task 9 is implemented and qualified on the ThinkPad
with the Steam Deck dock.** See [dock Ethernet](#9-dock-ethernet). The owner
accepted the [plan decisions](#owner-decisions-2026-10-11) on 2026-10-11. Other tasks require
their own owner go.

The ThinkPad T14 Gen 1 (AMD) stays the primary machine. A Steam Deck OLED adds a
second AMD machine with a handheld form factor: a portrait panel, a touchscreen,
built-in controls and no internal USB keyboard or NIC. The aim is to boot and use
Pyxis on it, and to make it the development device for a touch-first desktop (see
[the desktop note](#touch-and-the-desktop)). It is not a games-console port, and
it does not add Steam, SteamOS or Proton compatibility.

## Hardware and what carries over

Collected on 2026-10-11 from SteamOS 3.7.17 (Linux 6.11) with the Deck in its
official dock. The raw inventory stays outside the repository; MACs, serials and
UUIDs are not recorded here. Board: Valve Galileo, firmware F7G0112 (2024-08-01),
UEFI x64. The firmware also offers UEFI PXE over IPv4 and IPv6 and an IOMMU option
(off by default; SteamOS boots with `amd_iommu=off`).

| Area | Hardware | Pyxis status |
| --- | --- | --- |
| CPU | AMD Custom APU 0932, Zen 2 (family 17h, model 91h), 4 cores/8 threads | First boot (2026-10-09) showed 8 CPUs, HPET and uACPI working; the battery appears in the bar |
| Panel/GPU | eDP 800x1280 portrait; AMD GPU `1002:1435`; DisplayPort from the dock | Limine's GOP framebuffer works at 800x1280, unrotated. No native GPU driver, as on the ThinkPad ([display](../kernel/display.md)) |
| Storage | Phison PS5021-E21 NVMe (`1987:5021`), 954 GB, SteamOS GPT with eight partitions | Covered by the [NVMe driver task](nvme-coexistence.md); the Deck boots over PXE or from a USB stick meanwhile |
| SD card | O2 Micro `1217:8621` reader | Later |
| USB | `04:00.3` xHCI (dock hubs and ports) and `04:00.4` xHCI, internal, whose USB 2 port carries the Steam Controller; its USB 3 root has no ports | The [xHCI driver](../devices/usb-xhci.md) runs on `04:00.3`; whether `04:00.4` came up is not recorded; see [first boot](#first-boot-2026-10-09) |
| Dock | VIA hubs `2109:2817` (USB 2) and `2109:0817` (USB 3); the keyboards used for the inventory sit on the USB 2 side | The same [USB 2 hub path](../devices/usb-hid.md) that works on the ThinkPad dock |
| Dock Ethernet | ASIX AX88179A (`0b95:1790`) in CDC-NCM class mode, on the SuperSpeed hub | Native NCM driver: ThinkPad with this dock passes DHCP, Remote, cable/USB recovery and file integrity. Deck-host check remains unrun; see [task 9](#9-dock-ethernet). Firmware PXE is independent |
| Wi-Fi | Qualcomm QCA2066 (`17cb:1103`, ath11k) | Out of scope, as for the ThinkPad |
| Bluetooth | The same QCA2066, attached by UART (ACPI `AMDI0020`), not USB | Needs a UART transport under the [HCI layer](../devices/bluetooth-hci.md); see [task 10](#10-bluetooth-over-uart) |
| Controls | Steam Controller `28de:1205`, internal USB: three HID interfaces and a CDC-ACM pair | Lizard mode only; see [task 2](#2-built-in-controls-in-lizard-mode) |
| Touchscreen | FTS3528 (`2808:1015`), I2C HID v1.00, ACPI `FTS3528` with compatible ID `PNP0C50` | None; the owner's priority, [tasks 4 to 8](#touch-track) |
| Audio | AMD ACP `1022:15e2` with SOF; nau8821 codec and two max98388 amplifiers behind I2C; HDMI/DP audio through HDA `1002:1640` | ACP/SOF is a large separate task; see [task 11](#11-audio) |
| IOMMU | Present and optional in firmware | Pyxis uses none; leave it off |

## First boot (2026-10-09)

Limine and Caelum reached the rescue shell from a USB-C stick. The portrait panel
was not rotated. No NIC exists without the dock. The boot log then reported
xHCI `4:0.3` root port 2 insertion unsupported until reboot, and the system and
home volumes failed with CALL_NOT_FOUND. The exact message is quoted from the
owner's historical record. The startup diagnostic now reads "changed after
startup snapshot; excluded from startup admission". Supported hubs/HID have
bounded runtime recovery; runtime storage and pool reattachment remain excluded.

Hypothesis, from code reading only and unmeasured on the Deck: `prepare_ports`
takes one startup snapshot of connected root ports, so a stick (or the USB-C port
controller) that reconnected afterwards was retired with its volumes.

## Tasks

Tasks 1 to 3 make the Deck usable at all; touch follows ([decision 2](#owner-decisions-2026-10-11)).
Tasks 4 to 6 need no display work and can proceed in parallel with tasks 2 and 3;
task 7 needs task 3.

### First check: PXE through the dock

**Result, 2026-10-11: passed.** The owner booted the Deck over UEFI PXE through the
dock (dnsmasq tag `deck`), fetching Limine, the kernel and the initrd over TFTP
with only the firmware's own network driver, as on the ThinkPad. The dock's USB
keyboard and mouse worked in Pyxis. The internal xHCI `04:00.4`, which carries the
Steam Controller, timed out; task 2 starts from that. The Deck's development loop
is the PXE loop, with the USB stick as the fallback. The dock NIC's MAC stays in
horse's dnsmasq configuration and never in the repository.

### 1. xHCI settle window

Measure first: trace when each root port reports its connection on the Deck, with
the stick plugged in before power-on and plugged in at the Limine menu. Then add a
bounded settle window before `prepare_ports` takes its snapshot, so a stick whose
link comes up late is part of the boot inventory. The window ends early once the
connected ports stop changing, with a fixed upper bound, and the log says whether
it ran to the bound. It is a boot-time wait, not a hot-plug design: runtime
attachment of storage stays outside the current [USB profile](../devices/usb-enumeration.md#bounded-runtime-hid-attachment).
The cost is added boot time on every machine, so record it on the ThinkPad too.
A PXE boot removes the stick from the boot path but not the rule: Pyxis's own
xHCI still takes one snapshot of the dock's hubs and devices, so measure the dock
keyboards too, and keep the task for the stick fallback.

**After this task the owner can:** boot the Deck from the USB-C stick to the
shell with the system and home volumes mounted, as on the ThinkPad.

### 2. Built-in controls in lizard mode

The controller's own default mode emulates a keyboard and mouse. Its descriptors
(`lsusb -v`, 2026-10-11) fit the [admitted boot profile](../devices/usb-hid.md#binding-and-ownership)
only partly: interface 0 is a mouse by protocol (2) but has subclass 0, not the
boot subclass, so SET_PROTOCOL(boot) does not apply; interface 1 is a boot
keyboard; interface 2 is a vendor-style 64-byte HID; interfaces 3 and 4 are
CDC-ACM. The device is USB 2.0 on the internal controller. Steps:

1. Find why the internal controller (`04:00.4`) timed out in the 2026-10-11 PXE
   boot, then whether the composite's CDC interfaces stop the keyboard interface
   binding.
2. Bind the keyboard interface with the existing support.
3. Take the mouse interface through the descriptor-driven USB report mouse
   support in the [pointer quality reference](../devices/pointer-input-quality.md),
   rather than a Deck-only path.

Pyxis sends no vendor commands and switches no controller mode, per that plan.
Which buttons produce which keys in lizard mode is not established; the task's
proposal records it from native evidence.

**After this task the owner can:** move the pointer with the trackpads and
reach the shell with the Deck's own buttons, without an external keyboard.

### 3. Portrait rotation

The panel is portrait and the handheld is held landscape. Proposed contract:
composition, the navigation bar, TTYs, graphics frames and the software pointer
all see a 1280x800 landscape screen, and only the presenter's final copy from the
[RAM staging frame](../kernel/display.md#ram-staging) to scanout memory
transposes it, in cache-sized blocks. Which direction is upright is measured on
the Deck. The setting is a configured display rotation, set by the Deck's image,
rather than a hard-coded match on the board name. GPU scanout rotation is not
used: no GPU driver exists. Known limits: panic output writes the front buffer
directly and would appear sideways; Limine's own menu stays portrait; the
transposed copy costs more than the linear one, so measure it against the
[presentation baseline](../kernel/display.md#presentation-timing) and the
[frame skipping](../kernel/presenter-frame-skipping.md) already in place.

**After this task the owner can:** use the shell and graphics applications
upright in the Deck's handheld orientation.

### Touch track

Touch follows the owner's stated path. The Deck's touchscreen is an ACPI
`FTS3528` device (compatible ID `PNP0C50`, HID over I2C) on the second
DesignWare I2C controller (`AMDI0010`, ACPI path `\_SB.I2CB`). Decoding the
DSDT by hand gives a 400 kHz I2C connection at address 0x38, a GpioInt and a
GpioIo on `\_SB.GPIO` (`AMDI0030`), and the standard HID-over-I2C `_DSM`; this is
unconfirmed on hardware. Linux binds it with `hid-multitouch`. The DesignWare
controller serves other devices too: the audio amplifiers sit behind I2C, and
the DesignWare and GPIO pieces may serve I2C touchpads on laptops. Whether the
ThinkPad's touchpad or TrackPoint uses them is unverified; the
[pointer reference](../devices/pointer-input-quality.md) implements PS/2 there and keeps I2C-HID
outside its scope.

#### 4. DesignWare I2C controller and ACPI resources

A kernel I2C master for the AMD DesignWare block, found through ACPI, with
bounded polled transfers and explicit arbitration, NACK and timeout results
(Linux logs lost arbitration on the neighbouring instance at boot, so these
paths are exercised). Add ACPI resource decoding for I2cSerialBus, GpioInt and
GpioIo to the kernel's [ACPI](../kernel/acpi.md) support, on uACPI's resource
decoder. No input is produced yet. The task proves the bus by reading the
device's HID descriptor register at the `_DSM`-reported address.

**After this task the owner can:** read the touchscreen's HID descriptor and
report descriptor from the Deck's boot log, matching Linux's view.

#### 5. HID over I2C with polling

The HID-over-I2C v1.00 protocol: reset handshake, power on, descriptor and
report-descriptor fetch, then input-report reads. Interrupt delivery is polled
first: read the GPIO line level through the `AMDI0030` registers, or read the
input register on a fixed short interval, whichever the task shows to be correct
for the FTS3528 and cheaper in latency and CPU. The polling interval and its CPU
cost are recorded as an accepted limit. Reports are decoded in the next task.

**After this task the owner can:** touch the screen and see raw report bytes
arrive in the log, at a bounded polling cost.

#### 6. Multitouch reports and a contact event type

Parse the multitouch digitizer collection from the report descriptor (contact ID,
tip switch, X/Y, contact count, optionally pressure/width, scan time), following
Linux `hid-multitouch` behavior as a reference rather than copying it. The input
layer gains a contact event type: contact identity, down/move/up, position in
panel coordinates and a timestamp, kernel-normalized and per source like the
[system pointer](../interfaces/pointer.md#input-source-coordination). The first
consumer is a small native program that draws active contacts. Gesture
recognition is not part of the kernel.

**After this task the owner can:** run a program that draws every finger on the
screen with stable IDs, in panel coordinates.

#### 7. Touch-to-screen mapping

Map contact coordinates to screen coordinates through the same rotation as task
3, so that a touch lands under the finger in the upright orientation, and clip to
the display area. Mapping is applied once, in the kernel, as for pointer scroll
direction; applications see screen coordinates. Needs the display rotation and the
panel's reported extents.

**After this task the owner can:** touch a point on the upright screen and see
the contact drawn under the finger.

#### 8. GPIO interrupt

Replace polling with the `AMDI0030` interrupt: pin controller registers, the
controller's shared interrupt and per-pin edge or level configuration from the
ACPI resource. Keep polling as the path if the interrupt is not delivered. How the
controller's interrupt reaches the I/O APIC is part of the task's proposal.

**After this task the owner can:** use touch with no polling interval, with CPU
use at idle matching the earlier measurement.

### 9. Dock Ethernet

The native [CDC-NCM driver](../devices/usb-cdc-ncm.md) implements the accepted
profile in [#859](https://git.internal/PyxisOS/pyxis-os/pulls/859) with
[userland #267](https://git.internal/PyxisOS/pyxis-userland/pulls/267).
The Steam Deck dock's AX88179A `0b95:1790` selects configuration 2 NCM on the
`2109:0817` SuperSpeed hub; no vendor driver or ECM fallback is added.

The three owner defaults (2026-10-14) are implemented: bounded class/carrier
preparation before BIND with read-only lookup, bounded boot/admitted-branch
recovery with fresh fenced storage and retained identity, and ordinary UDP log,
Remote and file transfer with emergency USB transport deferred. Owning BSP USB
workers retain USB/DMA state; the network worker exchanges copied frames.
NTB16 buffers are at most 16 KiB, MTU 1500, with bounded TX batching and prepared
candidate/replacement capacity. Configuration and budgets are tunable, not ABI.
See the [driver contract](../devices/usb-cdc-ncm.md) for ownership and refusal rules.

- [x] Native carrier, pooled DHCP and Remote on the ThinkPad with the Steam Deck dock.
- [x] Ethernet cable down/up preserves the lease and restores traffic.
- [x] Boot loss and USB-C unplug/replug recover the same NIC and binding.
- [x] Native transfer into tmp:// with independent integrity verification.

Owner qualification (2026-10-15) recovered boot generation 1 and subsequent
USB generations 2 and 3, all on the same NIC, with ping passing after each
cycle. HTTP copies of 64 KiB and 15 MiB into tmp:// matched independent host
SHA256 digests. The [experiment record](../development/experiments/usb-cdc-ncm/README.md)
separates exact native/probe revisions, QEMU checks and unrun cases. AX88179A
quirks are documented beside the driver: advisory datagram reply 28 vs descriptor
1514, SPEED_CHANGE naming the data interface, and the observed boot dock/PD
power interruption. No periodic summaries, raw notices or DHCP wire observer
are in the submitted branch.

**After this task the owner can:** use a supported NCM dock for DHCP, normal
UDP logging, Remote and native file transfers. This is qualified on the
ThinkPad with the Steam Deck dock; the Deck itself and vela remain hardware
checks. RTL8156 adapters are expected to offer an NCM configuration but still
need descriptor/native confirmation. This does not cover a hang before network
startup. [Fatal USB UDP and stopped-kernel GDB](https://git.internal/PyxisOS/pyxis-os/issues/1281)
remain deferred; a dock-only Deck cannot yet rely on them.

Firmware PXE uses its own driver and is independent of this task. The native
scope does not add general USB hotplug or storage/HCI readmission. No shared
host USB gadgets may be created or torn down; NCM gadget checks belong only in
a disposable VM, or use real hardware. No compiler/container rebuild is needed.

### 10. Bluetooth over UART

The QCA2066 hangs off an `AMDI0020` UART. Needs a UART HCI transport (H4 framing)
below the [HCI layer](../devices/bluetooth-hci.md) now bound to the USB AX200, the
QCA vendor firmware download (`qca/hpbtfw21.tlv`, then the NVM `hpnv21g.309`) and
the same userspace stack as the [Bluetooth mouse milestone](bluetooth-mouse.md).
The firmware files need an owner mirror entry before any build uses them
(upstream URL and files to be named in the task proposal), and their licence
recorded.

**After this task the owner can:** pair the MX Master 3S to the Deck.

### 11. Audio

HDMI/DP audio is the easier piece: it reuses the [HDA](../devices/hda.md) driver
for the GPU's audio function and needs an external display. Built-in speakers need
the ACP with SOF firmware, plus I2C control of the codec and amplifiers (task 4),
which is a large separate piece. See [audio](../interfaces/audio.md) and
[later audio directions](later-os-directions.md#audio).

**After this task the owner can:** hear Pyxis audio on the Deck, first through a
docked display, later through its speakers.

## Touch and the desktop

A direction, not a decision. The Deck is the development device for the desktop's
touch interface, so touch should be its own input, not a pointer emulation. A
contact has an ID, state, position and timing; gestures such as tap, drag, scroll,
pinch and long-press are recognized in userspace, in the desktop or a library,
from those contacts. The kernel's pointer stays the mouse and touchpad source, and
any contact-to-pointer translation is a userspace policy that an application or
the desktop can replace. The [desktop direction](desktop-graphics.md) covers
windows and the compositor; touch adds hit testing by contact, per-contact
focus and an on-screen keyboard. [Technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1344)
records that the pointer has no touch-contact API today.

## Owner decisions (2026-10-11)

1. **Touch representation: accepted.** Contacts are their own event type from the
   first touch task. Only a small contact-drawing program consumes them at first;
   pointer translation, if wanted, is a later userspace policy, so the shell and
   mux do not respond to taps at first.
2. **Priority: accepted.** Tasks 1 to 3, then touch (4 to 8), then dock Ethernet,
   Bluetooth and audio. The remote tools depend on task 9, so the owner can move
   Ethernet earlier if Deck iterations become frequent.
3. **Development loop: PXE**, as on the ThinkPad; the
   [first check](#first-check-pxe-through-the-dock) passed. The USB stick is the
   fallback. Pyxis-side networking keeps its place in task 9.

## Not decided

Installing Pyxis on the Deck's internal disk, beside SteamOS or instead of it, is
outside this plan; it follows the NVMe and installer work. Wi-Fi, the SD reader,
the Deck's haptics, gyro, ambient light sensor, fan and thermal control, charging
and the USB-C port controller are not planned.

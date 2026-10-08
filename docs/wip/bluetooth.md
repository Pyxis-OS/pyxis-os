# Bluetooth investigation

Status: **tasks 1, 2 and 3a complete, 2026-10-08; HCI/firmware/scan probes unassigned.**
The owner wants to pair a Logitech MX Master 3S, a Bluetooth-only LE mouse, and use it on Pyxis.
This investigation establishes the path as far as a first LE scan and ends in a
report and a milestone proposal. Shared kernel interrupt-IN support is now an
authorized task to merge before the HCI probe. HCI, firmware and scan probe code
stays on unmerged branches; those probes remain unassigned.

## Hardware

The ThinkPad's Bluetooth is the AX200's USB function, separate from its PCI
Wi-Fi function (`03:00.0`):

- **Device:** Intel `8087:0029`, full speed, on the AMD xHCI `07:00.4` (Linux
  bus 4), port 4. Port 3 is the fingerprint reader.
- **Interfaces:** interface 0 carries HCI: commands over control transfers,
  events on an interrupt IN endpoint, and ACL data on bulk endpoints. Interface
  1 carries isochronous voice (SCO), which a mouse does not need.
- **Pyxis already enumerates it natively:** the
  [first native installation](../targets/t14-gen1-amd/usb-bringup.md) lists it
  among the devices on `07:00.4`.
- **Firmware:** Linux's `btusb`/`btintel` can load Intel `ibt-*.sfi` firmware and
  `.ddc` configuration when initializing the controller. The prepared-host
  [task 3a validation](../development/experiments/usb-interrupt-in/README.md#firmware-observation)
  received an operational-firmware version after attachment and HCI Reset.
  Cold native startup and the exact required firmware files remain unqualified.

## Passthrough to QEMU

Like the [NIC passthrough](../development/thinkpad-nic-passthrough.md), QEMU on
the ThinkPad's Fedora lets the driver be developed in a VM first. This one is a
USB device, so it uses QEMU's `usb-host` instead of VFIO and has no IOMMU group
to consider. The owner sets it up:

1. Free the device: `sudo systemctl stop bluetooth` and
   `rfkill unblock bluetooth`. QEMU's libusb detaches `btusb` when it has
   permission; otherwise unbind it.
2. Give QEMU access to the device node under `/dev/bus/usb/004/`, by running it
   as root or changing the node's owner. Select the device by ID, because its
   address can change after a reset.
3. Attach it to an emulated xHCI:

   ```
   -device qemu-xhci,id=xhci
   -device usb-host,bus=xhci.0,vendorid=0x8087,productid=0x0029
   ```

Attachment must not be assumed to leave the controller in its bootloader:
the prepared-host validation received an operational version. Pyxis's own
firmware load remains part of the investigation for native startup and reset
states that require it. A `USB_HOST=` launcher option can follow once the setup
works, recorded beside the NIC reference.

## Steps

- [x] **1. Passthrough and inventory.** With the setup above, `lsusb` in the guest
   lists `8087:0029` and both interfaces. Record the descriptors and endpoint
   addresses. Natively, confirm the same from the existing inventory.
   The [task 1 report](../development/experiments/bluetooth-task1/README.md)
   records successful QEMU attachment, guest descriptors and native evidence limits.
- [x] **2. Interrupt IN assessment.** Record the endpoint context, ring ownership,
   buffering and loss requirements shared by HCI events and the planned
   [USB HID mice](pointer.md#devices).
   The [task 2 assessment](../development/experiments/bluetooth-task2/README.md)
   records the code gaps, required hardware fields and proposed ownership/loss
   policies, with an addendum linking the later accepted decisions below.
- [x] **3a. Shared kernel interrupt-IN support.** Implement the accepted
   narrow profile below through private kernel USB interfaces, with no public ABI.
   Configure the interrupt endpoint, retain receive/ring ownership, dispatch and
   copy completions, and report terminal stream failures. Complete ordinary build,
   interactive boot and debugger validation before marking this task done. No HCI
   class binding or commands enter the merged kernel; validation uses a separate
   unmerged consumer. The [implemented interface](../devices/usb-interrupt-in.md)
   and [qualification report](../development/experiments/usb-interrupt-in/README.md)
   record the build, passthrough traffic, idle wait, wrap, progress and removal checks.
- [ ] **3b. HCI transport and controller state probe.** On an unmerged probe branch,
   send HCI commands as class requests to interface 0 and read events from the
   interrupt endpoint. Issue HCI Reset and Intel's Read Version, and record whether
   the controller is in its
   bootloader or operational firmware.
- [ ] **4. Firmware load.** Derive the firmware name from the version reply, load the
   `.sfi` and `.ddc` through Intel's vendor commands, reset into operational
   firmware and confirm with Read Version. Linux's `drivers/bluetooth/btintel.c`
   documents the protocol; read it for the sequence, not to copy code. The owner
   mirrors the exact linux-firmware files before any committed build uses them.
- [ ] **5. First LE scan.** Configure and enable LE scanning, and receive advertising
   reports. With the MX Master 3S in pairing mode, identify its reports: the HID
   service (`0x1812`) or mouse appearance (`0x03C2`) and its name.

Stop there. Connections, pairing, bond-key storage, GATT and HID over GATT need
their own decisions.

## Accepted interrupt-IN decisions

The owner accepted the narrow admission, buffering/loss policy and active-removal
limit in comments on [merged PR #517](https://git.internal/PyxisOS/pyxis-os/pulls/517),
and subsequently authorized shared kernel support as task 3a before the HCI
probe. On 2026-10-08 the owner also accepted terminal failure on STALL. These are
accepted choices for that task. Task 3a now implements them; its linked report
states which paths have hardware observations and which have source review:

- Admit boot-present, root-connected full-speed interrupt-IN endpoints only;
  report other speed/topology profiles explicitly as unsupported. Selection uses
  checked descriptors, not the ThinkPad port or Linux bus/address.
- Reserve two receive buffers with 257-byte DMA capacity each and eight copied
  completion entries; the Bluetooth event profile posts 257-byte receives. These
  are accepted resource choices, not measured burst requirements or a losslessness
  guarantee. Overflow latches an explicit stream
  discontinuity/failure and ceases rearming; pending DMA remains owned until
  terminal completion is accounted for, or retained if retirement is uncertain.
- A STALL is a terminal stream failure. Retain its DMA backing and ring identity
  until reboot, with no automatic endpoint recovery.
- Keep the existing controller-wide quarantine on active removal, retaining DMA
  until reboot. The owner accepted its effect on unrelated storage on that
  controller as an investigation limit for the internal AX200.
- Share kernel endpoint configuration, receive ownership and copied completion
  collection through private interfaces, with no public ABI. HCI framing and
  commands, firmware loading, LE scan and HID interpretation remain separate
  consumers/probes.

The initial private interface admits one stream per root device and lets the
consumer select a receive length up to the reserved 257-byte capacity. Terminal
stream failure takes precedence over delivery of already queued bytes.

The existing BSP worker remains the ring/state owner. Receive capture and rearm
must also progress while other controller operations drain events; command and
EP0 ownership must not recurse from completion consumption. Idle absence of an
event is not a transfer failure or permission to reuse a posted buffer. The
[task 2 assessment](../development/experiments/bluetooth-task2/README.md) describes
these source/specification requirements. The accepted limits and revisit points
are recorded in [technical debt](../technical-debt.md#xhci-hardware-profile-and-runtime-retention).

## Deliverables

- **A report** with measured results for each step, in QEMU passthrough and,
  where possible, natively. Distinguish observation from inference.
- **A milestone proposal** covering at least:
  - where the stack lives: the kernel owning the USB transport and HCI packets,
    with L2CAP, ATT/GATT and pairing in userspace, is the starting lean to
    examine, not a decision;
  - firmware: pinned files, mirror, licensing and when they load;
  - LE pairing with Secure Connections, and where bond keys persist
    (npfs, with what authority);
  - HID over GATT reaching the [system pointer](pointer.md);
  - how much of interrupt-transfer support to share with USB HID.

The [Bluetooth direction](later-os-directions.md#bluetooth) records the goal.

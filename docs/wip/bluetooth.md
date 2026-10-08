# Bluetooth investigation

Status: **task 1 qualified, 2026-10-08; tasks 2–5 not yet assigned.** The owner wants
to pair a Logitech MX Master 3S, a Bluetooth-only LE mouse, and use it on Pyxis.
This investigation establishes the path as far as a first LE scan and ends in a
report and a milestone proposal. Probe code stays on a branch and is not merged;
nothing here authorizes committed implementation.

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
- **Firmware:** the controller starts in a bootloader and needs Intel's
  firmware (`ibt-*.sfi` and its `.ddc` configuration, from linux-firmware) loaded
  by the host after every reset. Linux's `btusb`/`btintel` does this today.

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

QEMU resets the device on attach, so the guest is expected to find the
bootloader rather than Linux's loaded firmware. That makes Pyxis's own firmware
load part of the investigation, as it would be natively. A `USB_HOST=` launcher
option can follow once the setup works, recorded beside the NIC reference.

## Steps

- [x] **1. Passthrough and inventory.** With the setup above, `lsusb` in the guest
   lists `8087:0029` and both interfaces. Record the descriptors and endpoint
   addresses. Natively, confirm the same from the existing inventory.
   The [task 1 report](../development/experiments/bluetooth-task1/README.md)
   records successful QEMU attachment, guest descriptors and native evidence limits.
- [ ] **2. Interrupt IN transfers.** Pyxis's xHCI configures only bulk endpoints for
   USB storage today. HCI events need an interrupt IN endpoint. This gap is shared
   with the planned [USB HID mice](pointer.md#devices), so record what a real
   implementation needs: endpoint context, ring ownership, buffering and loss.
- [ ] **3. HCI transport and controller state.** Send HCI commands as class requests
   to interface 0 and read events from the interrupt endpoint. Issue HCI Reset
   and Intel's Read Version, and record whether the controller is in its
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

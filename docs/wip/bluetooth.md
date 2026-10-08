# Bluetooth investigation

Status: **tasks 1–5 complete for the accepted warm-host scope, 2026-10-08; scan results awaiting owner review before final deliverables.**
The owner wants to pair a Logitech MX Master 3S, a Bluetooth-only LE mouse, and use it on Pyxis.
This investigation establishes the path as far as a first LE scan and ends in a
report and a milestone proposal. Shared kernel interrupt-IN support is implemented
and merged. HCI, firmware and scan probe code
stays on unmerged branches. After merging
[PR #526](https://git.internal/PyxisOS/pyxis-os/pulls/526), the owner authorized
task 4 and accepted warm-firmware verification with cold upload deferred.
After merging [PR #528](https://git.internal/PyxisOS/pyxis-os/pulls/528), the owner
assigned task 5 through the orchestrator. Scan code remains on an unmerged probe
branch; the owner must see the scan results before the investigation report and
milestone proposal begin. Bluetooth device addresses must stay out of the
repository, PRs, docs and recorded output.

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
- [x] **3b. HCI transport and controller state probe.** On an unmerged probe branch,
   send HCI commands as class requests to interface 0 and read events from the
   interrupt endpoint. Issue HCI Reset and Intel's Read Version, and record whether
   the controller is in its
   bootloader or operational firmware. The
   [task 3b report](../development/experiments/bluetooth-task3b/README.md) records
   checked Reset/Read Version replies, command credits and operational firmware
   after Reset on prepared-host QEMU passthrough. Cold native state remains unknown.
- [x] **4. Warm firmware verification.** Detect controller state from a checked
   version reply. Accept existing operational firmware for this investigation,
   skip loading and confirm its version remains unchanged. Warm reboot from
   another OS or passthrough can retain that OS's chosen firmware build; USB port
   reset and HCI Reset do not establish a cold bootloader state. Unknown and
   bootloader states must remain explicit rather than claim readiness. On
   2026-10-08 the owner accepted this bounded task and deferred cold upload.
   Version comparison against a future Pyxis firmware pin is a later policy
   decision. Cold loading needs Intel bootloader bulk transport, secure upload,
   boot-event handling and mirrored `.sfi`/`.ddc` files before it can be qualified.
   The [cold-upload debt](../technical-debt.md#bluetooth-cold-firmware-upload-and-running-version-policy)
   records the consequence and revisit point.
   The [task 4 report](../development/experiments/bluetooth-task4/README.md) records
   identical checked version replies after the skip decision, host initialization
   evidence and the unmerged probe revision. No upload was performed or qualified.
- [x] **5. First LE scan.** Configure and enable LE scanning, and receive advertising
   reports. With the MX Master 3S in pairing mode, identify its reports: the HID
   service (`0x1812`) or mouse appearance (`0x03C2`) and its name.
   The [task 5 report](../development/experiments/bluetooth-task5/README.md) records
   a successful scan, correlated complete name plus both HID service and mouse
   appearance, confirmed scan disable, and address-free captures.

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

## Accepted task 5 scan profile

On 2026-10-08 the owner accepted the
following profile through the orchestrator and authorized its implementation:

- Legacy 1M active scanning for 30 seconds after confirmed enable, with a 100 ms
  interval/window, controller duplicate filtering enabled and no accept-list filter.
  Active scan requests use the controller's existing public address over the air;
  no scanner address is logged and no random/privacy address is configured.
- Correlate advertisements and scan responses by address/type only in RAM, in a
  bounded table of 64 advertisers. Table exhaustion aborts identification explicitly.
  Log parsed names, HID service/appearance and RSSI for candidates, with addresses
  redacted before output. No raw HCI advertising packet or address-keyed debugger
  state dump may enter a capture. Identification needs the name plus HID service
  `0x1812` or mouse appearance `0x03C2`; name-only reports remain candidates.
- Configure the private endpoint during enumeration and run the scan afterward
  with its own deadline. Consume reports during command waits. Attempt disable on
  every exit after a potentially submitted enable, using a fresh cleanup deadline;
  terminal stream failure may leave disable unconfirmed, which must be reported.

The linked task 5 report records implementation, revision, configuration and
measurement limits. The probe stays unmerged; the report branch contains only
documentation. The final investigation report and milestone proposal remain
unassigned until the owner has seen the scan results. No connections, pairing,
bond keys, GATT or pointer delivery are authorized by this completed scan.

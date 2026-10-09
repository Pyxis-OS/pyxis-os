# AX200 Bluetooth investigation

The 2026-10-08 investigation qualified ThinkPad AX200 passthrough through a first
LE scan of the owner's MX Master 3S. The owner has seen the results and accepted
the next milestone's stack, security and closure direction. This reference
replaces the completed WIP; the [final report](../development/bluetooth-investigation.md)
and linked task reports retain measurements, revisions, captures and limits.

The shared private [interrupt-IN interface](usb-interrupt-in.md) and the
[runtime HCI transport](bluetooth-hci.md), merged in #552, are on main. The
investigation's own probe code, including its warm-firmware verification and
scan consumers, remains on unmerged branches. The investigation did not qualify
connections, pairing, bonds, GATT or pointer delivery, and main has no pairing,
HID input or Bluetooth service support. The [mouse milestone](../wip/bluetooth-mouse.md)
retains pending policy decisions and unassigned implementation tasks; its task 2,
the runtime transport, is complete.

## Hardware and passthrough

The Intel AX200 Bluetooth USB function is full-speed `8087:0029`, separate from
its PCI Wi-Fi function. Native ThinkPad inventory places it on AMD xHCI
`07:00.4`, root port 4. Interface 0 carries HCI commands on EP0, events on
interrupt IN `0x81` (packet 64, interval 1), and ACL on bulk `0x02`/`0x82`.
Interface 1's isochronous SCO transport is unused by this investigation.

The physical ThinkPad ran Fedora/KVM; QEMU attached the device to an emulated
xHCI, with no VFIO/IOMMU assignment:

```text
-device qemu-xhci,id=xhci
-device usb-host,bus=xhci.0,vendorid=0x8087,productid=0x0029
```

The owner freed the device by disabling/stopping Fedora Bluetooth, unblocking
rfkill and granting the invoking user device-node access. QEMU/libusb detached
both `btusb` interfaces and rebound them on exit; root QEMU and manual unbind
were unnecessary. Repeat preparation must inspect actual host ownership rather
than infer it from this historical setup. USB bus/device-node numbers, QEMU ports
and Pyxis root ports are different numbering domains; checked descriptors and
VID/PID select the device. See the [attachment report](../development/experiments/bluetooth-task1/README.md).

## Qualified scope

The [final report's task table](../development/bluetooth-investigation.md#evidence-by-task)
links inventory, source assessment, interrupt-IN qualification, checked HCI
Reset/Read Version, warm verification and the successful first scan. Native
Pyxis evidence stops at inventory; HCI and radio results used real AX200
passthrough behind emulated xHCI, not the native AMD controller.

Intel firmware variant `0x23` after attachment and HCI Reset was interpreted as
operational using the referenced protocol. The owner accepted skipping upload
and verifying an unchanged version for this warm-host investigation. Fedora's
earlier upload supports retained warm firmware; reset does not establish a cold
bootloader state. Installed SFI/DDC names, aliases and hashes are host observations,
not selected Pyxis assets. [Cold upload and running-version policy](../technical-debt.md#bluetooth-cold-firmware-upload-and-running-version-policy)
remain unimplemented; cold native readiness is required by the next milestone.

## Accepted interrupt-IN decisions

The owner accepted boot-present, root-connected full-speed admission through
private USB interfaces, with one stream per device, two receive buffers up to
257 bytes and eight copied completion entries. These are resource choices,
not measured burst requirements or a losslessness guarantee. HCI and HID framing
remain separate consumers.

The BSP controller worker owns ring/state mutation. Independent physical TD
identities protect posted buffers; completion copying and rearm progress inside
command/control/bulk event drains, without recursive class commands. An idle wait
timeout does not cancel a posted receive or permit DMA reuse.

FIFO overflow terminates the stream with explicit discontinuity; STALL is
terminal, retaining backing/ring identity until reboot. Active removal can
quarantine the controller and unrelated storage, with uncertain DMA retained.
There is no endpoint recovery, runtime stream replacement or reattachment claim.
The [implemented interface](usb-interrupt-in.md) owns the detailed contract;
its [qualification](../development/experiments/usb-interrupt-in/README.md) separates
observed wrap, idle, EP0 progress and removal from source-reviewed error paths.

## Accepted task 5 scan profile

The owner accepted legacy 1M active scanning for 30 seconds after confirmed
enable, with 100 ms interval/window, controller duplicate filtering and no
accept-list filter. The existing public scanner address was used over the air
without recording it; random/privacy addressing was not configured.

A bounded 64-entry table correlated advertisements and scan responses by peer
address/type only in RAM. Identification required name plus HID service `0x1812`
or mouse appearance `0x03C2`. Output used parsed names/evidence/RSSI and run-local
labels, never peer addresses, raw advertising packets or address-keyed debugger
state. Malformed data, table exhaustion or transport discontinuity aborted the
probe rather than publishing partial success.

The endpoint was prepared during enumeration; the bounded scan ran afterward
under its own deadline, consuming reports during command waits. Every exit after
potential enable attempted disable with a fresh deadline; ambiguous replies or
terminal failure leave cleanup explicitly unconfirmed. The measured run
identified the correlated complete name `MX Master 3S`, HID service and mouse
appearance, and confirmed disable. That discovery is not authenticated identity
or proof of Secure Connections support. See the [task 5 report](../development/experiments/bluetooth-task5/README.md).

## Follow-up

The [production proposal](../wip/bluetooth-mouse.md) records accepted direction
and the authorized documentation-only task 1. The owner's later
[SMP account](../development/bluetooth-investigation.md#owner-reported-smp-evidence)
reports Fedora SC Just Works and size-16 encryption, with peer identity-key
distribution; it answers the mouse capability question and requires LTK/IRK
bond storage. Pyxis-side checks and report decoding remain future qualification.
The owner's native batch is completed in #547; it qualifies no Pyxis Bluetooth
pairing. No firmware upload, pairing or host-service change was performed by
this documentation. Bluetooth addresses remain out of repository content,
PRs, docs and recorded output.

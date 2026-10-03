# Native USB inventory observation

The owner reported this ThinkPad T14 Gen 1 AMD run on 2026-10-03, from image
revision `d04c6a65a9fe5b318cd4f36f79efaaa56a3c2030`, with the dock attached.
Its pinned userland is `a6e1a4091353d77c1eec25518f4c26335cedc44c`.
This is an owner-supplied `lsusb`, `lspci` and retained boot-log observation,
not an agent-run native measurement or a complete hardware qualification.
The exact build flags and firmware boot method were not supplied; the output
establishes that native xHCI initialization was enabled.

## Observed controllers and devices

All three xHCI functions produced device records. `lspci` retained the same USB
PCI identities as the [Linux inventory](notes.md#usb-controllers-and-observed-port-routes).

| Controller | Snapshot state | Root ports | Reported preparation |
| --- | --- | ---: | --- |
| AMD `07:00.4`, `1022:1639` | Incomplete | 6 | Capability-summary line was not retained |
| AMD `07:00.3`, `1022:1639` | Complete | 6 | xHCI 1.10, 64 slots, 64-byte contexts, 2 scratchpads |
| Renesas `06:00.0`, `1912:0015` | Complete | 4 | xHCI 1.00, 32 slots, 64-byte contexts, 4 scratchpads |
| Realtek EHCI `02:00.4`, `10ec:816d` | Unsupported | Unknown | No native EHCI driver |

Ports below are Caelum's discovered physical paths. They are observations of
this boot, not stable socket names, driver selection rules or topology limits.

| Controller | Port | Speed | Device ID and observed interfaces |
| --- | --- | --- | --- |
| `07:00.4` | `1` | High | Lenovo hub `17ef:3071`, hub protocols 01/02 at alternates 0/1 |
| `07:00.4` | `3` | Full | Synaptics fingerprint reader `06cb:00bd`, vendor class |
| `07:00.4` | `4` | Full | Intel AX200 Bluetooth `8087:0029`, wireless interfaces |
| `07:00.4` | `5` | Unknown | Unidentified, incomplete |
| `07:00.4` | `1.2` | High | Second Lenovo hub `17ef:3071`, hub protocols 01/02 at alternates 0/1 |
| `07:00.4` | `1.5` | High | Lenovo `17ef:3075`, Billboard class |
| `07:00.4` | `1.2.2` | Full | SHARKOON keyboard `1ea7:0066`, HID keyboard/mouse interfaces |
| `07:00.4` | `1.2.4` | High | Lenovo dock audio `17ef:306f`, audio and HID interfaces |
| `07:00.3` | `1` | Full | Lenovo `17ef:3074`, Billboard and HID interfaces |
| `07:00.3` | `6` | Super | Separate SanDisk stick `0781:55a3`, configuration 1, interface 0, alternate 0, class/subclass/protocol `08/06/50`, two endpoints |
| `06:00.0` | `4` | High | Bison camera `5986:2130`, video control/streaming interfaces |

The overall listing exited 1. Unsupported EHCI makes the global snapshot
partial even when xHCI observations are complete. AMD `07:00.4` additionally
retained its unidentified root-port-5 device; other observed devices on that
controller remained inspectable.

The owner confirmed that `0781:55a3` is a different stick from the documented
`0781:55a9`. The older stick remains attached through the dock's uninspected
SuperSpeed branch. Its identity was not observed by Caelum in this run.
The direct stick advertises Bulk-Only Transport, but descriptor enumeration does
not establish readable media; native USB block transfers remain unimplemented.

## Retained log evidence and interpretation

Selected owner-supplied lines, before diagnostic wording corrections:

```text
xHCI 7:0.3: version=110 slots=64 ports=6 context=64 scratchpads=2
xHCI 7:0.3: controller prepared, command/event rings=256 TRBs; DMA disabled
xHCI 6:0.0: version=100 slots=32 ports=4 context=64 scratchpads=4
xHCI 6:0.0: controller prepared, command/event rings=256 TRBs; DMA disabled
xHCI 7:0.4: root port 5 USB 3.16 speed-id=5 slot=4 enabled; addressing pending
usb: 7:0.4 port 5 device 0:0 (incomplete): descriptor/control request failed
usb: 7:0.4 port 1.2 device 17ef:3071
usb: 7:0.4 port 1.2.2 device 1ea7:66
usb: 7:0.4 port 1.2.4 device 17ef:306f
xHCI 7:0.4: boot USB enumeration complete
```

Code inspection at the recorded revision explains the unknown-speed record: `protocol_speeds` classifies
USB 3 rates only for the supported 5 Gb/s profile, while `usb_host_address`
returns `USB_UNSUPPORTED` for an unknown speed before issuing Address Device.
The generic detail incorrectly suggests a descriptor transfer failure.
Speed ID 5 is consistent with the Linux-observed 10 Gb/s dock link, but the
retained lines contain no PSI table; the ID alone does not establish a bit rate
when the controller defines explicit mappings. No native controller failure is
reported here.

`USB 3.16` prints the raw minor BCD byte `0x10` as decimal. It represents USB 3.1,
not a USB 3.16 protocol. The final enumeration message means the worker finished
its boot scan; the READ snapshot remains incomplete as `lsusb` reports.

This run provides native evidence for 64-byte contexts, nonzero scratchpads,
high-speed nested hubs and full-speed EP0 transactions behind a high-speed
transaction translator. It does not qualify low-speed descendants, recovery,
hotplug, delayed attachment, every controller/firmware profile, USB storage or
SuperSpeed hub traversal. The checked-in `CONFIG_XHCI=n` remains appropriate
until broader qualification is agreed. See [USB 2 hub bring-up](../../development/usb-hub-bringup.md)
for the separate agent-run QEMU observations.

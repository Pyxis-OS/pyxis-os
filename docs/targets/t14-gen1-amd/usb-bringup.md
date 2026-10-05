# Native USB bring-up observations

## 2026-10-03 inventory snapshot

The owner reported this ThinkPad T14 Gen 1 AMD run on 2026-10-03, from image
revision `d04c6a65a9fe5b318cd4f36f79efaaa56a3c2030`, with the dock attached.
Its pinned userland is `a6e1a4091353d77c1eec25518f4c26335cedc44c`.
This is an owner-supplied `lsusb`, `lspci` and retained boot-log observation,
not an agent-run native measurement or a complete hardware qualification.
The exact build flags and firmware boot method were not supplied; the output
establishes that native xHCI initialization was enabled.

### Observed controllers and devices

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
not establish readable media; native USB block transfers were
unimplemented in that image.

### Retained log evidence and interpretation

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

## 2026-10-04 read-only storage and USB 3 hub follow-up

The owner reported a native PXE boot with `CONFIG_XHCI=y`, dock attached and two
SanDisk sticks, relayed by Claude in [PR #379](https://git.internal/PyxisOS/pyxis-os/pulls/379).
The PR review covers `420b5d226181436e69740a11fe3884dc478c8f10`; the native
image's Git revision and dependency pins were not supplied. This is owner-reported
execution evidence, not an independently repeated agent measurement.

Retained storage lines:

```text
usb-bot: 7:0.3 port 6: read-only probe ready: read-only BOT first-span and final-block probe complete
usb-bot: 7:0.3 port 6: blocks=60088320 block-bytes=512 read-bytes=66048
usb-bot: 7:0.4 port 5.1: read-only probe ready: read-only BOT first-span and final-block probe complete
usb-bot: 7:0.4 port 5.1: blocks=240328704 block-bytes=512 read-bytes=66048
```

| Controller/path | Observed route | Blocks × block bytes | Byte capacity |
| --- | --- | --- | ---: |
| `07:00.3`, `6` | Built-in port, SuperSpeed root, nominal 32 GB stick | 60088320 × 512 | 30765219840 |
| `07:00.4`, `5.1` | Dock USB 3 hub descendant at SuperSpeed, nominal 128 GB stick | 240328704 × 512 | 123048296448 |

Both capacities match the owner's Linux `lsblk -b` results. Each probe reported
completion of the first 65536-byte span and final 512-byte block. Native byte
samples and known-content comparisons were not supplied; this establishes
successful command completion and consistent geometry, not full-media integrity.
The paths are observations of this boot and do not define selection or socket
identity.

The owner reported the dock's root-port-5 hub as Lenovo `17ef:3070`, at
`super-plus` speed. Its SuperSpeed descendant advertised BOT at alternate 0 and
UAS (protocol `62`) at alternate 1; the probe selected BOT. This exercises USB 3
hub traversal and the default BOT alternate, not selection of a nonzero alternate.
The earlier unidentified root-port-5 observation is now this identified hub.
Both AMD controller records were complete. The Renesas camera, fingerprint
reader, Bluetooth, dock high-speed hubs, keyboard and audio still enumerated.
`lsusb` exited 1 because Realtek DASH EHCI `02:00.4` remains unsupported.

This adds native read-only media and USB 3 hub-path evidence to the earlier
inventory snapshot. It does not execute stall, transaction-translator cleanup or
reset recovery; no errors occurred. Full/low-speed storage behind a high-speed
hub, low-speed descendants, hotplug, writes, flushes and broader firmware/controller
profiles remain unqualified. The checked-in `CONFIG_XHCI=n` default is unchanged.
Test built-in ports before dock paths when collecting future recovery evidence.

## 2026-10-05 first native installation (C.4)

The owner installed Pyxis onto the nominal 128 GB SanDisk stick (`0781:55a9`)
from PXE live media, booted the stick alone, and checked persistence across a
synced power-off, as defined in [USB installation](../../devices/usb-installation.md#validation).
This is owner-reported evidence from screen photos, transcribed by Claude, not
an agent-run measurement. The firmware version was not recorded. No dock hub was
enumerated in the photographed boot. The stick was in a built-in port.

### Pre-C.3 live media

A first attempt used live media built from main `a12aa3b`, before
[PR #407](https://git.internal/PyxisOS/pyxis-os/pulls/407) merged, so the
installer inventory was still VirtIO-only. Read the room reported
`No eligible disks. Nothing was written.` without listing the stick. The same
boot qualified the stick for writes:

```text
xHCI 7:0.3: root port 6 USB 3.1 speed-id=4 slot=1 enabled; addressing pending
usb: 7:0.3 port 6 device 781:55a9
usb-bot: 7:0.3 port 6: media probe ready: BOT first-span and final-block probe complete
usb-bot: 7:0.3 port 6: blocks=240328704 block-bytes=512 read-bytes=66048
usb-bot: 7:0.3 port 6: writable=1 flush=1; write protection clear and cache synchronization qualified
block: USB device 1 ready
GPT: device 1 unsupported; primary=2 backup=2 partitions=0
```

Write protection read clear, and the qualifying blocking SYNCHRONIZE CACHE (10)
succeeded, so neither [C.1 compatibility watchpoint](../../technical-debt.md#usb-writable-media-qualification-limits)
was observed. Both GPT copies were absent. The kernel's map classification is
`unsupported` because the stick still carried its factory MBR with a data
partition. The installer's own consent scan does not use that classification.
Other enumerated devices were the fingerprint reader (`06cb:00bd`) and
Bluetooth (`8087:0029`) on `07:00.4`, and the camera (`5986:2130`) on `06:00.0`.

### Installation and target-only boot

Live media from main `4dc804a`, the PR #407 merge, ran Install with Read the
room. The owner reported that the stick was eligible and that installation
finished in about 15 seconds. The installer screen was not photographed. Booted
alone from the stick, the installed system reported:

```text
net0: DHCP 192.168.0.50/24 with default gateway
home://> ping 1.1.1.1
4 attempted, 4 replies, 0 unanswered
round-trip min/avg/max = 11.018/11.682/12.518 ms
home://> fastfetch
Kernel: Caelum 4dc804aab123
CPU: AMD Ryzen 5 PRO 4650U (12)
Memory (allocator): 117.27 MiB / 31.06 GiB (0%)
Disk (system://): npfs - shared pool capacity: 114.10 GiB
home://> ls system://
SAFE_TO_WIPE
```

### Synced power-off persistence

The owner finalized the installation and wrote a file:

```text
rm system://SAFE_TO_WIPE
sync system://
cat app://vi.pxe > system://keep.bin
sync system://keep.bin
sha256sum system://keep.bin
bdcc20ff1d2b1b8c9cb2a374bb8245e442c0b61a13f751afff942d940cdf8846  system://keep.bin
```

After a synced power-off, the next stick-only boot (uptime 12 seconds) again
reported `Caelum 4dc804aab123`. `ls system://` listed only `keep.bin`, and its
SHA-256 was unchanged. Both the marker removal and the new file persisted.

The owner tagged `4dc804a` as `0.0.1`, a signed annotated tag, as the first
native installation.

### Update round trip

Later on 2026-10-05, after SMP task 2a ([PR #411](https://git.internal/PyxisOS/pyxis-os/pulls/411)),
the owner booted live media from that work and ran Update on the stick. The live
media was built from `9b7b932`, the merge of that PR, and the stick then booted
that revision. Update took 14 s and rewrote the ESP's
installed command line from the 0.0.1 `init.primary=app://init-installed` grammar
to `space.pyxis=app://init-installed`. Afterwards, `sha256sum` of `system://keep.bin`
still matched, so the pool was preserved.

### Not covered

- **Power loss during writes,** uncertain I/O and other sticks, ports or the
  dock path.
- **The [configured-mount discovery latency measurement](../../technical-debt.md#configured-mount-discovery-latency),**
  which remains deferred.

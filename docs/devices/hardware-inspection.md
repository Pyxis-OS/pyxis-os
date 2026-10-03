# Native hardware inspection

Native [lspci](../userland/lspci.md) and [lsusb](../userland/lsusb.md) consume
read-only boot observations through the existing
[system_info READ](../interfaces/system-information.md) grant. Listing grants no
configuration access, USB transfers, resets, rescans or storage access. Local
and remote commands that already receive READ can use both tools.

PCI inventory is immutable before tasks start. USB workers inspect each discovered
xHCI controller independently, then publish one immutable snapshot. Other USB
host interfaces remain unsupported controller records. USB covers root-port
devices and [USB 2 hub descendants](usb-hubs.md), with checked IDs, speed and every validated configuration/alternate
interface. Unknown/vendor classes are unbound observations. Unsupported hubs or uninspected descendants make the snapshot partial.

Controller identity comes from discovered PCI functions. Port counts, protocols
and speed identities come from controller capabilities. No vendor, device ID,
BDF, controller count, Linux bus/address or machine port map selects a device.
The ThinkPad inventory is qualification evidence, not a driver contract.

USB initializing/unavailable states have zero counts. After final release/acquire
publication, records remain unchanged even after removal or controller failure.
Resource exhaustion, descriptor/control failure, unsupported hardware or incomplete
PCI discovery retain usable observations and report partial inventory. Device
strings, serial numbers and SuperSpeed hub traversal are deferred. The inventory path does
not configure or select a storage transport. It has no unused BOT matching,
bulk-endpoint setup or bulk-ring reservation.

Userland resolves descriptive labels from pinned plain-text databases:

| Database | Guest path | Recipe |
| --- | --- | --- |
| PCI IDs | `app://share/hwdata/pci.ids` | [pciids](../../ports/pciids/README.md) |
| USB IDs | `app://share/hwdata/usb.ids` | [usbids](../../ports/usbids/README.md) |

The recipes preserve source revision, provenance and the elected BSD database
license. Database updates must not depend on a fixed size, line count or known
vendor entry. Labels never choose a driver or grant authority. Tools support
`-n` and `-i FILE`, escape labels for terminal output and retain numeric IDs when
names are missing. Missing names keep status 0 for complete inventory; partial,
unavailable or initializing observations exit 1.

`CONFIG_XHCI=n` remains the default until physical qualification. Firmware can
still boot the kernel/archive from USB. The
[xHCI profile and retention limits](../technical-debt.md#xhci-hardware-profile-and-runtime-retention)
and [descriptor budgets](../technical-debt.md#usb-descriptor-bounds-and-per-port-preparation)
remain explicit. [Storage selection/media work](../wip/usb-installation.md#b-native-read-only-usb-storage)
is a separate milestone. The
[everyday pipeline target](../development/io-reliability-attribution.md#everyday-pipeline-performance-target)
uses the databases as ordinary text-tool input without imposing a database version.

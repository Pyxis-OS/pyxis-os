# Native lsusb

`lsusb` lists the immutable USB boot snapshot supplied by
[system_info READ](../interfaces/system-information.md#usb-inventory). Local and
remote commands receive this existing observation grant. No new grant or raw USB
access is required.

```sh
lsusb
lsusb -n
lsusb -i host://usb.ids
lsusb | cat
```

Each controller shows its PCI address, IDs, inspection state and advertised physical
root-port count, or `root ports unknown` when that count was not inspected.
Controller presentation follows boot registry order, which is stable within the
boot but otherwise unspecified; no address sorting is guaranteed.
Devices show their physical root/downstream port path, speed and checked VID/PID,
with vendor/product labels from `boot://share/hwdata/usb.ids` when available. Checked
interfaces show their configuration, number, alternate, class/subclass/protocol and
endpoint count. These descriptors do not imply an active configuration or binding.
`super` and `super-plus` distinguish USB 3 link categories without claiming a
numeric rate or lane count. Unidentified connected ports are labeled explicitly. Supported USB 2 and USB 3 hubs expose observed descendants; unsupported or failed branches are partial.

`-n` skips names; `-i FILE` chooses another database. The bounded streaming parser
reads vendor and one-tab product labels, ignoring nested interface and class sections.
Labels escape backslashes and nonprintable/non-ASCII bytes for the terminal. They
are descriptive data, never driver selection policy. See the
[usbids recipe](../../ports/usbids/README.md) for the pinned source and BSD license.

A complete inventory exits 0, including unknown names or a missing/unreadable
name database. A database error discards partial names and prints numeric IDs with
a diagnostic. Partial inventories print retained records and exit 1. Disabled USB,
initializing snapshots, query/authority failures, bad usage and output failures exit 1.

This slice covers root devices and [hub descendants](../devices/usb-hubs.md).
It has no strings or serials, rescan or hotplug. Later removal or failure leaves the boot observation
unchanged. `CONFIG_XHCI=y` is the default; disable it explicitly when a
bring-up image should leave native USB untouched. The [controller profile](../devices/usb-xhci.md) still has limits, and the
ThinkPad native clock prerequisite remains separate.

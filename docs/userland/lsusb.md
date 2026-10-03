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
root-port count. Root devices show their physical port, speed and checked VID/PID,
with vendor/product labels from `app://share/hwdata/usb.ids` when available. Checked
interfaces show their configuration, number, alternate, class/subclass/protocol and
endpoint count. These descriptors do not imply an active configuration or binding.
Unidentified connected ports are labeled explicitly. Hubs have uninspected descendants.

`-n` skips names; `-i FILE` chooses another database. The bounded streaming parser
reads vendor and one-tab product labels, ignoring nested interface and class sections.
Labels escape backslashes and nonprintable/non-ASCII bytes for the terminal. They
are descriptive data, never driver selection policy. See the
[usbids recipe](../../ports/usbids/README.md) for the pinned source and BSD license.

A complete inventory exits 0, including unknown names or a missing/unreadable
name database. A database error discards partial names and prints numeric IDs with
a diagnostic. Partial inventories print retained records and exit 1. Disabled USB,
initializing snapshots, query/authority failures, bad usage and output failures exit 1.

This slice covers direct root-port devices. It has no strings or serials, hub
traversal, rescan or hotplug. Later removal or failure leaves the boot observation
unchanged. `CONFIG_XHCI=n` remains the default; enable it explicitly to qualify
hardware. The [controller profile](../devices/usb-xhci.md) still has limits, and the
ThinkPad native clock prerequisite remains separate.

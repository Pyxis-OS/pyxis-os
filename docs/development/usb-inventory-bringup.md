# USB inventory bring-up

Manual validation on 2026-10-03 used the existing Pyxis GCC 16.2 toolchain and
QEMU 10.2.2 with the upstream AHCI fix. Configuration was q35, nested KVM,
CPU `max`, four vCPUs, 8 GiB RAM, OVMF raw CODE/VARS, ISO boot and VirtIO RNG.
Those values are experiment inputs, not driver requirements. `CONFIG_XHCI=y`
was selected only for enabled cases; the checked-in default remains `n`.

## Matched startup comparison

Baseline kernel revision `1335beee0ad5` used its matching SDK/userland/ports
bundles, verified through ordinary assembly. Updated kernel base `9a445f4` plus
its reviewed task-owner, system_info dispatch and PCI-unavailable changes used
an ordinary full source image. Both used one default `qemu-xhci`, `usb-mouse`
at virtual port 1 and image-backed read-only `usb-storage` at virtual port 2,
with networking absent. The image was only a USB device fixture: no USB block
read implementation or media benchmark was exercised.

Two separate boots of each image gave these identical counters in GDB after
startup and continued shell activity:

| Kernel | Interrupt notifications | Commands completed | Events consumed |
| --- | ---: | ---: | ---: |
| Baseline, both samples | 15 | 5 | 15 |
| Inventory, both samples | 13 | 4 | 13 |

The baseline configured its provisional BOT interface and bulk endpoints. The
inventory path does not, accounting for one fewer command and two fewer events.
Both updated boots published complete snapshots containing the two direct devices
and their two interfaces. No controller failed. These are protocol-work counters,
not elapsed-time, throughput, idle wakeup or laptop power measurements. IRQ counts
for multiple controllers include shared-vector notifications and are not isolated
hardware interrupt counts. Physical performance remains unmeasured.

Representative launch parameters for that matched comparison:

```text
-machine q35 -accel kvm -cpu max
-smp cpus=4,sockets=1,cores=4,threads=1 -m 8G -rtc base=utc
-device qemu-xhci,id=usb
-device usb-mouse,bus=usb.0,port=1
-drive if=none,id=stick,format=raw,readonly=on,file=<USB fixture>
-device usb-storage,bus=usb.0,port=2,drive=stick
```

## Controller and device variation

A two-controller boot placed one controller at root PCI address 08.0 and another
behind a PCIe root port at 09.0. Storage at virtual port 1, keyboard at 3 and mouse
at 4 appeared with their controller's actual physical root-port identities.
GDB before user launch showed INITIALIZING and two pending entries; after the
workers finished, it showed a complete snapshot, two controllers and three
root devices/interfaces. The remote shell ran `lsusb`, `lsusb -n`,
`lsusb -i app://missing.ids` and `lsusb | cat`. All command completion events
reported status 0. Missing data retained numeric IDs with a diagnostic; an unknown
product kept its numeric identity. Packaged names came from the pinned database.
The first attempted serial device used a closed null character backend and was
not attached by QEMU; this was not counted as an observed connected device.

A further boot used four xHCI controllers, including the bridged one and root
functions at 08.0, 0b.0 and 0c.0. The latter two used `p2=1,p3=1` and
`p2=2,p3=1`, advertising two and three ports rather than eight. Added devices:

```text
-device ich9-usb-ehci1,id=legacy,addr=0xa
-device usb-hub,bus=usbA.0,port=1
-device usb-mouse,bus=usbA.0,port=1.1
-chardev null,id=usbserial
-device usb-serial,chardev=usbserial,always-plugged=on,bus=usbA.0,port=2
-device usb-mouse,bus=usbD.0,port=1
```

This boot published five controller records, five direct devices and five checked
interfaces. The empty two-port controller was complete. EHCI was unsupported,
the hub was retained with descendants unavailable, and class ff/ff/ff serial
interface remained a valid unbound observation. The aggregate snapshot was partial.
Named and numeric `lsusb` reported status 1 while the shell stayed usable. The
hub-attached mouse was deliberately absent from the root-only snapshot. No
ThinkPad BDF, PCI ID, controller count or port route was used in these configurations.

A default-disabled image booted with two xHCI controllers. `lspci -n` listed
both functions with status 0, while `lsusb` reported unavailable/status 1. GDB
showed no driver controller contexts, no USB registry entries and UNAVAILABLE.
The shell and PCI observation remained usable.

## Limits

This validates emulated multi-controller discovery, read-only publication and the
native consumer. It does not qualify ThinkPad firmware ownership, Renesas firmware,
nonzero scratchpads, 64-byte contexts, nondefault PSI, physical debounce/link
settling, hotplug, recovery, controller-resource exhaustion or USB storage access.
The native 32-bit HPET work is separate. No new tests, CI or boot automation were
added. All QEMU/debugger sessions were closed after inspection.

# Bluetooth task 1: passthrough and inventory

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

On 2026-10-08, QEMU attached the ThinkPad's Intel AX200 Bluetooth USB function `8087:0029` as the ordinary Fedora user,
and Pyxis main `078c9759b2803e57368201f4ecd3b44b59bb3af9` enumerated it with complete inventory, both interfaces and all
alternate settings; guest configuration bytes establish the endpoint addresses below. This completes
[investigation task 1](../../../devices/ax200-bluetooth.md#qualified-scope). No kernel, ABI, launcher or target-userland
code changed. Interrupt transfers, HCI commands, controller firmware state, firmware loading and LE scanning remain
unmeasured: USB enumeration does not show whether the device runs bootloader or operational firmware after attachment or
reset.

## Host preparation and attachment

The physical T14 Gen 1 AMD ran Fedora with QEMU `10.2.2-1.fc44` and KVM (`systemd-detect-virt` reported `none`), so these
are host KVM checks, not nested-VM measurements. Linux identified the device at AMD xHCI `0000:07:00.4`, root port 4, bus 4
address 3, full speed (12 Mb/s). After the owner ran `sudo systemctl disable --now bluetooth`, `bluetooth.service` was
`inactive`/`dead`/`disabled` and Bluetooth was neither soft nor hard blocked; `/dev/bus/usb/004/003` belonged to the
invoking user (group `root`) and was readable and writable by that user, with both interfaces initially bound to
`btusb`.

The initial attachment check ran paused QEMU (`-machine q35,accel=kvm -m 256M -nodefaults -display none -serial none
-monitor stdio -S`) with `qemu-xhci` and `usb-host,vendorid=0x8087,productid=0x0029`. While paused, `info usb` showed only a
generic 1.5 Mb/s `USB Host Device`, which does not establish attachment; after `cont` it showed `Device 0.1, Port 1,
Speed 12 Mb/s, Product host:4.3, ID: bluetooth`. Both interface driver links in sysfs were absent while QEMU owned the
device, there were no libusb permission or detach errors, and both interfaces rebound to `btusb` after `quit`. No manual
unbind or root QEMU was needed.

## Matching image and guest run

Local artifacts came from an older modified GCC revision and were not used; the `pyxis-image` artifact (ID 6220) from
successful [main CI run 1177](https://git.internal/PyxisOS/pyxis-os/actions/runs/1177) supplied `pyxis.iso` and the matching
`caelum.elf` symbols, with the existing `build` and `filesystem` jobs successful for `078c9759b2`. No target compiler was
built locally; only the host remote client was rebuilt (`make -C tools remote`). Pins: userspace
`47308da28c04c2e71a460bacec5767471479a3dd`, ports `1064c452a0236040c7d673ab51dbea3a5b81ff80`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`, filesystem `b427df29f865bc361b8da92bcd74e114581e9a32`.

The guest used one CPU, 2 GiB, Q35/KVM, `-cpu max`, standard VGA with no display window, the Fedora OVMF pair with fresh
variables, ISO boot, a loopback-forwarded VirtIO NIC, VirtIO RNG, `qemu-xhci` with the same `usb-host` device, and
`-S -gdb tcp:127.0.0.1:1234`. GDB with the matching ELF stopped at `parse_configuration`, conditional on
`device->info.vendor_id == 0x8087 && device->info.product_id == 0x0029`; the recorded `total` was 200, and `dump binary
memory` read `device->owner->descriptors` through `+ total` without calling kernel functions (per-controller scratch, so
captured before continuing). After detaching GDB, the remote client ran `lsusb -n`: it reported controller `0000:00:04.0`
(`1b36:000d`), complete inventory and device `8087:0029` at full speed on root port 5, with `exit_status=0`. QEMU's USB
monitor port 1 and Pyxis's xHCI root port 5 are separate numbering domains, and neither is the physical host port 4.

## Descriptors and endpoints

The guest saw class/subclass/protocol `e0/01/01`, one configuration at full speed. Configuration 1 has 200 bytes, two
interfaces, self-powered attributes `0xc0` and maximum power 100 mA, and every interface alternate uses `e0/01/01`.

| Interface / alternate | Endpoint | Transfer type | Maximum packet bytes | bInterval |
| --- | --- | --- | ---: | ---: |
| 0 / 0 | `0x81` IN | Interrupt (HCI events) | 64 | 1 |
| 0 / 0 | `0x02` OUT | Bulk (ACL) | 64 | 1 |
| 0 / 0 | `0x82` IN | Bulk (ACL) | 64 | 1 |
| 1 / 0 | `0x03` OUT, `0x83` IN | Isochronous (SCO) | 0 | 1 |
| 1 / 1 | `0x03` OUT, `0x83` IN | Isochronous (SCO) | 9 | 1 |
| 1 / 2 | `0x03` OUT, `0x83` IN | Isochronous (SCO) | 17 | 1 |
| 1 / 3 | `0x03` OUT, `0x83` IN | Isochronous (SCO) | 25 | 1 |
| 1 / 4 | `0x03` OUT, `0x83` IN | Isochronous (SCO) | 33 | 1 |
| 1 / 5 | `0x03` OUT, `0x83` IN | Isochronous (SCO) | 49 | 1 |
| 1 / 6 | `0x03` OUT, `0x83` IN | Isochronous (SCO) | 63 | 1 |

A live Fedora descriptor capture after guest cleanup reports the same alternates, endpoint addresses, types, packet sizes
and intervals, USB 2.01 and EP0 maximum packet size 64. The host configuration attributes are `0xe0` (remote wakeup
advertised) while the guest-captured ones are `0xc0`; this difference does not establish guest remote-wakeup behavior.

## Native confirmation and limits

The owner-supplied [native Pyxis inventory](../../../targets/t14-gen1-amd/usb-bringup.md#2026-10-03-inventory-snapshot) at
`d04c6a65a9fe` records the same full-speed device on `07:00.4`, port 4, with wireless interfaces but not individual
interface numbers, alternates or endpoint addresses; the checked-in
[Linux inventory](../../../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt) records interfaces 0 and 1 at that host
path, and the live Fedora capture supplies the detailed descriptors. No new native Pyxis boot was run. This single guest
boot establishes successful boot-time inventory on the qualified host setup; it does not qualify hotplug, repeated resets,
permission restoration after re-enumeration, HCI transport or firmware state. No `USB_HOST` launcher option was added. The
remote session ended normally with complete drain, GDB detached, both QEMU processes exited, Bluetooth stayed inactive and
disabled and `btusb` rebound.

# Bluetooth task 1: passthrough and inventory

On 2026-10-08, QEMU attached the ThinkPad's Intel AX200 Bluetooth USB function
`8087:0029` as the ordinary Fedora user. Pyxis main `078c9759b2803e57368201f4ecd3b44b59bb3af9`
then enumerated it with complete inventory, both interfaces and all alternate
settings. Guest configuration bytes establish the endpoint addresses below.
This completes [investigation task 1](../../../wip/bluetooth.md#steps).

No kernel, ABI, launcher or target-userland code changed. Interrupt transfers,
HCI commands, controller firmware state, firmware loading and LE scanning remain
unmeasured. In particular, USB enumeration does not establish whether the device
is running bootloader or operational firmware after attachment or reset.

## Host preparation and attachment

The physical T14 Gen 1 AMD ran Fedora with QEMU `10.2.2-1.fc44` and KVM;
`systemd-detect-virt` reported `none`. These are host KVM checks, not nested-VM
measurements. Linux identified the device at AMD xHCI `0000:07:00.4`, root port 4,
bus 4 address 3, full speed (12 Mb/s).

The owner's initial stop had not left `bluetooth.service` inactive. After the
owner used `sudo systemctl disable --now bluetooth`, inspection reported
`ActiveState=inactive`, `SubState=dead`, `UnitFileState=disabled`. Bluetooth was
neither soft nor hard blocked. `/dev/bus/usb/004/003` belonged to the invoking
user (group `root`) and was readable and writable by that user. Both interfaces
initially remained bound to `btusb`.

The initial attachment check used:

```sh
qemu-system-x86_64 -machine q35,accel=kvm -m 256M \
  -nodefaults -display none -serial none -monitor stdio -S \
  -device qemu-xhci,id=xhci \
  -device usb-host,id=bluetooth,bus=xhci.0,vendorid=0x8087,productid=0x0029
```

While paused, `info usb` showed a generic `USB Host Device` at 1.5 Mb/s; that
alone did not establish attachment. After `cont`, it showed:

```text
Device 0.1, Port 1, Speed 12 Mb/s, Product host:4.3, ID: bluetooth
```

Both interface driver links in sysfs were absent while QEMU owned the device.
There were no libusb permission or detach errors. After `quit`, both interfaces
were bound to `btusb` again. No manual driver unbind or root QEMU was needed.

## Matching image and guest run

The local build artifacts were from an older modified GCC revision, so they
were not used. The `pyxis-image` artifact (ID 6220) from successful
[main CI run 1177](https://git.internal/PyxisOS/pyxis-os/actions/runs/1177)
provided both `pyxis.iso` and its matching `caelum.elf` symbols. The CLI reported
both existing `build` and `filesystem` jobs successful for `078c9759b2`.

Pinned dependencies were unchanged:

| Repository | Revision |
| --- | --- |
| userspace | `47308da28c04c2e71a460bacec5767471479a3dd` |
| ports | `1064c452a0236040c7d673ab51dbea3a5b81ff80` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |
| filesystem | `b427df29f865bc361b8da92bcd74e114581e9a32` |

Image SHA-256 was
`2735be8241986c02375f73fa031c0182a6b794d1141f432d4eb242a5ffe41448`;
ELF SHA-256 was
`4f710e43042c48085864b4bf06c4045995dba0a5a21a9fdd63f77e724c855a7b`.
No target compiler was built locally; only the current host remote client was
rebuilt with `make -C tools remote`.

The guest used one CPU, 2 GiB RAM, Q35/KVM, standard VGA with no display window,
the raw Fedora OVMF pair and fresh copied variables. The exact invocation was:

```sh
qemu-system-x86_64 -machine q35 -accel kvm -cpu max -rtc base=utc \
  -smp 1 -m 2G \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=/tmp/pyxis-bluetooth-task1/OVMF_VARS.fd \
  -cdrom /tmp/pyxis-bluetooth-task1/image/pyxis.iso -boot d -display none \
  -serial file:/tmp/pyxis-bluetooth-task1/serial.log -monitor stdio \
  -netdev user,id=pyxis_net,hostfwd=tcp:127.0.0.1:2323-10.0.2.15:2323 \
  -device virtio-net-pci,netdev=pyxis_net,disable-legacy=on \
  -object rng-random,id=pyxis_rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=pyxis_rng,disable-legacy=on \
  -device qemu-xhci,id=xhci \
  -device usb-host,id=bluetooth,bus=xhci.0,vendorid=0x8087,productid=0x0029 \
  -S -gdb tcp:127.0.0.1:1234
```

GDB with the matching ELF stopped at `parse_configuration`, conditional on
`device->info.vendor_id == 0x8087 && device->info.product_id == 0x0029`.
The recorded `total` was 200. Inspection and `dump binary memory` read
`device->owner->descriptors` through `device->owner->descriptors + total`;
no kernel functions were called. This is per-controller scratch, so it was
captured before continuing. See the [GDB capture](gdb.txt) and exact
[configuration bytes](guest-configuration.txt).

After detaching GDB, the ordinary remote client ran `lsusb -n`. Its
[output](guest-lsusb.txt) reported controller `0000:00:04.0` (`1b36:000d`),
complete inventory and device `8087:0029` at full speed on root port 5.
The command-complete event reported `exit_status=0`. QEMU's USB monitor port 1
and Pyxis's xHCI root port 5 are separate numbering domains; neither is the
physical host port 4.

## Descriptors and endpoints

Guest device observations were class/subclass/protocol `e0/01/01`, one
configuration and full speed. Configuration 1 contains 200 bytes, two interfaces,
self-powered attributes `0xc0`, and maximum power 100 mA.
Every interface alternate uses class/subclass/protocol `e0/01/01`.

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

The [live Fedora descriptor capture](host-lsusb.txt), taken after guest cleanup,
reports the same interface alternates, endpoint addresses, types, packet sizes
and intervals. Its device descriptor also reports USB 2.01 and EP0 maximum packet
size 64. The host configuration attributes are `0xe0` (remote wakeup advertised),
whereas the captured guest configuration advertises `0xc0`. This observed
descriptor difference does not establish guest remote-wakeup behavior.

## Native confirmation and limits

The existing owner-supplied
[native Pyxis inventory](../../../targets/t14-gen1-amd/usb-bringup.md#2026-10-03-inventory-snapshot)
at `d04c6a65a9fe` records the same full-speed device on `07:00.4`, port 4,
with wireless interfaces. It does not retain individual interface numbers,
alternates or endpoint addresses. The checked-in
[Linux inventory](../../../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt)
explicitly records interfaces 0 and 1 at that host path. Today's live Fedora
capture supplies their detailed descriptors; no new native Pyxis boot was run.

This single guest boot establishes successful boot-time inventory on the
qualified host setup. It does not qualify hotplug, repeated resets, automatic
permission restoration after re-enumeration, HCI transport or firmware state.
No `USB_HOST` launcher option was added. The remote session ended normally with
complete drain; GDB detached and both QEMU processes exited. Bluetooth remained
inactive and disabled, with `btusb` rebound. Task 2 awaits assignment and its
interrupt-transfer ownership decisions.

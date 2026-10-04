# Read-only USB storage bring-up

2026-10-04: manual QEMU validation of the internal per-device
[BOT/SCSI probe](../devices/usb-storage.md), based on `9cbb0d3`. The implementation
commit adding this record identifies the measured source. Submodule pins were
fs `d352c7e`, ports `bf7667c`, userspace `d730e4f`, lwIP `a1aadb9`; none changed.
Native results were subsequently [reported by the owner](../targets/t14-gen1-amd/usb-bringup.md#2026-10-04-read-only-storage-and-usb-3-hub-follow-up).
No tests, fault injection or boot automation were added.

## Build and machine

Ordinary `make -j16 sdk image` built the baseline with `CONFIG_XHCI=y`, followed
by `make -j16 image` for changed inputs. The final enabled/default-disabled image
builds passed. Existing vendored sbase/Doom warnings occurred in rebuilt ports;
changed USB sources compiled without warnings. The existing compiler was used;
no compiler container rebuild was needed.

The guest used QEMU 10.2.2 from `/tmp/pyxis-qemu-ahci-fix/build`, q35, KVM,
`-cpu max`, four vCPUs (one socket/four cores/one thread), 8 GiB RAM, matching
`/usr/share/OVMF/OVMF_CODE.fd` and a private copy of `OVMF_VARS.fd`. This is an
agent-environment KVM measurement, not an owner-host or native ThinkPad result.
The ISO was the changed build's `build/pyxis.iso`; USB backing files were
read-only disposable raw files under `/home/chronium/tmp`.

Common QEMU arguments:

```sh
-machine q35 -accel kvm -cpu max \
-smp cpus=4,sockets=1,cores=4,threads=1 -m 8G -rtc base=utc \
-drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
-drive if=pflash,format=raw,unit=1,file=/home/chronium/tmp/usb-bot-final-vars.fd \
-cdrom build/pyxis.iso -boot d -display none \
-serial file:/home/chronium/tmp/usb-bot-final-serial.log -monitor stdio \
-gdb tcp:127.0.0.1:12876 -no-reboot -no-shutdown \
-netdev user,id=net,hostfwd=tcp:127.0.0.1:24567-10.0.2.15:2323 \
-device virtio-net-pci,netdev=net,disable-legacy=on \
-object rng-random,id=rng,filename=/dev/urandom \
-device virtio-rng-pci,rng=rng,disable-legacy=on
```

## Matched inventory and storage workload

The pre-change baseline and final build used identical devices:

```sh
-device qemu-xhci,id=usb,p2=2,p3=2 \
-device usb-hub,bus=usb.0,port=1,ports=4 \
-device usb-kbd,bus=usb.0,port=1.3 \
-drive if=none,id=disk_a,format=raw,readonly=on,file=/home/chronium/tmp/usb-bot-disk-a.raw \
-device usb-storage,bus=usb.0,port=2,drive=disk_a \
-drive if=none,id=disk_b,format=raw,readonly=on,file=/home/chronium/tmp/usb-bot-disk-b.raw \
-device usb-storage,bus=usb.0,port=1.2,drive=disk_b \
-device qemu-xhci,id=usb_second,p2=1,p3=1 \
-drive if=none,id=disk_c,format=raw,readonly=on,file=/home/chronium/tmp/usb-bot-disk-c.raw \
-device usb-storage,bus=usb_second.0,port=1,drive=disk_c
```

Files A/B/C were 48/80/24 MiB, each with its own filename and first/final-block
marker. These are validation fixture choices, not driver constants. GDB checked
both retained samples against the original markers. All three devices reported
512-byte blocks and completed a 65536-byte first read plus a 512-byte final read.
The full-speed hub descendant and both SuperSpeed roots each had five SCSI
commands, two reads and zero reset recoveries.

Before editing, three manual boots (initial plus two monitor `system_reset`
commands) produced the same inventory and command/event counts. Three equivalent
final boots were inspected after enumeration. `lsusb` retained names, all five
devices and five interfaces; both controller records were complete. Remote
`lsusb` exited 0. Its output was unchanged by the storage consumer.

| Controller | Baseline commands/events, each of 3 samples | Final commands/events, each of 3 samples |
| --- | --- | --- |
| `00:05.0`, one root disk | 2 / 6 | 3 / 23 |
| `00:04.0`, root disk + hub disk + keyboard | 9 / 66 | 11 / 102 |

GDB commands included `p inventory.info`,
`p 'xhci.c'::controllers->commands_completed`, `events_consumed`, the corresponding
`next` controller fields, and each discovery record's `storage` geometry/counters/
byte samples. Counters had zero observed variation in these samples. Additional
work comes from endpoint configuration, device configuration/LUN requests and
real CBW/data/CSW exchanges. Source inspection shows an additional 528 KiB DMA
arena and 64 KiB read scratch per controller, plus private record metadata.
This is a command-work/resource baseline, not a latency, power or throughput
benchmark. The old implementation could not read USB media, so it supplies no
storage-throughput baseline. The guest was not profiled; GDB inspected final
state after the workload.

## Geometry and independent unsupported device

A second manual boot used one xHCI (`p2=3,p3=3`), the same four-port full-speed hub
and keyboard, and these storage attachments:

- A root `usb-storage` disk with `logical_block_size=4096,physical_block_size=4096`,
  backed by a 96 MiB raw file. It reported 24576 blocks and read 65536 + 4096 bytes.
- A hub descendant backed by a sparse 3 TiB + 32 MiB raw file with 512-byte blocks.
  It reported 6442516480 blocks, selected READ CAPACITY (16)/READ (16), and read
  65536 + 512 bytes. The final-block marker proves the read used a large LBA.
- A root `usb-bot,id=multi` with two `scsi-hd` devices on `multi.0`, LUNs 0 and 1,
  backed by the earlier A/C files. It reported `unsupported: BOT multiple LUNs are
  deferred`, with zero SCSI commands/reads. Both supported disks continued.

GDB confirmed the first/final markers for the 4 KiB and large-LBA disks. The
controller inventory remained complete and named `lsusb` exited 0: unsupported
storage is separate from descriptor observation completeness.

The restored `CONFIG_XHCI=n` image booted to the remote shell without claiming
controllers, creating workers or allocating USB DMA. All task-owned QEMU sessions
were closed after inspection.

## Limits

These boots qualify ordinary QEMU reads, descriptor-driven per-device binding,
multiple controllers, hub routing, 512/4096-byte blocks, large-LBA commands and
independent multiple-LUN rejection. They do not execute stall/TT/reset recovery,
nonzero alternate selection, ring wrap, corrupt events or physical removal.
Those paths received source/spec review; no forced-error evidence is claimed.
USB 3 hubs and SuperSpeedPlus remain unexecuted profiles in QEMU. Existing
SET_SEL/SET_ISOCH_DELAY omissions mean this is not full USB 3 conformance.
The owner-reported ThinkPad follow-up observed successful reads from a root disk
and a disk behind the dock's USB 3 hub, with capacities matching Linux. Recovery
was not exercised; native byte-sample verification was not supplied. Broader native qualification and
write/flush/durability behavior remain pending.

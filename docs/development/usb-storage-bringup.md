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

## 2026-10-04 kernel block registration and GPT follow-up

Baseline `b352a1d` and implementation `3cfa63f` used fs `d352c7e`, ports
`bf7667c`, userspace `ad1d53a`, and lwIP `a1aadb9`. No gitlinks changed in this
PR. Ordinary `make -j16 sdk image usb-image USB_IMAGE_MIB=512 USB_ESP_MIB=128`
built the enabled baseline and disposable 512 MiB GPT image. Changed builds
used `make -j16 image` with `CONFIG_XHCI=y`, followed by the restored default
`CONFIG_XHCI=n`. Changed kernel sources compiled without warnings; existing
vendored sbase/Doom warnings occurred when ports rebuilt. The compiler/container
was reused. No tests, fault injection or boot/output automation were added.

The guest used the same QEMU 10.2.2 binary, q35/KVM, four vCPUs, 8 GiB RAM,
OVMF files, network, RNG and common arguments recorded above. These are local
agent-environment measurements, not owner-host or native performance results.
The baseline was rebuilt in an isolated checkout for its later samples, using
verified SDK/userland/ports bundles from the same pinned sources. Kernel source
builds used their own revision; no changed kernel was reused as a baseline.

### Matched fresh-process boots

The first baseline boot preceded implementation. Three baseline and three
implementation samples each used a fresh QEMU process and private OVMF variable
file. Monitor resets that produced no new boot logs were excluded. Storage and
controller arguments were identical:

```sh
-device qemu-xhci,id=usb,p2=2,p3=2 \
-device usb-hub,bus=usb.0,port=1,ports=4 \
-device usb-kbd,bus=usb.0,port=1.3 \
-drive if=none,id=usb_disk,format=raw,readonly=on,file=build/pyxis-usb.img \
-device usb-storage,bus=usb.0,port=1.2,drive=usb_disk \
-drive if=none,id=virtio_disk,format=raw,readonly=on,file=/home/chronium/tmp/usb-bot-disk-a.raw \
-device virtio-blk-pci,drive=virtio_disk,disable-legacy=on \
-device ich9-usb-ehci1,id=ehci
```

The baseline had one block device (VirtIO) and only the private USB media probe.
The implementation retained two READY block devices. USB device 2 published
`GPT_HEALTHY`, matching `sgdisk -p`: disk GUID
`254CA48F-2CF8-4E45-A844-CA22C685D8AB`, entries 1/2 at blocks 2048/264192,
with 262144/782336 blocks respectively. These media identities and sizes are
fixture observations, not selection policy or driver constants.

USB descriptor inventory remained partial solely because EHCI was unsupported;
its xHCI record, hub, storage and keyboard were complete. Named remote `lsusb`
exited 1 in both builds and retained the same observations. Despite aggregate
partial USB inventory, the USB GPT reads and the VirtIO disk's three GPT reads
completed. VirtIO's marker-only image correctly published `GPT_ABSENT`.

Debugger commands included:

```text
p 'xhci.c'::controllers->commands_completed
p 'xhci.c'::controllers->events_consumed
p 'blk.c'::devices[0].published
p 'blk.c'::devices[0].completed
p 'kernel/storage/block.c'::device_count
p 'kernel/storage/block.c'::devices[1].usb->slots
p 'gpt.c'::devices[1].snapshot
```

| Observation after scans | Baseline samples | Implementation samples |
| --- | --- | --- |
| xHCI commands | 8, 8, 8 | 8, 8, 8 |
| xHCI consumed events | 79, 81, 80 | 94, 93, 94 |
| VirtIO published/completed reads | 3/3 in each | 3/3 in each |
| USB SCSI commands / reads | 5 / 2 | 10 / 7 |
| USB bytes read including probe | 66048 | 100352 |

Event variation was two events in the baseline and one in the implementation;
these counts include asynchronous port events. The additional successful USB
work is five GPT reads (34304 bytes), each with a CBW/data/CSW exchange. The last
collected slot was FREE, generation 5, with successful 16384-byte completion and
`submitted=true`; no caller destination remained in worker state.

Boot-ready resource counters were stable across each set of samples:

| Resource | Baseline | Implementation |
| --- | --- | --- |
| PMM allocated frames | 4674 | 5208 |
| VM backed pages / range records | 4072 / 88 | 4605 / 93 |
| TLSF pools / reserved bytes | 2 / 524288 | 6 / 2691072 |

This records work and reservation cost, not latency or throughput. The old
backend supplied no public-ticket USB throughput baseline. Source inspection
accounts for an additional 512 KiB captured-buffer budget per prepared controller,
candidate/snapshot metadata covering discovered root/descendant capacity, and
one approximately 132 KiB shared USB GPT scratch buffer. Scratch is freed after
initial scans; retained snapshot/slot storage and heap backing remain. Larger
controller trees increase metadata reservations; those bounds are independent
of the ThinkPad topology and image defaults.

### Geometry, independent candidates and controllers

A separate fresh-process boot used two xHCI controllers, the same full-speed hub
and keyboard, one VirtIO disk, and unsupported EHCI. The first xHCI used
`p2=3,p3=3` with these USB attachments:

- Port 2: a 512 MiB GPT metadata fixture exposed with
  `logical_block_size=4096,physical_block_size=4096`. Its metadata used 4096-byte
  header/array locations and logical extents with recomputed CRCs; it copied no
  filesystem payload. Device 3 reported 131072 blocks and `GPT_HEALTHY`, with
  entries at 256/33024 and lengths 32768/97792 blocks.
- Port 3: `usb-bot,id=multi` with two `scsi-hd` devices at target 0, LUNs 0 and 1.
  Device 4 retained `BLOCK_DEVICE_UNSUPPORTED` and `GPT_UNAVAILABLE`; it did not
  prevent any supported disk from completing.
- Hub port 1.2: the sparse 3 TiB + 32 MiB marker-only image from the earlier
  bring-up. Device 5 reported 6442516480 blocks and `GPT_ABSENT`. Its final block
  ticket completed a 512-byte READ (16) at LBA 6442516479, with `submitted=true`.

The second xHCI used `p2=1,p3=1` and a root attachment of the original 512-byte
GPT image. Device 2 independently published `GPT_HEALTHY`. The 512-byte and
4 KiB GPT fixtures intentionally retained the same disk GUID; internal discovery
published both physical devices without deduplicating them or granting mounts.
VirtIO device 1 again published `GPT_ABSENT`. Named `lsusb` retained six devices
and exited 1 due to EHCI. No controller, root/child path, vendor or sampled
capacity was used to select a block backend.

### Limits and next slice

Ordinary GPT consumers exercised submit, wait and successful exact-byte
collection through both backends, including hub routing and large LBAs. Slot
exhaustion, active abandonment, timeout, stall/TT/reset recovery and physical
removal received source review only; no forced-error execution is claimed.
No public USB disk grants, USB filesystem mounts, executable launch, writes or
flushes were added. Existing VirtIO authority uses its own complete inventory
view, so partial/pending USB discovery does not disable that domain. The next
B.5 slice must deliberately integrate USB mount authority and validate reads and
executable launch; it must preserve explicit IDs and ambiguity handling.
The restored default-disabled image also reached the remote shell in the same
attachment configuration. It retained only the VirtIO block device and its
`GPT_ABSENT` snapshot; GDB confirmed the xHCI controller list was NULL. `lsusb`
reported unavailable inventory and exited 1. All task-owned QEMU/debugger jobs
were closed, and both validation checkouts had their default configuration
restored.

ThinkPad B.5 testing remains deferred. The earlier owner-reported B.4 results
are not qualification of this new block/GPT integration.

## 2026-10-04: configured read-only USB mounts

Baseline `4b5e256` was captured before edits; implementation `27e6367` includes
both configured USB mount authority and the accepted runtime READ-rejection
follow-up from [PR #384](https://git.internal/PyxisOS/pyxis-os/pulls/384).
Submodule pins were fs `d352c7e`, ports `bf7667c`, userspace `ad1d53a`, and lwIP
`a1aadb9`; none changed. Ordinary enabled and default-disabled `make -j16 image`
builds passed using the existing compiler. No tests, fault injection, CI changes
or boot automation were added.

### Matched discovery observations

Both revisions used `CONFIG_XHCI=y`, packaged init defaults and no `MOUNT_DISK`.
Three fresh QEMU processes per revision used private fresh OVMF variables, the
common QEMU arguments above, and these identical attachments:

```sh
-device qemu-xhci,id=usb,p2=2,p3=2 \
-device usb-hub,bus=usb.0,port=1,ports=4 \
-device usb-kbd,bus=usb.0,port=1.3 \
-drive if=none,id=usb_disk,format=raw,readonly=on,file=build/pyxis-usb.img \
-device usb-storage,bus=usb.0,port=1.2,drive=usb_disk \
-drive if=none,id=virtio_disk,format=raw,readonly=on,file=/home/chronium/tmp/usb-bot-disk-a.raw \
-device virtio-blk-pci,drive=virtio_disk,disable-legacy=on \
-device ich9-usb-ehci1,id=ehci
```

The saved baseline/final ISO and ELF pairs supplied each revision's unchanged
inputs. This is the same four-vCPU, 8 GiB q35/KVM agent environment described
above, not a native ThinkPad measurement. Boot-ready serial resource counters
and post-discovery debugger counters gave:

| Observation | Baseline samples | Implementation samples |
| --- | --- | --- |
| xHCI commands | 8, 8, 8 | 8, 8, 8 |
| xHCI consumed events | 94, 95, 95 | 95, 89, 95 |
| VirtIO published/completed reads | 3/3 each | 3/3 each |
| USB GPT status | HEALTHY each | HEALTHY each |
| PMM allocated frames | 5208 each | 5208 each |
| VM backed pages / range records | 4605 / 93 each | 4605 / 93 each |
| TLSF pools / reserved bytes | 6 / 2691072 each | 6 / 2691072 each |

Command/read counts and reserved resources did not vary. Event counts varied by
one before and six after, including asynchronous port activity. These are work
and reservation observations, not latency or throughput measurements. The old
revision withheld USB mount authority, so it supplies no working USB filesystem
throughput baseline. Remote named `lsusb` retained the hub, storage and keyboard
observations and exited 1 solely because EHCI was unsupported in both revisions.

### USB files, delegation and executable capture

The existing 512-byte image retained the observed disk GUID
`254CA48F-2CF8-4E45-A844-CA22C685D8AB`, partition 2 at block 264192 with 782336
blocks, and the populated `usb-test` volume. It was never regenerated or changed
while attached. The GUID, sizes, names and paths here are fixture observations,
not kernel selection constants. A temporary trusted init contained:

```sh
#!app://shell.pxe
title --optional "USB read-only"
mount --partition 2 --volume usb-test --read-only usb://
namespace create
service start text app://textfs.pxe
session app://session.pxe --configure-network --start-remote-services
```

The ISO build selected that script without rebuilding the USB image:

```sh
make -j16 image INIT=/home/chronium/tmp/init-usb-readonly.sh INIT_CPUS= \
  MOUNT_DISK=254ca48f-2cf8-4e45-a844-ca22c685d8ab
```

With the same hub/EHCI/marker-only VirtIO attachments, ordinary remote commands
used the existing client:

```sh
build/tools/pyxis-remote --machine --no-shell-echo --columns 100 --rows 35 \
  127.0.0.1 24567
```

`ls usb://` listed `bin/`, `README.txt` and `SAFE_TO_WIPE`. Both
`cat usb://README.txt` and `usb://bin/cat.pxe usb://README.txt` displayed the sample
text and exited 0. The latter captured and launched the native executable from
the USB-backed file grant. `fastfetch` exited 0 and reported npfs read-only mode
with the observed 381.99 MiB shared-pool capacity. The unrelated VirtIO GPT was
ABSENT; partial EHCI inventory did not veto the observed unique USB GUID.

`cat app://init > usb://new.txt` was denied during redirection, before launch.
`rm usb://SAFE_TO_WIPE` and `sync usb://` were denied and exited 1; the final
listing retained the original entries. Initial attempts using unpackaged `echo`
and `printf` did not exercise redirection and were replaced by the packaged
`cat` command. GDB observed pool device 2, the expected partition extent,
`store->writable=false`, BOT READY and free captured slots. After file and
executable reads, retained BOT counters were 113 reads, 542720 read bytes and
116 commands; no USB write or flush path was supplied.

### Geometry and failure policy

A separate populated 4 KiB image reused the earlier 4 KiB GPT metadata fixture.
A host-side copy read each image's partition-2 extent and copied the sample pool
into the destination extent; both had the same byte offset and capacity. The
original images were preserved and no attached image was edited. QEMU used a
root attachment with `p2=1,p3=1` and
`logical_block_size=4096,physical_block_size=4096`, alongside unsupported EHCI.
The same configured-GUID ISO mounted device 1: 131072 logical blocks, partition
2 at 33024 with 97792 blocks, max transfer 65536 bytes, writable/flush false.
Both file reads and captured executable launch exited 0; `fastfetch` again
reported read-only mode. This exercises filesystem reads with discovered device
geometry and a different attachment shape.

Two fresh-process boots used `-S` and a GDB hardware breakpoint at `complete_job`
for `job->operation == NPFS_ROOT`, without modifying guest state:

- A trusted init requesting `--read-write` on the root-attached USB disk returned
  CALL_READ_ONLY, with no retained pool and BOT still READY.
- The read-only init with the same GUID observed through both USB and VirtIO
  returned CALL_IO. Both GPT snapshots were HEALTHY; no pool opened and no device
  was selected. All backing attachments were read-only.

The accepted clean READ rejection followed by valid REQUEST SENSE now fails
only that ticket with BLOCK_IO_ERROR and retains READY. Genuine transport or
protocol errors, failed/malformed sense, failed recovery and timeouts retain the
existing sticky device/controller failure behavior; probe-time READ rejection
remains a setup failure. These error branches received source review only;
no natural rejected READ occurred in these QEMU boots. Missing-match complete
versus partial status, scan deadlines, selected-device failure propagation and
installer raw-USB exclusion were also source-reviewed, without forced failures.

### VirtIO preservation and default configuration

A fresh boot used a private writable copy of the sample image through VirtIO,
an unrelated root-attached marker-only USB disk, and unsupported EHCI. Trusted
init selected the same observed GUID and mounted `data://` read-write. File
reads and captured executable launch exited 0. Copying `app://init` into
`data://copied-init`, file sync, matching SHA-256 hashes, removal and directory
sync all succeeded. GDB observed the selected VirtIO pool writable on device 1,
with HEALTHY VirtIO GPT and ABSENT USB GPT. No USB write was attempted.

The checkout restored `CONFIG_XHCI=n` and packaged init/default image settings.
The default-disabled image reached the remote shell with the matched discovery
attachments. GDB confirmed no xHCI controllers and only the VirtIO block device;
`lsusb` reported unavailable inventory and exited 1. All task-owned QEMU and
GDB processes were closed. USB writes/flush, public raw USB access, hotplug and
physical filesystem qualification remain deferred. ThinkPad mount testing is
still deferred; earlier owner-reported BOT reads do not qualify this integration.

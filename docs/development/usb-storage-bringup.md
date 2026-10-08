# USB storage bring-up

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
read-only disposable raw files under `~/tmp`.

Common QEMU arguments:

```sh
-machine q35 -accel kvm -cpu max \
-smp cpus=4,sockets=1,cores=4,threads=1 -m 8G -rtc base=utc \
-drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
-drive if=pflash,format=raw,unit=1,file=$HOME/tmp/usb-bot-final-vars.fd \
-cdrom build/pyxis.iso -boot d -display none \
-serial file:$HOME/tmp/usb-bot-final-serial.log -monitor stdio \
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
-drive if=none,id=disk_a,format=raw,readonly=on,file=$HOME/tmp/usb-bot-disk-a.raw \
-device usb-storage,bus=usb.0,port=2,drive=disk_a \
-drive if=none,id=disk_b,format=raw,readonly=on,file=$HOME/tmp/usb-bot-disk-b.raw \
-device usb-storage,bus=usb.0,port=1.2,drive=disk_b \
-device qemu-xhci,id=usb_second,p2=1,p3=1 \
-drive if=none,id=disk_c,format=raw,readonly=on,file=$HOME/tmp/usb-bot-disk-c.raw \
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
-drive if=none,id=virtio_disk,format=raw,readonly=on,file=$HOME/tmp/usb-bot-disk-a.raw \
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
-drive if=none,id=virtio_disk,format=raw,readonly=on,file=$HOME/tmp/usb-bot-disk-a.raw \
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
make -j16 image INIT=$HOME/tmp/init-usb-readonly.sh INIT_CPUS= \
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

## 2026-10-04: qualified writes and cache synchronization

Phase C.1 adds captured WRITE (10)/(16), ordered blocking whole-medium
SYNCHRONIZE CACHE (10), protection/cache qualification and explicitly requested
writable GUID mounts. The measured baseline was `68729a3`; final kernel sources
were `87e3a00`. Pins were fs `d352c7e`, ports `2c1448a`, userspace `2b23085`,
lwIP `a1aadb9`, unchanged by this task. The checked-in xHCI default remains off.
The [storage reference](../devices/usb-storage.md) describes the implemented
ownership and qualification contracts.

### Builds and common configuration

Ordinary `make -j16 image` passed with xHCI enabled and disabled. Enabled variants
used the trusted init shown in the preceding mount record, replacing
`--read-only` with `--read-write` for writable runs. `MOUNT_DISK` selected the
sample image's observed GUID, `254ca48f-2cf8-4e45-a844-ca22c685d8ab`, and
`INIT_CPUS=` selected one session. `make -j16 fs-tools` and `make -C tools remote`
passed. Changed kernel sources compiled without warnings; rebuilt vendored Quake
sources emitted existing warnings. The existing cross compiler was used, with
no compiler-container rebuild or dependency update.

All runs used the preceding QEMU 10.2.2 q35/KVM configuration: `-cpu max`, four
vCPUs, 8 GiB RAM, matching OVMF code and fresh private vars, ISO boot, virtio-net
with the existing remote client and virtio-rng. These are nested/agent-environment
results, not native ThinkPad measurements. Serial logs, remote command results
and GDB observations were captured manually under `~/tmp`.
No tests, self-tests, fault injection or boot/output automation were added.

### Matched read-only baseline

Three fresh baseline boots and three fresh final boots used identical attachments:
`qemu-xhci,p2=2,p3=2`, a four-port USB 2 hub at root port 1, the original sample
disk at `1.2`, keyboard at `1.3`, an unrelated marker-only VirtIO disk and
unsupported EHCI. Both disk attachments were read-only. The command workload
was `lsusb`, `cat usb://README.txt`, captured `usb://bin/cat.pxe usb://README.txt`
and `fastfetch`. File/executable/report commands exited 0; `lsusb` exited 1 for
the retained unsupported EHCI record. GDB collected counters after commands,
without a profiler. Each metric was identical across its three boots:

| Observation | Baseline | Final |
| --- | ---: | ---: |
| xHCI commands / events | 8 / 187 | 8 / 190 |
| BOT commands / reads | 41 / 38 | 42 / 38 |
| BOT read bytes | 235520 | 235520 |
| BOT writes / flushes | 0 / 0 | 0 / 0 |
| VirtIO published / completed | 3 / 3 | 3 / 3 |
| PMM allocated frames | 5218 | 5218 |
| VM reserved pages | 15902 | 15904 |
| VM backed pages / range records | 4605 / 93 | 4605 / 93 |
| TLSF pools / pool bytes | 6 / 2691072 | 6 / 2691072 |

Final protection was known and set, with BOT READY and writable/flush false.
The one new MODE SENSE command accounts for three additional transfer events.
Backing and pool costs did not increase; two more virtual pages were reserved.
This is a workload/resource comparison, not a boot-latency measurement.

### Writable 512-byte hub disk

A sparse private copy, `usb-write-pool-512.raw`, replaced only the USB backing
attachment, with `cache=writeback` and no `readonly=on`. The original sample
image remained untouched. Discovered geometry was 1048576 logical blocks of
512 bytes; GPT entry 2 began at 264192 and contained 782336 blocks. These are
fixture observations, not driver limits or topology assumptions. MODE SENSE
reported WP clear and blocking synchronization succeeded. GDB observed a writable
npfs pool, BOT READY, writable/flush true and `write_failed=false`.

Ordinary shell operations copied `app://init` to `usb://saved-init`, synced it,
compared SHA-256 hashes, wrote and verified the existing 1 MiB iobench fixture,
renamed the init copy, removed a benchmark file and synced the directory. The
same file contents, rename and removal survived a fresh QEMU process using the
same private disk. Captured USB executable launch also succeeded after reboot.
The final mutation observation recorded 555 successful writes, 10547200 written
bytes and 249 successful synchronizations, including the boot qualification
command. The npfs journal was EMPTY, with no retained writeback error.

For the reported timing run, the existing unprofiled command was:

```text
iobench write usb://bench-measured.bin --buffer 65536 --rounds 3 --sync
sha256sum app://share/iobench.bin usb://bench-measured.bin
```

After one warmup, each verified 1 MiB sample used 258 native file-write calls
(257 short writes under the existing native file-call bound):

| Sample | Transfer ms | File sync ms |
| --- | ---: | ---: |
| 1 | 274.884 | 96.425 |
| 2 | 272.911 | 94.590 |
| 3 | 279.981 | 95.020 |

Transfer median was 274.884 ms, range 272.911–279.981 ms, 3.638 MiB/s at the
median. Sync median was 95.020 ms, range 94.590–96.425 ms. No GDB stop or kernel
profiling occurred during these samples. An earlier debugger-perturbed timing
run was discarded; repeating its existing filename correctly failed exclusive
creation, so the measured run used a fresh name. There was no working USB write
baseline before C.1; these figures establish initial QEMU behavior, not a
throughput regression comparison or physical flush latency.

### 4 KiB and wide-LBA disks

A separate populated private image, `usb-write-pool-4k.raw`, attached directly
to root port 1 with `qemu-xhci,p2=1,p3=1` and
`usb-storage,logical_block_size=4096,physical_block_size=4096`. No VirtIO disk
was required. Discovered geometry was 131072 logical blocks, with entry 2 at
33024 for 97792 blocks. Init copying, file sync, fixture writes and matching
hashes succeeded. GDB observed writable/flush true, `write_failed=false`, BOT
READY, 264 writes / 5201920 bytes and 117 synchronizations including qualification.

The same unprofiled iobench command, using `usb://bench-4k.bin`, reported three
verified samples after warmup: transfer 273.020, 262.777 and 274.874 ms; sync
71.216, 71.900 and 94.448 ms. Transfer median was 273.020 ms, range
262.777–274.874 ms, 3.663 MiB/s; sync median 71.900 ms, range 71.216–94.448 ms.
A fresh process with a read-only attachment and read-only init reopened the
files. The benchmark hash matched the archive fixture; a write redirection
was refused by the delegated read-only directory grant. The persisted init copy
matched the earlier writable init, whose contents differ from the reboot's
read-only init.

A third sparse private image, `usb-write-pool-wide.raw`, had 6442516480 logical
512-byte blocks and only GPT entry 2, starting at 4294969344 for 782336 blocks.
The existing populated pool was copied into that exact-size extent; no large
capacity allocation or new filesystem format was needed. An initial larger
extent was corrected to match the copied pool's declared size before the usable
mount run. Attachment was direct root port 1, without a VirtIO disk. GDB
observed `wide=true` and a natural `usb_bot_write` at LBA 4294969456 for eight
blocks, selecting WRITE (16) above the 32-bit boundary. File copying, sync,
matching hashes and captured executable launch exited 0. A fresh read-only
process reopened the file with its expected hash. Whole-medium SYNCHRONIZE
CACHE (10) succeeded on this wide medium too.

After closing each QEMU process, the pool extent was extracted using its actual
GPT entry. Existing `fsck.npfs` reported a successful structural check for all
three images, without replay. Existing `npfs-inspect` extracted the persisted
files, and host `cmp` matched the writable init and benchmark fixture bytes.
These checks establish orderly QEMU persistence, not power-loss durability.

### Refusal, coexistence and default configuration

A trusted writable init against the original read-only USB attachment returned
`CALL_READ_ONLY` at its natural root operation. GDB observed known WP set,
writable/flush false, BOT READY, no writes and no synchronizations. This refusal
did not poison transport. The read-only filesystem remains usable under the
matched workload above.

With a private writable VirtIO copy selected by the same configured GUID and an
unrelated read-only USB marker disk, copying init, file sync, matching hashes,
removal and directory sync all exited 0. The retained pool used device 1,
VirtIO's eight request slots, writable/flush true and an EMPTY journal. The
existing installer/public raw view was not changed.

The checkout restored `CONFIG_XHCI=n`, packaged init and default image settings.
The default image reached the remote shell with the matched discovery
attachments. `fastfetch` exited 0; `lsusb` reported unavailable inventory and
exited 1. GDB confirmed no xHCI controller records and only the VirtIO block
device. All task-owned QEMU and GDB processes were closed.

### Limits

Mutation failure/abandonment, queued mutation rejection after failure, concurrent
flush fences, MODE SENSE (10) fallback, unsupported synchronization and malformed
capability responses were inspected in code/spec review, without forced-error
execution. Normal filesystem write/sync traffic exercised WRITE (10)/(16) and
blocking flush; no deliberate disconnect, stall or power-loss experiment was
performed. Hotplug and physical writable mounts remain deferred. Native ThinkPad
testing is still deferred, and the accepted GUID/discovery limits are unchanged.

## Persistent USB development loop (C.2)

Manual qualification on 2026-10-04 used main `91bf91d`, containing merged C.1
PR #395. Pins were fs `d352c7e`, ports `55b6f8e`, userspace `61cad34`, lwIP
`a1aadb9`. None changed in this task. Kilo, TCC, native filesystem and USB runtime
sources were unchanged: C.2 adds the
[development walkthrough](edit-build-run.md#persistent-usb-development),
qualification record and milestone completion. There is no performance change
or timing comparison claimed.

### Build and disk selection

Ordinary `make -j16 image` built writable and read-only trusted-init variants
with `CONFIG_XHCI=y`, `INIT_CPUS=` and the actual configured disk GUID. The
checkout subsequently restored `CONFIG_XHCI=n` and packaged init/default image
settings; that ordinary image build also passed. `make -j16 fs-tools` and
`make -C tools remote` passed. Existing vendored Quake warnings occurred in the
full source build, with no kernel warnings. The existing cross compiler was
used; no compiler container, public ABI, dependency pin or build workflow changed.

The initial disk was a sparse private copy of the original sample image,
`~/tmp/usb-c2-development.raw`. Its observed GUID was
`254ca48f-2cf8-4e45-a844-ca22c685d8ab`, capacity 1048576 blocks of 512 bytes.
Entry 2 began at 264192 with 782336 blocks and contained volume `usb-test`.
This is a chosen fixture, not an image-size or topology contract. Trusted init
used the walkthrough's script with `--read-write usb://`, then handed off to
the existing network/session services. The ISO booted independently of the
disk's EFI partition. No image builder or formatter ran against that private
disk after work began, and the original sample image's full hash remained intact.

QEMU 10.2.2 from `/tmp/pyxis-qemu-ahci-fix/build` used q35/KVM, `-cpu max`, four
vCPUs (one socket/four cores/one thread), 8 GiB RAM, UTC RTC, matching
`/usr/share/OVMF/OVMF_CODE.fd` and a fresh private variables file for each
process. VirtIO net and rng were enabled, with host loopback 24567 forwarded to
guest 2323. No VirtIO disk or host filesystem export was attached. These are
agent/nested-KVM observations, not native ThinkPad results.

The first process used `qemu-xhci,p2=2,p3=2`, a four-port USB 2 hub on bus port
1, the selected disk on `1.2`, a keyboard on `1.3`, and unsupported EHCI. The
native inventory reported the disk at discovered path `3.2`; `lsusb` retained
EHCI and exited 1 for partial inventory. The usable observed GUID still mounted.
The fresh processes used `qemu-xhci,p2=1,p3=1` and direct root attachment at bus
port 1, with EHCI retained. Writable disk attachments used `cache=writeback`;
the final media-protected process used `readonly=on` instead. Serial logs,
remote terminal events and debugger observations were captured manually under
`~/tmp`; no tests, fault injection or boot/output automation were added.

### Editor, compiler and checkpoint

The existing machine client was kept open for interactive Kilo input, with
100 columns and 35 rows. Foreground input went to Kilo until it quit; command
completion events were checked before the next stage. The guest created
`usb://work`, changed into it and entered the walkthrough's C program in Kilo.
Ctrl-S saved it and Ctrl-Q quit. `tcc hello.c -o hello.pxe` exited 0, and
`./hello.pxe` printed `Hello from USB, first build` and exited 0.

Kilo reopened the same source. Its search selected `first`, which was replaced
with `second`; save and quit succeeded. The second build used:

```text
tcc -c hello.c -o hello.o
tcc hello.o -o hello.pxe
./hello.pxe
sync usb://work/hello.c usb://work/hello.o usb://work/hello.pxe usb://work usb://
sha256sum hello.c hello.o hello.pxe
```

All exited 0; the executable printed the second message. GDB then observed
BOT READY, writable/flush true, `write_failed=false`, 92 writes / 548864 bytes
and 53 synchronizations including boot qualification. The journal was EMPTY
(sequence 27), with no writeback error. QEMU was closed through its monitor
after successful synchronization; it was not suspended as the persistence check.
Sync's contract is durable COMMITTED, while background checkpointing makes
the journal EMPTY. The observed EMPTY state permitted later read-only opening;
sync success alone is not a guarantee that an immediate read-only open will
succeed. The walkthrough includes writable recovery before a read-only retry
when the existing replay-required diagnostic occurs.

### Fresh-process persistence and read-only use

A fresh writable process used the same disk with the direct root attachment.
Reading source, hashing the three saved artifacts and launching the saved
executable all exited 0. Hashes matched the prior process. TCC compiled the
persisted source into a new `rebuilt.pxe`, which also printed the second message.
Directory sync succeeded; GDB observed an EMPTY journal (sequence 31) and no
writeback error. The direct-source build and earlier object-link build have
different executable hashes; byte-identical compiler output is not the
persistence contract.

The next fresh process used a read-only trusted mount with the disk still
writable, separating grant attenuation from media protection. GDB observed
device writable/flush true but the mounted pool writable false. Reads, hashes
and saved executable launch passed. Creating a directory, removing or renaming
the source, and TCC output to the existing USB executable each failed with
permission denial and status 1. TCC could still read USB source, compile into
RAM-backed `home://` and run that output successfully. Kilo displayed
`Can't save! I/O error: Permission denied` for a buffer edit. The edit was
discarded with its repeated quit confirmation; editor exit 0 did not mean a
successful save. Saved file hashes remained unchanged. BOT recorded zero writes
and only the one boot qualification synchronization.

A final fresh process used both read-only mount and `readonly=on` attachment.
Source/hash reads, saved executable launch and compilation into `home://` all
passed; output to `usb://work/blocked.pxe` failed with permission denial.
GDB observed writable/flush false, zero writes/flushes and a read-only pool.
Both read-only processes left the whole private disk's hash unchanged.

### Detached host inspection and limits

After closing QEMU, the actual GPT pool extent was extracted. Existing
`fsck.npfs` reported `structural check passed` without replay; `npfs-inspect`
listed and extracted the retained source, object and two executables. The source
matched the authored second program byte-for-byte, including its final blank
line. Host hashes matched those captured in the guest:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `work/hello.c` | 92 | `147aa8ff44324428526d2b1f75a28d94ce2f2a3ac55f142dd1ab1d8e36c5962f` |
| `work/hello.o` | 1337 | `137d32462551b476e846d0e397a9ad8f21dcf7424da63258f4a593ca73a0cb40` |
| `work/hello.pxe` | 51765 | `a119417a857eb2506e32e795472372e6c97a537c9cf918ba93ab130bf1fcd02a` |
| `work/rebuilt.pxe` | 51765 | `be6cef5c496042c3a36d054d301789a2554f888ffe71c898e51a7880b48985a0` |

This qualifies the manual QEMU development loop and orderly synchronized
persistence. It adds no atomic editor/compiler save, hotplug, raw USB installer
access, physical write qualification or power-loss claim. The two merged C.1
[hardware compatibility watchpoints](../technical-debt.md#usb-writable-media-qualification-limits)
are retained for evidence-guided physical follow-up. C.4 and ThinkPad testing
remain deferred. All task-owned QEMU, remote client and debugger processes were
closed, and the checked-in configuration remains unchanged.

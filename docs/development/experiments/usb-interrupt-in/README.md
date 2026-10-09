# Root full-speed interrupt-IN qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

On 2026-10-08, Bluetooth investigation task 3a implemented the private
[interrupt-IN interface](../../../devices/usb-interrupt-in.md), following the
owner's decisions on [#517](https://git.internal/PyxisOS/pyxis-os/pulls/517).
The kernel change contains endpoint configuration, retained receive/ring state,
copied FIFO collection, progress at every event-drain point and terminal failure
accounting. Bluetooth binding and commands remain in a separate unmerged
consumer used for qualification.

## Revisions and configuration

Baseline: main `caa337fe19294261bdce1b8ab4a0e0e05b938eba`, from successful
[CI run 1200](https://git.internal/PyxisOS/pyxis-os/actions/runs/1200).
Kernel implementation: `a9ce718fb3e6e7f2157dec0024893c40e52b3218`.
The standalone
[consumer commit](https://git.internal/PyxisOS/pyxis-os/commit/e84a327c2860eb347571d1d3cf1323250ba8c7d7)
is on `probe/bluetooth-interrupt-consumer`; it is not part of the kernel PR and
must not be merged. It sends HCI Reset and repeated Intel Read Version requests
through the existing EP0 transport, collects the event replies, then leaves one
last reply queued for storage-coexistence inspection.

The physical ThinkPad ran Fedora and QEMU `10.2.2-1.fc44` with KVM, not nested
virtualization. Each guest had four CPUs, 2 GiB RAM, Q35, standard VGA with
`-display none`, the Fedora raw OVMF pair and fresh copied variables. It used
VirtIO RNG and VirtIO networking with loopback TCP forwarding for the ordinary
remote shell. AX200 `8087:0029` was attached to `qemu-xhci` by VID/PID as the
invoking user, with Bluetooth inactive/disabled and rfkill unblocked.

The local Clang/LLD 23.1.3 compiler was built from fork commit
`41ab6043cc4fd63e0a358d1e60bba249a751d8ee`. Normal kernel builds used:

```sh
PATH="$HOME/opt/pyxis-llvm/bin:$PATH" make -j16 image PREBUILT="sdk userspace ports"
```

SDK, userspace and ports bundles came from the same successful baseline CI run
and passed the existing bundle verifier. No public ABI changed. Pins were:

| Repository | Revision |
| --- | --- |
| userspace | `e8ad03985519132cda6044e2b46b23680d8cf562` |
| ports | `978528f5931a0ff703c3baa92fa3a409ea651a0a` |
| filesystem | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |

The core boot arguments, using the corresponding image path and a writable copy
of `/usr/share/edk2/ovmf/OVMF_VARS.fd`, were:

```text
-machine q35 -accel kvm -cpu max -rtc base=utc -smp 4 -m 2G
-drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd
-drive if=pflash,format=raw,unit=1,file=FRESH_VARS_COPY
-cdrom IMAGE_PATH -boot d -display none -serial file:SERIAL_CAPTURE -monitor stdio
-netdev user,id=net,hostfwd=tcp:127.0.0.1:2323-10.0.2.15:2323
-device virtio-net-pci,netdev=net,disable-legacy=on
-object rng-random,id=rng,filename=/dev/urandom
-device virtio-rng-pci,rng=rng,disable-legacy=on
-device qemu-xhci,id=xhci
-device usb-host,id=bluetooth,bus=xhci.0,port=1,vendorid=0x8087,productid=0x0029
```

Coexistence runs also exposed the baseline ISO as a read-only USB disk on the
same controller, independently of CD-ROM boot:

```text
-drive if=none,id=storage,format=raw,readonly=on,file=BASELINE_ISO_PATH
-device usb-storage,bus=xhci.0,port=2,drive=storage
```

GDB used the ELF from the same build as its guest, with
`-gdb tcp:127.0.0.1:1234` and, when setting startup breakpoints, `-S`.
Debugger sessions only inspected state and ran the existing execution flow;
they did not call kernel functions or inject register/event faults.
The numbered values in the GDB output below are the expressions the text names.

## Kernel-only boot and matched baseline

Both baseline and kernel-only guests produced complete AX200 USB inventory;
`lsusb -n` exited 0. Neither image had Bluetooth binding. Three unprofiled
connect/inventory/exit workloads per image used the existing host remote client:

```sh
printf 'lsusb -n\nexit\n' | /usr/bin/time -p \
  build/tools/pyxis-remote --machine --no-shell-echo --columns 120 --rows 40 \
  127.0.0.1 2323
```

| Host elapsed seconds | Sample 1 | Sample 2 | Sample 3 |
| --- | ---: | ---: | ---: |
| Baseline | 0.03 | 0.03 | 0.03 |
| Kernel-only interrupt support | 0.03 | 0.03 | 0.03 |

All sessions reported successful command completion and complete final drain.
There was no variation visible at the timer's 0.01-second resolution. This tiny
whole-session workload cannot isolate interrupt-copy cost, continuous event
throughput, native timing or sub-resolution regressions. Baseline kernel came
from CI and the new kernel was locally built with the same compiler fork/version
and flags; their host compiler installations were separate builds.

Boot allocation snapshots, at the same pre-userspace log point, were:

| Counter | Baseline | Kernel-only |
| --- | ---: | ---: |
| PMM allocated frames | 2192 | 2216 |
| VM reserved pages | 28181 | 28209 |
| VM backed pages | 1084 | 1084 |
| VM range records | 78 | 79 |
| Heap live allocations | 61 | 62 |
| Heap pool bytes | 2539520 | 2539520 |

The prepared arena reserves three pages per advertised root port: one ring and
two receive pages. QEMU advertises eight ports, accounting for 24 additional DMA
frames (96 KiB), even before admission. Its stream metadata array is 19008 bytes
(`sizeof(struct xhci_interrupt)=2376`). Overall VM counters include kernel/image
layout effects as well as prepared buffers; they are not a second DMA-byte
authority. One arena adds one VM record. This explicit upfront cost keeps runtime
receive handling allocation-free.

## Real event traffic, idle wait and ring wrap

The consumer selected the checked interface-0 interrupt endpoint `0x81`, configured
the host before SET_CONFIGURATION, and started two 257-byte receives. A six-second
wait returned `USB_TIMEOUT` without terminating reception. HCI Reset then returned
a six-byte event. Then 270 Intel Read Version requests completed; sampled replies
were 15 bytes.
The first, second and 271st collected sequences appeared in the
serial output.

The consumer sent one final version request without taking its event. Post-boot
GDB inspection showed sequence 272, one copied completion,
producer 19 with cycle false and both receives still `INTERRUPT_POSTED`. The
one-page transfer ring has 255 usable TRBs plus its Link TRB, so this workload
crossed the producer wrap while retaining two independently owned receives.
The stream remained `USB_OK`.

The observed context has DCI 3, Interrupt IN type 7, packet 64, interval exponent
3 and Max ESIT Payload 64. An earlier
Bluetooth-only snapshot recorded words `00030001`,
`0040003e` and `00400101`, matching running state, the periodic fields and a
257-byte Average TRB Length. This qualifies the observed full-speed `bInterval=1`
profile, not every permitted interval or packet size. That snapshot used the
same kernel with a prepublication consumer variant: a 100-ms idle wait and no
extra final command, rather than the published consumer's six-second wait and
queued final reply.

## Progress inside waits and storage coexistence

In a separate run of the same consumer, GDB stopped at `drain_events` inside
`usb_host_control_wait` for the final command. After `finish`, while still in
that control wait, sequence advanced 271 → 272, producer 18 → 19 and copied queue
count 0 → 1. The backtrace and before/after values establish
receive capture and rearm at that inner wait, rather than only at the outer
controller loop. The shared drain point in command and bulk waits has source
review; this capture specifically exercised the control wait.

The emulated USB disk completed BOT probing and registered READY; GPT was healthy
with three partitions. Inventory retained both the super-speed disk and full-speed
AX200. GDB showed disk slot 1 and Bluetooth slot 2, each using DCI 3 for IN,
without cross-dispatch. The bulk exchange was idle with `USB_OK`; interrupt work
remained live with its queued final reply. This covers read-only emulated storage
coexistence, not writes or native USB-media qualification.

## Active removal and retained backing

After the same traffic and disk preparation, the QEMU monitor's
`device_del bluetooth` detached that guest device while its two receives were
posted. The removal log recorded controller quarantine,
confirmed halt and disabled interrupt delivery. GDB showed
`running=false`, `failed=true`, `interrupt_ready=false`, stream `USB_IO`, both
receives `INTERRUPT_HELD`, nonzero retained arena/ring physical addresses and
the copied completion still present. No register corruption or fabricated
Transfer Event was used.

This observes the accepted coarse shutdown path, including the effect on the
shared storage controller. It does not qualify recovery, reattachment or future
I/O after removal. STALL, overflow, corrupt completion and rejected collection
paths have source review; they were not forced in the guest. Captured event
payloads were 6 and 15 bytes, so large multi-packet HCI events remain unmeasured.

## Firmware observation

The version event was:

```text
0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00
```

Interpreting its return parameters using Linux v6.18's
[legacy version layout](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.h)
gives status 0 and firmware variant `0x23`;
[btintel's version interpretation](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.c)
identifies that variant as firmware, rather than bootloader `0x06`. This is an
inference from the captured bytes and reference protocol, not a firmware load
performed by Pyxis. It corrects the brief's unconditional prepared-host bootloader
expectation. Cold native startup/reset state and pinned `.sfi`/`.ddc` files remain
work for the later probes.

## Delivery and remaining limits

The primary tree restored `core.c` and rebuilt the ordinary image after the
consumer runs; its ELF has no interrupt-probe symbols. The unmerged consumer
branch is preserved separately for review. All QEMU and GDB jobs exited, `btusb`
rebound, and the owner's Bluetooth service remained inactive and disabled.
Dependency pins and compiler/container inputs did not change.

Native periodic transfer hardware, other speed/topology profiles, endpoint
recovery/cancellation, forced overflow/STALL handling and sustained event-rate
performance remain unqualified. The accepted
[retention/profile debt](../../../technical-debt.md#usb-interrupt-in-initial-profile-and-failure-retention)
records consequence and revisit points. Task 3a completes the kernel mechanism;
the HCI/firmware/scan work remains on unmerged probe branches and is not merged
as part of this change.

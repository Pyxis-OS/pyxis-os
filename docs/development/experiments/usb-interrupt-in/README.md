# Root full-speed interrupt-IN qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

On 2026-10-08, Bluetooth investigation task 3a implemented the private
[interrupt-IN interface](../../../devices/usb-interrupt-in.md), following the owner's decisions on
[#517](https://git.internal/PyxisOS/pyxis-os/pulls/517). The kernel change contains endpoint
configuration, retained receive and ring state, copied FIFO collection, progress at every event-drain
point and terminal failure accounting. Bluetooth binding and commands stay in a separate unmerged consumer
used for qualification.

## Revisions and configuration

Baseline: main `caa337fe19294261bdce1b8ab4a0e0e05b938eba` from successful
[CI run 1200](https://git.internal/PyxisOS/pyxis-os/actions/runs/1200). Kernel implementation:
`a9ce718fb3e6e7f2157dec0024893c40e52b3218`. The standalone
[consumer commit](https://git.internal/PyxisOS/pyxis-os/commit/e84a327c2860eb347571d1d3cf1323250ba8c7d7)
on `probe/bluetooth-interrupt-consumer` is not part of the kernel PR and must not be merged; it sends HCI
Reset and repeated Intel Read Version requests through the existing EP0 transport, collects the event
replies, and leaves one last reply queued for storage-coexistence inspection. No public ABI changed.
Pins: userspace `e8ad03985519132cda6044e2b46b23680d8cf562`, ports
`978528f5931a0ff703c3baa92fa3a409ea651a0a`, filesystem `b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`; SDK, userspace and ports bundles came from the baseline CI run
and passed the bundle verifier.

The physical ThinkPad ran Fedora and QEMU `10.2.2-1.fc44` with KVM (not nested). Each guest had four CPUs,
2 GiB, Q35, `-display none`, the Fedora raw OVMF pair with fresh variables, VirtIO RNG and networking
(loopback TCP forwarding for the remote shell), and AX200 `8087:0029` attached to `qemu-xhci` by VID/PID
(`usb-host,bus=xhci.0,port=1`) as the invoking user, with Bluetooth inactive/disabled and rfkill unblocked.
Coexistence runs also exposed the baseline ISO as a read-only `usb-storage` disk at port 2 of the same
controller. The local Clang/LLD 23.1.3 compiler was built from fork commit
`41ab6043cc4fd63e0a358d1e60bba249a751d8ee`, and builds used
`make -j16 image PREBUILT="sdk userspace ports"`. GDB used each build's own ELF over `-gdb` (with `-S` for
startup breakpoints) and only inspected state and ran the existing flow, with no kernel calls or injected
register or event faults.

## Kernel-only boot and matched baseline

Baseline and kernel-only guests both produced complete AX200 USB inventory (`lsusb -n` exited 0), neither
with Bluetooth binding. Three unprofiled connect/inventory/exit workloads per image used the host remote
client (`printf 'lsusb -n\nexit\n' | time -p pyxis-remote --machine --no-shell-echo … 127.0.0.1 2323`):
every sample took **0.03 s** at the timer's 0.01 s resolution on both images, with successful completion
and complete final drain. This tiny whole-session workload cannot isolate interrupt-copy cost, continuous
event throughput, native timing or sub-resolution regressions; the baseline kernel came from CI and the new
one was built locally with the same compiler fork and flags, in separate installations.

Boot allocation snapshots at the same pre-userspace log point:

| Counter | Baseline | Kernel-only |
| --- | ---: | ---: |
| PMM allocated frames | 2192 | 2216 |
| VM reserved pages | 28181 | 28209 |
| VM backed pages | 1084 | 1084 |
| VM range records | 78 | 79 |
| Heap live allocations | 61 | 62 |
| Heap pool bytes | 2539520 | 2539520 |

The prepared arena reserves three pages per advertised root port (one ring and two receive pages); QEMU
advertises eight ports, accounting for 24 more DMA frames (96 KiB) even before admission. The stream
metadata array is 19008 bytes (`sizeof(struct xhci_interrupt)=2376`). The overall VM counters include
kernel and image layout effects, so they are not a second DMA-byte authority; one arena adds one VM record.
This upfront cost keeps runtime receive handling allocation-free.

## Real event traffic, idle wait and ring wrap

The consumer selected the checked interface-0 interrupt endpoint `0x81`, configured the host before
SET_CONFIGURATION and started two 257-byte receives. A six-second wait returned `USB_TIMEOUT` without
terminating reception; HCI Reset then returned a six-byte event, and 270 Intel Read Version requests
completed (sampled replies were 15 bytes), the first, second and 271st sequences appearing in the serial
output. A final version request was sent without taking its event. Post-boot GDB showed sequence 272, one
copied completion, producer 19 with cycle false and both receives still `INTERRUPT_POSTED`; the one-page
ring has 255 usable TRBs plus its Link TRB, so this workload crossed the producer wrap while keeping two
independently owned receives. The stream stayed `USB_OK`.

The observed context has DCI 3, Interrupt IN type 7, packet 64, interval exponent 3 and Max ESIT Payload 64.
An earlier Bluetooth-only snapshot (same kernel, a prepublication consumer variant with a 100 ms idle wait
and no extra final command) recorded words `00030001`, `0040003e` and `00400101`, matching running state,
the periodic fields and a 257-byte Average TRB Length. This qualifies the observed full-speed `bInterval=1`
profile, not every permitted interval or packet size.

## Progress inside waits and storage coexistence

In a separate run GDB stopped at `drain_events` inside `usb_host_control_wait` for the final command. After
`finish`, still in that control wait, the sequence advanced 271 → 272, producer 18 → 19 and the copied queue
count 0 → 1, establishing receive capture and rearm at the inner wait, not only at the outer controller loop.
The shared drain point in command and bulk waits has source review only. The emulated USB disk completed BOT
probing and registered READY with a healthy three-partition GPT; inventory retained both the super-speed disk
and the full-speed AX200, GDB showed disk slot 1 and Bluetooth slot 2 each using DCI 3 for IN without
cross-dispatch, the bulk exchange was idle with `USB_OK`, and interrupt work stayed live with its queued
final reply. This covers read-only emulated storage coexistence, not writes or native USB media.

## Active removal and retained backing

After the same traffic and disk preparation, the QEMU monitor's `device_del bluetooth` detached the device
while both receives were posted. The log recorded controller quarantine, confirmed halt and disabled
interrupt delivery, and GDB showed `running=false`, `failed=true`, `interrupt_ready=false`, stream `USB_IO`,
both receives `INTERRUPT_HELD`, nonzero retained arena and ring physical addresses and the copied completion
still present. No register corruption or fabricated Transfer Event was used. This observes the accepted
coarse shutdown path, including its effect on the shared storage controller; it does not qualify recovery,
reattachment or later I/O. STALL, overflow, corrupt completion and rejected collection have source review
only. Captured payloads were 6 and 15 bytes, so large multi-packet HCI events remain unmeasured.

## Firmware observation

The version event was `0e 0d 01 05 fc 00 37 14 01 23 03 c1 21 18 00`. Read with Linux v6.18's
[legacy version layout](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.h), it gives
status 0 and firmware variant `0x23`, which
[btintel's interpretation](https://github.com/torvalds/linux/blob/v6.18/drivers/bluetooth/btintel.c)
identifies as operational firmware rather than bootloader `0x06`. This is an inference from the bytes and
the reference protocol, not a firmware load performed by Pyxis, and it corrects the brief's unconditional
prepared-host bootloader expectation. Cold native startup/reset state and the pinned `.sfi`/`.ddc` files
remain for later probes.

## Delivery and remaining limits

The primary tree restored `core.c` and rebuilt the ordinary image after the consumer runs, and its ELF has
no interrupt-probe symbols. The consumer branch is kept for review. `btusb` rebound after the guests exited
and the owner's Bluetooth service stayed inactive and disabled. Native periodic transfer hardware, other
speed and topology profiles, endpoint recovery and cancellation, forced overflow or STALL handling and
sustained event-rate performance remain unqualified; the
[retention/profile debt](../../../technical-debt.md#usb-interrupt-in-initial-profile-and-failure-retention)
records consequence and revisit points. Task 3a completes the kernel mechanism; the HCI, firmware and scan
work stays on unmerged probe branches.

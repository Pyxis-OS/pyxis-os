# Bluetooth mouse task 2: runtime HCI transport

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Status: **task 2 implemented and warm passthrough validated 2026-10-08**, delivered by
[#552](https://git.internal/PyxisOS/pyxis-os/pulls/552). This is task 2 of the
[mouse milestone](../../../wip/bluetooth-mouse.md), separate from the completed
investigation's identically numbered source-assessment task. The owner authorized
production AX200 binding, an exclusive process-owned controller grant, event and
asynchronous ACL reception, kernel-owned command/data credits and bounded
progress/exit/loss behavior. The [accepted contracts](../../../wip/bluetooth-task1-contracts.md)
settle policy and the [runtime reference](../../../devices/bluetooth-hci.md) describes
the implementation; this record holds the measurements.

Review exposed the independent-stream handle-reuse limit, and the owner accepted
fail-closed reuse for task 2 on 2026-10-08: connection and reconnect tasks must
qualify the retirement boundary before bonded reconnect is ready (see the
[technical debt](../../../technical-debt.md#bluetooth-hci-connection-handle-reuse-boundary)).
The owning xHCI worker must not block on HCI completion or userspace operations.
Cold upload, scan/connection policy, pairing, durable bonds and pointer injection are
later tasks. The historical warm profile gives staged development evidence only, not
production firmware qualification.

## Baseline

The ThinkPad ran Fedora kernel `6.19.10-300.fc44.x86_64` and QEMU `10.2.2` with KVM on
the physical host (not nested); Fedora Bluetooth was inactive and rfkill unblocked.
The Linux journal reported firmware revision 0.3, build 193/week 33/2024, matching the
[historical warm observation](../bluetooth-task4/README.md#measured-warm-state); that is
a Linux observation, not a Pyxis readiness measurement.

Kernel, SDK, userspace and ports bundles came from successful
[CI run 1297](https://git.internal/PyxisOS/pyxis-os/actions/runs/1297), revision
`01edf6d49814b6450105eb1d2878363542540d97`, which was code-identical to main `3bda2c3`
(the merge of [#548](https://git.internal/PyxisOS/pyxis-os/pulls/548)), and passed the
bundle verifier. Compiler: Clang 23.1.3, fork `49e2c1a1518b3e4687b52ceb6001069c1b6d261e`.
Pins: userspace `b83ff679e91911e9483e24901afca9b3b26d0071`, ports
`a642f07382e14bd233ac1be2b6a814e95c32d835`, filesystem
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

A private 68 MiB GPT disk holds a 64 MiB npfs `system` volume (8 MiB journal), the 1 MiB
`iobench` fixture and a boot override granting the volume read-only to the single
network-owning space. Each run used a fresh writable copy (the installed defaults open
`system` read-write before reading the override), prepared with `mkfs.npfs`, `fsck.npfs`
and `sfdisk`; no physical storage was used. Guests: Q35, KVM, `-cpu max`, four CPUs,
2 GiB, fresh Fedora OVMF variables, ISO boot, no display, VirtIO RNG/network and
`qemu-xhci` with the AX200 `8087:0029` (libusb attachment, port 1) and the storage disk
(port 2). A preparation guest without the AX200 confirmed the storage workload works;
it is not a coexistence baseline.

Each of three fresh baseline guests ran, through the remote shell:

```text
lsusb -n
cat system://README.txt
iobench read system://iobench.bin --buffer 65536 --rounds 3
exit
```

All commands succeeded, AX200 and storage appeared in the complete inventory, and one
warmup plus three 1 MiB read samples verified in every boot (repeated warm reads through
the npfs cache, not independent cold-media trials). After the workload each boot measured
a 30-second idle interval: GDB read existing xHCI counters and per-CPU timer counts at
the boundaries (matching ELF, no inferior calls, detached during the interval) and
`/proc/PID/stat` supplied whole-QEMU CPU at 100 ticks/s. The percentage is relative to
**one host CPU**, includes the whole guest and QEMU background work and does not isolate
Bluetooth. No LLVM build ran during measurement; profiling was disabled.

| Boot | Actual interval (s) | QEMU user/system CPU (s) | One-CPU cost | xHCI IRQ/command/event deltas | Timer deltas, BSP/AP1/AP2/AP3 |
| --- | ---: | --- | ---: | --- | --- |
| 1 | 30.000514 | 3.75 / 3.93 | 25.60% | 0 / 0 / 0 | 7966 / 3611 / 3612 / 3612 |
| 2 | 30.000082 | 3.32 / 3.65 | 23.23% | 0 / 0 / 0 | 7080 / 3612 / 3612 / 3612 |
| 3 | 30.000105 | 3.34 / 4.13 | 24.90% | 0 / 0 / 0 | 8095 / 3612 / 3612 / 3612 |

Across nine samples the payload median/range was **118.857 / 112.298–123.701 ms**, and
complete consumption **120.753 / 114.210–126.093 ms**; idle cost was **24.90 /
23.23–25.60%** of one CPU with zero additional xHCI IRQs, commands and events (ordinary
timer activity continued; tick resolution is 10 ms). These establish the matched
workload, not native storage throughput or radio-traffic qualification; BSP timer and
process CPU variation are not evidence that Bluetooth caused either. No HCI binder or
posted receive stream exists in this baseline.

## Runtime implementation and initial warm check

The public ABI change required new matching SDK, userspace and ports bundles (pins
unchanged); no userspace service or connection procedure is added. An initial warm boot
(CI run 1322, `b896bc4`) found by scalar GDB inspection: initialization ready (stage 11),
complete sealed inventory, one attachment, no owner or terminal failure, HCI version
11/revision 8641, LE features `80059ff`, validated mandatory LE commands, one command
credit, ACL payload length 251 and three available/total ACL packet credits. Ten event
USB completions supplied the initialization replies; neither stream had a partial frame;
interrupt and bulk receive slots stayed posted with empty copied queues. This is
framing/readiness and idle-ownership evidence, not ACL payload traffic.

Integration review corrected late retired-link ACL handling, post-callback USB failure
reporting, HCI request progress during storage waits, singleton candidate counting before
transport admission, and bounded first-link ACL deferral across independent drains (eight
whole frames in order for at most five seconds, with captured epoch/generation and
explicit overflow/expiry loss). Cleanup is deliberately conservative: radio-changing
command publication, connection admission or ACL publication permanently taints the
session, so release/exit requires reboot even after credits and links settle; read-only
sessions can be re-granted only after fully confirmed accounting. HCI BSS is 214344 bytes
and the private async bulk pool reserves five DMA pages per controller plus copied
metadata, independent of the storage budget. These are implementation settings, not
public capacity guarantees.

## Matched runtime validation

Three fresh guests used bundles from successful
[CI run 1326](https://git.internal/PyxisOS/pyxis-os/actions/runs/1326), code revision
`8e67c3c9b1c4b359131ad13515bdf6c2e1111b95`, with the baseline configuration, disk,
workload and idle sampling; only the kernel, public ABI and SDK content changed. All
three reached development readiness with the same checked firmware, features and
credits, both receive slots on each endpoint stayed posted, and there was no partial
frame, copied-queue backlog or terminal failure. The firmware/size snapshot showed the
operational tuple and zero deferred frames; no packet or peer identity was dumped. All
commands, warmups and nine storage samples verified, and the private disk copies kept
their original hash. Final captures ran GDB-before, the 30-second `/proc` sample and
GDB-after in one shell invocation; a preliminary boot-1 capture (26.30%) whose manual
gaps extended the debugger interval was excluded and repeated. No sample was chosen by
its performance.

| Boot | Actual interval (s) | QEMU user/system CPU (s) | One-CPU cost | xHCI IRQ/command/event deltas | Timer deltas, BSP/AP1/AP2/AP3 |
| --- | ---: | --- | ---: | --- | --- |
| 1 | 30.000446 | 3.90 / 4.07 | 26.57% | 0 / 0 / 0 | 7988 / 3618 / 3618 / 3618 |
| 2 | 30.000800 | 3.58 / 4.09 | 25.57% | 0 / 0 / 0 | 7800 / 3618 / 3619 / 3619 |
| 3 | 30.000249 | 3.44 / 3.86 | 24.33% | 0 / 0 / 0 | 6129 / 3618 / 3618 / 3618 |

| Measure | Baseline median / range | Runtime median / range |
| --- | --- | --- |
| Payload (ms) | 118.857 / 112.298–123.701 | 121.789 / 113.906–125.406 |
| Complete consumption (ms) | 120.753 / 114.210–126.093 | 123.336 / 115.348–126.836 |
| Whole-QEMU CPU (% of one CPU) | 24.90 / 23.23–25.60 | 25.57 / 24.33–26.57 |

Payload median rose 2.5%, complete consumption 2.1% and whole-QEMU CPU 0.67 percentage
points (2.7% relative); ranges overlap, and three boots of cached reads under ambient host
scheduling cannot attribute the differences to Bluetooth or show cost equivalence. Source
inspection shows additional bounded bookkeeping in existing worker ticks and no new
polling interval; idle IRQ/event cost was zero despite posted receive DMA. Memory cost is
HCI BSS 214344 bytes, async bulk metadata 33152 bytes and five DMA pages per controller.

Measured: event framing, warm readiness, retained idle event/ACL reception and storage
coexistence. Source-reviewed only: acquisition/release/process-exit syscall paths, real
ACL payload and credit recycling, first-link deferral and handle reuse/error paths;
task 4 supplies the service and connection qualification. No native Pyxis Bluetooth
readiness is claimed, and active radio with concurrent storage remains a later gate.

## Main integration

A signed merge (`ca4d0f602019ec9b254d6cb73452b2216ff5383f`) integrated main `0179163`.
Main had added terminal-pointer protocol 43/object 45, so Bluetooth now uses protocol
44/object 46, preserving both typed dispatch paths; this renumbers an unmerged ABI without
a compatibility shim, and transport/framing code is unchanged. The integrated build
passed, all jobs in [CI run 1332](https://git.internal/PyxisOS/pyxis-os/actions/runs/1332)
passed, and one fresh warm guest repeated the workload and idle procedure: storage payload
median/range 120.977 / 115.190–121.294 ms, complete consumption 123.359 /
117.086–124.565 ms, the disk hash unchanged, development-ready stage 11 with the same
checked features/credits, both receives posted on both endpoints, and whole-QEMU CPU
24.70% of one CPU over 30.000749 s with zero IRQ/command/event deltas. This single check is
separate from the matched comparison, since main's other changes cannot be attributed to
Bluetooth.

A logging-only follow-up from the review of #552 emits one `Bluetooth HCI: AX200 USB
ready (development firmware)` line on successful initialization, with version, features,
credits and USB transport details at `ktrace` and failures at `klog`; the kernel build
passed and no further guest was started. Fedora Bluetooth stayed inactive and the AX200
node unchanged, and no address, private packet or key was recorded. Task 3 remained
unassigned when this record was written.

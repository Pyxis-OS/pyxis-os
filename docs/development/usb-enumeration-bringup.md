# USB enumeration bring-up record

Phase B.3 was developed on October 2, 2026, from merged controller PR #322,
main `a6c6dbf`. Core/class parsing is commit `a959194`; integrated controller
and core corrections are `bafb264`. Dependency pins remain at that base.
Interfaces and behavior are in [USB enumeration](../devices/usb-enumeration.md)
and the [xHCI reference](../devices/usb-xhci.md).

## Build and manual QEMU inspection

Ordinary `make -j16 image` passed with the existing compiler. Final changed kernel
code produced no warnings. No tests, self-tests, fault injection, CI workflow or
boot/output automation was added. All launches and debugger inspection were
interactive. Initial integration boots found an interrupt-state error in the
worker-owner check and unsupported width syntax in kernel log formatting; both
were corrected before the successful measurements below. Review also corrected
Input Slot construction, asymmetric PSI-pair recognition and unbound class codes.

Baseline and successful after runs used the same nested-KVM Q35 configuration
as the [controller bring-up command](xhci-bringup.md#configuration-and-manual-checks):
QEMU 10.2.2 with the existing AHCI fix, CPU max, four CPUs/four cores/one socket,
256 MiB RAM, ISO boot, one qemu-xhci, one readonly USB disk at connector 1, mouse
at connector 2, VirtIO RNG, no network and no VirtIO block. Each launch copied
fresh raw OVMF variables. The disk was the unchanged 512 MiB image with 128 MiB
ESP from B.2, SHA-256
`812ae7b9f8a6c87d008ce4ee70308a55cb26bb512b36ac2bdcd755b466a0c8a1`.
These RAM/image/capacity values are validation parameters, not requirements.

Three matched after boots completed Address Device for storage and HID,
descriptor requests, unique BOT selection, SET_CONFIGURATION and Configure
Endpoint. GDB found `DISCOVERY_TRANSPORT_READY`, one candidate, five completed
controller commands, no pending command and a running/nonfailed controller.
The disk described IN endpoint 81 and OUT endpoint 02, both packet 1024/burst 15.
Output contexts used actual DCIs 3 and 4 and highest context entry 4; endpoint DW1
was `04000f36` for IN and `04000f16` for OUT. The mouse's descriptors were checked,
its record was unbound, and its interrupt endpoint remained unconfigured.
Manual monitor `device_del usb_mouse` completed Disable Slot and retired only
that port; the disk stayed configured and the controller stayed running.

A separate four-CPU launch added a second readonly `usb-storage` device at
connector 3 with its own drive ID, using the same image as backing. GDB found two
physical-device candidates and `DISCOVERY_AMBIGUOUS`; neither disk was configured,
selection remained unset and the controller stayed healthy. Shared media contents
cannot collapse distinct USB device candidates.

A one-CPU launch set `qemu-xhci,p3=1`, disk connector 2 and mouse connector 1,
keeping the other launch options. Connector 2 then offered USB 2 only. The disk
was classified at high speed with EP0 packet 64 and bulk packet 512/burst 0;
SET_CONFIGURATION and Configure Endpoint completed with the same unique-device
policy. This exercises the actual descriptor fields on a second speed path,
not a separate host-controller implementation.

A four-CPU launch replaced the mouse at connector 2 with `usb-hub`. Its
full-speed device/configuration descriptors were read, but downstream inventory
was unavailable. GDB found `DISCOVERY_INCOMPLETE` despite the disk candidate;
the disk remained unconfigured and the controller stayed healthy. No downstream
enumeration or hub control was attempted.

## Matched startup cost

The pre-implementation baseline used `a6c6dbf`, matching ELF SHA-256
`8e0df08a21ab96dfad14f5fe1fbcd9e35345f8dacda10c8f52141fed7c0a85ee`
and ISO SHA-256
`24ef75da9eb11998426d908eebb32d51ca5e76d6b00af10affbb7905e57b2f2f`.
After measurements used the `bafb264` source, matching ELF SHA-256
`e0846acde3f4c4605693069c5dbc84541e0a46c085958552e30012c755d34d18`
and ISO SHA-256
`4287432e15177e7fe6afa1bb2badeb43ca2dd09f83012d0052d16861f2e44844`.
Host image/artifact paths differed from the older B.2 record, but were held fixed
between this baseline and after set.

GDB stopped at `controller_worker` entry and before the normal scan/enumeration
completion log, after two slot commands before B.3 and after full enumeration/
endpoint setup afterward. Temporary hardware breakpoints and counter reads used:

```text
thbreak controller_worker
continue
set $startticks = *(unsigned long long *)0xfffffe80402020f0
thbreak kernel/usb/xhci.c:829
continue
set $endticks = *(unsigned long long *)0xfffffe80402020f0
p/d ($endticks - $startticks) * 10
```

The baseline completion line was 829; the after completion line was 1432.
The observed HPET period was 10,000,000 fs, hence 10 ns per counter tick.
No target calls or memory mutation were used. Absolute counter values below
exclude firmware loading and work before clock initialization.

| Run | Baseline entry / complete (ms) | Baseline worker (ms) | After entry / complete (ms) | After worker (ms) |
| --- | --- | --- | --- | --- |
| 1 | 292.31842 / 314.90949 | 22.59107 | 184.34679 / 219.85699 | 35.51020 |
| 2 | 230.93087 / 244.40760 | 13.47673 | 191.39166 / 229.87233 | 38.48067 |
| 3 | 191.38163 / 208.09119 | 16.70956 | 193.37123 / 230.37529 | 37.00406 |

Mean worker interval increased from 17.59245 to 36.99831 ms, a 19.40586 ms
increase for real addressing, descriptor parsing and endpoint setup. Ranges were
9.11434 ms before and 2.97047 ms after. This is debugger-observed startup elapsed
time in a nested VM, including scheduler/service interleaving, rather than CPU
time, end-to-end boot reliability, disk latency or throughput. The endpoint has
more completed work after B.3; it is not an equal-work microbenchmark.
No performance optimization is inferred from the lower absolute after counters.

Initial console preparation statistics before user scheduling were stable across
the observed runs: PMM allocated frames increased from 4,168 to 4,216; VM reserved
pages from 8,760 to 8,808, with 4,000 backed pages reported in both; VM range records
from 35 to 83; heap live allocations from 48 to 50 in the same 262,144-byte pool.
The 48 additional pages/range records correspond to six preallocated DMA buffers
per advertised port, 192 KiB in this eight-port profile. Larger host/discovery
records and scratch storage add heap use; the pool did not grow. Live worker
snapshots include concurrent userspace/service allocations and cannot isolate
controller cost. The larger kernel also changed the available PMM total; it is
not part of the 48-frame prepared-DMA difference.

## Limits

This validates descriptor-driven provisional transport setup and controller
interrupts, not GET_MAX_LUN, SCSI/BOT bulk exchange, geometry, USB block reads,
mounting or native USB boot reliability. The known pre-kernel
[Limine file-open failure](qemu.md#usb-firmware-file-open-failure-before-kernel-entry)
was avoided by ISO boot. No physical drive, passthrough, ThinkPad or writable
media was used.

QEMU's exercised profile uses 32-byte contexts, zero scratchpads, default PSI
speed IDs, configuration 1 and alternate 0. Full-speed packet evaluation, 64-byte
contexts, nondefault/asymmetric PSI, BIOS-owned handoff, nonzero alternate
selection, control short packets, active abandonment, error recovery and ring
wrap are implemented/reviewed paths without synthetic runtime coverage.
All QEMU/debugger processes were closed after inspection.
[Qualification and resource debt](../technical-debt.md#usb-descriptor-bounds-and-per-port-preparation)
records the concrete consequence and revisit point.

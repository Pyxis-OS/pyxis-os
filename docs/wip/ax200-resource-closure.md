# AX200 resource and asset closure

Wi-Fi stage 1, assigned 2026-10-18; **in progress, no driver or radio run**.
The [Wi-Fi plan](wifi.md) owns the accepted architecture. This source/asset
audit uses Pyxis `2f0f3621`; the separate multiple-interface plan is accepted
in merged #966. No dependency pin, image payload or compiler container changes.

## Pinned reference and assets

Use OpenBSD source commit `65596250e06438442ce07bad87e5ba129229607d`:
[iwx transport](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwx.c),
[registers](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwxreg.h)
and [structures](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwxvar.h).
These are CVS revisions 1.230/1.75/1.50. The initial reuse scope is AX200
family-22000 context self-load, command/RX transport, NVM and passive-scan
commands, adapted to native PCI/DMA/worker interfaces. AX210 gen3/PNVM/IML,
data TX queues, association/security and optional firmware debug-monitor/core
dumps stay outside this slice. The complete transport is a reference, not an
approved wholesale import or a proven minimum scan initialization sequence.

The same commit pins the [net80211 reference](https://github.com/openbsd/src/tree/65596250e06438442ce07bad87e5ba129229607d/sys/net80211).
Its 30 inspected C/header files are not all needed for passive scan; the later
port must retain notices for every file actually reused. iwx has ISC-style and
Intel dual BSD/GPL notices (select the BSD terms); net80211 includes ISC,
BSD-2-Clause and BSD-3-Clause. No OpenBSD kernel compatibility layer is planned.

Actual downloads used only these existing cache prefixes, with no upstream
fallback or vendoring:

- `https://repo.internal/repository/raw-github/openbsd/src/raw/65596250e06438442ce07bad87e5ba129229607d/`
- `https://repo.internal/repository/raw-gitlab/kernel-firmware/linux-firmware/-/raw/c822cbbb14ce5b8ee1f27346220640ac350bbf34/`

Firmware is linux-firmware `20260309`. Preserve unmodified bytes, the complete
[Intel licence](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENCE.iwlwifi_firmware)
and WHENCE provenance when packaging. Binary redistribution requires notices
and disclaimer, forbids endorsement/reverse engineering, and has the stated
OSI-licensed-OS patent-grant condition. This is separate from the Bluetooth
firmware licence. Stage 2 must add image consumption explicitly.

| File relative to firmware root | Bytes | SHA-256 |
| --- | ---: | --- |
| `intel/iwlwifi/iwlwifi-cc-a0-77.ucode` | 1,368,096 | `d23a382871e6e638957845b47424ab98dca5e68e022ab73eeeebb0b6f751d7e9` |
| `LICENCE.iwlwifi_firmware` | 2,046 | `16d5040c7cf851fc693b7542e20870935b99802533ea1bfd231c377a2305e5c5` |
| `WHENCE` | 421,898 | `b763b1df1341d86664e8e881f5214333d5f904c31e8cc58bb5302774f0369630` |

Offline container inspection found valid magic, 180 bounded/aligned TLVs and
50 regular section records including two separators. The version TLV reports
`77.f39cc7f9.0`; the human-readable label says `release/core74::f39cc7f9` and
WHENCE says `74.f39cc7f9.0`. Record those fields separately. The reference uses
the version TLV; the filename is not proof of a successful firmware handshake.
The scan-channel field is 67, matching the reference parser's bound. This was
container/size inspection, not execution, disassembly or full loader validation.

## Resource and ownership profile

AX200 is non-integrated PCIe `03:00.0 8086:2723 rev 1a` in the
[ThinkPad inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt).
It does not supply this function's verbose PCI/PM/BAR/interrupt details; adjacent
Realtek details belong to Ethernet. Firmware Bluetooth readiness proves nothing
about PCIe Wi-Fi readiness.

The reference prepares NIC ownership/clock state, initializes interrupt routing,
then publishes the context and starts firmware self-load. NVM follows regular
firmware ALIVE, `INIT_EXTENDED_CFG(INIT_NVM)`, `NVM_ACCESS_COMPLETE`,
`INIT_COMPLETE` and `NVM_GET_INFO` (`if_iwx.c:4483–4576`). Live rfkill is a
powered CSR observation, not a PCI capability. Neither can be obtained from
Pyxis's inventory-only `lspci`.

The following are **derived reservation sizes**, not measured RAM use or new
architectural capacity limits. They assume separate 4 KiB allocations and the
reference's 512 RX buffers and 256 descriptors per queue. RX buffers need 4 KiB
alignment because descriptor low bits encode the buffer index.

| Allocation | Page-rounded bytes |
| --- | ---: |
| RX free/used/status rings plus 512 × 4 KiB buffers | 2,109,440 |
| One command queue: descriptors, inline commands, byte-count table | 155,648 |
| Context plus ICT | 8,192 |
| One serialized large-command buffer | 4,096 |
| Firmware LMAC: 14 sections | 380,928 |
| Firmware UMAC: 15 sections | 401,408 |
| Firmware paging: 19 sections | 589,824 |
| **Subtotal** | **3,649,536** |

Firmware entries are the sum of individually page-rounded lengths in the pinned
blob, following `iwx_init_fw_sec`, not the raw blob size. All three groups fit
the reference's 64-pointer context groups. Raw firmware storage (1,368,096
bytes), response-copy scratch (up to 4 KiB), parser/controller state, stack,
page tables, MMIO/MSI-X mappings and bounded scan results remain additional.
LMAC/UMAC DMA may retire only after confirmed load; paging survives until a
confirmed stop. Using the reference's ten TX queues with the same one-slot
large-command allowance raises the subtotal before firmware from 2,277,376
to 3,678,208 bytes. A command-
only subset still needs scan initialization qualification. Do not inherit the
optional firmware monitor's allocation of up to 64 MiB.

Prefer one MSI-X entry, matching the reference and Pyxis's checked helper; MSI
is a possible native-qualified fallback, not an assumed capability. ICT is
included conservatively above even though verified MSI-X may omit it. No new
INTx fallback is proposed. BAR extents/table placement, PM and pre-existing
BME/firmware ownership remain native admission gates.

Follow [PCI claims](../devices/pci.md), [DMA allocation](../kernel/memory.md)
and [SMP worker](../kernel/smp.md)
contracts: reserve/map/preallocate on the BSP with IF=0 before AP startup;
firmware waits and parsing belong to a BSP worker with IF=1. IRQ work is bounded
acknowledgement/publication. Queue indices, response lengths, generations and
command versions must be checked. A timeout never returns submitted DMA to the
allocator. Reference void stop helpers ignore some idle/master timeout results;
Pyxis must check fencing and retain claim, DMA and late-IRQ context until reboot
when stop/reset is uncertain. Radio work must leave wired management usable.

## Native evidence and baseline still open

Existing network-debugger PCI monitors can read a bounded allowlist of identity,
COMMAND, PMCSR and MSI/MSI-X configuration at a readiness checkpoint over the
built-in RTL8111. This is post-enumeration power-on evidence, not untouched
reset state. An owner-scheduled power cycle is needed; a Remote reboot is warm.
No arbitrary MMIO, BAR-sizing writes, power transitions or firmware commands
are part of that capture. A separate ordinary boot is needed for performance.

The baseline uses existing [ttcp workloads](../development/network-throughput.md):
three 64 MiB sends with 8 KiB writes, three with 2 KiB writes, and three discard
receives over built-in Ethernet, retaining byte totals and medians/ranges.
Record exact kernel/pins/compiler/configuration and image/ELF hashes, clock,
AC/cable/peer and cold/warm state; repeat unchanged after hardware work.
Current production tools have no native idle/BSP utilization accounting.
Historical throughput is a reference, not this task's baseline. No native
capture, baseline, QEMU radio run or measured memory use is claimed here.

### Native packet checkpoint (2026-10-18)

`/shared/present/wifi-ax200-stage1/README.md` is ready for the orchestrator's
native batch. Full ordinary/debug images and host tools were built from
`b31389cc` on main `fed2ce88`, using the existing `3d69e83` builder and -j8.
Their source/pins/effective kernel configuration match; per-image SHA256SUMS,
FLAGS and provenance are included. Packet hash checks, Python compilation and
shell syntax passed. No native connection or boot has been performed by beta.

The numbered run sheet has one host command per phase: scheduled power-on into
the exact-image read-only network-GDB capture, then nine existing `ttcp` samples
from an ordinary wired boot. It neither changes PXE menus nor reboots. Raw
observations and samples remain in the packet's `results/`; add a short
configuration/results summary here after the native batch, then tick stage 1.
No live NVM/rfkill or native idle/BSP utilization result is claimed.

### Accepted stage-1 completion scope

Owner accepted 2026-10-18: stage 1 keeps pinned source/firmware assets,
source-derived resource budgets, read-only cold PCI evidence and the existing
wired TCP baseline. Live NVM/rfkill and runtime memory/queue confirmation move
to stage 2, which initializes firmware. Native idle/BSP utilization remains
explicitly unmeasured: production tools do not provide that accounting.
Stage 1 remains unchecked until its PCI evidence and wired samples are recorded.
No initialization/NVM or CPU-accounting probe is part of stage 1.

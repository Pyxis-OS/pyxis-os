# AX200 Wi-Fi controller

The ThinkPad's Intel AX200 (`03:00.0 8086:2723 rev 1a`, subsystem `8086:0080`)
is the first [Wi-Fi](../wip/wifi.md) target. Stages 1–2 deliver firmware
startup, live NVM/rfkill and bounded passive scanning through the
[native Wi-Fi interface](../interfaces/wifi.md); there is no association, data
TX or Wi-Fi IP interface yet. The driver is opt-in (`wifi.ax200=1`, image
option `WIFI_AX200=1`). Native evidence is in the
[stage-1](../development/experiments/ax200-resource-closure.md) and
[stage-2](../development/experiments/ax200-passive-scan.md) records.

## Reference source and firmware

The driver in `kernel/wifi` is a native port guided by OpenBSD commit
`65596250e06438442ce07bad87e5ba129229607d`:
[iwx transport](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwx.c),
[registers](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwxreg.h)
and [structures](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwxvar.h)
(CVS 1.230/1.75/1.50), with the
[net80211 tree](https://github.com/openbsd/src/tree/65596250e06438442ce07bad87e5ba129229607d/sys/net80211)
of the same commit for later stages. The reused scope is AX200 family-22000
context self-load, command/RX transport, NVM and passive-scan commands. AX210
gen3/PNVM/IML, data TX queues, association/security and the firmware debug
monitor stay out. iwx carries ISC-style and Intel dual BSD/GPL notices (BSD terms
selected); later net80211 reuse must retain each reused file's notice. Reset/BISR
ordering and bootstrap DMA-span admission also follow Linux v6.12 iwlwifi PCIe
transport files (BSD-3-Clause selected), reimplemented without vendored code.
There is no OpenBSD or Linux compatibility layer. `kernel/wifi/NOTICE` records
the notices and local changes.

Firmware is linux-firmware `20260309`, fetched only through the owner's cache
prefixes for the two pinned commits. The image carries the unmodified bytes at
`firmware/ax200-wifi/iwlwifi-cc-a0-77.ucode`, with the complete
[Intel licence](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENCE.iwlwifi_firmware)
and WHENCE provenance. The filename is an image choice, not a firmware ABI.

| File relative to firmware root | Bytes | SHA-256 |
| --- | ---: | --- |
| `intel/iwlwifi/iwlwifi-cc-a0-77.ucode` | 1,368,096 | `d23a382871e6e638957845b47424ab98dca5e68e022ab73eeeebb0b6f751d7e9` |
| `LICENCE.iwlwifi_firmware` | 2,046 | `16d5040c7cf851fc693b7542e20870935b99802533ea1bfd231c377a2305e5c5` |
| `WHENCE` | 421,898 | `b763b1df1341d86664e8e881f5214333d5f904c31e8cc58bb5302774f0369630` |

The version TLV reports `77.f39cc7f9.0`; the readable label says
`release/core74::f39cc7f9` and WHENCE says `74.f39cc7f9.0`. The driver uses the
version TLV. Redistribution requires the notices and disclaimer, forbids
endorsement and reverse engineering, and carries the licence's patent-grant
condition; it is separate from the [Bluetooth firmware](ax200-bluetooth.md) licence.

## Resources and ownership

The driver follows [PCI claims](pci.md), [DMA allocation](../kernel/memory.md)
and the [SMP worker contract](../kernel/smp.md): it reserves, maps and
preallocates on the BSP with IF=0 before AP startup, and a dedicated BSP worker
with IF=1 owns firmware waits and parsing. IRQ entry only acknowledges and
publishes bounded work. It uses one MSI-X entry; there is no INTx fallback.
Queue indices, response lengths, generations and command versions are checked.

Sizes assume separate 4 KiB allocations, 512 RX buffers and 256 descriptors
per queue; RX buffers need 4 KiB alignment because descriptor low bits encode
the buffer index. Firmware entries are the sum of individually page-rounded
sections.

| Allocation | Page-rounded bytes |
| --- | ---: |
| RX free/used/status rings plus 512 × 4 KiB buffers | 2,109,440 |
| One command queue: descriptors, inline commands, byte-count table | 155,648 |
| Context plus ICT | 8,192 |
| One serialized large-command buffer | 4,096 |
| Firmware LMAC: 14 sections | 380,928 |
| Firmware UMAC: 15 sections | 401,408 |
| Firmware paging: 19 sections | 589,824 |
| **Total** | **3,649,536** |

Stage 2 measured exactly this DMA total natively, with one command queue; raw
firmware storage, response scratch, controller state, mappings and scan
results are additional. The ten-TX-queue layout of the reference and its
optional firmware monitor (up to 64 MiB) are not allocated.

A timeout never returns submitted DMA to the allocator. When stop or reset is
uncertain, the driver keeps the claim, DMA and late-IRQ context until reboot;
the radio's failure leaves wired networking usable.

The ThinkPad's power-on PCI state (read only, 2026-10-18) had Mem/BME off, D0,
a 64-bit non-prefetchable BAR0 and a disabled 16-entry MSI-X table at
BAR0+`0x2000` (PBA `0x3000`). Bare metal additionally needed the upstream root
port's bus mastering enabled; PCI now owns verified ancestor BME activation
before endpoint mastering ([stage-2 record](../development/experiments/ax200-passive-scan.md)).

## Limits

- Live hardware-rfkill transitions are unqualified on this ThinkPad
  ([technical debt](../technical-debt.md#ax200-hardware-rfkill-qualification)).
- Native idle and BSP worker cost are unmeasured
  ([technical debt](../technical-debt.md#wi-fi-idleworker-cost-baseline)).
- Simultaneous Bluetooth use and shared-radio coexistence are unchecked.

# AX200 Wi-Fi controller

WPA2-Personal is under qualification; the previously qualified open-link path
remains the delivered association support. No native WPA2 success is recorded.

The ThinkPad's Intel AX200 (`03:00.0 8086:2723 rev 1a`, subsystem `8086:0080`)
is the first [Wi-Fi](../wip/wifi.md) target. Stages 1–3 deliver firmware startup,
passive scan and one explicit legacy 2.4 GHz OPEN or WPA2-Personal link through the
[native Wi-Fi interface](../interfaces/wifi.md). The driver is opt-in
(`wifi.ax200=1`, image option `WIFI_AX200=1`); stock spaces receive no radio
rights unless configured. wlan0 has independent addressing/DHCP and immutable
packet scopes; Ethernet retains the configured default/DNS/management roles.
Native evidence is in the [stage-1](../development/experiments/ax200-resource-closure.md),
[stage-2](../development/experiments/ax200-passive-scan.md) and
[stage-3](../development/experiments/ax200-open-ap.md) records.

## Reference source and firmware

The driver in `kernel/wifi` is a native port guided by OpenBSD commit
`65596250e06438442ce07bad87e5ba129229607d`:
[iwx transport](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwx.c),
[registers](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwxreg.h)
and [structures](https://github.com/openbsd/src/blob/65596250e06438442ce07bad87e5ba129229607d/sys/dev/pci/if_iwxvar.h)
(CVS 1.230/1.75/1.50), with the
[net80211 tree](https://github.com/openbsd/src/tree/65596250e06438442ce07bad87e5ba129229607d/sys/net80211)
of the same commit for bounded management/frame conversion. The reused scope
is AX200 family-22000 context self-load, command/RX transport, NVM/scan, one data
TX queue, CCMP key commands and legacy station management. AX210 gen3/PNVM/IML and SAE/PMF key
management and the firmware debug monitor stay out. iwx carries ISC-style and Intel dual BSD/GPL notices (BSD terms
selected); each reached net80211 notice is retained. Reset/BISR
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

Current native status on stage-3 image `6bbe8ea9`:

| Resource | Measured/configured value |
| --- | --- |
| Retained DMA | 4,329,472 bytes |
| RX entries / buffers | 512 / 512, 4 KiB each |
| Command entries / queues | 256 / 1 |
| Data TX entries / queues | 256 / 1 |

RX buffers are slices of one owned 2 MiB region; page-aligned firmware sections
are slices of one firmware DMA allocation. These do not consume a VM range per
buffer/section. Station/data backing is preallocated before AP startup. Raw
firmware bytes, parser/response scratch, controller state and scan results are
additional. The reference's extra TX queues and firmware monitor are not allocated.

A timeout never returns submitted DMA to the allocator. When stop or reset is
uncertain, the driver keeps the claim, DMA and late-IRQ context until reboot;
the radio's failure leaves wired networking usable.

The ThinkPad's power-on PCI state (read only, 2026-10-18) had Mem/BME off, D0,
a 64-bit non-prefetchable BAR0 and a disabled 16-entry MSI-X table at
BAR0+`0x2000` (PBA `0x3000`). Bare metal additionally needed the upstream root
port's bus mastering enabled; PCI now owns verified ancestor BME activation
before endpoint mastering ([stage-2 record](../development/experiments/ax200-passive-scan.md)).

## Open-link behavior

The BSP worker owns station setup, firmware waits and DMA. Runtime never allocates
new DMA. The network worker exchanges bounded copied Ethernet frames (MTU 1500);
IRQ handling only acknowledges and wakes the owner. One explicit SSID/BSSID/channel
profile uses Open System authentication and association. OPEN supplies no
encryption; neither profile actively probes or roams. WPA2-Personal adds a private userspace RSN
worker, hardware CCMP with host replay checks and same-target ordinary-loss
recovery; [stage-4 native qualification](../wip/wifi-wpa2-personal.md) is pending.
Only legacy 20 MHz, non-QoS, nonaggregated and unfragmented traffic is supported.

Live firmware LAR/MCC selects valid 2.4 GHz channels, including 12/13 when permitted;
TX additionally needs ACTIVE permission and qualified platform/power/SAR/rfkill
checks. Channel 14 remains passive-only until CCK-only station operation is
qualified. Its profile replaces NVM regulatory defaults; hardware band/antenna bounds
remain. Missing/malformed profile data uses a logged passive-only1–11 fallback
and denies TX. An explicit boot country is a hint, never an override or MCC setter;
its channel mask can narrow the firmware set. No 5 GHz channel is admitted.
Country notices refresh the profile during a running station; bounded notice
bursts can retire the link but do not alone quarantine the controller. Ordinary
ERP/slot/preamble updates modify the MAC context, and other BSSIDs cannot exhaust
the target-confirmation table. Completed/late TX responses with no remaining
retirement work are drained as no-ops; malformed transport replies still fail closed.
Wi-Fi leases obey the [connected-route and address-collision safeguards](network-interfaces.md#wi-fi-lease-admission);
[network trust profiles](network-interfaces.md#network-trust-profile-direction)
remain a future policy.

Final owning lease close, cancellation, AP loss or revocation retires the link,
clears its address incarnation and invalidates flows. A cleanly drained controller
allows a new explicit connection without reboot; uncertain retirement retains
DMA and disables Wi-Fi until reboot. READ-only observations cannot keep a link
alive. The [interface](../interfaces/wifi.md#connection-attempts-and-leases) defines
rights, capture, deadlines and startup lineage.

## Limits

- Live hardware-rfkill transitions are unqualified on this ThinkPad
  ([technical debt](../technical-debt.md#ax200-hardware-rfkill-qualification)).
- Native idle and BSP worker cost are unmeasured
  ([technical debt](../technical-debt.md#wi-fi-idleworker-cost-baseline)).
- Native direct-control/observer/CUT edge cases, RF on 12/13 and quantitative
  association/load costs remain [qualification follow-ups](../technical-debt.md#ax200-open-link-qualification-follow-ups).
- Link speed is unreported; payload correctness is not a throughput claim.
- Simultaneous Bluetooth use and shared-radio coexistence are unchecked.

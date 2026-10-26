# AX200 Wi-Fi controller

The ThinkPad's Intel AX200 (`03:00.0 8086:2723 rev 1a`, subsystem `8086:0080`)
is the first [Wi-Fi](../wip/wifi.md) target. Stages 1–4 deliver firmware startup,
passive scan and one legacy 2.4 GHz OPEN or WPA2-Personal/CCMP link through the
[native Wi-Fi interface](../interfaces/wifi.md). The driver is opt-in
(`wifi.ax200=1`, image option `WIFI_AX200=1`); stock spaces receive no radio
rights unless configured. wlan0 has independent addressing/DHCP and immutable
packet scopes; Ethernet retains the configured default/DNS/management roles.
Native evidence is in the [stage-1](../development/experiments/ax200-resource-closure.md),
[stage-2](../development/experiments/ax200-passive-scan.md) and
[stage-3](../development/experiments/ax200-open-ap.md) and
[WPA2 qualification](../development/experiments/ax200-wpa2.md) records.

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
recovery, with the [accepted qualification limits](https://git.internal/PyxisOS/pyxis-os/issues/1285).
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

For OPEN, final owning lease close, cancellation, AP loss or revocation retires the link,
clears its address incarnation and invalidates flows. A cleanly drained controller
allows a new explicit connection without reboot; uncertain retirement retains
DMA and disables Wi-Fi until reboot. READ-only observations cannot keep a link
alive. The [interface](../interfaces/wifi.md#connection-attempts-and-leases) defines
rights, capture, deadlines and startup lineage.

## WPA2-Personal

The private leaf worker links the reached `src/rsn_supp`, RSN IE/KDE and internal
crypto closure from wpa_supplicant 2.12 (BSD-3-Clause), pinned in the ports recipe:
`wpa_supplicant-2.12.tar.gz`, SHA-256
`08e23937e16d0155e55cab2b51f51fbe10d80a1aa91c4e15442645059b737ef6`.
The mirror-only source and reached-file notices are retained; detached-signature
signer trust is not claimed. There is no generic daemon, socket driver, control
socket, D-Bus or config writeback. Native adapters supply owned events and
`wait_many`, absolute deadlines, declared random/clock grants, allocation and
explicit key-material erasure. No entropy fallback or new crypto construction.

The profile is RSN/WPA2-PSK with CCMP-128 pairwise/group keys. Optional-PMF
WPA2/WPA3 transition APs are supported as WPA2; required-PMF and SAE-only APs are
refused. PMF is not negotiated, so unprotected deauthentication remains spoofable.
No WEP/TKIP, enterprise EAP, roaming, FT, WPS, PMKSA reuse or silent downgrade.
The existing legacy 20 MHz and regulatory/platform restrictions still apply.

`wifi connect --profile FILE` prompts without echo, outside shell parsing, and
seals a volatile anonymous credential FILE. Credentials are never profile,
argv/environment, status or log text. The configured session owns the leaf
worker and private SUPPLICANT-only opener; ordinary CONNECT holders receive no
key-management authority. Final item release erases credential backing; reboot
or session end requires re-entry. Stored credentials remain in the parked
[credential-service direction](../wip/credentials-and-biometrics.md).
Remote is unencrypted TCP: no echo is not encryption. Chronium accepted prompt
entry only on the trusted wired native-test LAN (2026-10-21), not as secure
remote provisioning.

Association alone does not open the data port. Confirmed PTK/GTK installation
and handshake completion gate ordinary data/DHCP; generation-bound EAPOL remains
available. CC77 uses checked `SEC_KEY` v1, firmware CCMP and host per-key/per-TID
48-bit PN checks. Same-key repeats do not reinstall or reset host PN. Rejected
protected frames are counted drops; uncertain authenticated key state closes the
port. See the [auth/credential ABI](../interfaces/wifi.md#wpa2-auth-and-credential-boundary).

Ordinary AP loss closes the port, drains keys and clears address/flows while the
same owning lease retains network intent and its sealed credential. Recovery
uses a fresh association/handshake/key state, never another target. A worker loss
also drains old keys before the session creates a fresh broker/worker with
1–30 s backoff; waiting for replacement is bounded at 60 s. Final close/CUT or
session end stops recovery; uncertain key/group cleanup retains ownership and
fails closed. Ethernet remains usable. Native AP-loss recovery passes; native
held-link worker-death recovery remains unqualified.

## Limits

- Native GTK rekey, held-link worker-death recovery, independent over-air
  confidentiality/replay and firmware PN behavior remain unqualified, accepted
  for stage-4 closure (chronium, 2026-10-22); consequences and revisit points are
  in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1285).
- Live hardware-rfkill transitions are unqualified on this ThinkPad
  ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1282)).
- Native idle and BSP worker cost are unmeasured
  ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1288)).
- Native direct-control/observer/CUT edge cases, RF on 12/13 and quantitative
  association/load costs remain [qualification follow-ups](https://git.internal/PyxisOS/pyxis-os/issues/1284).
- Link speed is unreported; payload correctness is not a throughput claim.
- Simultaneous Bluetooth use and shared-radio coexistence are unchecked.

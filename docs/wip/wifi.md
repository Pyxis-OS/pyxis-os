# Wi-Fi

Stages 1–4 are complete: AX200 resource/firmware closure, passive scanning,
legacy open links and WPA2-Personal/CCMP with independent DHCP and verified data.
Implemented behavior is in the [AX200 controller](../devices/ax200-wifi.md) and
[native interface](../interfaces/wifi.md). The [WPA2 qualification record](../development/experiments/ax200-wpa2.md)
separates delivered checks from the [accepted limits](https://git.internal/PyxisOS/pyxis-os/issues/1285).
The [multiple-interface core](../devices/network-interfaces.md) keeps wired
management independent.

Owner order (2026-10-20): **stage 3 is the Wi-Fi item for 0.0.7; after 0.0.7,
AX200 WPA2-Personal → vela RTL8922AE → AX200 WPA3-Personal.** The RTL8922AE
investigation/proposal follows WPA2. AX200 5 GHz and Steam Deck QCA2066 remain
later; the ROG Strix G513IC and low-end target need inventories first.

Owner direction (2026-10-20): future [network trust profiles](../devices/network-interfaces.md#network-trust-profile-direction)
start newly joined networks untrusted, usable without application changes;
their connected routes cannot override another interface and services are not
exposed there. One owner trust decision enables today's Ethernet behavior.
This direction is not implemented. The immediate safeguards rank connected
preference before prefix length and refuse exact lease-address collisions with
another interface's gateway/DNS; overlapping home-network leases are allowed.

## Inspected hardware and driver model

| Target | Evidence | Driver, host work and firmware work |
| --- | --- | --- |
| ThinkPad | `03:00.0`, `8086:2723`, AX200 in the [undocked inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt); USB Bluetooth `8087:0029` is a separate function | Linux [iwlwifi](https://github.com/torvalds/linux/tree/v6.12/drivers/net/wireless/intel/iwlwifi) and OpenBSD [iwx](https://man.openbsd.org/iwx.4). Host software MAC manages station/authentication/association and security; firmware assists scanning and TX rate selection; hardware can perform CCMP. Bluetooth firmware/support does not initialize PCIe Wi-Fi. |
| vela | `02:00.0`, RTL8922AE; Fedora names `rtw89_8922ae` and loads firmware format 4 in the [inventory](../targets/ideapad-slim5-16iph11/ideapad-inventory.txt) | Linux [rtw89](https://github.com/torvalds/linux/tree/v6.12/drivers/net/wireless/realtek/rtw89), PCI ID `10ec:8922`. mac80211 owns station MLME; driver callbacks install keys into security CAM; firmware implements rate adaptation and substantial radio setup. |
| Deck, later | QCA2066, `17cb:1103`, [Deck record](steam-deck.md#hardware-and-what-carries-over) | [ath11k](https://wireless.docs.kernel.org/en/latest/en/users/drivers/ath11k.html), also mac80211-based, with firmware scanning/rate control/key installation. The PCI ID is shared with WCN6855 variants; SoC revision/subversion chooses the QCA2066 profile. |

These are **firmware-assisted SoftMAC**, not interchangeable FullMAC devices
that supply a complete connection service. Linux's [mac80211](https://wireless.docs.kernel.org/en/latest/en/developers/documentation/mac80211.html)
provides host MAC/MLME; offloaded scan, keys or rate control do not remove that
requirement. OpenBSD iwx likewise depends on net80211. Driver support for a
Wi-Fi 6/7 chip does not establish support for all of that standard's modes.

All three use PCIe and device-owned DMA: command/firmware transport and TX/RX
rings, with checked completions and reset/timeout ownership. AX200's inspected
Linux transport tries MSI-X then MSI/INTx; iwx tries one MSI-X vector, MSI, then
legacy interrupts. Inspected rtw89 uses one **MSI or INTx**, not MSI-X. ath11k
uses copy-engine rings, firmware boot/control transports and multiple MSI vectors
with a single-MSI fallback. These facts come from the
[AX200 transport](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/trans.c),
[iwx source](https://github.com/openbsd/src/blob/master/sys/dev/pci/if_iwx.c),
[Realtek PCI transport](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/realtek/rtw89/pci.c)
and [ath11k PCI transport](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/ath/ath11k/pci.c);
none has been exercised in Pyxis.

## Firmware and source reuse

The inspected firmware catalog is linux-firmware `20260309`, commit
`c822cbbb14ce5b8ee1f27346220640ac350bbf34`, already used by the Bluetooth
[asset record](../development/experiments/bluetooth-firmware-readiness/README.md).
Its [WHENCE](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/WHENCE)
assigns separate licenses to these Wi-Fi assets:

| Target | Files and matching requirement | Binary redistribution license |
| --- | --- | --- |
| AX200 | `intel/iwlwifi/iwlwifi-cc-a0-77.ucode` (legacy top-level link). Linux v6.12 accepts API 77; inspected iwx expects package name `iwx-cc-a0-77`. Confirm the loader's format/API before selecting identical bytes under either name. | [LICENCE.iwlwifi_firmware](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENCE.iwlwifi_firmware) |
| RTL8922AE | Native Fedora uses `rtw89/rtw8922a_fw-4.bin`. The v6.12 reference selects format 1 with an unsuffixed fallback; it is **not** a format-4 implementation. Freeze a newer matching driver/firmware pair for vela. | [LICENCE.rtlwifi_firmware.txt](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENCE.rtlwifi_firmware.txt) |
| QCA2066, later | `ath11k/QCA2066/hw2.1/{amss.bin,m3.bin,board-2.bin,Notice.txt}`; confirm the board-data entry and SoC identity, not just the PCI ID. | [LICENSE.QualcommAtheros_ath10k](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENSE.QualcommAtheros_ath10k) |

All permit unmodified binary redistribution subject to accompanying notices and
disclaimers, no endorsement and no reverse engineering. Qualcomm additionally
limits use to its chipsets. Intel/Realtek's patent grants constrain combinations
with the OS to an OSI-approved license; these are not source-code licenses or
unconditional patent grants. Preserve the complete applicable text, WHENCE and
provenance. OpenBSD distributes Intel firmware separately from its base system;
its packaging choice is not a blanket prohibition on binary redistribution.

Inspected iwlwifi and rtw89 files carry `GPL-2.0 OR BSD-3-Clause`; ath11k files
carry `BSD-3-Clause-Clear`. iwx has ISC-style and Intel BSD/GPL alternative
notices. **net80211 is not uniformly ISC:** the inspected files include ISC,
BSD-2-Clause and BSD-3-Clause. Audit the complete chosen closure and preserve
individual notices; a permissive device driver does not license the rest of
Linux mac80211/cfg80211 for import.

| Inspected source, physical lines including comments | Bytes | Lines | Meaning |
| --- | ---: | ---: | --- |
| iwx C file plus register/variable headers | 709,759 | 22,722 | Three files, excluding net80211 and OS dependencies |
| OpenBSD net80211: 18 configured C files + 12 referenced headers | 734,439 | 24,478 | Includes features outside the proposed station subset; excludes generic crypto/kernel glue |
| iwlwifi: eight selected configuration/transport/MAC/rate files | 639,780 | 21,321 | Partial sample, **not** whole-driver size |
| rtw89: eight selected chip/PCI/MAC/rate files | 861,535 | 29,437 | Partial sample, **not** whole-driver size |

Measurements used cached Linux v6.12 (commit
`adc218676eef25575469234709c2d87185ca223a`) and OpenBSD files identified by
CVS revisions: iwx C `1.230`, register header `1.75`, variable header `1.50`;
net80211 input `1.263`, configuration `1.84`, with individual revisions/hashes
in the research manifest. This is an order-of-magnitude warning, not a binary,
RAM or port-effort estimate. Runtime budgets remain unmeasured.
[FreeBSD's approach](https://www.freebsd.org/status/report-2026-01-2026-03/lkpi-wireless/)
reuses permissive Linux drivers through LinuxKPI and native net80211; importing
that compatibility stack is an alternative, not a small prerequisite.

All local source/license retrieval used `repo.internal`: `raw-github` for
`torvalds/linux`, `openbsd/src` and `freebsd/freebsd-src`, and `raw-gitlab` for
the pinned linux-firmware catalog. The initial investigation fetched no firmware
binaries and vendored no sources; its manifests/counts are under
`/shared/build/wifi-investigation/`. Stage 1 later fetched and pinned the AX200
blob; the [controller reference](../devices/ax200-wifi.md#reference-source-and-firmware) records it.
An implementation needs exact source/firmware pins, hashes, a working cached
asset entry and a license audit before image integration; no upstream fallback.

## The stack above the driver

A station still needs bounded management-frame/IE parsing, passive/active scan,
BSS selection, authentication/association, timeouts, sequence/replay handling,
802.11/Ethernet conversion and controlled data admission. Unsupported aggregation
or PHY modes must not be advertised. Roaming, AP mode, enterprise EAP, WEP/TKIP,
monitor/injection mode, mesh and suspend/resume are outside the first profile.

WPA2-Personal needs RSN selection, PMK derivation, the EAPOL four-way and group-key
handshakes, retransmission/replay/reinstallation protection, and ordered pairwise
and group-key installation. EAPOL (`0x888e`) must pass before ordinary data is
allowed. Hardware AES-CCMP does not implement that handshake. Driver-declared
per-key offload must include packet-number/replay semantics; otherwise use a
qualified software CCMP path or refuse, never send plaintext as a fallback.

WPA3-Personal adds SAE authentication, its ECC/bignum/hash requirements, and PMF:
IGTK/BIP, protected management, replay checks and SA Query. Data CCMP offload is
not proof of PMF support. The inspected OpenBSD MAC accepts only Open System
authentication; a native SAE authentication request/result path to the supplicant
and clear PMF/SA Query ownership must be designed before the WPA3 stage. Use the
existing kernel random generator through an
explicit random grant for userspace security work; no predictable fallback.
The [mac80211 crypto contract](https://docs.kernel.org/driver-api/80211/mac80211-advanced.html#hardware-crypto-acceleration)
and [wpa_supplicant driver interface](https://github.com/freebsd/freebsd-src/blob/main/contrib/wpa/src/drivers/driver.h)
show these separate responsibilities.

| Approach | Benefit | Cost/limit |
| --- | --- | --- |
| New minimal native supplicant | Narrow native API and WPA2-only scope | New security state machines and replay/key-install correctness to maintain; not recommended |
| BSD-3-Clause [wpa_supplicant](https://www.w1.fi/wpa_supplicant/) subset | Existing WPA2/SAE implementation; reusable for both chips | Native driver/event/OS/config/crypto adapters, EAPOL transport and grants; it does not supply the software MAC |
| OpenBSD net80211 security in kernel | Integrated WPA2 handshake, CCMP and PMF | Much larger kernel trust boundary; inspected configuration/input implements no SAE authentication, despite recognizing SAE advertisements |

Recommended: adapt the needed iwx/net80211 hardware/MAC pieces, with a trusted
userspace wpa_supplicant subset owning credentials, RSN/EAPOL/SAE and connection
policy; do not run a second kernel four-way handshake. Its
[porting guide](https://www.w1.fi/wpa_supplicant/devel/porting.html) is old (2.5);
audit the current source rather than assuming its example socket/select API is
required. Cached FreeBSD-vendored source reports 2.12; sampled WPA, SAE and SME
files alone contain 7,841, 2,517 and 5,748 lines respectively, not a measured
minimal closure. Reuse audited crypto implementations, with a real native crypto
adapter if needed; no new cryptographic construction or fake POSIX operations.

Regulatory enforcement is separate from supplication. Firmware LAR/MCC selects
the passive 2.4 GHz set, including 12/13 when permitted (owner, 2026-10-20);
missing/malformed profile data falls back to passive-only 1–11, not a country map.
The firmware country profile is authoritative. Association still requires an
explicit boot-init set-once host country hint, valid firmware ACTIVE permission,
hardware/platform power and SAR limits, and rfkill clearance. No default or
inferred host country or `ZZ` policy. The delivered open-link stage has no active
probe or automatic reconnect; [WPA2](../devices/ax200-wifi.md#wpa2-personal) adds same-network
recovery with the held key. An explicit boot channel mask may narrow permissions. AX200 5 GHz and 6 GHz remain
later work; no new band is admitted by the 2.4 GHz mask update.

## Fit to Pyxis and authority

Proposed placement: a kernel BSP-owned hardware/MAC worker owns PCI claims,
firmware parsing/loading, DMA, interrupts and bounded frame/state lifetimes.
Reuse the current [BSP contracts](../kernel/smp.md), checked PCI/MSI-X helpers
and [network worker](../devices/networking.md); exchange owned bounded Ethernet
frames, not borrowed radio DMA. Radio setup must not block Ethernet progress.
A userspace hardware driver would first need safe device/DMA/interrupt delegation
which is not supplied by the [provider model](capabilities-and-providers.md).
Userspace providers fit connection/credential policy; they do not justify raw
MMIO or DMA authority for ordinary programs.

Use declared grants: read-only inventory/status, separately delegated scan and
connection/key-management authority, and only the chosen credential item for the
trusted supplicant. Neither a URI nor possession of ordinary TCP/UDP authority
permits radio configuration. Existing `net` READ must remain observational and
must not trigger a scan. It should add truthful driver/preparation/link status;
nearby SSID/BSSID lists and profile secrets need separate observation authority.
No ABI layout, operation numbers or new scheme are defined here.

[network.lua/session setup](../userland/session-configuration.md) already chooses
one [net0](../devices/net0-selection.md) and DHCP/static policy. Keep profile
selection there, not passphrases in Lua, argv, environment TEXT or logs. Proposed
first credential input is an explicit private, session-held credential FILE,
with no persistence/default credentials; wipe transient secret/key copies on
retirement. The parked [credential service](credentials-and-biometrics.md) later
supplies scoped item authority and at-rest protection; Wi-Fi must not invent a
second vault or claim an unencrypted profile is secure storage. A later store
adapter must explicitly authorize raw-secret use/export or a matching derivation
operation; an item name alone is insufficient.

**Network prerequisite delivered:** the [multiple-interface core](../devices/network-interfaces.md)
provides naming, addressing, routing/DNS, authority and explicit management
binding for independent Ethernet management during Wi-Fi traffic. Radio
association and traffic still need their Wi-Fi stages. Keep wired Remote/log/GDB
independent during radio bring-up. This does not authorize a hidden second
interface inside a driver. Link-up must mean association and required
key/data-port admission, not firmware-ready or a completed scan.

## Qualification and proposed stages

Installed QEMU 10.2.2 `-device help` lists Ethernet models, no Wi-Fi controller.
[mac80211_hwsim](https://wireless.docs.kernel.org/en/latest/en/users/drivers/mac80211_hwsim.html)
is a Linux kernel simulator, not a Pyxis QEMU device. Later QEMU checks can cover
portable parsing/state machines and unchanged Ethernet; they cannot qualify RF,
firmware DMA or a native radio. Actual AX200 results are in the
[stage-2 record](../development/experiments/ax200-passive-scan.md).

Stage 2 used owner-run Fedora/VFIO differential evidence on the isolated AX200
IOMMU group, with host cleanup verified. Native startup then exposed an upstream
PCI bridge BME gate; the PCI-layer fix and subsequent scan qualification are in
the [record](../development/experiments/ax200-passive-scan.md).
The [passthrough guide](../development/thinkpad-nic-passthrough.md) separately
qualifies RTL8111; neither path makes QEMU an emulated radio.

For native radio bring-up retain ThinkPad built-in Ethernet as `eth0`, run
[Remote and UDP log](../development/remote-debugging.md) over it, and use the
[network GDB bridge](../kernel/network-debugger.md) with the matching ELF.
USB NCM supports normal Remote/log but lacks stopped-kernel/fatal transport.
Use bounded diagnostics without credentials or public SSID/MAC dumps. Owner
packets follow `/shared/present/RUNSHEETS.md`; the [stage-1 record](../development/experiments/ax200-resource-closure.md)
links its PCI and wired-baseline packet. Capture the wired baseline before code;
native idle/BSP utilization is unavailable under the accepted stage-1 scope.
Repeat matched controls after each hardware/MAC addition. Radio runs
record AP/security/channel, distance, AC/battery, power-on/warm boot and exact images.

The stages are accepted. Stages 1–4 are complete (2026-10-22),
as described in the [AX200 controller](../devices/ax200-wifi.md) reference. Later stages still need
their own go and interface/ownership review before code:

1. [x] **AX200 resource/asset closure.** Pin the permitted reference subset and matching
   firmware; inspect read-only power-on PCI/PM state and DMA/interrupt requirements;
   record derived resource budgets and the wired baseline. Live NVM/rfkill and
   runtime memory/queue confirmation move to stage 2 (owner, 2026-10-18); native
   idle/BSP utilization is unavailable through production tools.
   [Stage-1 record](../development/experiments/ax200-resource-closure.md): pinned assets and derived budgets,
   native power-on PCI evidence, and nine wired baseline samples complete.
   After this task the owner can: review the real hardware/asset and ownership
   profile before any scan capability is delivered.
2. [x] **AX200 firmware and passive scan.** Power-on and warm readiness, live
   NVM/rfkill confirmation and runtime memory/queue confirmation carried from
   stage 1. Bounded legal channel scanning, cancellation and malformed/completion
   failure cleanup while wired networking remains usable. No credential or data
   service yet. Native known-AP visibility, repeated scans, Ctrl+C/terminal
   drain/recovery and matched wired controls pass. Final restacked image passes
   the native scan/READY check; hardware-rfkill transition remains unqualified
   because F8/Fn+F8 do not assert the line on this target.
   After this task the owner can: see bounded scan results and an explicit failure
   on an unavailable radio, without losing wired Remote.
3. [x] **AX200 open test AP.** One explicit legacy 2.4 GHz link, firmware-selected
   regulatory channels, independent wlan0 DHCP and verified payload. Native
   repeated links, owning aliases/final close, foreground cancellation and
   AP-loss/fresh explicit recovery pass; remaining native checks are
   [recorded](https://git.internal/PyxisOS/pyxis-os/issues/1284).
   Ethernet remains the default/DNS/management path.
   After this task the owner can: exchange verified traffic on the open test AP.
4. [x] **AX200 WPA2-Personal.** [Implemented](../devices/ax200-wifi.md#wpa2-personal)
   and [qualified](../development/experiments/ax200-wpa2.md) with correct/wrong keys,
   scoped protected payload, lifetime checks and automatic same-network AP-loss
   recovery. The corrected native wired comparison confirms the FILE-read fix;
   four security/recovery qualification limits are accepted (chronium, 2026-10-22),
   not relabelled passed. No WPA3/PMF or new band is delivered.
   After this task the owner can: connect to the chosen WPA2-CCMP network.
5. [ ] **vela RTL8922AE.** The [investigation/proposal](rtl8922ae-wifi.md)
   records fresh read-only Fedora inventory (2026-10-22), an exact matching
   firmware file and proposed source reference, and native reuse boundaries.
   The three defaults are accepted (chronium, 2026-10-22); RF-silent
   resource/asset preparation is assigned, with later stages separately authorized.
   Then implement the accepted chip transport and repeat the supported native gates.
   After this task the owner can: use the qualified profiles on vela.
6. [ ] **AX200 WPA3-Personal.** SAE and mandatory PMF end to end; explicit refusal
   instead of security downgrade, including unsupported crypto/offload paths.
   After this task the owner can: use the qualified SAE-only AP profile.

These are large driver/stack integrations, not timing commitments. DMA fencing,
firmware ABI, MAC adaptation, RF restrictions and security correctness dominate
risk; source counts do not predict final RAM or throughput. Roaming, advanced
PHY/aggregation, power saving/coexistence tuning and suspend follow evidence.
Check simultaneous Bluetooth use on AX200; separate transports do not establish
absence of shared-radio/coexistence effects.

### Later machines

Deck QCA2066 stays deferred: its firmware/board selection and ath11k boot/control
closure are distinct work, not reuse of an AX200 loader. Unknown ROG/low-end
adapters need PCI/subsystem IDs, actual driver/firmware and regulatory facts first.
No schedule or driver choice is inferred from the laptop brand.

## Owner decisions (accepted 2026-10-18)

1. **Placement/reference:** kernel hardware/MAC from the permissive
   iwx/net80211 subset, with native worker/lifetime integration and userspace
   connection/security policy; no LinuxKPI import.
2. **Supplication/credentials:** a bounded BSD wpa_supplicant port with native
   grant/event/crypto adapters and ephemeral item-scoped credentials; WPA2 then
   SAE/PMF, no original supplicant or separate vault.
3. **First milestone/qualification:** AX200 cold-ready plus passive legal
   2.4 GHz channels 1–11 without an installed country (amended 2026-10-19),
   intersecting firmware/NVM and any installed policy; explicit country is
   required before active scan/association. Native evidence with Ethernet
   retained for debugging. Later open/WPA2/WPA3 stages retain separate scopes.

The owner also accepted the stages and chip order above, requested multiple
network interfaces, and assigned stage 1 to Codex beta after the multiple-interface plan was
settled. The delivered [reference](../devices/network-interfaces.md) owns that
interface contract; radio implementation retains its separate stage assignments.

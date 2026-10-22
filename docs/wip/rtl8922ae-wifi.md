# Vela RTL8922AE Wi-Fi

**Plan accepted (chronium, 2026-10-22).** Stage 1 is assigned; later stages need
a separate go. Native runs on vela go through the orchestrator. This expands [Wi-Fi stage 5](wifi.md#qualification-and-proposed-stages),
after the delivered [AX200 WPA2 profile](../devices/ax200-wifi.md#wpa2-personal).
The three accepted decisions below define the initial scope.

**Fresh read-only inventory completed, 2026-10-22**, through Tailscale SSH after
chronium approved its check. No known_hosts change or host-check bypass was
used. Only selected sysfs, module/package metadata, firmware files and retained
kernel messages were read; no scan, interface change, driver reload, target file
creation or Pyxis boot occurred. MACs, SSIDs and serials were excluded from the
capture.

## Evidence and matching reference

Code inspection: Pyxis `acb1dd6c5176f2c7d3137e460f96389175b409fd`, userland
`0831b08854e5911fc275c22f9324a77a505e8d88`. No pins or source files changed.

| Field | Fresh Linux observation, 2026-10-22 |
| --- | --- |
| Controller/bus | `02:00.0`, Realtek RTL8922AE, PCI `10ec:8922`, revision `01`; PCIe endpoint, negotiated 5 GT/s x1 |
| Subsystem | Lenovo `17aa:4923` |
| Linux | Fedora 44, kernel/ethtool driver version `7.2.10-200.fc44.x86_64`; driver/module `rtw89_8922ae`, package `kernel-modules-7.2.10-200.fc44.x86_64`; no separate module version reported |
| Interface | `wlp2s0`, operational state up and carrier present; no SSID or address collected |
| Resources | BAR0 I/O 256 bytes; BAR2 non-prefetchable memory 1 MiB; one MSI message enabled, capability up to eight; no MSI-X capability listed |
| Loaded Wi-Fi file | Retained startup log names `rtw89/rtw8922a_fw-4.bin`; installed as `.bin.xz` by `realtek-firmware-20260916-1.fc44.noarch` |
| Reported firmware | Normal/type 1 and WoW/type 3 `0.35.80.3 (8ef4f0cf)`, command version 1; BBMCU/type 64 `0.1.0.0 (7b393818)`, command version 0 |
| Chip profile | CID 71, CV 2, AID 6933, ACV 1, RFE 1 |

Fresh redacted evidence: `/shared/present/vela-wifi-inventory/`
(`fedora-2026-10-22.json`, `interface-2026-10-22.txt`, collector `collect.py`).
The PCI, resource, firmware and chip facts agree with the
[2026-10-14 inventory](../targets/ideapad-slim5-16iph11/ideapad-inventory.txt)
and [target notes](../targets/ideapad-slim5-16iph11/notes.md). These are Linux
observations, not Pyxis readiness or proof that every device sharing the PCI ID
has this profile. Interface carrier does not qualify Pyxis association or traffic.

This is a **discrete PCIe Realtek Wi-Fi controller**, not Intel CNVi/CNVio2.
The Intel host CPU does not turn it into an AX200-compatible device. The matched
[RTL PCI table and BE generation profile](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/rtw8922ae.c)
select Realtek PCI transport, DMA and radio setup. The separate Realtek USB
Bluetooth function and its firmware do not initialize this Wi-Fi function.

### Accepted source/asset reference

- Linux **v6.18**, commit `7d0a66e4bb9081d75c82ec4957c50034cb0ea449`, native
  adaptation of the needed `rtw89` BSD-3-Clause alternative. This is the accepted
  reference, not the revision of vela's currently installed Linux driver.
- linux-firmware `20260309`, commit `c822cbbb14ce5b8ee1f27346220640ac350bbf34`,
  `rtw89/rtw8922a_fw-4.bin`: **1,849,226 bytes**, SHA-256
  `d11927f593c82879bd0437435475d7915a60932374747984b3dc906a23009dea`.
  Source-guided header inspection finds the normal/WoW and CV1 BBMCU versions in
  the fresh inventory. The installed compressed file is 359,472 bytes; its
  decoded content is exactly the 1,849,226-byte file and SHA-256 pinned above.
  This verifies the on-disk candidate against the loaded filename/version, not a
  readback of firmware device memory. The blob also includes another cut's
  BBMCU and RF/BB, NCTL, power and regulatory elements. Preserve it unmodified; it is not an
  Intel TLV blob or a separate board/PNVM file.

The [v6.18 chip file](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/rtw8922a.c#L18)
selects maximum firmware format 4. The old Wi-Fi investigation's v6.12 reference
selects format 1 and is not this loader contract. Chip-cut/security selection,
section bounds and actual command/key behavior still need qualification.

All retrieved bytes came through `repo.internal`; hashes/manifests and header
inspection remain under `/shared/build/wifi-investigation/rtl8922ae-v6.18/`.
The complete reached-file closure is not audited yet. Three reached local
headers exist in the official fixed-ref tree but currently return HTTP 404 from
the mirror: `txrx.h`, `util.h`, `wow.h`. Before implementation, populate:

```text
https://repo.internal/repository/raw-github/torvalds/linux/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/<header>
```

Inspected rtw89 files carry `GPL-2.0 OR BSD-3-Clause`; preserve each reached
notice using the BSD alternative. Linux mac80211 is not covered by that licence:
its [CCMP implementation](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/net/mac80211/wpa.c)
is GPL-2.0-only. No LinuxKPI/mac80211 import is proposed. Firmware keeps the
complete [Realtek licence](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENCE.rtlwifi_firmware.txt)
and WHENCE: binary redistribution, notice/disclaimer and patent conditions are
separate from the driver's BSD source terms.

## What carries over, and what must change

| Area | Reuse and required RTL work |
| --- | --- |
| Authority/lifetime | Keep READ/SCAN/CONNECT and private SUPPLICANT rights, credential FILE/CUT lineage, owned attempts/leases and cancellation. These public contracts are in [the Wi-Fi ABI](../../include/abi/wifi.h); RTL register details require no new application authority. |
| BSP/device ownership | Reuse PCI claims, bounded DMA allocation, IRQ publication and BSP worker ownership. Reimplement BAR2/register/reset handling, descriptor/release-report rings, DMA-address admission and bounded interrupt drain. [RTL PCI transport](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/pci.c#L3322) uses conditional 36-/32-bit DMA and one MSI or INTx; AX200 BAR0/MSI-X is not its profile. |
| Firmware/commands | New multi-firmware/cut parser and download, H2C category/class/function/sequence/ACK and C2H dispatch. [Firmware startup](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/fw.c#L1780) loads the normal and BBMCU suits. Intel context self-load, group/opcode commands and NVM layouts do not carry over. |
| Board/radio setup | New BE eFuse/autoload, cut/RFE, crystal/TSSI/gain, RF calibration and power handling. [Chip operations](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/rtw8922a.c#L2783) require Realtek MAC/PHY/radio commands. Do not dump raw eFuse containing device identity. |
| Native station MAC | Factor the bounded IE/RSN parsing, explicit-target legacy authentication/association, frame conversion and controlled-port behavior already in [AX200 station code](../../kernel/wifi/ax200_station.c). Intel station/MAC/PHY commands and RX-status layouts stay in their backend. |
| Network integration | Reuse generation-scoped Ethernet copies, independent addressing/DHCP, retained-interface flows and explicit wired management. [net Wi-Fi](../../kernel/net/wifi.c) currently hardcodes AX200 driver kinds; add truthful RTL dispatch/identity when implemented. |
| RSN/credentials | Reuse the [native userspace RSN worker](../../userspace/wifi-supplicant/rsn.c), existing crypto/clock/random grants and sealed volatile credential path. No new vault, Linux sockets or second four-way handshake. |
| CCMP hardware boundary | Reuse replay, non-reinstallation, closed-port and uncertain-retirement invariants. Implement RTL key CAM, key completion/removal and PN metadata. Hardware differences below must be qualified before data admission. |

These are factoring candidates, not a ready generic backend. Public
`wifi_prepare`/worker entry points live in
[AX200 firmware code](../../kernel/wifi/ax200_firmware.c), while
[auth-object wakeups](../../kernel/object/wifi_auth.c) call AX200 transport directly.
Factor only the boundaries needed by these two controllers, retaining AX200
behavior and its documented qualification limits.

### Regulatory and key correctness gates

Realtek's firmware REGD element maps countries to power-rule families; Linux also
uses eFuse country, host cfg80211 channel rules and platform SAR/TAS inputs.
See [REGD selection](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/regd.c#L713)
and [SAR handling](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/sar.c).
The power-family map is not a complete allowed-channel database. Intel's live
LAR/MCC authority and passive-only 1–11 fallback are AX200-specific, not RTL
policy. Resolve a pinned native channel-rule input, explicit country,
board/firmware restrictions, platform power/SAR and rfkill before permitting RF.
Missing evidence must not select Linux's unconstrained SAR fallback. If
wireless-regdb is chosen, its exact revision, ISC licence and owner mirror entry
need closure first; no new regulatory parser or default country is authorized.

RTL also changes **TX packet-number ownership**. Its hardware-security-header
path has the [host increment the PN and supply descriptor bytes](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/core.c#L618).
AX200's firmware-owned TX PN cannot be reused. RTL RX decryption success does
not establish host replay validation. The native backend needs checked per-key
PN allocation/exhaustion, authenticated RX metadata, GTK transitions and drain
before reuse/removal. The [security CAM command](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/cam.c#L69)
requests receive ACK, not done ACK: enqueue success alone cannot authorize data.
Establish the actual installation/removal confirmation contract before WPA2
implementation; stop for review if it cannot meet the existing native contract.

## Stages

The fresh read-only inventory is complete. Stage 1 is owner-assigned
(chronium, 2026-10-22); stages 2–4 need their own go.

1. [ ] **Resource/asset closure — assigned (chronium, 2026-10-22).** Complete the source/licence/cache
   closure and firmware hash pin; inspect current PCI/PM/MSI, cut/board and
   platform requirements. Derive bounded DMA/queue backing and capture unchanged
   wired controls before code. Implement only the opt-in preparation/loader
   boundary that can be proved RF-silent; hold firmware/RF disabled if that proof
   is absent. Preserve BSP allocation and quarantine on uncertain DMA ownership.
   **After this task chronium can:** review the actual supported resource/asset
   profile and an honest unavailable result without scanning or losing wired access.
2. [ ] **Firmware readiness and legal passive scan.** Bring up the selected firmware,
   confirm live eFuse/rfkill/board state and readiness on power-on and warm boots.
   Resolve the native regulatory/platform-power inputs before scan/calibration
   can affect RF; then add bounded passive 2.4 GHz scan and cancellation through
   the existing grants. Malformed firmware/frame input and failed stop retain
   unsafe DMA until reboot; never borrow radio DMA into the network worker.
   **After this task chronium can:** see the qualified passive scan results or a
   bounded refusal while wired management stays usable.
3. [ ] **Explicit OPEN test AP.** One chosen legacy 2.4 GHz, 20 MHz, non-QoS,
   nonaggregated/unfragmented station. Implement RTL station contexts/frame
   conversion and controlled data admission; exercise independent DHCP and
   verified payload with the existing interface/routing model. Keep management
   explicitly wired. Qualify owning aliases/final close, Ctrl+C/CUT, AP loss,
   repeated connection and RX/TX pressure without a radio-triggered LAN outage.
   **After this task chronium can:** exchange verified traffic on the explicit
   isolated OPEN test AP, with no claim of confidentiality.
4. [ ] **WPA2-Personal/CCMP parity.** Connect the existing RSN worker to qualified RTL
   key/PN operations; check correct/wrong credentials, EAPOL gating, protected
   payload, replay/non-reinstallation, group-key change, AP-loss recovery,
   worker loss and final retirement. Reuse sealed credentials and scope; never
   downgrade to OPEN. Any unqualified security path needs a specific review
   outcome rather than inheriting AX200's accepted limitations automatically.
   **After this task chronium can:** use the selected WPA2-CCMP profile on vela.

QEMU has no RTL8922AE RF model. It can qualify software/ownership regressions and
unchanged Ethernet, not this device's DMA, firmware or radio. Later native packets
must follow `RUNSHEETS.md` and keep a USB Ethernet management path independent.
CDC-NCM supports running Remote/log; it lacks the RTL8111's stopped-kernel/fatal
transport, and vela has no built-in Ethernet. Keep failure evidence and recovery
explicit. No Pyxis boot, scan, driver unbind, VFIO handoff or platform change is
part of today's read-only inventory task.

## Accepted decisions (chronium, 2026-10-22)

1. **Reference and native boundary:** use the pinned Linux v6.18 rtw89/format-4
   pair above, selecting the BSD source alternative after complete licence/cache
   closure. Native opt-in BSP backend and one MSI for this profile, no LinuxKPI,
   Linux mac80211 or new application API for device-specific commands.
2. **First gate:** resource/asset preparation with no RF; firmware is not started
   unless it can remain RF-silent. No passive scan, calibration or TX permission
   is inferred from AX200 defaults. Resolve RTL channel/platform-power inputs
   before the scan stage, with a review stop if authority or safe defaults remain
   unsettled.
3. **Initial usable profile:** match the delivered legacy 2.4 GHz OPEN-test then
   WPA2-PSK/CCMP scope. Reuse native RSN/grants, qualify Realtek key/PN behavior
   independently, and defer 5/6 GHz, HT/HE/EHT/MLO, WPA3/PMF, AP/monitor/roaming,
   suspend and Bluetooth coexistence. No Wi-Fi 7 capability is claimed by PCI ID.

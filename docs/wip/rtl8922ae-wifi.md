# Vela RTL8922AE Wi-Fi

**Plan accepted (chronium, 2026-10-22).** Stage 1 is qualified. The stage-2
firmware-first cut is implemented for review; native firmware/reset
qualification remains pending. Scan and later stages need a separate go. Native runs on vela go through the orchestrator. This expands
[Wi-Fi stage 5](wifi.md#qualification-and-proposed-stages),
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

Source comes from the commit archive through the cache (not raw single-file
URLs):

```text
https://repo.internal/repository/raw-github/torvalds/linux/archive/7d0a66e4bb9081d75c82ec4957c50034cb0ea449.tar.gz
```

The complete gzip/tar is **255,333,425 bytes**, SHA-256
`2617efd103533b57613d16f144ceea575b1a6b890ce5e16201b1d254e2b7b33c`.
Only `drivers/net/wireless/realtek/rtw89/` and
`LICENSES/preferred/BSD-3-Clause` were extracted. All 94 C/header files offer
the BSD-3-Clause alternative; all quoted local includes resolve within the
extracted directory, including `txrx.h`, `util.h` and `wow.h`. The 36 earlier
cached source files match the archive byte-for-byte. No upstream download or
Linux compatibility header is needed for a native adaptation.

Archive/per-file manifests, licence checks and firmware inspection remain under
`/shared/build/wifi-investigation/` (`rtl8922ae-v6.18-archive-manifest.json`,
`rtl8922ae-v6.18-source-closure.json`, and the extracted
`linux-v6.18-rtl8922ae-reference/`). The firmware licence and WHENCE were also
retrieved through the fixed-commit firmware mirror; their manifest is in
`rtl8922ae-firmware-20260309/` there.

The rtw89 C/header files carry `GPL-2.0 OR BSD-3-Clause`; preserve each reached
notice using the BSD alternative. Linux mac80211 is not covered by that licence:
its [CCMP implementation](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/net/mac80211/wpa.c)
is GPL-2.0-only. No LinuxKPI/mac80211 import is proposed. Firmware keeps the
complete [Realtek licence](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/c822cbbb14ce5b8ee1f27346220640ac350bbf34/LICENCE.rtlwifi_firmware.txt)
and WHENCE: binary redistribution, notice/disclaimer and patent conditions are
separate from the driver's BSD source terms.

### Stage 1 audit boundary

Source/asset closure, CPU-only preparation and unchanged wired controls are
qualified. Operational firmware/radio readiness remains a later-stage gate.

Linux's PCI probe enables bus mastering before chip-information setup
(`pci.c:3238–3244,4754–4760`). Even its eFuse setup calls
`rtw89_mac_partial_init(false)` (`core.c:6087`): this enables HCI DMA,
performs PCI preinit and downloads normal firmware (`mac.c:4045–4069`).
BE preinit publishes ring addresses and enables firmware/global DMA
(`pci_be.c:370–387`); download enables WCPU and waits for FreeRTOS readiness
(`fw.c:1827–1853`). None of that is a preparation-only path.

Stage 1 stopped before it: retained PCI/configuration metadata and CPU-only
inspection of the immutable firmware, with no BAR sizing or MMIO, PCI command/MSI
writes, DMA allocation/publication, firmware execution, calibration or scan.
Live cut/eFuse, secure-boot state, board selection, rfkill and operational
readiness remain unknown. This initiates no radio operation; it does not prove
that inherited firmware from a warm boot has already stopped. Do not report
physical RF silence or a halted device without a proven handoff.

The reference defines 13 TX and two RX channels (`txrx.h:632–658`). Its ring and
RX-buffer allocation totals 6,704,128 bytes before metadata, packets, firmware
and padding (`pci.c:3508–3740`, `pci.h:1105–1111,1447–1453,1514–1519`). This is
source-derived Linux allocation arithmetic, not a hardware minimum or a native
budget. CPU-only preparation needs no device-visible ring backing; later full
physical DMA spans must fit the chosen addressing limit before encoding.

### Stage 1 delivery status

Stage 1 qualified the CPU-only boundary: no device writes, claim, worker, DMA or
firmware execution. The six interleaved native cohorts and their USB-wired
controls are in the [stage-1 record](../development/experiments/rtl8922ae-preparation.md).
Those guarantees apply to the recorded stage-1 binary. The stage-2 first cut
below extends the same opt-in option to explicitly authorized firmware startup.
Images without it keep the AX200/default path; selecting both backends refuses
activation. Only opt-in images include the pinned RTL asset and complete notices.

The allocation-free CPU inspector remains separate from activation. It checks
container/image/section/MSS spans, accepts CE/shared spans and keeps unknown
versions/elements opaque. The activation planner additionally requires the
successfully read live cut/security facts and a supported normal-image/key
profile; inspection alone never authorizes a download. No RTL network interface
or WIFI_READY publication is added by the firmware-first cut.

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

## Stage 2: firmware transport before scan

**Decided (chronium, 2026-10-22); first cut assigned, not implemented.**
Inspected Pyxis `454d2504` (stage 1 merged) and the pinned v6.18 source above.
Stage 2 remains incomplete until legal passive scan is qualified.

### Inspected boundary

The reference does not establish RF-silent firmware startup. Chip power-on
programs WLAN RF/analog/PLL controls, enables CMAC TX/RX and response generation,
and releases baseband reset (`rtw8922a.c:329–408`). Those operations precede
`partial_init(false)` and normal firmware execution (`core.c:6087,6185–6223`,
`mac.c:4045–4069`). `include_bb=false` skips later baseband preparation and
BBMCU download; it does not keep the radio in reset. These writes establish an
RF-capable path, not evidence that a packet was emitted. Conversely, no host
scan or TX request is not proof of silence from opaque firmware or inherited
warm state. FreeRTOS-ready proves firmware startup, not RF inactivity.

Linux initializes/polls GPIO9 rfkill only after this powered probe
(`core.c:6122–6136,6370–6382`). Reading that GPIO is not a demonstrated early
physical interlock. Full initialization later requests DACK/RXDCK; channel
changes can request TSSI scan calibration (`rtw8922a.c:2067–2084,2130–2141`).
The empty scan-calibration hook does not make the whole scan path RF-silent.

Ownership is a separate gate. BE preinit has DMA-idle polls, but also clears
indices using allocated ring state and later enables DMA
(`pci_be.c:162–220,353–406`). Its shutdown helper alone is not a halt proof.
Linux recovery may toggle DMA back on after an idle failure
(`pci_be.c:519–534`); that is not a native failure-unwind policy. The accepted
standard PCI reset below establishes the boot handoff and terminal stop.

### Decided (chronium, 2026-10-22)

1. **Firmware power-up permission:** explicitly authorize an RF-capable
   bootstrap in the opt-in qualification image. This replaces the earlier
   firmware-silence condition for stage 2; stage 1 keeps its qualified CPU-only
   boundary. Do not claim physical RF silence. The first cut sends no host scan,
   association, packet/offload or calibration requests and omits BBMCU/full
   radio initialization. Normal firmware execution and the necessary power
   sequence still have unqualified RF effects. Native boots remain separately
   scheduled through the orchestrator with wired management.
2. **Transport admission and failure: one MSI, checked 36-bit DMA,
   fixed reference queue capacities and one startup attempt per boot.** Use the
   pinned 13-TX/two-RX layout initially rather than assume omitted queues are safe
   when global DMA starts. The source-derived backing above is not the final
   native allocation: count alignment, metadata and command staging explicitly
   before allocation, with no runtime growth. Check every full physical span and
   encoded descriptor slice; refuse unsupported BAR/MSI/cut/security state or
   unavailable backing. Do not silently fall back to INTx, different firmware,
   a reduced queue profile or repeated reset/download attempts. Reusing the
   existing [BSP allocation/worker contract](../kernel/smp.md) and
   [DMA lifetime](../../include/kernel/mm/dma.h), retain published backing and
   uncertain device ownership until reboot.
3. **First delivery: normal firmware readiness and checked terminal reset only; scan
   remains a subsequent review point within stage 2.** Report live cut/security,
   checked eFuse/board facts and firmware readiness without exposing raw identity
   or claiming scan readiness. Keep WIFI_READY clear, SCAN_START/CONNECT_START
   unavailable and no RTL data interface published in this first cut. Before the
   passive-scan cut, settle the explicit country, pinned native channel-rule
   source, board restrictions, platform SAR/power inputs and rfkill semantics
   described above. Unknown, malformed and validly absent inputs must be
   distinguished; neither eFuse autoload failure nor Linux's maximum-power SAR
   fallback becomes native permission. No default country, regulatory parser
   or calibration permission is added by this decision.

The single-attempt startup limit is for this bring-up cut, not the everyday
recovery contract. **Revisit before Wi-Fi on vela is everyday-usable:** implement
and qualify a real retry/reset recovery path with proven DMA ownership and
command retirement. Chronium prefers proven recovery over requiring a reboot
for every recoverable failure. The present cut does not authorize blind retries
or reuse of backing after an uncertain stop.

### Ordered first cut

1. Reserve the endpoint and complete the accepted standard PCI reset before
   claiming the device, sizing BAR2 or publishing DMA. Disable IO/MEM decoding
   with readback, size/map BAR2 and prepare the disabled MSI. Re-enable only the
   required memory decoding before further MMIO. All-ones reads, unsupported
   reset/power state or unproved reset completion refuse activation. An attempted
   reset or uncertain handoff retains ownership and never restores inherited
   bus mastering.
   **After this cut chronium can:** inspect a transport with DMA confirmed
   stopped or a precise handoff refusal, without any claim of firmware readiness.
2. Preallocate fixed pools on the BSP before AP startup; use a dedicated BSP
   worker for power/download waits and parsing, with bounded IRQ publication.
   Read live cut, execute the approved power-on once and check security-selector
   eFuse before selecting the normal image/key; never copy Fedora's values.
   Download normal firmware, observe readiness, then inspect board/PHY eFuse
   facts, mask interrupt delivery, perform Linux's PCI DMA pre-deinit and MAC
   power-off, then perform the checked terminal reset. Security selection needs
   powered hardware; full board
   inspection follows firmware startup (`core.c:6087–6099`). Bound header/section
   transfers and retain published command/DMA storage through the terminal
   reset and until reboot. Reject
   invalid indices, lengths, versions and C2H records without borrowing DMA into
   another worker. Linux's ignored security-read failure and five download
   retries (`mac.c:1494–1496`, `fw.c:1866–1878`) are not inherited. A timeout or
   uncertain stop masks delivery where possible and quarantines claim, buffers
   and late-IRQ state until reboot. There is no automatic retry.
   **After this cut chronium can:** see a qualified firmware-ready/stopped result
   or bounded failure while wired management remains usable. It does not enable
   scan, association or data traffic and does not complete stage 2.

### Standard PCI reset handoff

**Decided (chronium, 2026-10-22):** use a standard PCI function reset at boot
handoff and terminal stop. Prefer PCIe FLR when Device Capabilities bit 28
advertises it; otherwise admit a PM D3hot-to-D0 reset only when PMCSR
NoSoftReset is clear. Check capability spans, power state, request completion
and configuration readbacks before proceeding. Neither advertised support nor
writing the request alone proves completion. A failed attempt does not authorize
another reset method or firmware-download retry in this cut. An inherited D3hot
function first wakes to D0; when NoSoftReset is clear that normalization itself
resets the function before a selected FLR. D1/D2 are unsupported in this cut.
If neither reset
method is supported, refuse activation before DMA publication.

The stage-1 evidence did not retain either actual support bit. Reset selection
therefore remains a live observation, not an established unsupported profile.
The existing reset-free PCI probe and retained-D3hot wake interfaces keep their
NoSoftReset requirement and semantics; this separately authorized reset path
does not relax them. Native reset behavior and firmware handoff still require
the separately scheduled orchestrator qualification.

After a destructive or uncertain attempt, keep the endpoint reserved, bus
mastering disabled where possible and published DMA backing retained until
reboot. Successful terminal reset also retains the first cut's fixed backing;
there is no reuse, automatic retry or everyday recovery claim.

### First-cut implementation status

- [x] Checked PCI reset and retained ownership, fixed 36-bit DMA pools, disabled
  MSI preparation and BSP firmware worker; immutable normal-image/key transfer,
  bounded RX-envelope drain, board/PHY status observations, DMA pre-deinit,
  MAC power-off and terminal reset.
- [x] Target build and QEMU/GDB absent-device boundary; WIFI_READY stays clear
  and SCAN_START remains unavailable.
- [ ] Native live reset selection, firmware readiness/stop, warm handoffs and
  independent wired controls. The nested control series has lower candidate
  medians and substantial variation; its cause is unresolved, not a performance
  pass. See the [qualification record](../development/experiments/rtl8922ae-firmware-first.md).

The worker returns after its one terminal result. It publishes no network
interface, never lends DMA to another worker and keeps allocated pools/late-IRQ
context through reboot after publication. An unsupported initial power state,
security/firmware profile or unconfirmed reset produces a truthful refusal.
The stop path follows the pinned Linux BE PCI pre-deinit and RTL8922A MAC
power-off sequences before the terminal standard reset. A startup fault does
not skip shutdown; the terminal reset gets a fresh bounded deadline even if
MAC shutdown fails. All MMIO access ends before that reset; no private vendor
stop probe is used. `WIFI_STATUS.boot` reports validity-tagged reset/cut/security/
firmware/board observations and independent completed-action bits, without
duplicate kernel log lines. Reserved firmware statuses 8/9 keep polling as in
Linux; ring consumers use only bits 27:16 before their capacity check. PM reset
allows PME/Data_Select to reset or survive, restoring/checking the retained
configuration afterward.

The generic MSI helper admits both address widths and optional standard
per-vector masks, including this card's advertised mask-capable profile. It
prepares all supported messages masked and enables only message zero. BAR
sizing, insufficient BAR2 extent, mapping, MSI layout/programming and command
readback failures have separate status details.

Two deliberate differences from the Linux reference remain: power-on requires
MAC state zero, whereas Linux rejects only active state one and its wrapper
powers off/retries after failure (`mac.c:1484–1488,1517–1527`). This first cut
keeps its one-attempt policy, so other inherited states refuse startup. After
the off handshake and scoreboard write, it additionally polls for MAC state
zero; Linux's power-off sequence does not make that final observation. A
timeout therefore reports an unconfirmed MAC stop even if the subsequent PCI
reset succeeds. If both startup and shutdown fail, status retains the startup
failure detail; independent completed-action bits show which stop steps
confirmed. These differences still need native observation. Stage 2 remains
incomplete until separately authorized scan work and qualification are delivered.

### Qualification of the first cut

Capture exact-main controls before implementation: opt-out and opt-in boots,
actual retained backing, boot/worker behavior and repeated USB-wired TX/RX using
matched image contents and settings. Repeat those controls after implementation;
report ranges and any regression without attributing a speedup to radio work.
QEMU/GDB can inspect allocation/unwind, dispatch and unchanged AX200/Ethernet
paths; it cannot qualify RTL MMIO, DMA or physical RF effects.

A separately approved vela packet must cover power-on and Fedora/Pyxis warm
handoffs, truthful live cut/security selection, firmware readiness, command/RX
completion, stop/quarantine and unchanged independent USB-wired management.
Record which refusal/timeout paths were actually observed; source inspection
alone does not qualify hardware failure recovery. Do not provoke DMA failures
on vela merely to fill a checklist. CDC-NCM supplies running Remote/log, not
fatal/stopped-kernel GDB: keep captured logs, screen/console failure evidence and
an orchestrator-controlled recovery boot. No claim of passive scan or RF silence
follows from successful firmware readiness; that requires its own resolved
contract and native qualification.

## Stages

The fresh read-only inventory and stage-1 CPU-only qualification are complete
(2026-10-22). Stage 2's firmware-first cut is assigned. Passive scan and stages
3–4 need a separate go.

1. [x] **Resource/asset closure — qualified 2026-10-22.** Complete the source/licence/cache
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
explicit. The fresh Fedora inventory involved no Pyxis boot, scan, driver
unbind, VFIO handoff or platform change. Native preparation checks remain
separate and go through the orchestrator.

## Accepted decisions (chronium, 2026-10-22)

1. **Reference and native boundary:** use the pinned Linux v6.18 rtw89/format-4
   pair above, selecting the BSD source alternative after complete licence/cache
   closure. Native opt-in BSP backend and one MSI for this profile, no LinuxKPI,
   Linux mac80211 or new application API for device-specific commands.
2. **First gate:** stage-1 resource/asset preparation initiates no RF operation.
   The stage-2 decision above explicitly permits RF-capable firmware bootstrap
   without a silence claim. No passive scan, calibration or TX permission is
   inferred from AX200 defaults. Resolve RTL channel/platform-power inputs
   before the scan stage, with a review stop if authority or safe defaults remain
   unsettled.
3. **Initial usable profile:** match the delivered legacy 2.4 GHz OPEN-test then
   WPA2-PSK/CCMP scope. Reuse native RSN/grants, qualify Realtek key/PN behavior
   independently, and defer 5/6 GHz, HT/HE/EHT/MLO, WPA3/PMF, AP/monitor/roaming,
   suspend and Bluetooth coexistence. No Wi-Fi 7 capability is claimed by PCI ID.

# Vela RTL8922AE Wi-Fi

**Plan accepted (chronium, 2026-10-22).** The 0.0.11-only placement decided on
2026-10-26 is superseded: eligible for 0.0.10 if ready before its release,
otherwise 0.0.11 (chronium, 2026-10-28). Stage 1 and the stage-2
firmware-first/checked-stop cut, request-driven PHY initialization/RFK/checked stop and one-channel DBCC
passive discovery, including two sequential same-channel requests with independent
FILEs, and repeated 1+6 firmware sweeps are native qualified on vela. Returned
hopping records have only demonstrated channel-1 attribution; channel 6,
complete admitted-mask discovery, physical rfkill response and radiated-power
behavior remain unqualified. Native
runs on vela go through the orchestrator. This expands
[Wi-Fi stage 5](wifi.md#qualification-and-proposed-stages), after the delivered
[AX200 WPA2 profile](../devices/ax200-wifi.md#wpa2-personal).
The accepted decisions below define the initial scope; this track must preserve
the released status prefix; release eligibility does not authorize another task.

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
policy. The accepted scan contract below selects wireless-regdb's world domain
`00` when no country is configured, or its configured-country rules otherwise.
Board/firmware restrictions, platform power/SAR and rfkill still constrain RF.
Missing evidence must not select Linux's unconstrained SAR fallback. The pinned
regdb archive and licence/cache closure below precede implementation.

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

**Decided (chronium, 2026-10-22); firmware-first cut implemented and native
warm-reboot qualified (2026-10-23).**
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
   passive-scan cut, settle the effective country/domain, pinned native
   channel-rule source, board restrictions, platform SAR/power inputs and rfkill semantics
   described above. Unknown, malformed and validly absent inputs must be
   distinguished; neither eFuse autoload failure nor Linux's maximum-power SAR
   fallback becomes native permission. This first-cut decision added no scan
   permission; the accepted scan-stage domain/calibration contract below
   supersedes that earlier boundary for the next cut.

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

The native card advertises FLR; boot and terminal FLR completed in all three
qualified warm-reboot candidate cohorts. The PM fallback was not exercised on
this card. The existing reset-free PCI probe and retained-D3hot wake interfaces
keep their NoSoftReset requirement and semantics; this separately authorized
reset path does not relax them.

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
- [x] Physical-card VFIO firmware readiness and checked stop (2026-10-23),
  including the native packet's info-level build. This is not bare-metal or
  RF/scan/data qualification; MSI emulation and IOMMU isolation still apply.
- [x] Native first cut qualified on vela (2026-10-23): three candidate warm
  reboots confirm FLR, live firmware/board facts, firmware readiness and checked
  MAC-off/terminal reset; all six interleaved cohorts complete independent USB
  wired controls with overlapping ranges. Cold boots, original Linux-driver
  handoff and failure recovery remain unqualified. See the
  [qualification record](../development/experiments/rtl8922ae-firmware-first.md#native-warm-reboot-qualification-2026-10-23).

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
readback failures have separate status details. Extended Message Data Enable
is cleared and checked; the message uses only the programmed 16-bit data.
Download-mode readiness failures retain the substep, register, sampled value,
mask and expected condition before stop. Ordinary RMW, ring/quota programming,
and table writes follow the pinned reference without value whitelists or
blanket write/read equality. `0x8404`, `0x0888` and `0x9c94` no longer need
individual exceptions to a generic refusal rule. This does not establish the
undocumented meaning of their read values.

VFIO and three native warm-reboot cohorts confirm firmware 0.35.80.3
initialization, live board observations and completed MAC-off/FLR. The trace
additionally observes H2C/header readiness. `WIFI_READY` remains clear and no
WLAN interface is published. Country `ffff` is a raw board observation, not
regulatory permission. The independent native wired controls complete without
a material regression in this run. See the
[qualification record](../development/experiments/rtl8922ae-firmware-first.md#native-warm-reboot-qualification-2026-10-23).

Register access and readiness checks follow pinned rtw89 predicates
(chronium, 2026-10-27). Power-on rejects active WLMAC state one, not every
nonzero state. Power-off uses the reference handshakes without a final
`0x03f0 == 0` requirement. DMA pre-deinit posts the reference sequence; terminal
PCI reset supplies the DMA-halt proof. Request cancellation, rfkill, outer
budgets and backing retention remain native lifetime constraints. The
startup active-MAC handling follows the reference caller (accepted, chronium,
2026-10-28): if the initial WLMAC state is active, run the reference power-off
sequence and try power-on once more, including its existing active-state check.
Both steps use the original request deadline. An unconfirmed power-off or failed
second power-on takes the existing terminal-stop path. This is a bounded startup
handoff, not a firmware-download or SCAN retry; no further startup is admitted
after failure in that boot.

The exceptions protect specific admitted hardware effects: PCI/MSI/DMA
ownership and bounds, suppression of autonomous packet TX, factory-board input
validity without defaults, and the numeric SAR/regulatory ceiling with its
force/additive bypass controls. They are not general register-integrity tests.
Broad by-rate, reference, shape, TSSI and MACID equality checks are removed;
only enforcement fields are checked before and after TX-capable RFK. RFK
reports use the latest completion as in Linux, and semantic helper failures do
not skip later reference-ordered helpers while native safety gates hold.
Qualification still records failure if any required RFK helper failed; an
attempt is never described as verified calibration merely because writes ran.
See the [register audit and native limit](../development/experiments/rtl8922ae-phy-init.md#register-refusal-audit-2026-10-27).

### Qualification and next review point

The firmware-first cut is qualified for the recorded vela warm-reboot profile:
VFIO/MMIO comparison, checked normal firmware download and terminal stop, then
native A1/B1/A2/B2/A3/B3 with unchanged independent wired controls. The earlier
nested/slirp samples remain noisy and are not the native performance evidence.
Cold boots, original Linux-driver handoff, PM fallback and failure recovery were
not exercised. No passive-scan, data or physical RF-silence claim follows.
CDC-NCM supplies running Remote/log, not fatal/stopped-kernel GDB.

**Next cut: passive-scan prerequisites and implementation.** The accepted
contract below resolves domain selection and necessary calibration authority;
board/power/SAR and operational rfkill still need implementation and evidence.
The connection stage must later treat a remembered network as a standing boot
request, rather than making manual scan requests the permanent activation model.

## Passive-scan prerequisites (2026-10-23)

**Decided (chronium, 2026-10-23); data-input cut under qualification.** Baseline: main `4647c424`,
including merged userland #348 and
[#1120](https://git.internal/PyxisOS/pyxis-os/pulls/1120). The qualified first cut
ends MAC-off/reset: it has not initialized a working receive-scan radio. Scan
implementation is authorized after the pinned cache archive is available and
verified; native runs remain separately scheduled through the orchestrator.
Keep the retry/reset recovery requirement before everyday use.

### Inspected facts and remaining evidence

- **Country/channel authority:** firmware REGD maps alpha2 to per-band power-rule
  families and features, not a complete channel-permission database
  ([`fw.c:1194–1229`](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/fw.c#L1194)).
  Linux combines it with stack channel flags; unknown eFuse country follows the
  stack setting (`regd.c:723–740`). Vela's observed `ffff` supplies neither
  jurisdiction nor permission. AX200 MCC/LAR and its 1–11 fallback do not carry
  over. The existing [set-once policy authority](../../include/abi/wifi.h) can
  carry explicit host country/channel restrictions; its AX200-specific wording
  will need backend-aware clarification, not an invented RTL MCC reply.
- **RF meaning:** first channel activation invokes `RTW89_TSSI_SCAN`; its
  [firmware request sets `hwtx_en=true`](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/fw.c#L6404).
  The empty scan RFK hook does not suppress this. Late init also requests
  DACK/RXDCK. Ordinary response hardware needs an ACK/BA audit. Zero probe
  requests is supportable; a calibration-free, no-on-air-TX receive profile is
  not established. Actual emitted RF/power was not measured by the first cut.
- **Board/power:** RFE 1, cut 2, firmware 0.35.80.3 and basic trims are observed,
  not qualified power limits. The pinned reference selects firmware power
  elements by RFE and permits a default-RFE fallback (`fw.c:1061–1100`). It
  combines regional board tables, antenna gain, SAR and other constraints
  (`phy.c:2342–2412`). The exact pinned asset has been inspected: its power tables have no exact
  RFE-1 selector. The explicit reference RFE-0 selection below is accepted;
  sparse regional entries remain distinct from missing values. No silent
  regional-zero or worldwide-table fallback is introduced. Full reference receive initialization also
  loads BBMCU0 and MAC/BB/RF tables; the WCPU-only first cut is insufficient.
- **Platform limits:** Realtek uses WRDS, RWRD/RWSI, RWGS, RTAG and a TAS `_DSM`
  query, rather than borrowing Intel's ACPI decoder. The reference
  [ACPI reader](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/acpi.c#L80)
  collapses absence, evaluation/malformed-data and allocation errors; its SAR
  query may then return the maximum MAC limit. Do not copy that collapse.
  Linux uses static SAR during scan (`sar.c:802–818`); dynamic TAS can otherwise
  raise or lower a limit. Vela's source states are captured below; effective
  programmed/calibration limits remain unqualified.
- **rfkill:** the [current reader](../../kernel/wifi/rtl8922ae_hw.c) configures
  active-low GPIO9 and samples once before the first cut stops. Operational
  monitoring, physical switch coverage and stop latency are unqualified. The
  reference polls GPIO9; a high sample is not proof of an instantaneous physical
  RF interlock.

### Accepted decisions (chronium, 2026-10-23)

1. **World default and configured-country rules.** With no country configured,
   use domain `00` from the same pinned wireless-regdb, so installation can use
   Wi-Fi without a country question. A configured country selects its own rules,
   which may narrow or widen the world profile; the request/channel mask and
   qualified device/board limits still intersect those permissions. No locale
   or AP-beacon country inference. Invalid configured countries or unsupported
   rules are errors, not an excuse to invent permissions.

   Derive bounded immutable target tables on the host, checking occupied
   bandwidth, flags and power rather than only center frequency. Preserve NO_IR
   restrictions: world-domain channels 12–13 and passive 5 GHz rules do not
   become active-TX permission. These are database semantics, not an expansion
   of the initial legacy 2.4 GHz/20 MHz backend profile to 5 GHz. The first
   controller cut admits its supported channels within the effective rules;
   it must not lose usable world-domain discovery merely because country was
   omitted. Reuse the existing country/channel policy fields and authority,
   with precise backend-aware validation/semantics; no calibration field is added.

   The policy ABI names this domain as country `00` (`WIFI_COUNTRY_WORLD`),
   which Install applies on AX200 as well
   ([decision 70](pkg-upgrade.md#decided)).

   Pinned source: **wireless-regdb 2026.09.03**, whose project documents an
   [ISC licence](https://wireless.docs.kernel.org/en/latest/en/developers/regulatory/wireless-regdb.html).
   Mirror:
   `https://repo.internal/repository/raw-kernel/software/network/wireless-regdb/wireless-regdb-2026.09.03.tar.xz`.
   Upstream:
   `https://www.kernel.org/pub/software/network/wireless-regdb/wireless-regdb-2026.09.03.tar.xz`.
   [Published SHA-256](https://www.kernel.org/pub/software/network/wireless-regdb/sha256sums.asc):
   `b22e0901227b820cd1c280abe681a15b773a5103a5e10dc442e94ebb34cbf58d`.
   Fetched through that mirror and independently verified against the digest.
   The archive `LICENSE` grants ISC terms with notices for Luis R. Rodriguez,
   Johannes Berg and Michael Green (2008). Retain those notices with derived
   data and any reused compiler/parser source; no upstream fallback or compiler
   rebuild is needed.

2. **Effective regulatory rules authorize necessary calibration.** The world
   or configured-country rules in force are themselves the authorization for
   TX-capable calibration needed for receive discovery, within their applicable
   channel/power constraints. No separate calibration permission, POLICY payload
   extension or extra user question. Ordinary SCAN requests cannot widen the
   effective rules or send packets. No probes, authentication, association, data
   or offload TX; suppress or establish ineligibility of automatic ACK/BA/null
   responses. Do not claim RF silence or skip required calibration to obtain an
   unsupported no-TX claim. NO_IR remains a real restriction: qualify a legal
   calibration/tuning sequence rather than treating receive-only channel
   permission as authorization to transmit there.

   Before any newly permitted RFK, qualify the selected cut/RFE's firmware
   tables, trims, antenna/EIRP-to-path power conversion and applicable platform
   constraints. Preserve distinct validated-present, validated-absent/not-
   applicable, unknown/unavailable and malformed/unsupported states. Valid
   absence removes only that particular extra constraint when the qualified
   platform contract permits it; it never supplies maximum power. Unknown or
   malformed required input blocks RF while the first-cut diagnostic remains
   usable. Start with static SAR, taking the restrictive envelope of validated
   dynamic profiles where supported, and no TAS power boost. Do not invent a
   dBm cap or silently choose table zero, default RFE or worldwide power tables.
   Check that the programmed limits also constrain each authorized calibration;
   if that cannot be established, return for review before running it.

3. **Request-driven scanning now; remembered connection intent later.** For
   this scan stage, no automatic scan or new calibration at boot/READ. In the scan
   profile, replace the one-shot firmware worker with deferred activation rather
   than running the old first cut and a second bootstrap in one boot. Admit a
   SCAN only after policy and hardware inputs are valid, with fresh checked
   rfkill state; monitor it during initialization/calibration/scan at a bounded,
   documented cadence. Block or unreadable state closes admission and cancels the operation;
   report completion only after checked drain/stop or quarantine. No immediate
   physical-silence guarantee. Unblock never starts RF by itself: a new explicit
   request must revalidate. Reuse existing process-owned scan handles, deadlines,
   cancellation and immutable result FILEs; no new application-specific API.

   Healthy completed/cancelled scans may be followed by fresh requests on a
   confirmed live context. Keep the bring-up cut's one firmware-start attempt
   per boot: an uncertain command/calibration/stop or terminal reset retains
   backing and leaves further startup unavailable, without hidden retry/replay.
   **Before everyday use**, separately design and qualify real retry/reset
   recovery, stale completion retirement and re-admission after rfkill or loss.
   This preserves the accepted revisit point rather than making reboot the
   permanent recovery contract.

   **Connection-stage requirement:** after a successful connection, remember the
   selected network and reconnect automatically across reboots until manual
   disconnect clears that standing intent. A remembered network is a standing
   request at boot; the scan-stage request-driven design must permit this later
   trusted requester, rather than require fresh manual interaction each boot.
   Reconnect still obeys effective regulatory rules, rfkill, authority and
   checked retirement; implement/qualify the necessary recovery path rather
   than carrying the one-attempt bring-up limit into everyday connections.
   Until the credential manager exists, the network key may be stored
   unencrypted. That accepted limitation and its consequence are recorded in
   [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1287).
   This connection behavior/storage is a requirement for that stage, not an
   implementation delivered by the current scan work.

### Accepted reference default power-table selection

**Decided (chronium, 2026-10-23):** explicitly use the pinned Linux reference's
RFE-0 default power tables when the firmware has no RFE-1 power tables. Report
the selection as a default rather than an exact match. Retain every format,
regional, board, antenna and SAR check, and qualify the selected tables and
applicable limits before any new RF work; the fallback supplies no extra power
permission.

Inspected the exact pinned format-4 asset: every power-table family contains
selectors 0, 3 and 4, with no selector 1. Linux names 0
`RTW89_TXPWR_CONF_DFLT_RFE_TYPE` (`core.h:4108`) and deliberately selects it
when the exact RFE is absent (`fw.c:1061–1100`). RTL8922A supplies no separate
RFE-1-to-0 alias (`rtw8922a.c:2881–2882`; `core.c:5983–6008`). Its BB/RADIO
headlines do include exact RFE-1/cut-2 entries; the missing match concerns power
tables. The accepted reference selection resolves this dispatch policy, not
physical power qualification. The separate evidence that programmed effective
limits constrain TX-capable firmware calibration is still required; selecting
an arbitrary dBm cap would not establish it.

### Numeric regulatory ceiling — corrected after review (2026-10-28)

Before TX-capable calibration, the prepared image intersects each active
normal/RU limit with the firmware regional value, normalized SAR and the
selected regdb numeric EIRP ceiling, rounded down to half-dBm. Physical-path
ceilings also take the SAR/regdb minimum. Firmware board/antenna constraints
remain in force; this does not infer physical antenna gain or radiated power,
add a guessed chain-count deduction, or add an antenna-availability refusal.
Daybreak's AR channel-4/SAR-24 regression now caps 21/24 dBm values at 20 dBm
without refusing admission. Vela's world/SAR-16 complete images are unchanged.
See the [host regression record](../development/experiments/rtl8922ae-phy-init.md#numeric-regulatory-ceiling-correction-2026-10-28).

### Native input result and accepted fixed-SAR profile

Native input capture completed on vela (2026-10-23), with verified menu restore:
firmware ready/MAC-off/FLR, world domain `"00"`, exact PHY headlines and explicit
RFE0 power fallback. WRDS is absent, RWRD/RWSI valid, RWGS/RTAG absent and TAS
malformed. The first observer established bounded structural decoding; the
follow-up below establishes normalized SAR inputs. Neither applies hardware
limits or qualifies calibration enforcement. A malformed diagnostic alone does
not prove bad firmware.
See the [native record](../development/experiments/rtl8922ae-scan-inputs.md#native-input-capture-2026-10-23).

**Decided (chronium, 2026-10-23):** keep TAS entirely disabled in the initial
scan profile. Enforce a fixed, unboosted SAR ceiling derived from the minimum
correctly normalized 2.4 GHz value across every valid retained RWRD profile,
per path and with the applicable 2TX downgrade. This does not invent a WRDS
table. Preserve the TAS malformed diagnostic, but do not require the optional
time-averaging algorithm when those independent hard limits have been qualified.
Unknown/malformed required SAR or power inputs still block new RF.

The reference supports baseline SAR without TAS (`sar.c:274–312,694–709`) and
forces that baseline during scanning (`sar.c:802–817`). This clarification
resolves TAS applicability only; it neither supplies missing numeric evidence
nor proves firmware calibration obeys the programmed limits. First expose the
exact TAS rejection stage and the normalized RWRD path ceilings through bounded
read-only diagnostics, without treating an error as absence or maximum power.

Implemented input diagnostics now retain the first TAS rejection stage and
reason, observed type/length/support mask, and independent alias-conflict flag.
The CPU-only SAR decoder revalidates retained spans, normalizes each recognized
source independently and takes the per-path minimum over every RWRD profile and
present WRDS. HP uses `10 + raw/2` dBm, path A/B antenna indices 1/0 and a 3 dB
2TX reduction; RT uses raw eighth-dBm, indices 0/1 and no 2TX reduction. Both
are reported in eighth-dBm without upward rounding. RWSI is not consumed when
all profiles constrain the fixed envelope. TAS cannot alter it. A present
uninterpreted geographic constraint, malformed required source or incomplete
capture yields no numeric envelope. This observation does not yet apply the
regdb/board/antenna constraints or program/calibrate hardware.

The follow-up native capture completed on vela (2026-10-23), with verified
byte-for-byte PXE restoration. TAS fails our function-0 buffer-type check: the
observed object is an integer, with no alias conflict. Function 5 was not
evaluated, and no supported-function mask or integer value is inferred. TAS
remains disabled under the accepted decision; this optional rejection does not
invalidate the separately validated SAR inputs.

RWRD is RT CID `0x5452`, revision 2, four antennas, eleven subbands and one
profile. The fixed envelope is **16 dBm on paths A and B, for both 1TX and 2TX**
(`128` eighth-dBm); this CID has no HP-specific 3 dB reduction. WRDS remains
absent and is not synthesized. The selected standard WW/RFE0 normal-rate
ceilings are lower (at most 14.5 dBm for 1TX, 11.5 dBm for 2TX), before SAR
intersection. These are reference-table/input values, not measured EIRP.
Firmware readiness, MAC-off/FLR and the ordinary FILE control also passed.
See the [follow-up record](../development/experiments/rtl8922ae-scan-inputs.md#native-sar-details-capture-2026-10-23).

### Delivery and qualification

1. [x] **Small first task: data/authority closure, no new RF commands.** Close the
   regdb cache/licence and host-table derivation; inspect the exact pinned
   firmware's 2.4 GHz/RFE power elements and trim meanings. Add bounded native
   Realtek ACPI/input decoding with honest states, effective-policy reporting
   and rejection before RF admission. A separately authorized read-only native
   packet can establish vela's inputs; no new calibration/scan in this task.
   **After this task chronium can:** inspect the selected country/channel/power
   envelope or the exact missing prerequisite, with existing firmware diagnostics.

   Implementation: pinned ISC regdb generator/data, first-fitting 20 MHz rules,
   backend-aware one-shot policy, checked immutable firmware table selection
   and separate Realtek ACPI snapshots. READ exposes rule limits/restrictions,
   source states, default-table provenance, precise TAS rejection and normalized
   fixed SAR bounds; these are input observations,
   not an effective calibrated TX limit. No new RF commands, scan readiness or
   RTL data interface. Native structural and normalized-SAR input captures are
   complete. Hardware limit programming/readback and calibration enforcement
   remain task 2's gate before newly enabled RF. See the
   [source/QEMU record](../development/experiments/rtl8922ae-scan-inputs.md).
2. [x] **One-PHY receive initialization and checked stop.** Implement the required
   BBMCU/MAC/BB/RF path, power limits, regulatory-authorized RFK and ongoing
   rfkill/cancellation, with no scan result publication yet. Qualify firmware
   acknowledgments/completions, RX/IRQ ownership and stop before proceeding.
   **After this task chronium can:** boot an explicitly qualified radio-preparation
   profile and verify its inputs, calibration outcome and quiescence.

   Assigned (chronium, 2026-10-23); normal-path native qualification passed
   (2026-10-27) on audited kernel `eb92ab2b` and helper `a1bece88`: PHY `df`,
   all four RFK completions/final OK, five power checks and confirmed MAC-off/FLR.
   Four interrupts and seven firmware packets were observed; wired management
   survived. This does not qualify ordinary-frame RX, hopping or physical rfkill.
   Boot and READ now reserve/observe only; one SCAN operation owns startup,
   regulatory-authorized calibration and checked stop. This cut completes with
   UNAVAILABLE and no result FILE even after successful preparation, keeping
   WIFI_READY clear until discovery exists. The selected 20 MHz calibration
   channel excludes NO_IR and other unqualified restrictions. A fixed effective
   normal/RU/path-SAR image is read back before RFK and its hard limits checked
   after each serialized PRE_NOTIFY, DACK, RXDCK and TSSI completion. No TAS,
   dynamic power boost or ordinary packet/automatic-response path is enabled.

   GPIO9 is configured and checked immediately after MAC power-on, before
   security/firmware startup. Current rfkill/cancellation checkpoints bound
   waits to 10 ms; the unpowered AFE/power-on interval cannot read live GPIO.
   Cleanup continues independently after withdrawal/deadline/rfkill, retaining
   all backing until reboot. Status exposes historical readbacks and RFK/stop
   facts, not measured radiated power. Source/QEMU checks cannot establish
   native programming, calibration outcome or a physical rfkill response.
   See the [source/QEMU qualification record](../development/experiments/rtl8922ae-phy-init.md).
3. [ ] **Bounded unassociated passive discovery.** Use admitted channels only, no
   probe packets, no association/packet offloads; validate bounded records and
   preserve existing operation/FILE lifetime and cancellation semantics.
   **After this task chronium can:** request repeated explicit passive scans on
   the supported profile while wired management remains independent.

The first explicit-channel firmware-offload slice is implemented and native
qualified on vela (2026-10-28): a channel-1 request returned two real records in
an immutable result FILE with observed RX and a retained healthy DBCC context.
See the [passive-scan record](../development/experiments/rtl8922ae-passive-scan.md).
- [x] One explicit legal channel: native immutable result FILE and observed RX.
- [x] Repeated same-channel requests and operation/FILE independence.
- [ ] Channel hopping and complete admitted-channel-set delivery.
- [ ] Cancellation, withdrawal, physical rfkill where controllable and recovery.

The full admitted-channel-set task above remains unchecked.

### Next passive-results task — accepted direction (chronium, 2026-10-27)

Follow the pinned Linux reference's actual path for firmware 0.35.80.3: modern
BE firmware scan offload. Linux enables SCAN_OFFLOAD for RTL8922A firmware from
0.34.35.0 and uses the older BE command only before 0.35.21.0
([feature selection](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/fw.c#L854),
[scan entry](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/mac80211.c#L1214)).
Implement the necessary unassociated role/port/context, BE channel list and
START/STOP/completion path. Use a zero-SSID passive request, no probe templates,
no null/data TX and the reference's passive channel settings. Preserve native
memory bounds, regulatory/SAR/rfkill authority and existing narrow TX/power
hazard gates; add no hardware-value assumptions beyond the reference.

Keep the existing native SCAN operation, deadlines, cancellation and immutable
result FILE; no new application API. The first slice accepts **one explicitly
requested legal channel**, with subsequent requests limited to that same
calibrated channel. Reject zero-channel and multichannel requests before effects
until their full semantics are implemented; never scan one channel and report
success for a broader request. This is a bounded first slice, not qualification
of the complete admitted channel set.

Initial TX-capable calibration stays within the requested mask and effective
rules. A first NO_IR-only request refuses before startup; do not silently
calibrate elsewhere. Listening on channels 12–13 and complete world-domain
discovery remain later qualification work, preserving their receive-only
permission without authorizing probes or calibration TX there. Match the
reference's channel/power updates and band/PHY-dependent TSSI hooks when adding
further channels.

Retain driver-owned firmware/DMA on a healthy context and continuously drain/drop
RX outside an active operation. Keep autonomous TX disabled and monitor rfkill.
No background scan, calibration, result cache or retained caller pointer.
Confirmed healthy completion/cancellation retires the operation before reuse;
uncertain command/transport/stop or rfkill closes admission and uses checked
terminal stop, with no restart this boot. The accepted one-start bring-up limit
and separate real retry/reset recovery requirement before everyday use remain.

Decode bounded Realtek descriptors and CRC/ICV-checked management frames. Reuse
native counted-SSID/RSN accumulation, BSSID/channel de-duplication,
security-conflict handling, the 128-record limit and honest overflow/error
outcomes. Signal/channel metadata comes from Realtek descriptors/reports; use
the existing unknown-signal value `-256` when no valid RSSI is available. Drain
or correlate old observations before attribution; the latest software channel
variable alone does not establish a frame's channel.

Publish a real immutable FILE only after normal firmware END_SCAN or confirmed
STOP and retirement of the fixed set of queued RX captured at that boundary.
Command receipt or successful initialization alone is not scan completion. A
completed zero-record scan is valid; an incomplete/failed scan publishes no
success FILE. The reference's
[BE command](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/fw.c#L5863)
and [END_SCAN handling](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/realtek/rtw89/mac.c#L5042)
define the firmware completion path; borrowed DMA storage never outlives its
checked delivery.

One normal channel-1 result and two sequential same-channel requests with
independent FILEs are native qualified. Hopping, cancellation/timeout/final-close,
physical rfkill response and recovery remain separate gates. Keep independent
USB-wired controls and SSID/BSSID captures private/redacted.

QEMU/source checks cover data bounds, narrowing and lifecycle, not RF. Separately
scheduled VFIO/native checks must cover unknown/malformed inputs, channel/power
programming, normal and cancelled scans, rfkill where the platform exposes a
control, warm handoffs and independent wired controls with exact revisions.
Record absent physical controls and unobserved failures as limits, not passed
checks. Keep SSIDs, MACs and raw eFuse/ACPI identity out of repository evidence.
Implementation is authorized once the mirrored regdb archive is verified.
Native runs still require the separately scheduled orchestrator go. Reconnection
and key persistence are requirements for the later connection stage.

## Stages

The fresh read-only inventory and stage-1 CPU-only qualification are complete
(2026-10-22). Stage 2's firmware-first cut is qualified for native warm reboots
(2026-10-23). Passive-scan decisions are accepted and implementation is
authorized after regdb cache verification. Stages 3–4 and native runs retain
their separate go; the connection stage must support remembered boot requests.

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

### DBCC initialization — implemented (chronium, 2026-10-28)

The passive-results implementation uses the reference's coherent DBCC/EMLSR
setup: CMAC1/BB1, matching quotas and firmware notification, PHY-indexed
initialization and required calibration for both contexts. Both use the
explicitly requested legal channel. Regulatory/SAR limits are applied and
verified for both MAC banks with shared physical SAR before calibration and
after each RFK helper; autonomous-response and packet-TX gates cover both MACs.
No probes, association or data. Existing cancellation/rfkill, idle RX draining
and checked whole-device stop/FLR cover both contexts.

The checked BAR2 window is `0x60000` for the reference's PHY1 BBMCU control
registers. RTL8922A still downloads one BBMCU firmware image. No new grant,
application interface, public ABI or simultaneous multichannel capability.
RFK completed bits mean completion on both PHYs, not either one. Native
`f85199a6` passed one normal channel-1 request; `4b6c74cc` subsequently passed
two sequential channel-1 requests. Neither qualifies hopping. Retained
firmware/DMA is intentional after healthy completion.

### Repeated same-channel qualification — delivered (2026-10-28)

The two-request contract accepted by chronium on 2026-10-28 passed natively on
vela at `4b6c74cc` with userland `1b546c9f`. Two explicit channel-1 requests on
one healthy DBCC activation returned status 0 and counts 2/1. The first immutable
FILE stayed held and unchanged through the second request; identities and close
lifetimes were independent. Power verification boundaries stayed 10/10, boot
state 3/3 and RFK `0xf`/`0xf`: no extra host startup/RFK. Wired management and
byte-exact PXE restoration passed. See the
[qualification record](../development/experiments/rtl8922ae-passive-scan.md#retained-context-repetition-2026-10-28).
After this slice chronium can request channel 1 twice without rebooting and
keep independent result snapshots. No automatic scan, retry or new API.

### Channel hopping — accepted (chronium, 2026-10-28)

Keep BE firmware scan offload. The first hopping slice is an explicit
channel-1-plus-6 request in one firmware list, with the existing START/END,
DBCC context and immutable FILE. It does not add a host-directed channel walk,
automatic scanning, association, probes or data TX. First qualify channel
attribution on the already supported fixed channel; then qualify the two-channel
request. Full admitted-mask delivery and NO_IR channels 12–13 remain later
slices; the latter require receive-only retuning qualification. Cancellation and
recovery remain separate gates.

#### Reference findings

Reference is Linux v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449`,
`drivers/net/wireless/realtek/rtw89/`:

- `mac_be.c:2008–2025` enables PPDU reports; `core.c:1682–1734,1982–2031`
  bounds/parses them. RTL8922A uses IE01 channel metadata
  (`rtw8922a.c:2674–2708`). Ordinary RX descriptors lack a channel.
- `core.c:2118–2152,3084–3136,3490–3517` correlates one current PPDU group
  per BB using its counter and normalized rate/GI/bandwidth. A counter change
  or report drains the group. Invalid/missing/unmatched reports retain Linux's
  saved software-channel fallback; they do not fail its scan. ENTER_CHANNEL
  alone is not per-frame channel evidence. RXQ is ordinary RX; RPQ handles TX
  releases (`pci.c:436–453,713–733`), not a second channel-ordering guarantee.
- Host channel changes call `set_txpwr` (`core.c:453–480`), including shape,
  normal/RU limits and SAR (`rtw8922a.c:2257–2274`). Firmware scan channel-list
  elements have no host power-image field (`fw.c:5660–5749`). ENTER handling
  updates software channel state without a host power/RFK barrier
  (`mac.c:5042–5085`); the RTL8922A scan RFK hook is empty
  (`rtw8922a.c:2137–2142`). Post-ENTER writes cannot establish pre-hop ordering.

The preceding fixed-channel cut dropped PPDU packet type 1. The delivered
hopping decoder now uses the reference's IE01 and per-BB matching; fixed-channel
fallback is not evidence of a hopping channel. No new hardware readback-equality
checks or stricter PPDU counter/timestamp matching are introduced.

#### Accepted contract (chronium, 2026-10-28)

1. **Channel evidence: PPDU-attributed records for hopping.** Follow
   Linux's per-BB current-group matcher and report parsing, but omit hopping
   candidates without a valid matched requested-channel report. Keep the current
   fixed-channel fallback for single-channel requests. Missing, malformed,
   late or unmatched reports are counted omissions, not terminal radio failures;
   a completed empty FILE is valid. This deliberately yields fewer observations
   than Linux's software-channel fallback, rather than presenting that fallback
   as proven attribution. No report cache, timestamp-equality gate or extra
   modulo-counter generation is introduced.
2. **Bounded pending records; retain a visibly truncated result on overflow.**
   Use 512 copied candidates shared by two BB queues, at most 64 bytes each
   (32 KiB). Copy parsed BSS/correlation metadata, never whole frames or borrowed
   DMA. This is an authored capacity, not an RX-ring lifetime guarantee. Keep
   the existing 128 final BSS-record bound. At either capacity, keep what fits,
   count dropped candidate records where known and mark the immutable result
   visibly TRUNCATED; overflow alone does not fail the scan. Missing/unmatched
   PPDU metadata remains a separately counted omission, not proof of capacity
   truncation. Drain captured RX at END/confirmed STOP before resolving pending
   groups and publishing; clear all groups between operations.
3. **Power across hops: one conservative request-wide image before
   RFK/START for the first explicit 1+6 slice.** Prepare both channels from the
   validated tables, regulatory rules and fixed SAR; take the lower signed
   ceiling for each corresponding normal/RU limit and physical-path SAR.
   Other power/shape/by-rate controls must be compatible, not bytewise-minimized.
   Apply that image to both contexts before calibration on requested channel 1,
   retain it throughout firmware hopping, and follow Linux's scan RFK behavior
   without an invented per-ENTER calibration sequence. This may reduce available
   power; it never grants TX beyond initialization/calibration. Repeated exact
   masks reuse the healthy context; a different retained-context mask remains
   unsupported in this first slice. If compatible controls or enforcement
   continuity cannot be established from the reference and programming evidence,
   stop before enabling hopping and bring that concrete difference back.

These decisions authorize implementation, not a claim that hopping is qualified.
No new grant is needed. The immutable result carries truncation metadata so it
outlives operation close; in-tree readers are updated together under the
development ABI policy. The common power image is an explicit Pyxis policy,
not a Linux firmware-upload format.
Historical limit readbacks are not radiated-power measurements or proof of
closed-firmware internals.

#### Assigned delivery and qualification

1. Enable reference PPDU reporting/decoding and bounded per-BB correlation on
   channel 1; qualify known-channel records, omitted/unusable reports and FILE
   retirement while keeping normal repeated single-channel results intact.
   **After this task chronium can:** obtain channel-attributed fixed-channel
   observations using the metadata required for hopping.
2. Prepare/qualify the common 1+6 power image, then submit one firmware list with
   both channels. Require reference START/END and RX retirement, channel values
   derived from reports, decoded power ceilings covering both channels, rfkill
   observations and independent wired management. Repeat the same request while
   holding the first FILE; normal completion retains healthy context.
   **After this task chronium can:** request a passive two-channel snapshot and
   keep it independently of another completed request.

Source/QEMU checks cover bounds, decoding and lifetime; only orchestrator-run
native packets qualify PPDU delivery and firmware hopping on vela. No nearby AP
or nonempty result is guaranteed. Channel attribution and programmed limits are
separate evidence; neither proves radiated power. No native run is authorized
by this document alone. The implementation and each native packet are qualified
separately; the one-channel implementation and reviewed power correction are merged.

### Hopping implementation and bounded native qualification

The accepted slice is merged: reference PPDU reporting,
bounded per-BB current-group matching, copied candidates, the BE two-element
1+6 firmware list and the common power image. Overflow retains fitting records
and carries TRUNCATED/drop metadata in the immutable 24-byte FILE header;
SCAN STATUS adds a flag without growing its reply. In-tree readers change
together; no retained release ABI is listed by the current development tree.

The candidate applies/verifies the common image before RFK on both contexts and
checks it after END/captured RX retirement, with collection already closed before
any power helper can poll. Endpoint checks are not an on-air power measurement.
Known single-channel requests keep fallback; different retained masks still
refuse. No new grant, background scan, association or data path.

- [x] CPU-only power compatibility and reference wire-format/lifetime qualification.
- [x] Current integration QEMU, matched controls and sealed hands-free packet.
- [x] Native PPDU delivery, 1+6 completion, retained repetition and FILE independence.

Qualified on vela (2026-10-28): two successful 1+6 sweeps returned 4 and 5
records, both without truncation. Firmware visited channels 1 and 6; returned
records were PPDU-attributed to channel 1. The first immutable FILE stayed held
and unchanged through the second request, with independent result identities.
Power readbacks advanced 11 to 12 without another startup/RFK; wired management
and byte-exact PXE restoration passed. Execution of the startup active-MAC
power-cycle branch was not observable in the saved logs. This qualifies the
bounded slice, not the complete channel set, physical rfkill or general recovery.

Open qualification items (chronium, 2026-10-28):

- [x] Channel-6 PPDU attribution demonstrated by the period-232 native scans
  (2026-10-29): the known AP was received twice per scan, with PPDU, raw,
  advertised and firmware-step channel all 6.
- [x] Startup active-MAC power-off/power-on observed in the earlier attribution
  capture (2026-10-29). The period-232 boot found a non-active MAC and did not
  need that branch. See the [attribution record](../development/experiments/rtl8922ae-attribution.md).

[Qualification record](../development/experiments/rtl8922ae-hopping.md).
These checkboxes do not complete full admitted-mask discovery or cancellation.

## Next discovery slice — accepted (chronium, 2026-10-29)

Close the two observation gaps, then broaden the same firmware-offload path to
the admitted channel 1–13 set. Follow Linux's regulatory distinction: world
channels 12–13 are passive/listen-only under NO_IR, not omitted from discovery.
Keep the existing opt-in backend, READ/SCAN grants, world/configured-country
rules, platform/SAR restrictions and request-driven activation. Release target
remains 0.0.10 if ready before release, otherwise 0.0.11.

### Current source boundary

Inspected main `397a22bffdd8b8be79829754e381abfdb6eb5e2e`, userland
`8cefca4017769e5e79d455cfff127947554a6fc3`, ports
`18b56bf05ec2e2d50f7aec383a0b452d4f36f1ec`:

- [`rtl8922ae.c:760–779`](../../kernel/wifi/rtl8922ae.c#L760) admits one
  channel or exactly 1+6, refuses unqualified rule profiles and retains one
  prepared mask. A zero/default request is currently refused.
- [`rtl8922ae_scan.c:51–53,411–440`](../../kernel/wifi/rtl8922ae_scan.c#L411)
  has an authored two-element list bound; BE entries are 32 bytes with a
  four-byte list header. Thirteen entries fit the existing 2,000-byte H2C payload
  bound and Linux's BE list limit (`fw.h:338–344`). Follow pinned Linux
  `fw.c:5644–5750,7774–7877` for this list; no host channel walker or new
  transport is needed.
- [`rtl8922ae_power.c:441–494`](../../kernel/wifi/rtl8922ae_power.c#L441)
  specializes common-image preparation to channels 1 and 6. Extending the
  channel list alone would not establish a power image for the broader sweep.
- Existing extended PHY status has historical observations but no fact showing
  that the startup active-MAC branch ran. Successful scanning cannot supply it.

### Accepted decisions (chronium, 2026-10-29)

1. **Channel profile and default request.** Default scans cover the admitted
   channel 1–13 set under the effective host/regdb rules; world channels 12–13
   use the reference passive/listen-only mode. An omitted/zero mask selects
   that complete admitted set; publish the actual selected mask in the result.
   An explicit request is exact: refuse unsupported/restricted bits before
   effects rather than silently dropping them. An empty admitted set remains
   unavailable. NO_IR is permission to listen, not to calibrate or transmit
   there; retain the existing prohibition on borrowing a calibration channel
   outside the requested mask. Resolve the reference's NO_IR-only startup
   behavior when that task is reached. Channel 14, probes and automatic scans
   remain outside this slice.
2. **Power and retained context.** Generalize the accepted
   conservative image to every requested channel: lower corresponding signed
   firmware/SAR/numeric-regdb limits, with compatible by-rate/shape/other
   controls, before any TX-capable RFK. Initialize both existing DBCC contexts
   on the lowest requested eligible channel. Keep that image through the BE
   firmware sweep; no post-ENTER power race or per-hop host RFK. If compatible
   controls cannot be established, stop for review instead of inventing a
   fallback. Retain the current exact-mask reuse limit: another mask refuses
   before effects; this task adds no reset/reinitialization recovery. The
   default full-set request can repeat without changing that mask. Starting
   with a narrow explicit request still requires a fresh boot before a broader
   default request; this is a slice limit, not the eventual connection contract.
3. **Observable proof.** Retain validity-qualified historical
   startup facts in the existing extended PHY STATUS state word: initial MAC
   state observed, active state seen, normalization attempted and normalization
   completed. Completed means the power-off handshakes and ensuing power-on
   both succeeded, not merely that the second state read was non-active.
   Preserve reply sizes and released-prefix delivery; add no command,
   authority or klog line. Qualify real channel-6 attribution using an
   orchestrator-provided AP whose channel is independently known, with its
   identities kept private. Use natural warm handoffs to observe the startup
   branch; do not force hardware state or fabricate frames. A non-active initial
   MAC leaves that branch observation open, not permission for an unplanned retry.

**Channel-6 bug evidence (chronium, 2026-10-29):** his main 2.4 GHz network is
already on channel 6 near vela. Yesterday's two 1+6 scans returned all 4/5
records on channel 1. Task 1 must explain whether the expected BSSID was not
received or its PPDU/channel attribution was wrong; do not dismiss it as a
missing test AP. Compare the exact BE IE layout/field Linux uses and the BSSIDs
actually observed. Network identifiers stay in private qualification inputs and
evidence, never repository text or public logs.

**Further direction (chronium, 2026-10-29):** re-prepare the retained context in
place so a broader mask does not need a reboot, as Linux permits. The exact-mask
limit above is temporary. Design its ownership, retirement and failure handling
at that task; do not implement an unreviewed reset/retry mechanism in task 1.

### Passive scan period — accepted (chronium, 2026-10-29)

Use fixed firmware period **232 per channel** for the RTL8922AE receive-only
1+6 profile and the default passive sweep. The [Linux controls](../development/experiments/rtl8922ae-attribution.md#native-and-linux-controls-vela-2026-10-29)
missed channel 6 at period 45; a channel-6-only control actually used 232 and
received two channel-6 BSSs. The exact Linux known-BSSID comparison waits for
the next Fedora window. This is an accepted receive policy, not a format limit
or a universal Linux default.

This task changes only period 45 to 232. Preserve channel-list contents,
zero SSIDs/no probes, DFS/NO_IR interpretation, common power, RFK, filters,
operation deadline and snapshot lifetime. No new request field or setting.
The default sweep uses the same period when its later delivery lands; this
does not assign the wider sweep or in-place re-preparation work.

Qualify the same two sequential 1+6 scans with the first immutable FILE held
through the second. Verify firmware-reported period 232 and the known channel-6
BSSID using horse's private input. Keep pre-filter tracing temporary and
packet-only. Report empty/partial results or capture loss honestly, never
retrying automatically. Native qualification passed on vela (2026-10-29): both
scans completed with 5/7 records and no truncation; all four firmware visits
reported actual/expected period 232. The privately identified channel-6 AP
appeared twice per scan, correctly attributed and accepted. The first FILE
stayed held and unchanged through the second scan; independent snapshots,
retained context, wired management and byte-exact PXE restoration passed.

Passive visits last longer; caller deadlines and cancellation remain effective.
No fixed period guarantees reception of every AP or beacon interval. The matched
1+6 test isolates duration from the Linux control's separate mask change.

### Ordered delivery

Task 1 is qualified. Task 2 is assigned (chronium, 2026-10-29): the full
admitted 1–13 passive sweep, period 232 and unchanged 15-second deadline, with
first-startup and retained-scan timings reported separately. Further tasks
need their own go; native windows are scheduled by the orchestrator.

1. [x] **Status facts and channel-6 bug investigation.** Capture the
   current baseline before changes, add the observational startup facts and
   matching SDK flag validation/status reader, and audit channel decoding against
   pinned Linux. Compare observed BSSIDs privately with the known channel-6
   network, fix any demonstrated decoder/receive bug, then qualify the existing
   1+6 path and independent FILEs across repetition. Report only redacted results;
   an unexplained missing known network remains a bug, not a qualification pass.
   Native qualification (2026-10-29): startup normalization observed in the
   earlier capture; the period-only change to 232 demonstrated correct known-AP
   channel-6 reception and independent repeated results. No decoder, retune or
   filter fix was needed. See the [capture interpretation](../development/experiments/rtl8922ae-attribution.md#native-and-linux-controls-vela-2026-10-29).
   **After this task chronium can:** tell whether startup normalized an active
   MAC, and inspect evidence for a genuinely second-channel BSS.
2. [ ] **Admitted 1–13 sweep.** Generalize common-image construction and the bounded
   BE list, implement the default-mask semantics and retain existing 512-candidate/
   128-result bounds, successful TRUNCATED handling and immutable publication
   after END/RX retirement. Follow Linux's receive-only tuning and passive/listen
   flags on NO_IR channels, without calibration TX there. Qualify explicit
   subsets and the default full set,
   both known channels, repeated retained scans/held FILEs, unchanged RFK,
   endpoint power checks, deadlines and wired management. Use shared 1+6 scans
   and wired controls for matched interleaved current/new measurements; report
   the new wider sweep's cost separately. Distinguish first startup from retained
   scans; QEMU cannot qualify this radio.
   **After this task chronium can:** run `wifi scan` for the currently admitted
   channel 1–13 set without spelling a mask, including world-domain passive
   listening on 12–13.

### Full-sweep implementation and qualification

The candidate admits zero/default as the complete effective 1–13 set, returns
that selected mask in the immutable FILE, and keeps explicit requests exact.
The same-mask retained check uses the selected mask, so default followed by
explicit full mask reuses the context. A different mask still refuses before
effects. NO_IR-only cold requests refuse before firmware startup; no calibration
channel is borrowed. Both contexts initialize on the lowest requested eligible
channel under a common firmware/SAR/numeric-EIRP image covering the entire mask.

The BE list has at most 13 entries (420 bytes), no probe IDs/null/random TX,
period **232 TU**, and the reference DFS/listen-only bit plus dwell 20 for NO_IR
entries (`fw.c:7487–7494,7814–7818`). The accepted value is unchanged; TU names
the firmware's units, not a new duration choice. Successful END and captured RX
retirement remain the publication boundary; no ENTER-notification gate is added.
The 15-second end-to-end deadline and existing cleanup contract are unchanged.

[Full-sweep record](../development/experiments/rtl8922ae-full-sweep.md).
CPU wire/power checks and builds are separate from native RF qualification.
Native admitted-set/NO_IR behavior and elapsed timings remain pending.

### Route from discovery to association

The following are later tasks, not implementation authorizations from these
decisions. Raise their shaping choices when each task is assigned.

3. **In-place context re-preparation and lifecycle.** Replace the exact-mask
   reboot limit with checked in-place retirement/re-preparation while held scan
   FILEs remain immutable. Qualify cancellation/withdrawal, physical rfkill where
   available and safe re-admission. Keep the accepted recovery requirement;
   a reboot-only bring-up limit is not the everyday contract. This need not
   block an explicit OPEN target on an already qualified channel.
   **After this task chronium can:** broaden a scan mask without rebooting and
   use the qualified cancellation/recovery path.
4. **Legacy station transport and OPEN target.** Establish real management/data
   TX, release/completion, RX metadata and retirement before auth/association.
   Replace the scan-only NOLINK role with a reference-aligned station transition;
   factor the necessary AX200-specific CONNECT, auth-wakeup and network dispatch
   boundaries without changing AX200 behavior. Then qualify one explicit legal
   2.4 GHz/20 MHz OPEN test AP, controlled-port admission, independent DHCP and
   verified data, lease/CUT/close/loss cleanup and wired management. Preserve
   existing authority; do not add Linux sockets or a successful fake TX.
   **After this task chronium can:** use the isolated OPEN test AP on vela.
5. **WPA2 through the existing port.** Ports already provides pinned
   [wpa_supplicant 2.12](../../ports/wpa-supplicant/metadata.lua) as `libwpa-rsn.a`,
   used by the trusted native
   [RSN worker](../../userspace/wifi-supplicant/rsn.c). Reuse its sealed-credential,
   private SUPPLICANT and EAPOL/key boundary. Before protected data, establish
   RTL CAM install/remove confirmation, host-owned TX PN allocation/exhaustion,
   authenticated RX/replay metadata and GTK transitions; H2C DMA consumption
   or a receive ACK is not key installation proof. Qualify credentials,
   controlled-port/key/replay behavior and failure retirement independently of
   AX200. No second supplicant port or four-way handshake is needed.
   **After this task chronium can:** use the selected WPA2-PSK/CCMP profile with
   an explicit connection request.
6. **Remembered connection and everyday recovery.** Deliver the already accepted
   reconnect-across-reboots-until-manual-disconnect requirement using the trusted
   requester and existing credential grants. Decide store ownership, durable
   clear/update and retry/loss semantics at that task; do not choose a store or
   permanent shutdown policy here. The accepted temporary plaintext-key limit
   remains [debt #1287](https://git.internal/PyxisOS/pyxis-os/issues/1287).
   **After this task chronium can:** reboot vela and have its remembered Wi-Fi
   connection restored without retyping the key.

These stages retain the initial legacy profile; WPA3, 5/6 GHz, roaming and
Wi-Fi 6/7 are not implied by a successful WPA2 connection. New firmware commands
and hardware guards must follow the pinned reference, with native evidence;
the AX200 path supplies reusable contracts, not Realtek hardware proof.

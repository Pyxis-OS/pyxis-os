# RTL8922AE scan-input preparation

2026-10-23. This cut adds CPU-side regulatory/table/platform input inspection,
not receive-radio initialization or new RF commands.

Baseline kernel/ABI code is main `2a6ab633` (built at docs-only `6eda843a`),
userland `6de2b65f`; candidate is the accompanying scan-input PR and userland
`99df3b1` before the packaging-only rebase; published dependency is `95f4a3f`.
That captured stack was based on main `0a3e0c03`, whose kernel/ABI code was unchanged
from the baseline. Later diagnostic changes are recorded separately below.
Qualification explicitly uses `DISTRIBUTION=full`. Both use builder `pyxis-llvm23.1.3-3d69e83`, info logs and the same
private live configuration granting Remote only the declared Wi-Fi READ/SCAN
rights. No compiler rebuild or game assets. Cached regulatory and firmware
provenance is retained beside the data and in `firmware/rtl8922ae/metadata.json`.

## Source and host inspection

The mirrored regdb archive matches its pinned SHA-256 and ISC licence. Generated
182-country/183-rule data independently matches the archive's `regulatory.db` in
order, frequencies, bandwidth, EIRP and flags. Deterministic regeneration,
wrong-digest refusal and a host C syntax check pass. Runtime lookup of world00
keeps channels 1–11 unrestricted and 12–13 NO_IR; RO admits 1–13, US 1–11, unknown
XX/lowercase input fails without fallback. These are database observations,
not permissions to calibrate/transmit.

Source-guided host ASan/UBSan decoding of the exact pinned firmware selects
RFE0 explicitly for the live RFE1 power families, with exact RFE1/cut2 BB/RADIO
headlines. Required structural records are present; unknown element28 stays
opaque and absent regional entries stay absent. Source inspection is not
qualification of register-directive semantics or emitted calibration power.

## QEMU

Four-vCPU q35/KVM, host CPU, 4GiB, modern VirtIO net (`disable-legacy=on`),
slirp and Fedora's matching raw OVMF code/variables pair, fresh variables for each
boot. No physical Wi-Fi controller. Full baseline and candidate images build.
Interactive Remote status passes helper validation: absent device, no DMA or
firmware activity, world rules visible, ACPI sources absent and scan UNAVAILABLE.
Matching-symbol GDB confirms the 448-byte status and absent ACPI snapshot. With
`wifi_policy = { channels = 4096 }`, GDB observes country00 and the unchanged
mask at the trusted backend, then UNAVAILABLE because the card is absent.
A one-CPU AX200 opt-in boot on the rebased stack keeps Realtek status empty
and rejects world-policy installation with BAD_REQUEST. That rejection records
the implementation before the later accepted shared `"00"` world-domain
decision and AX200 Install change (#1159), not the intended future behavior.

Existing control, in A1/B1/A2/B2/A3/B3 order:

```sh
iobench read boot://share/iobench.bin --rounds 3 --buffer 4096
```

Each boot warms once then verifies three 1 MiB samples, with 256 payload calls and
EOF. Nine payload samples per code revision (before the packaging-only rebase):

| Revision | Payload median | Range |
| --- | ---: | ---: |
| A | 0.414 ms | 0.243–0.701 ms |
| B | 0.265 ms | 0.235–0.567 ms |

Ranges overlap substantially; these short nested-VM controls do not establish a
speedup or native radio cost. Source inspection adds no work to the file-read
path. Raw local outputs, images and GDB observations remain under
`/shared/present/rtl8922ae-scan-inputs/qemu/`. A transitional VirtIO setup attempt
and a pipe benchmark without its creation grant are excluded, not successful
controls. The corrected runs use modern VirtIO and the ordinary FILE workload.

## Limits

The initial software cut did not qualify native inputs; the separately scheduled
capture is recorded below. Effective programmed board/SAR/antenna limits,
calibration enforcement, operational rfkill and receive initialization remain
unqualified. The existing one-attempt firmware-first diagnostic ends MAC-off/reset.

## Native input capture (2026-10-23)

Chronium authorized the exclusive vela run; the orchestrator ran the sealed
packet at runtime head `e3f383d0` / userland `95f4a3f`. The sequence exited0, its FILE
control verified, and PXE menu restoration was verified byte-for-byte.
Source records: `/shared/present/rtl8922ae-scan-inputs/native/results/run/`.

Firmware0.35.80.3 reached readiness and checked MAC-off/FLR (boot valid/state1f),
with cut2/RFE1, 2/2 NSS/antennas and9154560 retained DMA bytes. Tables decoded
with the explicit RFE0 power fallback. Board countryffff supplied no jurisdiction;
world00 rules were1fff,20dBm EIRP metadata, withNO_IR/AUTO_BW on12–13.

Platform capture completed: WRDS absent; RWRD/RWSI valid; RWGS/RTAG absent;
TAS malformed. These states prove structural discovery, not numeric power
normalization or applied/calibrated limits. The summary does not expose the
RWRD CID/revision/profile count, values or derived per-path ceiling, nor which
DSM/alias check produced the TAS rejection. It does not prove defective firmware.

No scan was requested and RF qualification remains false. The next RF cut needs
qualified normalized SAR/antenna/board limits, programming/readback and calibration
enforcement; the TAS-disabled profile clarification is in the milestone.

## Precise TAS and fixed SAR observations (2026-10-23)

Chronium accepted TAS-disabled scanning with an independently enforced fixed
SAR envelope. Runtime head `1700140f`, userland `b7e9cb0`, based on main
`6ea06599`, adds the exact TAS rejection and normalized SAR inputs, not RF
initialization. Status is now 544 bytes (radio inputs 256); matching consumers
are rebuilt together. The preceding native capture and its images remain frozen.

Host ASan/UBSan checks exercise 36 supported CID/revision/antenna/profile shapes,
mixed-source intersections, every retained length through 600 bytes, inconsistent
span metadata and required-source errors. HP conversion is `80 + 4 * raw`
eighth-dBm with path indices 1/0 and a 24 eighth-dB 2TX reduction; RT values
already use eighth-dBm, indices 0/1 and no reduction. All retained profiles
constrain both paths; present WRDS is independently intersected. TAS/RWSI errors
cannot boost or select a different ceiling. Unknown/bad required sources yield
no numeric envelope; present uninterpreted geographic constraints are refused.

The info/full target build passes. Four-CPU KVM QEMU with the earlier q35/OVMF/
VirtIO setup verifies the matching helper, 544/256-byte layouts, absent source
states, no invented numeric limits, and scan UNAVAILABLE. GDB observes the
actual absent ACPI snapshot and empty TAS diagnostic. A one-CPU AX200 comparison
keeps all RTL observations empty. An initial comparison used the wrong ISO
configuration path and is excluded; the corrected boot overrides
`boot/limine/limine.conf`.

Existing 1 MiB FILE controls verify all bytes (three warmed samples per boot).
Pre-diagnostic `a7be74cf` has a 0.254 ms payload median, range 0.252–0.322 ms;
the new observer has 0.311 ms, range 0.283–0.332 ms. These short overlapping
nested ranges do not establish a performance change; the rebase also includes
the intervening shared world-policy/package work. A later control taken during
a concurrent image build is excluded from timing comparison. No native timing
or RF-cost claim is made.

The new private packet is `/shared/present/rtl8922ae-sar-details/native/`.
Its marker, rebuilt archive/helper and ordinary FILE control pass in QEMU;
offline staging/restoration against a temporary PXE root preserves the menu and
protected payloads byte-for-byte, including publication under restrictive umask.
Both this and the frozen first packet passed their checksum checks. At that
software checkpoint no PXE/target/VFIO action had run for this extension;
the separately scheduled native numeric/TAS capture is recorded below.

## Power-programming evidence and remaining gate

Source inspection of the pinned Linux reference distinguishes intended hardware
limits from claims about emitted power. `core.c:465–478` programs the full channel
power settings before band-change RFK/TSSI. `rtw8922a.c:2238–2274` programs normal
and RU rate limits plus separate path A/B BB maxima; TSSI requests hardware TX
(`fw.c:6422–6438`) but that does not show a bypass of those maxima. The H2C lacks
an independent numeric cap and can use already programmed registers. Port
qualification must therefore check limits and ordering before calibration,
not infer a bypass or require a new external certification test from that absence.

Normal rate-limit pages begin at `0x11fac`, RU pages at `0x12048`; path maxima
use `0x2e61c`/`0x2e71c`, bits 20:12. The reference uses RF quarter-dBm table
values, MAC half-dBm values and BB reference eighth-dBm values; the BB path-max
field specifically receives RF quarter-dBm (`rtw8922a.c:2238–2260`). Downward conversion and
1TX/2TX reductions must survive programming. Reference/path/TSSI settings and
force/bypass controls also need checked ordering/readback; zeroed initialization
tables are not qualified ceilings.

An independent unpack of the exact firmware confirms explicit WW0, 20 MHz,
beamforming-off records on channels 1–13. Before SAR intersection, 1TX CCK is
14.5 dBm on 1–11, 13 on 12 and 10 on 13; 1TX OFDM is 14.5 throughout. 2TX CCK
is 10.5 on 1–12 and 7 on 13; 2TX OFDM is 11.5 throughout. These are nominal
conducted reference-table ceilings, not measured radiated EIRP. No missing row
was replaced by a zero or fallback. Other regional families have explicit gaps.

World00 disables dynamic antenna correction in the reference (`regd.c:24–25,
843–853`); absent RTAG likewise leaves that optional correction off. The standard
WW board tables and SAR still constrain power (`phy.c:1998–2002,2138–2163,
2342–2412`). Zero correction does not mean a physical 0 dBi antenna. The records
contain no physical antenna-gain field and do not supply a general regdb
EIRP-to-path conversion. Preserve the reference baseline and state its assumptions;
do not manufacture a gain from absence. Actual applied limits/calibration order
remain the next implementation/qualification gate before newly enabled RF.

## Native SAR-details capture (2026-10-23)

Chronium granted the exclusive vela window; the orchestrator ran the packet at
`1700140f` / userland `b7e9cb0`. The third attempt completed with sequence exit0,
verified FILE reads and byte-for-byte PXE restoration (`ORIGINAL VERIFIED`).
Source: `/shared/present/rtl8922ae-sar-details/native/results/run/summary.json`.
The packet and results are now frozen. No additional RF request was submitted.

The first attempt timed out before Remote READY: no command was sent and
nothing was staged. Chronium rebooted vela into the ordinary image and Remote
recovered; the old cause was not captured and must not be attributed to the
switch, DHCP or SAR code. The second attempt stopped on a results-directory
collision with the manual preflight, not a target failure. Its successful manual
read is retained as `results/inspect-manual-preflight-1430/`; the original timeout
is retained under `results-failed-preflight-20261023/`. The helper now preserves
client stderr. The unused transport diagnostic was not run.

Firmware0.35.80.3 reached readiness and checked MAC-off/FLR, boot valid/state1f,
with the same fixed DMA/ring backing as the first capture. Country00 rules and
explicit RFE0 table selection match the earlier observations. WRDS absent,
RWRD/RWSI valid and RWGS/RTAG absent remain distinct.

TAS diagnostic: `stage=query-0 reason=type object=integer alias-conflict=0`.
Our function-0 reader expects a buffer; no integer value/support mask is
inferred, and function5 was not called. This is the exact local rejection, not
proof that Linux's direct function5 reader would reject the same machine.
The accepted TAS-disabled profile needs no waiver of a required SAR constraint.

RWRD metadata: CID0x5452 (RT), revision2, four antennas, eleven subbands, one
profile. The CPU decoder yields128 eighth-dBm = **16 dBm** for pathA and pathB,
both1TX and2TX. That CID has no HP-specific3dB reduction; identical1TX/2TX
results are expected. There is no invented static WRDS or unobserved profile.
The nominal WW/RFE0 normal table remains lower (maximum14.5dBm1TX,
11.5dBm2TX before SAR), so the16dBm SAR input must not raise those board limits.

This closes the first task's input observations. Actual hardware power limits,
antenna/EIRP assumptions and calibration enforcement are not measured by this
capture. The next task must preserve the pinned standard WW power path with
optional dynamic antenna correction off, intersect fixed SAR, check normal/RU
and per-path programming/rounding before RFK, and retain ongoing rfkill and
checked stop. World NO_IR channels12–13 do not acquire calibration/transmit
permission from their passive rule. No new policy decision follows from these
readings; a concrete unresolved enforcement issue must return for review before RF.

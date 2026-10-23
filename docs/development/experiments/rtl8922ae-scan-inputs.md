# RTL8922AE scan-input preparation

2026-10-23. This cut adds CPU-side regulatory/table/platform input inspection,
not receive-radio initialization or new RF commands.

Baseline kernel/ABI code is main `2a6ab633` (built at docs-only `6eda843a`),
userland `6de2b65f`; candidate is the accompanying scan-input PR and userland
`99df3b1` before the packaging-only rebase; published dependency is `95f4a3f`.
The final stack is based on main `0a3e0c03`, whose kernel/ABI code is unchanged
from the baseline. Qualification explicitly uses `DISTRIBUTION=full`. Both use builder `pyxis-llvm23.1.3-3d69e83`, info logs and the same
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

Native table/platform states, effective board/SAR/antenna limits, calibration
power enforcement, operational rfkill and receive initialization are unqualified.
No native/PXE/VFIO action was performed for this record. A separately scheduled
native input capture is required before the next RF-capable implementation cut.
The existing one-attempt firmware-first diagnostic still ends MAC-off/reset.

## Native input capture (2026-10-23)

Chronium authorized the exclusive vela run; the orchestrator ran the sealed
packet at runtime head `e3f383d0` / userland `95f4a3f`. Current changes since
that image are documentation/comments only. The sequence exited0, its FILE
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

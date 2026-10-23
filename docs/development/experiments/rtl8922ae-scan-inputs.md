# RTL8922AE scan-input preparation

2026-10-23. This cut adds CPU-side regulatory/table/platform input inspection,
not receive-radio initialization or new RF commands.

Baseline kernel/ABI code is main `2a6ab633` (built at docs-only `6eda843a`),
userland `6de2b65f`; candidate is the accompanying scan-input PR and userland
`99df3b1`. Both use builder `pyxis-llvm23.1.3-3d69e83`, info logs and the same
private live configuration granting Remote only the declared Wi-Fi READ/SCAN
rights. No compiler rebuild or game assets. Cached regulatory and firmware
provenance is retained beside the data and in `firmware/rtl8922ae/metadata.json`.

## Source and host inspection

The mirrored regdb archive matches its pinned SHA-256 and ISC licence. Generated
182-country/183-rule data independently matches the archive's `regulatory.db` in
order, frequencies, bandwidth, EIRP and flags. Deterministic regeneration,
wrong-digest refusal and a host C syntax check pass. Runtime lookup of world00
keeps channels1–11 unrestricted and12–13 NO_IR; RO admits1–13, US1–11, unknown
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
Matching-symbol GDB confirms the448-byte status and absent ACPI snapshot. With
`wifi_policy = { channels = 4096 }`, GDB observes country00 and the unchanged
mask at the trusted backend, then UNAVAILABLE because the card is absent.

Existing control, in A1/B1/A2/B2/A3/B3 order:

```sh
iobench read boot://share/iobench.bin --rounds 3 --buffer 4096
```

Each boot warms once then verifies three1MiB samples, with256 payload calls and
EOF. Nine payload samples per code revision:

| Revision | Payload median | Range |
| --- | ---: | ---: |
| A | 0.414ms | 0.243–0.701ms |
| B | 0.265ms | 0.235–0.567ms |

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

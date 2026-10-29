# RTL8922AE admitted passive sweep

Task 2, qualified (chronium, 2026-10-29): full admitted channel 1–13 scan;
period 232, existing 15-second deadline; first-startup and retained timing.
No association, per-hop host RFK, background scan or new authority.

## Before-edit baseline

Kernel `b6e3b74184e17597316d9f55b7f7eb5c5a10f998`, userland
`e077f20fb2fc6cc3284df69ec0b9f81c0e7bf632`, published builder
`pyxis-llvm23.1.3-8bdf136`. Fresh selected controls, full SDK and unchanged helper
sources built before code. Four-CPU q35/KVM/host, 4 GiB, OVMF, virtio/slirp,
no radio/passthrough/disk: identity/marker, wired/log/status, invalid/absent-radio
requests, GDB and nine verified 1 MiB iobench reads (4096-byte buffers) passed.
Guest and helpers retired. Local evidence:
`/home/chronium/src/rtl8922ae-full-sweep-20261029/baseline/qemu/`.
The earlier native period-232 [1+6 capture](rtl8922ae-attribution.md#matched-period-232-pyxis-result-vela-2026-10-29)
remains the RF baseline; no new native action is claimed here.

## Reference and CPU qualification

Pinned Linux v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449`, rtw89:
`fw.c:7487–7494,7814–7818` supplies NO_IR's DFS/listen-only form and dwell 20;
`fw.c:5728–5733,7949–7964` encodes/accumulates period in TU. Keep wire value
232, not a converted millisecond value. The 13-entry list is 420 bytes, below
the existing 2,000-byte H2C capacity and reference 60-entry BE bound.

Local host checks execute actual native source with modeled transport/time or
MMIO; they do not qualify a device. Independent reference tuples cover full
list, NO_IR 12–13, zero probes/null/random/SSID, and ACK/END distinctions.
The 1+6 ADD+START payloads remain byte-identical to baseline. Firmware-backed
power checks cover all active signed normal/RU limits, both contexts, asymmetric
SAR and world/regulatory caps including AR/channel-4/SAR24 against 20 dBm EIRP.
The old 1+6 power image is byte-identical. Incompatible/unsupported inputs clear
the output; no fallback. Reproduction scripts/results stay local in `wire-checks/`
and `power-checks/` under the task evidence directory.

## Native qualification — vela, 2026-10-29

Chronium/orchestrator ran sealed `native-full-sweep-20261029`, fixed order
A1-hop/B1-hop/A2-hop/B2-hop/B3-default, once with no retry. A production source
`b6e3b74184e17597316d9f55b7f7eb5c5a10f998`, private kernel
`469d037468651fbed3e932707493e1f783831ea4`; B production source
`695792a2d25d5710f627522ae2c45850c0fdd35a`, private kernel
`d70d74307d31ad5cbb6a91314b4b11a54ffee154`. Both used the same compiler,
firmware 0.35.80.3, world `00`, validated SAR, current userland
`053e65dfe44d885aa32acd56b163dc9fa4ad68a0`, helpers/config and temporary observer.
Raw network identifiers stayed private. Public evidence:
`/shared/present/rtl8922ae-passive-scan/native-full-sweep-20261029/results/summary.json`.

| Profile | First-startup request to terminal (ms) | Retained request to terminal (ms) |
| --- | --- | --- |
| A: previous 1+6, two samples | 681.556–684.103 | 480.557–480.606 |
| B: new 1+6, two samples | 680.247–700.216 | 480.532–480.553 |
| B: full 1–13, one pair | 3339.429 | 3107.075 |

Times are target monotonic samples immediately before the request and after
terminal STATUS, including observation and matched packet-only trace overhead.
They are **profiled**, not uninstrumented production latency. The two matched
pairs do not demonstrate a material regression; they do not establish a precise
population difference. The wider full sweep is separate work, not an A/B speedup.

All five cases qualified and the sequence exited zero. Each hop operation
returned six records. The full pair selected mask `0x1fff` from request zero,
then reused that same context for explicit `0x1fff`, returning 9/9 records with
observed channel mask `0xa7`; no truncation or capture drops. Firmware ENTER/
LEAVE order covered 1–13 at actual/expected period 232. Every case retained the
first unchanged FILE through its independent second snapshot, preserved the
known channel-6 AP with no wrong-channel attribution, kept RFK `0xf` while
power checks advanced 11 to 12, and preserved wired management. PXE restored
byte-for-byte. All original source/images/seals and earlier failures remain intact.

Source rebase onto merged attribution/main changes no Wi-Fi or ABI code from
the frozen candidate. Host/no-radio results are distinct from this native pass.
Firmware visits on NO_IR 12–13 do not prove reception from a known AP there or
absence of radiated energy. Forced deadline/cancellation, physical rfkill,
recovery, general mask re-preparation and association remain later gates.

# RTL8922AE admitted passive sweep

Task 2, assigned (chronium, 2026-10-29): full admitted channel 1–13 scan;
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

## Native gate — pending

The sealed packet will match old/new 1+6 controls with fixed compiler, firmware,
helper, userland and request inputs, then run the new default full set separately.
Each boot permits two sequential scans, preserving the first immutable FILE
through the second. Target monotonic request-to-terminal timing separates cold
startup from retained scans; firmware period reports are separate observations.
Known AP comparison stays private. World NO_IR 12–13 listening, full-mask power
checks, immutable publication and wired management require the native run.
No timing or RF result is inferred from the no-radio QEMU or host checks.

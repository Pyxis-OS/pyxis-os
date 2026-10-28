# RTL8922AE: one-channel firmware passive discovery

Native one-channel discovery passed on vela on 2026-10-28. This is the first
slice of the [RTL8922AE milestone](../../wip/rtl8922ae-wifi.md), following the
[PHY/RFK qualification](rtl8922ae-phy-init.md). Two normal same-channel requests also passed as recorded below. Hopping,
cancelled scans, association and data transmission remain unqualified.

## Revisions and configuration

Native candidate `f85199a6ffd167d46a24e84c5ca492260aae9304`, userland
`b9776609ccfb56ec33fc7d9d80cedcaccdf1306f`; packet
`/shared/present/rtl8922ae-passive-scan/native-dbcc-20261028/`, results in
`results/run/`. Firmware 0.35.80.3, cut 2/RFE 1 with the accepted RFE-0 table
fallback, country 00, TAS disabled, fixed validated SAR limits. Clean RAM-only
PXE image, wired USB management, disk writes disabled; no association.
The integration PR carries the tested DBCC driver sources, with current-main
rebases and updated dependency pins qualified separately from this native head.

Reference: Linux v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449`, BSD
alternative; reached source notices remain in `kernel/wifi/NOTICE`. Successful
Linux firmware/MMIO capture used Fedora 7.2.10 and the same device/firmware.

## Native result and attribution

One explicit channel-1 request returned a real immutable result FILE:
`status=0 stage=complete count=2 rx=observed`. The helper verified read-only
rights, second-TAKE refusal and unchanged reread after operation close.

| Observation | Native result |
| --- | --- |
| Radio | READY, healthy context retained |
| Interrupts / firmware and ordinary packets | 32 / 55 |
| Passive mask / PHY state | 1 / `0xcf` |
| Effective-power verification boundaries | 10 |
| Physical SAR ceilings, quarter-dBm A/B | 64 / 64 |
| RFK completed / final report | `0xf` for both PHYs / OK, version 30 |
| Valid rfkill observations | 30 |
| OFLD_CFG DONE_ACK | raw `00`, sequence 1 |
| Stop | Not attempted: healthy context retained as accepted |
| Wired management / PXE restoration | Preserved / menu restored byte-for-byte |

The preceding topology-only native head `08ae261d5c94` rejected START with
`raw=03 code=03 seq=15` despite the complete source-derived START payload
matching Linux. Its OFLD_CFG ACK succeeded. Earlier isolated OFLD_CFG and bitmap
changes also left START rejected. Coherent DBCC initialization removed that
rejection while preserving START bytes, RX filter, firmware, channel and limits.
This identifies the missing DBCC initialization state in this comparison; it
does not isolate a particular register or assign a firmware-internal name to 03.

DBCC now includes CMAC1/BB1, reference quotas and notification, both indexed BB
contexts, topology `0x1011` and required per-PHY calibration on channel 1.
Power is applied/verified on both MAC banks with shared physical SAR before
RFK and after every helper. RFK status reports the intersection of both PHYs'
completion masks. The bounded checked BAR2 mapping is `0x60000` for PHY1 control
registers; RTL8922A still downloads one BBMCU firmware image, as Linux does.

## Retained-context repetition (2026-10-28)

Native kernel `4b6c74ccfa607fbca0ea8635a092ee3a9b0dca1d`, userland
`1b546c9f7611a2a52cb2eeef181a2f0d353ec73a`; packet
`/shared/present/rtl8922ae-passive-scan/native-repeat-20261028/`, authoritative
result `results/run/summary.json`. The helper verified the running kernel.
This is native evidence for this integration head, separate from the earlier
single-request head and its matched controls below.

| Observation | Native result |
| --- | --- |
| Sequential channel-1 requests | Both status 0 / complete; counts 2 and 1 |
| First FILE held through second request | Unchanged bytes, size and read-only rights |
| Snapshot identity and close lifetime | Independent FILE identities; operation closes passed, then FILE2 survived FILE1 close |
| Power verification boundaries | 10 after each request |
| Host startup/RFK | No additional startup/RFK; boot state 3/3, RFK `0xf`/`0xf` |
| Radio / PHY state | READY, healthy context retained / `0xcf` |
| Wired management / PXE restoration | Preserved / orchestrator verified byte-exact restoration |

The first snapshot remained readable after the second operation closed, and
the second remained readable after the first FILE closed. Different counts
are ordinary independent observations, not a requirement that snapshots differ.
No stop was attempted on healthy completion. These observations do not expose
firmware-internal calibration or measure radiated power.

## Local checks and matched controls

The frozen DBCC kernel built using `pyxis-llvm23.1.3-8a4d001`; its archive reused
unchanged SDK/userland/helpers/firmware apart from the private identity marker.
Source review, exact-image QEMU/GDB and disposable full-image stage/restore
passed. QEMU: q35/KVM, host CPU, four CPUs, 4 GiB, OVMF, virtio/slirp, no radio
or disk. Running kernel/marker, wired Remote/log/RAM, invalid-channel refusal
and honest absent-radio UNAVAILABLE passed; GDB detached and owned jobs retired.
These checks do not execute RTL MMIO or firmware. Current integration checks
and exact-head CI are recorded in the PR separately.

The frozen production baseline used three existing iobench invocations, then
matched controls ran B/A/B/A/B/A, one invocation per boot:

```sh
boot://iobench.pxe read boot://share/iobench.bin --rounds 3 --buffer 4096
```

Each invocation includes the existing warmup and three verified 1 MiB samples.
Matched measurements below have nine samples per revision; all reads completed.

| Matched control | A: frozen production | B: passive slice |
| --- | --- | --- |
| Payload median (range), ms | 0.272 (0.253–0.311) | 0.268 (0.251–0.298) |
| Complete median (range), ms | 0.279 (0.261–0.324) | 0.275 (0.259–0.307) |
| Per-boot payload medians, ms | 0.297 / 0.265 / 0.256 | 0.268 / 0.255 / 0.269 |

Ranges overlap and A itself trends downward; no performance gain or active-RX
cost follows. Compiler activity had subsided before timed cohorts. Captured
intervals show 80–96% host idle and 0% steal; two unrelated idle QEMUs were left
alone. These are nested-VM whole-integration controls, not isolated driver
measurements or native performance. The original baseline and all variation
remain in the raw records. Every owned guest/client/monitor was retired by
recorded process identity; no native/PXE action occurred.

## Limits

Native qualification covers one normal channel-1 request and two sequential
channel-1 requests on a retained healthy context, with independent immutable
snapshots. It proves result publication and ordinary management-frame RX for
those requests, not hardware stop. No hopping, cancellation,
physical rfkill, radiated-power, recovery or connection claim. No probe/null/data
TX; initialization/RFK is TX-capable under the accepted regulatory/SAR profile.
Historical stop qualification remains in the preceding PHY/RFK record.

Raw redacted reports and prior refusal/reference-comparison evidence stay under
`/shared/present/rtl8922ae-passive-scan/` and PR #1528; SSIDs/BSSIDs and raw
hardware identity are not part of repository evidence.

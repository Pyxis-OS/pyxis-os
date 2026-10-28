# RTL8922AE bounded hopping qualification

Implementation checkpoint, native qualification pending. The accepted contract
is in the [milestone](../../wip/rtl8922ae-wifi.md#channel-hopping--accepted-chronium-2026-10-28).
No native/PXE action is performed by these source/host checks.

## Inputs and baseline

Baseline kernel main `750cd0b99e685c07eb55f9ffe0fd7db1cf83da1d`, built before
implementation with published `pyxis-llvm23.1.3-8bdf136`. Four-CPU q35/KVM,
host CPU, 4 GiB, OVMF, virtio/slirp, no radio or disk. The unchanged clean
qualification archive pins userland `1b546c9f`; only the new baseline kernel is
substituted. Actual running kernel and archive marker were checked via Remote;
GDB observed idle/absent radio, and owned processes retired through QMP.
Three existing iobench invocations each ran a warmup and three verified 1 MiB
RAM-archive reads, buffer 4096. Raw baseline and later matched control records
stay local under `rtl8922ae-hopping-20261028/`; the final PR/packet identifies
exact candidate and userland heads. Integration subsequently incorporates the
merged poll stack; these controls do not isolate a radio-performance effect.

## CPU-only checks

Reference Linux v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449` defines
BE descriptors, PPDU MAC/PHY prefixes, IE01 and normalized current-group matching.
Transient manual wire fixtures passed 24 cases / 2,795 checks under plain GCC
and ASan/UBSan, with no sanitizer finding. Inputs were overwritten/freed after
each callback. Cases cover channels 1/6, per-BB isolation, variable prefixes,
missing/invalid/late/unrequested reports, no report cache, fixed-channel fallback,
reset/stop, and pending/final capacity while retaining fitting records.

The 513-candidate case keeps 128 records, counts one pending-capacity drop and
384 final-capacity drops: 385 total. Existing final records still merge when
full. No firmware, MMIO, DMA or RF is simulated. FILE serialization is separately
source-inspected; native publication remains a later qualification gate.

The pinned firmware and accepted world/SAR-16 tables prepare compatible 1/6
nonlimit controls. Their signed common normal/RU/path image equals the prior
channel-1 image, including SAR 64/64 quarter-dBm. Existing regulatory regression
still admits 296 / refuses 16 cases with zero numeric ceiling violations; invalid
common-mask/input preparation clears its output. This proves CPU construction,
not physical RF enforcement.

## Integrated QEMU and matched controls

Candidate kernel `4c09ccb240cdef4e4e5c29068cc9528e8c85d79a`, pinned userland
`8cefca4017769e5e79d455cfff127947554a6fc3`, built with the same published
builder. Matching SDK/CLI/private helper rebuilt; unchanged ordinary control
executables and archive fixture reused intentionally. Four-CPU q35/KVM/OVMF
checks passed actual kernel/archive identity, wired/log/status, new CSV syntax,
invalid channels and absent-radio refusal before either helper scan. GDB showed
64-byte pending entries, zero queued candidates and collection disabled. Every
owned guest/client/monitor was retired. This does not emulate Realtek RF.

The existing iobench command was run three times per boot, three verified samples
per invocation after its warmup, interleaved A1/B1/A2/B2/A3/B3 (27 samples each):

```sh
boot://iobench.pxe read boot://share/iobench.bin --rounds 3 --buffer 4096
```

| Nested RAM control | A: main750cd0b99 | B: candidate4c09ccb24 |
| --- | --- | --- |
| Payload median (range), ms | 0.247 (0.230–0.573) | 0.249 (0.233–0.314) |
| Complete median (range), ms | 0.257 (0.237–0.593) | 0.256 (0.240–0.324) |
| Per-boot payload medians, ms | 0.258 / 0.247 / 0.239 | 0.251 / 0.250 / 0.238 |

Ranges overlap, and pair direction changes. No isolated radio-performance gain
or regression follows. Both have no radio; B also contains the merged poll
integration. Interval VM observations were mostly idle with no steal; initial
since-boot vmstat rows are not cohort observations. Native wired/RF cost remains
unmeasured by these nested controls.

Packet parser checks passed 16 synthetic host cases, including valid empty and
truncated results and refusal of inconsistent metadata/extra host RFK. Full-image
stage/restore passed on a disposable local PXE fixture under umask077, including
byte-identical menu, other entry/root/EFI preservation and readable TFTP payloads.
No real PXE path or target was touched. The finished once-only native packet is
`/shared/present/rtl8922ae-passive-scan/native-hopping-20261028/`; its manifest
records exact production heads and hashes.

## First native attempt: startup refusal (2026-10-28)

The orchestrator ran that packet once on vela with the frozen kernel and
userland above. Kernel identity, wired management and byte-exact PXE restoration
passed. The first request failed with `CALL_IO` (19); the second was not run.
Mask `0x21` is channels 1 and 6. The helper's `stage=completion` identifies its
wait for the asynchronous operation result, not completed firmware scanning.

Status reported `RTL8922AE MAC already powered on`, no firmware security/image
facts, zero interrupts/packets and PHY state `0x10`: only terminal stop was
confirmed. No Pyxis firmware download, PHY/RFK initialization, channel list or
START command was reached. The zero counters do not establish RF silence of the
inherited hardware state. Power-off and PCI stop completed (`boot.state=0x1d`).

The refusal is the active WLMAC state at `0x03f0`, checked before the power-on
sequence in `rtl8922ae_hw_power_on`. Pinned Linux `mac.c:1484-1487` has the same
inner refusal, but `rtw89_mac_pwr_on` at lines 1517-1527 powers off and tries
power-on once after failure. Pyxis explicitly omits that wrapper under the
bring-up one-attempt policy. An inherited powered MAC after a warm handoff is
therefore the supported diagnosis; the capture does not identify what left it
active. This attempt did not exercise the hopping implementation.

**Accepted (chronium, 2026-10-28):** when the initial MAC is active, use the reference
power-off sequence and one fresh power-on within the original request deadline;
stop on failure, with no automatic firmware-download or scan retry. The active
state is checked again by the second power-on attempt, as in the reference;
there is no loop. This replaces the explicit startup-wrapper deferral. Raw
results remain in the old packet's `results/` directory. Native hopping and the
new inherited-active startup handling remain pending qualification.

## Limits and next evidence

The immutable result grows from 16 to 24 header bytes in the development ABI;
flags/drop count persist after operation close. There is no legacy-format shim.
SCAN STATUS stays its existing size. Kernel, SDK, CLI and private helper must
match; no retained release ABI is listed by this development tree.

Native qualification must establish firmware ENTER/END for 1+6, actual matched
PPDU observations where an AP is heard, common-image endpoint checks, retained
same-mask repetition, unchanged first FILE and independent close lifetimes,
rfkill observations and wired management, followed by byte-exact PXE restoration.
No nearby AP or per-channel nonempty result is guaranteed. No radiated-power,
physical-rfkill, association, retry/recovery or full-mask claim.

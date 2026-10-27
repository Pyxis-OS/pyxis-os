# RTL8922AE: one-channel firmware passive discovery

Local integration qualification and first native refusal, 2026-10-27. Native
scan/RX is not qualified; the
preceding [PHY/RFK pass](rtl8922ae-phy-init.md) does not qualify this protocol
or ordinary frames. The [milestone](../../wip/rtl8922ae-wifi.md)
records the accepted firmware-offload and retained-context contracts.

## Revisions and source inspection

Candidate kernel `54f9ff7e86c99afdea4d05378a7d83b1f00eb6e6`, userland
`e945b7bb39b0ae127497df4e3ae036bb73936533`; comparison kernel
`eb92ab2b4e18a5f43c48dd8fea504db393bdf946`, userland
`a1bece881e3c2bc833da3a1d71777d84d83ae487`. The candidate rebuilds the C SDK,
runtime, six hash-checked cached libraries and C applications with
`pyxis-llvm23.1.3-8a4d001`. No compiler rebuild, personal archive or game data.
Subsequent documentation changes do not change the frozen candidate.

Pinned Linux v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449` selects
modern BE scan offload for firmware0.35.80.3 (`fw.c:854–861`,
`mac80211.c:1214–1247`). The native implementation follows its channel-list,
START/STOP and END_SCAN path. A zero-SSID request uploads no probe templates;
the unrestricted reference dwell is 45 ms (`fw.c:7439–7498`). It preserves the
reference ordering of RX synchronization, EDCCA, CAM, receive filters and DIG
at start and completion (`fw.c:8036–8142`, `core.c:5880–5948`).

BE ordinary RX supplies no channel number. This slice stays on the explicitly
requested, already calibrated channel and drains preceding RX before opening
collection; optional DS/HT information must agree. Inline RSSI uses the
reference conversion; unavailable signal remains the native `-256` convention.
No register equality refusal is added. The BSD alternative and reached source
notices are retained in `kernel/wifi/NOTICE`.

## Local build and QEMU

Kernel, rebuilt C userland and the private native result checker compile.
QEMU configuration: q35/KVM, host CPU, four CPUs, 4 GiB, OVMF, virtio/slirp wired
network, no disk or radio. Remote, beacon and debugger listeners bind loopback.
The actual running kernel and archive marker were verified.

READ reports absent/unprepared radio, zero DMA and idle PHY/RFK before and after
checks. `wifi scan --channel 0` and `15` reject malformed requests;
`wifi scan --channel 1` and the private result checker honestly return
UNAVAILABLE with no radio. Wired DHCP/Remote and log-to-RAM/read controls pass.
Read-only GDB inspection confirms unclaimed transport and inactive scan state;
it detached before benchmark samples. These checks do not execute RTL MMIO,
firmware or ordinary radio RX.

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

## First native run: START completion failed

The orchestrator ran `native-one-20261027` on vela with actual running kernel
`54f9ff7e86c9` verified before and after the request. The helper reported
`channel=1 status=19 stage=completion count=0 rx=unproved`. The kernel detail was
`RTL8922AE scan START ACK failed/timed out`. No result FILE was delivered.
PHY state was `0xdf`, all four RFK helpers completed, five power checks passed,
and MAC-off/FLR stop was confirmed with last-status19. Wired management and
byte-exact PXE restoration passed. This is a failed discovery run, not a
regression in the preceding PHY/RFK gate or proof of ordinary RX.

The combined detail establishes successful channel-list ACK and START DMA
consumption, then loses the distinction between negative ACK, missing ACK and
receive/transport failure. Status19 is native CALL_IO, not the firmware's ACK
return byte. The preserved capture cannot establish the exact firmware-side
cause.

### Reference comparison and correction

Pinned Linux source, unchanged from the commit above:

| Step | Linux behavior and comparison |
| --- | --- |
| Version/layout | `fw.c:856–865`: firmware0.35.80.3 uses the current40-byte BE configuration and32-byte channel element with period in `w7`; no V0 fallback. |
| List | `fw.c:5644–5748,7439–7498`: channel1,20MHz,45ms, pause-data, zero SSIDs/probes/nulls and eight packet IDs NONE match. Its DONE_ACK succeeded natively. |
| START | `fw.c:8217–8236,5861–6080`: standalone, MACID/port/band0, no MACC or operating-channel entries, `opch_end=0xff`, MLO`0x1002` and size words10/1/5 match this single-PHY profile. |
| Prohibited6GHz | `fw.c:5839–5857,5909,5973–5976`: with no exposed6GHz band Linux writes an all-prohibited bitmap. Pyxis left `w6/w7` zero; both now contain `0xffffffff`, also for STOP. |
| Envelope | `fw.c:1453–1474`: category1/class9, functions`0x16/0x2c`,8-bit sequence and receive/done-ACK bits match. |
| Completion | `fw.c:6074–6079`, `mac.c:5222–5230`: START waits DONE_ACK and checks the low six return bits. END_SCAN completes STOP (`mac.c:5744–5750`). The correction keeps that behavior. |
| Timeout | `core.c:5403–5404`, `core.h:4568`:100ms completion window after TX returns. Pyxis previously started it before blocking DMA consumption. It now installs the wait before TX, then starts the same100ms window after TX returns, still bounded by the native operation deadline. |

The omitted bitmap and reduced ACK window are source-proven differences; neither
is proven to explain this native failure. Existing status detail now distinguishes
malformed C2H, transport failure, timeout/interruption and a rejected DONE_ACK
with raw return, masked code and sequence. It adds no ABI field, klog producer,
retry, version fallback, longer timeout constant or new hardware refusal.

## Fresh native packet

A separate `native-start-reference-20261027/` packet under
`/shared/present/rtl8922ae-passive-scan/` preserves the failed packet and results.
It retains the one-request channel1 boundary, verifies actual kernel/archive
identity, and checks a real immutable READ-only FILE, second-TAKE refusal and
unchanged reread after operation close. No SSID/BSSID is printed. A valid empty
FILE leaves ordinary RX unproved and never triggers a retry.

Healthy completion must retain READY1, PHY`0xcf`, boot valid/state`1f/3`, all
four RFK reports, power/rfkill observations and wired management. It leaves the
radio powered with idle RX drained, not a hardware-stop claim. Failures collect
observations and stop without a second RF request. No physical rfkill,
repeated/cancelled hardware request, radiated-power, MSI-delivery, association,
data or multi-channel discovery claim is made. The earlier disposable staging
check preserved menu/other entries/root/EFI; fresh image checks and qualification
are reported on the PR. Raw evidence remains under the shared task directory.
Only the orchestrator schedules/runs the fresh native packet.

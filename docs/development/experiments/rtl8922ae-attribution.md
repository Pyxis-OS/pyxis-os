# RTL8922AE channel attribution and passive duration

[Task 1](../../wip/rtl8922ae-wifi.md#next-discovery-slice--accepted-chronium-2026-10-29)
adds startup observations and investigates a known nearby channel-6 network.
Its BSSID and raw captures remain private. Channel-6 reception in Pyxis is not
qualified; no decoder, RF retune, filter or dwell fix has been implemented.

## Software baseline and observation boundary

Before edits: exact main kernel `c65430c61e6b1ed91d5951c6edb9a85d951f4b8d`, published
builder `pyxis-llvm23.1.3-8bdf136`, four-CPU q35/KVM/host, 4 GiB, OVMF,
virtio/slirp, no radio or disk. A frozen userland `8cefca4` control archive was
intentional, not a full current-main image claim. Identity/marker, wired/log/
status, invalid/absent requests, GDB and nine verified 1 MiB iobench reads with
4096-byte buffers passed before code changes. Source candidate
`6f1ae9cd94367a156037ec0ff042b6021f467921`, reader pin
`2dfa643f695f90ff32f953c931cec542c409c31b`, passed matching no-radio QEMU/GDB
and selected runtime/SDK/Lua/CLI/control builds. These do not qualify hardware.

Four flags in the existing PHY state word record the initial WLMAC sample,
active state, startup cycle attempted and cycle completed. Completion requires
power-off plus successful fresh power-on; failure retains progress. No extra
MMIO, retry, allocation or production log. Replies remain 544/600 bytes. The
reader validates implications; sampled non-active is not RF-off proof.

## Native and Linux controls (vela, 2026-10-29)

| Capture | Request / actual firmware period | Receive observations |
| --- | --- | --- |
| Pyxis `native-attribution-20261029`, private `adc67efd` based on production `55702378e9` | Two sequential 1+6 scans / 45 each | 5/2 pre-filter frames, all PPDU/advertised channel 1; 3/2 independent FILE records; known channel-6 BSSID absent before filtering and from FILEs. |
| Linux `linux-vfio-scan-hop-20261029`, frozen Fedora 7.2.10, firmware 0.35.80.3 | One passive 1+6 scan / 45 each | Four BSSs, all DS channel 1, three HT channel 1; no channel-6 reception demonstrated. |
| Linux `linux-vfio-scan-ch6-long-20261029`, same reference | One channel-6 request, duration 1000 / **232** | Two BSSs at 2437 MHz with DS/HT channel 6, plus four channel-1 entries in the BSS results. Exact known-BSSID comparison pending. |

Pyxis preserved independent snapshots and a healthy retained context; wired
management and byte-exact PXE restore passed. Status `0xfcf` observed initial
active MAC and completed off/fresh-on, proving the warm-start branch ran.
The attribution gate correctly failed; no automatic retry followed.

RX `step=1` is the latest host-processed ENTER **channel number**, not STEP
index 1. All seven frames were consumed under the channel-1 observation.
Queue-consumption order is not physical reception time. Historical PHY
`channel=1` is initial setup, not live tuning. This does not establish RF stuck
on channel 1. Chronium confirms the router is beside vela; distance is not used
to explain the missing reception.

Linux's 68-byte list and 40-byte START match Pyxis's source-generated payloads;
this is not a raw Pyxis H2C capture. Linux reports ENTER/LEAVE 1, ENTER/LEAVE 6,
and successful END, with no H2C between START and END. Long-control firmware
reports PHY 0, band 0, centre 6 and actual/expected period 232. Runtime/MMIO
traces and BSS frames are complete; guests retired and VFIO was released.

The long-control wrapper's CalledProcessError propagated the exporter's
**requested-1000 qualification refusal**. Linux's `u8 period` (`fw.h:373–375`)
truncated the assigned `req->duration` (`fw.c:7799–7800`) to 232 before the
16-bit wire encoder widened it (`fw.c:5731–5733`). The capture is not a qualified
1000-duration run. Forensic evaluation of the original, unchanged evidence
passes the same list/ACK/START/ENTER/LEAVE/END/completeness gates for observed
232. Two factors changed from the short Linux control: mask and period.

## Reference-driver findings

Pinned Linux v6.18 `7d0a66e4bb9081d75c82ec4957c50034cb0ea449`,
`drivers/net/wireless/realtek/rtw89/`:

- BE/v2 IE01 channel is `w0[23:16]`; prefix/IE sizes and 2.4 GHz decoding
  match (`txrx.h:602`, `core.c:1838–1862`). No decoder fix is established.
- BE sends list then START (`fw.c:8217–8236`). ENTER only updates software
  channel/NHM (`mac.c:5069–5082`, `chan.c:174–189`); the scan-RFK callback is
  empty (`rtw8922a.c:2137–2141`). Native RF digital-table upload exists
  (`rtl8922ae_phy.c:323–352`). Do not add a host per-hop retune or calibration.
- `FW_SCAN` is an informational C2H channel/PHY/PD-threshold report, not a
  missing command (`phy.c:3152–3162`, `fw.h:3677–3688`).
- Ordinary zero-SSID scans default to period 45 and no probes/null TX. NO_IR/
  RADAR selects the DFS form with minimum 105 and dwell 20
  (`fw.h:324–330`, `fw.c:7487–7492,7799–7818`). Channel 6 is not NO_IR in the
  world profile; marking it DFS merely for a longer visit would diverge.
- Both native CMAC management/PPDU paths exist. Linux enables inline PHY/RSSI
  on both MACs; native only MAC0. Zero inline length is accepted, so that
  difference is not a demonstrated explanation for a BSSID never received.

There is no universal cfg80211 passive dwell: request duration is in TUs with
optional mandatory semantics. mac80211 software scanning uses `HZ/9` jiffies
for passive/no-SSID visits (`net/mac80211/scan.c:27,966–973`); rtw89's firmware
offload retains its separate policy above. The successful control supports a
longer listen interval, but does not yet isolate the Pyxis cause.

## Temporary diagnostics and remaining gate

Chronium allowed packet-only bounded pre-filter metadata (2026-10-29): 512
frame records and 16 firmware-step events across the pair, copied before
filtering, no SSID/payload or DMA pointers. Raw identifiers stayed private and
UDP log export was disabled. The diagnostic worktree/builds are retired; no
trace code enters the production PRs. Immutable source/evidence packets remain.
Old Linux exporter limitations (integer-only frequencies and exact-size scan
reports) were corrected only in later packet tools; old seals/results remain
unchanged. Unknown report tails are never exported publicly.

The [proposed next candidate](../../wip/rtl8922ae-wifi.md#task-1-passive-period-proposal-2026-10-29-not-accepted)
is the same Pyxis 1+6 sequence with only period 45 changed to 232, retaining all
power/RFK/filter/deadline and FILE checks. It requires chronium's decision and
matched native known-BSSID evidence. A fixed dwell cannot guarantee every AP;
empty/partial results or capture loss never authorize an automatic retry.

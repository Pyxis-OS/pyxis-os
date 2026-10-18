# USB NCM receive throughput

Owner-assigned investigation, 2026-10-18. **Proposal only; no receive-path
change is authorized yet.** Native work uses the ThinkPad's RTL8156BG, not vela.
The [NCM reference](../devices/usb-cdc-ncm.md) remains the implemented contract.

## Measured baseline

ThinkPad PXE, clean main `a3efad31`, existing dependency pins, private explicit
NCM selection. The image kernel SHA-256 is
`baceaaf86c0960ce1380826990c083705effe48f3d2fe9cb3bc242e9f4cbe4bd`.
No personal archive or licensed game payload was copied. Commands used existing
`ttcp`, `tcp`, `sha256sum`, Remote and host `tcpdump`; no diagnostic kernel.
Three rounds rotate TX64/RX4/RX64 and RX64/RX4/TX64. For example:

```sh
ttcp -t -p 5001 -n 8192 -l 8192 PEER_IP
ttcp -r -p 5002 PEER_IP
```

The host serves exactly 4 or 64 MiB to port 5002; TX sends exactly 64 MiB to
5001. Rates below are guest ttcp times through closure/EOF, not Remote wall time.

| Direction / payload | Repeated rate, MiB/s |
| --- | --- |
| TX / 64 MiB | 118.152, 117.166, 116.991 |
| RX / 4 MiB | 1.367, 0.710, 0.663 |
| RX / 64 MiB | 0.777, 0.764, 0.711 |

All transfers completed; the separate 4 MiB content hash matched. USB inventory
reports SuperSpeed, and horse reports 2500 Mb/s full duplex. The adapter's
Ethernet ConnectionSpeedChange values and bcdDevice are not exposed by this
capture, so its negotiated Ethernet rate/firmware revision remain unmeasured.
`lsusb`'s nonzero exit is an unrelated unsupported PCI controller, not NCM loss.
The earlier firewall-blocked attempt is excluded.

In the three RX64 flows, gaps over 100 ms account for 74.63/78.01/82.85 s of
82.40/83.77/90.02 s: 90.6–93.1% of transfer time. The capture shows retransmitted
sequence ranges and recovery at roughly 200/400/800 ms. No zero receiver window
appears (minimum windows 12–13.5 KiB); TX has no overlapping retransmitted
sequence ranges. This supports **receive loss followed by timeout recovery**,
not a closed-window limit or simply slow copying. It does not locate the loss.
One RX64 recovery exposes a three-MSS hole: ACK 38852 advances in 1460-byte
repairs, then jumps from 41772 to 66592. Later bytes were already buffered, so
this is a missing contiguous region, not a stalled application reader. Its
shape fits burst-tail loss but host aggregates do not reveal NTB boundaries.

Host offload means captured packet sizes/counts are not wire-segment counts;
160-byte snapshots cannot validate payload checksums. Host capture reports zero
kernel drops. Raw native-baseline-02 records stay outside the repository.

## Inspected paths and remaining attribution

| Stage | Source finding; what remains unknown |
| --- | --- |
| NTB parsing | `usb_ncm_ntb16_validate` checks headers, lengths, NDP/span overlap and CRC mode before delivery. A failure discards the whole NTB and increments a private malformed count; no native reason/count snapshot exists. No demonstrated malformed-device block. |
| IN depth/rearm | Two posted 16 KiB TDs, eight software completion records. Rearm follows event consumption and ERDP publication. IRQ wakeups exist; the 10 ms poll is a fallback. No measured empty-post interval/rearm delay. |
| Frame handoff | A 16-frame provider queue, eight-frame service budget. NCM drains several completed NTBs without preserving a frame when enqueue returns false. Two ordinary ten-frame NTBs can exceed that queue unless the network worker runs between them. This is a source-confirmed silent-drop path; its native frequency is unknown. |
| Ethernet/IP/TCP acceptance | Ethernet/frame bounds and destination checks, IPv4 length/checksum checks and lwIP TCP checksum/pbuf allocation can reject packets. The host capture cannot distinguish those from earlier loss. No reason to relax validation. |
| TCP reassembly | Ordered input uses a bounded receive ring; lwIP's out-of-order queue is limited to 16 pbufs and the receive-byte budget, with SACK disabled. A hole can make that bound amplify upstream loss. Open windows and matching content do not prove this stage never discards. |

Inspected main `7e97b3a0` has no USB/network code difference from the measured
revision. Key sources: [NCM transport](../../kernel/usb/ncm.c),
[wire checks](../../kernel/usb/ncm_wire.c),
[provider queue](../../kernel/net/usb_ncm.c),
[xHCI completion/rearm](../../kernel/usb/xhci.c),
[lwIP options](../../kernel/net/lwip/include/lwipopts.h) and
[receive bridge](../../kernel/net/lwip/bridge.c). No native counters yet justify
naming any one of these as the measured drop point.

## Decisions proposed to the owner

1. **Attribute before tuning — default: one private diagnostic image.** Keep
   transfer behaviour unchanged. Capture bounded start/end counters for NTB
   validity/reasons and sequences, frame enqueue failures/high-water, posted-IN
   low-water/empty duration, completion pressure and rearm delay, plus downstream
   validation/allocation/reassembly drops. Record bcdDevice and real speed
   notifications once/when changed. Use private debug/trace summaries, no packet
   payloads, MAC addresses or permanent per-packet logging. Repeat the same
   ThinkPad packet and stop if counters point elsewhere.
2. **Candidate handoff fix — default: retain and resume, not enlarge-and-drop.**
   If provider pressure is confirmed, retain the validated NTB and next frame in
   its existing class buffer when the queue fills; resume when the network
   worker frees space. Reserve completion capacity for posted TDs and withhold
   rearm when that bounded capacity is occupied, rather than turning consumer
   pressure into a lost NTB or terminal discontinuity. Preserve ordering and
   epoch-based withdrawal; no network-stack call from the USB worker, new
   allocator or blocking wait. This prevents the known software handoff loss;
   it cannot promise a device never drops when its own buffers fill.
3. **Capacity isolation — default: retain 16 KiB NTBs and two posted IN TDs for
   the first comparison.** Do not simultaneously select the offered 36860-byte
   NTB, deepen rings or change TCP/SACK. Larger NTBs increase producer bursts;
   depth is a separate candidate only if the counters show starvation after the
   handoff is corrected. This keeps attribution and the memory delta reviewable.

After owner acceptance: QEMU correctness/withdrawal checks, then native
interleaved baseline/candidate runs on the ThinkPad with the same private
selection, peer and packet. Require hash integrity, no concealed counter drops,
RX recovery without the timeout plateau, unchanged TX/small Remote behaviour,
and bounded backing. QEMU alone cannot qualify this RTL8156 performance fix.
No implementation, new native connection or reboot was performed for this note.

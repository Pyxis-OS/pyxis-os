# NCM RX backlog comparison

Owner-run ThinkPad RTL8156BG, 2026-10-18. Private depth candidate `a285f024`
changes only RX provider storage/capacity from 16 to 1024 on retained-handoff
`1a020a4a`; TX stays 16. Same compiler, archive, configuration, peer and rotated
`ttcp` packet; 16 KiB NTBs, two posted IN TDs, eight completion records and TCP
settings remain fixed. Both images have the same private observers and the
original unconditional credit-return wake. Neither measures the later
conditional-wake change. These are separate boots, not an interleaved A/B series.

The host serves 4/64 MiB to `ttcp -r -p 5002 PEER_IP`; TX uses
`ttcp -t -p 5001 -n 8192 -l 8192 PEER_IP`. Rates include EOF/closure.

| Payload | RX depth 16, MiB/s | RX depth 1024, MiB/s | Median change |
| --- | --- | --- | --- |
| TX64 | 108.179, 107.294, 106.472 | 105.009, 105.878, 107.331 | −1.32% |
| RX4 | 92.859, 93.154, 91.384 | 81.173, 81.530, 81.437 | −12.30% |
| RX64 | 99.723, 99.215, 86.904 | 87.190, 82.956, 86.854 | −12.46% |

All transfers complete and the 4 MiB hash matches. Depth 1024 eliminates
provider-full retries in this run: zero versus 6504 at depth 16, where they
were retained rather than dropped. Its final enqueue/delivery counts agree at
176403, observed peak is only 38, and the startup handoff-failed count remains
one. There is no malformed NTB, sequence break, xHCI error/overflow or TCP
out-of-order tail-discard candidate. The larger queue did not improve this
workload; its RX4 range is entirely below the depth-16 range. All nine timed
capture flows cover the expected byte spans without observed retransmissions,
zero windows or gaps over 2 ms. Host RX64 rates also fall, excluding closure
accounting alone. The separate hash flow is only partly captured; guest hash
and peer source establish that check. Both captures report zero kernel drops;
offloads and 160-byte snapshots limit wire/payload interpretation.

## Inspected costs and limits

`next_frame` scans every slot to select each frame, even when nearly empty;
`reserve_frame` searches from the start for a free slot. Those scans and the
private diagnostic occupancy walk run with interrupts disabled. Increasing
the bound makes each complete walk inspect 64 times as many slots. The
occupancy walk runs on every accepted RX enqueue, so the same observer source
has a larger cost at depth 1024. Production lacks that observer; these results
do not isolate uninstrumented queue-selection cost.

The slot embeds its 1514-byte payload, so metadata walks also span a much larger
memory region. Requested two-NIC pool storage increases from 99112 to 3211880
bytes: +3112768 bytes per prepared controller. These are target type sizes,
not measured resident backing or cache misses. No CPU profile separates
selection, interrupt-off delay, diagnostic work or cache effects, and the
separate boots cannot rule out between-run variance. Larger full-queue
withdrawal scans are a source cost, not the cause of these steady transfers.

## Recommendation — awaiting owner decision

Keep depth 16 in production with retained handoff; do not include the private
1024-frame candidate in the fix. It already removes the observed software
loss, while 1024 adds backing and scan work without a measured speed benefit.
No intermediate depth has been measured: 64 would exceed this run's peak of
38, but that neither establishes a useful gain nor bounds other workloads.
If deeper burst capacity is required later, separate bounded selection and
observer cost from capacity before another comparison. No such redesign is
assigned by this result. The per-CPU backlog remains later direction only.

Raw captures: `/shared/present/ncm-throughput/native-handoff-01` and
`native-depth-01`; offline analysis: `native-analysis-depth`. Native runs belong
to the owner; no target connection or reboot was performed for this analysis.

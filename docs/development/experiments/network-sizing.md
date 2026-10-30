# Network storage sizing

Work package 8 of the limits audit. Nested QEMU 10.2 q35/KVM, 4 vCPUs, virtio-net on the default user network, the live
image (`DISTRIBUTION=full`), run from the shell. One scratch bundle drives the host side (a Python server holding TCP
connections, closing them after the guest's FIN, and sending a UDP burst). Single runs, not samples: the figures are
counts that follow from the budget, not timings.

Revisions: baseline = main `d1c409a8`; after = the same plus the network storage budget, listener backlog and UDP
receive-queue changes.

| Check | Before (64 MiB budget) | After, 4 GiB guest | After, 16 GiB guest |
|---|---|---|---|
| Streams held open until the first refusal (`NO_MEMORY`) | 31 | 123 | 507 |
| Connect, half-close, read EOF, close (guest closes first), until refusal | 62 of 300 | 246 of 300 | not run |
| UDP burst of 200 datagrams, reader asleep, then drained | 4 received | 200 received, 0 dropped | not run |
| UDP burst of 20,000 (about 11,100 reached the guest) | not run | 11,026 queued, 69 dropped (reported by INSPECT), first drop logged | not run |
| `LISTEN` backlog 4 / 300 / 0 | not applicable (fixed 4) | not run | accepted, capacity 4 / `CALL_LIMIT`, limit 126 logged / `BAD_REQUEST` |

The budget the log reported at 4 GiB was 259,697,664 bytes (RAM/16 less the memory the kernel holds); the refusal line
(`net: N admissions refused, X of Y bytes of network storage in use`) appeared once per second while the streams
test refused. The `LISTEN` check needed `tcp_listen = true` in the development space, set for the test image only.

Not measured: receive throughput (the window is delta's, #1647, and unchanged), upload throughput and loss
recovery. The send budget, the out-of-order buffer and the 32-packet budget were not changed because the QEMU user
network has no latency or loss to show any gain; see the limits section.

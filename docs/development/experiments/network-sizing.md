# Network storage sizing

Work package 8 of the limits audit. Nested QEMU 10.2 q35/KVM, 4 vCPUs, virtio-net on the default user network, the live
image (`DISTRIBUTION=full`), run from the shell. One scratch bundle drives the host side (a Python server holding TCP
connections, closing them after the guest's FIN, and sending a UDP burst). Single runs, not samples: the figures are
counts that follow from the budget, not timings.

Revisions: baseline = main `d1c409a8`; after = the same plus the network storage budget, listener backlog and UDP
receive-queue changes; the D5 row adds the TIME_WAIT commit on top.

| Check | Before (64 MiB budget) | After, 4 GiB guest | After, 16 GiB guest |
|---|---|---|---|
| Streams held open until the first refusal (`NO_MEMORY`) | 31 | 123 | 507 |
| Connect, half-close, read EOF, close (guest closes first), until refusal | 62 of 300 | 246 of 300 | not run |
| Same, with the TIME_WAIT reservation released at TIME_WAIT (D5, separate commit) | | 2,200 of 2,200 in three runs, no refusal | not run |
| UDP burst of 200 datagrams, reader asleep, then drained | 4 received | 200 received, 0 dropped | not run |
| UDP burst of 20,000 (about 11,100 reached the guest) | not run | 11,026 queued, 69 dropped (reported by INSPECT), first drop logged | not run |
| `LISTEN` backlog 4 / 300 / 0 | not applicable (fixed 4) | not run | clamped: capacity 4 / 30 (request 300, first clamp logged) / `BAD_REQUEST` |

The budget the log reported at 4 GiB was 259,697,664 bytes (RAM/16 less the memory the kernel holds); the refusal line
(`net: N admissions refused, X of Y bytes of network storage in use`) appeared once per second while the streams
test refused. Go (pyxis-go `8a8c61f116`, local build of the pinned toolchain): a bundle that `net.Listen`s on the guest address (backlog 128,
clamped to 30), dials it, echoes five bytes and reads 221 UDP datagrams from the host burst server passed on the 4 GiB
guest. The `LISTEN` checks needed `tcp_listen = true` in the development space, set for the test image only.

Caddy v2.11.7 built with that toolchain served static files on the clamp image (4 GiB): the listener asked for 128
and was clamped to 30, the body hash matched the source file, a missing path returned 404, and 40 concurrent
clients with four requests each all received 200 (160 of 160). The site needs `bind 10.0.2.15`: Pyxis listens on an
exact address, so a bare `:18080` fails with "unsupported operation" before any kernel call. A 50 MB `cat` from
`http://10.0.2.2` into `home://` completed with the full 50,974,720 bytes on both the baseline and the clamp image.

Not measured: receive throughput (the window is delta's, #1647, and unchanged), upload throughput and loss
recovery. The send budget, the out-of-order buffer and the 32-packet budget were not changed because the QEMU user
network has no latency or loss to show any gain; see the limits section.

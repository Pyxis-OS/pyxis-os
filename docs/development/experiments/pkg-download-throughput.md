# Package download receive window — 2026-10-30

The reported native installer rate (~5 MiB/s for clang-hosted over public HTTPS)
fits the old 65,535-byte receive window at the measured WAN RTT. Horse Linux
fetched the same cached IPv4 URL at 56.67 MiB/s, RTT 13.583 ms (minimum 11.104 ms).
65,535 bytes / 13.583 ms is 4.60 MiB/s. This is inference from the same LAN/edge,
not a native Pyxis measurement. The owner separately measured plain LAN HTTP
horse → vela at 44.4 MiB/s for a 283 MiB file.

## Matched QEMU qualification

Baseline source: Pyxis `10fc93598fc0`, userland `38eadc43dc1d`, ports
`46b9242b7d86`, filesystem `f3f22b6e07e7`, lwIP `4fb74e1b9213`. The comparison
changes only the [receive profile](../../devices/tcp.md#ownership-and-limits):
allocated 1,048,560-byte ring plus equal ingress reservation, receive scale four,
and unchanged 65,535-byte send budget. Both kernels use identical rebuilt
userland and the same initrd. No public ABI or compiler change; rebuild the kernel
and boot payloads. SDK/userland/ports need no rebuild for this change.

QEMU 10.2.2, nested KVM/Q35/max CPU, four vCPUs, 8 GiB, OVMF, VirtIO network/block.
The clock is the synchronized KVM source. Ordinary QEMU user networking declines
scaling: the changed kernel passes fetch/hash verification there with the normal
unscaled window. For negotiated coverage, passt uses an MTU of 1500 and QEMU's
stream backend. A 128-byte packet capture records SYN options and byte flight.

The controlled case uses QEMU `filter-buffer`, 10,000 microsecond interval in
both directions. This batches packets; it is not an exact fixed-RTT emulator.
Both peers terminate TCP on the host, so these guest timings cannot be treated
as native WAN timings. Timed workloads ran without concurrent builds, curl or
capture analysis; physical-host load was not controlled.

A temporary consumer links unchanged `pkg_fetch` and `pkg_archive`. It fetches
the exact released clang-hosted archive (281,421,824 bytes), verifies whole-file,
inventory and every payload hash, and optionally extracts into a disposable
npfs directory. It never activates or executes the incompatible 0.0.10 payload.
Archive SHA-256:
`de7bff540ab5eb6c65325632ef01189dfd0b911e7c87e4c27767dcfb816ced5a`.

| RAM HTTP staging, 10 ms batching | Baseline fetch | Scaled fetch | Ratio |
| --- | --- | --- | --- |
| Public HTTPS, packages.pyxis.nexus | 93.927 s / 2.86 MiB/s | 25.034 s / 10.72 MiB/s | 3.75× |
| Local TLS 1.3 ChaCha20-Poly1305 | 91.749 s / 2.93 MiB/s | 6.972 s / 38.50 MiB/s | 13.16× |
| Local plaintext HTTP | 97.274 s / 2.76 MiB/s | 16.300 s / 16.47 MiB/s | 5.97× |

The public capture moves from a 65,535-byte flight maximum to 443,520 bytes;
SYN offers scale four and the peer replies with scale zero. Receiver advertised
window fields after the handshake must be multiplied by sixteen. TLS remains
ChaCha20-Poly1305. Public fetch provider CPU changes from 1.229 s user + 3.953 s
kernel to 0.828 + 0.784 s; these exclude separate kernel worker CPU. Local TLS
provider CPU changes from 0.879 + 0.868 s to 0.711 + 0.607 s while wall time falls
by 84.8 seconds. This supports the window/wait explanation rather than a crypto
rewrite.

Without packet batching, the public RAM pair is 5.685 s baseline versus 6.333 s
scaled: no speedup is established on an already fast proxy path. Whole-archive
SHA remains about 0.29–0.37 s and inventory/payload verification 0.77–0.83 s.
The earlier stage profile attributed ordinary ram-npfs staging costs mainly to
small backing-file calls; no call-coalescing, TLS, hash or durability change is
included here.

## Disk-backed waterfall

Fresh 2 GiB npfs fixtures with a 64 MiB journal and separate data/http-work
volumes, same local TLS archive, no packet batching. Baseline and changed kernels
each ran twice; the repeat reversed their order. All four archive checks passed.

| Run | Fetch | Whole SHA | Inventory/extract/sync | Final sync, nested |
| --- | --- | --- | --- | --- |
| Baseline first | 14.122 s | 1.008 s | 5.747 s | 70.33 ms |
| Scaled first | 31.173 s | 1.421 s | 8.578 s | 57.38 ms |
| Scaled repeat | 15.573 s | 1.287 s | 6.810 s | 51.56 ms |
| Baseline repeat | 22.539 s | 1.460 s | 7.243 s | 33.93 ms |

No disk-path speedup or stable regression is established: even unchanged hash
and extraction stages vary substantially. Whole-workload backend writes were
about 571.4–571.6 MB, 10,648–10,690 operations, with 32–36 flushes (some mount
bookkeeping completed before the counter boundary). Summed backend write/flush
service times are not additive wall phases. Final directory sync remains a small
nested fraction; no durability barrier change is justified. These are nested-VM
host-filesystem results, not native NVMe measurements.

## Aggregate quota and remaining qualification

The 64 MiB TCP/UDP allowance in `kernel/net/storage.c` is a chosen admission quota,
not physical capacity or a protocol constraint. It is checked before separate
heap allocation, which can also fail. It remains unchanged by explicit owner
decision unless it blocks qualification; no observed qualification run hit it.
Roughly 30 active streams fit after the larger ring/ingress claim, before other
charges reduce capacity. This is an estimate, not a concurrency qualification.
Rings may be freed after peer FIN and draining; ingress reservations stay through
TIME_WAIT until PCB teardown. Recently closed streams also consume admission.
Revisit this artificial quota in the limits audit.

Native benefit remains pending one orchestrator-scheduled RAM-only vela check.
The owner has not authorized direct native access. The remaining loss recovery,
SACK and PMTU limitations in [debt #1437](https://git.internal/PyxisOS/pyxis-os/issues/1437)
remain open; this change addresses only window scaling.

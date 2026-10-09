# Network throughput

TCP on Pyxis was tuned toward the owner's gigabit LAN in #559 and userland
#169, completed 2026-10-09. The TCP behaviour itself is described in
[TCP](../devices/tcp.md#ownership-and-limits) and the benchmark in
[`ttcp`](../devices/tcp.md#ttcp). This report records the configuration
chosen, the native and QEMU measurements, how to repeat them, and what
remains.

## Configuration

Accepted by the owner 2026-10-08:

- **Segment size.** `TCP_MSS` is 1460, for the 1500-byte interface MTU.
  Without path-MTU discovery, a connection whose peer is reached through a
  gateway is capped at `TCP_ROUTED_MSS`, 536.
- **Windows.** `NET_TCP_RECEIVE_BYTES` and `NET_TCP_SEND_BYTES` in
  `include/kernel/net/tcp.h` are 65,535, the largest without window scaling.
  lwIP's `TCP_SND_QUEUELEN` formula gives 180 pbufs at these values.
- **Nagle off** on every connection. Native writes already hand lwIP up to
  4 KiB at once; with Nagle on, request/response framing waited for the
  peer's delayed ACK.
- **`ttcp -r [-p PORT] HOST`** connects to a host serving data, discards what
  it reads and reports bytes and MiB/s from establishment to EOF. It connects
  out because ordinary sessions hold connect-only TCP authority.

`tcp_connection_established` in `kernel/net/lwip/connection.c` applies the
segment cap and disables Nagle. It runs from the connect and accept callbacks,
after lwIP has fixed the effective MSS and before the application can send.

- **Why not the PCB-allocation hook.** For a passive open lwIP allocates the
  control block before parsing the SYN's options. `tcp_parseopt` and
  `tcp_eff_send_mss` then recompute `mss`, and an active open recomputes it
  again on the SYN-ACK, so a cap at allocation would be overwritten.
- **No second interface.** The review asked for a per-connection clamp before
  a 576-byte pseudo-interface; the clamp was enough.
- **Initial window.** lwIP has already set it for 1460, to 4380 bytes, which
  is within RFC 6928's initial window for 536, so a routed connection keeps it.
- **What peers see.** 1460 is advertised either way; the peer's own path
  handling covers what it sends.

## Native results (2026-10-09)

The owner ran the ThinkPad wired, on AC, from the local Development shell,
against the desktop at `192.168.0.213`. Both builds were PXE boots of main:
the baseline `114f2ac` before #559 and `183f793` after it, with
`pyxis-remote` built from the same revisions. The desktop captured with
`tcpdump -i any -s 128`; the captures are `/shared/throughput/pyxis-net.pcap`
(baseline) and `pyxis-net-boot2.pcap` (after). Ping was 0.539/0.624/0.715 ms
and 0.463/0.661/1.335 ms (min/avg/max).

| Run | Baseline `114f2ac` | After `183f793` |
| --- | --- | --- |
| Send, 8 KiB writes (`ttcp`, MiB/s) | 35.85, 35.83, 35.80 | 70.55, 70.35, 70.45 |
| Send, 2 KiB writes (`ttcp`, MiB/s) | 33.78, 33.75, 33.70 | 25.25, 44.23, 44.08 |
| Receive 64 MiB into `tmp://` (capture, MiB/s) | 27.75, 28.94, 29.29 | 56.82, 45.42, 46.56 |
| Receive 64 MiB, `ttcp -r` (MiB/s) | — | 85.24, 85.37, 85.33 |
| `xfer` 15 MiB to Pyxis | about 6 s | 6.5 s |
| `xfer` 15 MiB to the desktop | about 32 s | 7.5 s |

Every receive into `tmp://` and a `tcp | sha256sum` run matched the desktop's
SHA-256 of the source. The first baseline 2 KiB run was typed with `-l 2046`.
The capture rates run from the first to the last data segment.

- **Baseline send was segment-bound, not window-bound.** In flight had a
  median of about 2.1 KiB and never exceeded 9.6 KiB of the 16 KiB buffer.
  About 125,000 536-byte segments in 1.79 s is about 14 µs per segment, and
  2 KiB writes were only 6% slower than 8 KiB ones.
- **Baseline receive was window-bound.** In flight peaked at exactly 16,384,
  and Pyxis's advertised window reached zero in every run. 16 KiB per round
  trip gives the observed 28–29 MiB/s.
- **Send doubled with 1460-byte segments** and is still not window-bound. The
  desktop's window stayed at or above 51,100 bytes, and in flight had a median of
  8 KiB with 8 KiB writes and 4 KiB with 2 KiB writes, peaking at 24–26 KiB.
  2 KiB writes reach only 44 MiB/s against 70, so per-call cost now shows.
- **Receive tripled.** `ttcp -r` reaches 85 MiB/s, about 715 Mbit/s. Into a
  RAM file it is 45–57 MiB/s: Pyxis's advertised window falls close to zero
  and 0.36–0.65 s of each run is gaps over 2 ms, so the consumer writing
  `tmp://` is the limit.
- **`xfer` to the desktop went from 32 s to 7.5 s**, as the QEMU finding about
  Nagle and delayed ACKs predicted.
- **One stall.** The first 2 KiB run after #559 lost about 1.09 s. The
  capture shows two segments of one window never reaching the desktop. The
  first was repaired by fast retransmit within about 1.3 ms; the desktop's
  next ACK pointed at the second, and Pyxis resent it only on the
  retransmission timeout. It happened once in six sends.

The earlier native figure, 29.2 MiB/s send in the
[RTL8111 qualification](rtl8111-qualification.md), was measured on an older
revision. The desktop's GRO merges incoming segments, so the capture shows
sizes such as 2636 and 4096; in-flight figures are unaffected.

## QEMU results (2026-10-08)

QEMU 10.2.2 with nested KVM, 4 CPUs, 8 GiB, virtio-net and user networking.
Packets were captured at the guest NIC with `filter-dump`. Main `8c823b3`
against the #559 branch with userland `362574b`.

- **Send:** guest `ttcp -t -n 2048 -l 8192 10.0.2.2` (16 MiB) into a host
  `ttcp -r -s`.
- **Receive:** a host `socat -u OPEN:FILE TCP4-LISTEN:5002` serving 16 MiB of
  random data, read by guest `tcp 10.0.2.2 5002 > tmp://rx.bin` on main and
  `ttcp -r -p 5002 10.0.2.2` after.

| Check | Main | After |
| --- | --- | --- |
| Send, 16 MiB (MiB/s) | 1.659, 1.654, 1.649 | 3.179, 3.162, 3.156 |
| Receive, 16 MiB (MiB/s) | 4.31, 2.53, 2.16, 3.76 (into `tmp://`) | 4.340, 4.370, 4.436 (`ttcp -r`; capture within 1%) |
| `xfer send`, 15 MiB from `tmp://` | 52.2–56.9 s | 39.7, 39.5, 40.2 s |
| Routed send to a Tailscale address | 536-byte segments | 536-byte segments, although both sides advertised 1460 |

With MSS 1460 but Nagle still on, the same `xfer` took 59.7, 66.3 and 64.6 s:
each 2.8 KB frame became a full segment and a tail, QEMU delayed its ACK of
the full segment by about 2.1 ms, and Nagle held the tail until it arrived.
That is why Nagle is off. A passive on-link connection, the remote-terminal
server's, sent 1460 + 1322-byte frames. A routed passive open cannot be
produced with QEMU user networking, so that path was checked in code only.

These are nested-VM figures, about twenty times below native. They show where
per-packet time goes:

- **Not window-bound.** GDB reads of the lwIP control block mid-send showed
  `cwnd` and the send window at 65,535 with about 4.4 KiB queued.
- **Per packet and per call.** On main, 1, 2, 4 and 8 KiB writes gave 1.03,
  1.27, 1.53 and 1.56 MiB/s: about 0.3 ms per segment and its ACK, and about
  0.36 ms per call.
- **Mostly idle, mostly clock reads.** The CPUs were halted about 95% of the
  time. `perf kvm stat` counted about 27 HPET counter reads per data segment,
  about 4.8 µs each, nearly all from timer handling driven by the network
  worker's wake and sleep cycle. Each `arch_monotonic_ns` is three MMIO reads
  on QEMU's 64-bit HPET.
- **Larger windows changed nothing** in QEMU: a local build with only
  `TCP_MSS 1460` sent 2.6–3.2 MiB/s, the same as with both buffers at 65,535.

## Repeating the measurement

On the desktop, open the ports, capture, and start a discard sink and a
64 MiB source file:

```sh
sudo ufw allow from 192.168.0.50 to any port 5001:5002 proto tcp
sudo tcpdump -i any -s 128 -w /tmp/pyxis-net.pcap 'host 192.168.0.50 and tcp portrange 5001-5002' &
socat -u TCP4-LISTEN:5001,reuseaddr,fork OPEN:/dev/null &
head -c 67108864 /dev/urandom > /tmp/rx64.bin && sha256sum /tmp/rx64.bin
```

On Pyxis, three runs of each:

```text
ping -c 10 DESKTOP
ttcp -t -p 5001 -n 8192 -l 8192 DESKTOP
ttcp -t -p 5001 -n 32768 -l 2048 DESKTOP
```

For each receive run, first start
`socat -u OPEN:/tmp/rx64.bin TCP4-LISTEN:5002,reuseaddr` on the desktop:

```text
ttcp -r -p 5002 DESKTOP
tcp DESKTOP 5002 | sha256sum
tcp DESKTOP 5002 > tmp://rx.bin
```

Each 64 MiB receive into `tmp://` holds 64 MiB of pages until `rm` frees them.
Afterwards stop `tcpdump` and the sink, keep the capture, and remove the rule
with `sudo ufw delete allow from 192.168.0.50 to any port 5001:5002 proto tcp`.

`/shared/net-throughput/flow.py CAPTURE PORT [N]` summarizes the last N
connections on a port: rate, segment sizes, the receiver's advertised window,
bytes in flight and gaps. It reads Ethernet and Linux cooked captures.

## Limits

[TCP throughput limits](../technical-debt.md#tcp-throughput-limits) records the
remaining per-segment and per-call send cost, the retransmission-timeout stall,
the consumer-bound RAM-file receive and the clock reads per packet. Path-MTU
discovery, window scaling, SACK and IPv6 were out of scope.

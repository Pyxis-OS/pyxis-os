# Network throughput

Status: **accepted, 2026-10-08,** assigned to Claude. The owner accepted the
three [decisions](#accepted-decisions), with the review's adjustment to the
segment-size mechanism. Decisions 1 and 2 are implemented; two
[questions](#open-questions) came out of implementation and await the owner.
The native baseline waits for the owner's next ThinkPad batch.

## Goal

Move TCP on Pyxis toward the owner's gigabit LAN. The one native figure is the
[RTL8111 qualification](../development/rtl8111-qualification.md)'s `ttcp`
send: 64 MiB at 29.2 MiB/s from the ThinkPad to the desktop, one sample.
Receive throughput has not been timed natively.

## Current configuration

- **Segment size.** `TCP_MSS` is 536 in `kernel/net/lwip/include/lwipopts.h`,
  "conservative until path-MTU discovery exists". The lwIP interface MTU is
  already 1500 (`NET_PACKET_MAX_BYTES`), so the MSS alone sets the segment size.
- **Windows.** `TCP_WND` and `TCP_SND_BUF` come from `NET_TCP_RECEIVE_BYTES`
  and `NET_TCP_SEND_BYTES` in `include/kernel/net/tcp.h`, both 16 KiB. The
  receive side also allocates a ring of that size per connection. There is no
  window scaling.
- **Calls.** Each native TCP read or write moves at most 4 KiB and is handed to
  the single BSP network worker, which alone touches lwIP.

A 16 KiB window over the native ping round trip of 0.58–0.68 ms caps one
connection at about 23–27 MiB/s, close to the native figure. Ping includes
ICMP handling and may overstate the TCP round trip. That the window is the
ceiling is an inference; the [native plan](#native-measurement-plan) is meant
to confirm or rule it out.

## QEMU baseline (2026-10-08)

Main `8c823b3`. QEMU 10.2.2 with nested KVM, 4 CPUs, 8 GiB, virtio-net and
QEMU user networking (slirp). Packets were captured at the guest NIC with
QEMU's `filter-dump`.

- **Send:** guest `ttcp -t -n 2048 -l 8192 10.0.2.2` (16 MiB) into a host
  `ttcp -r -s`.
- **Receive:** the host `socat` served 16 MiB of random data to guest
  `tcp 10.0.2.2 5002 > tmp://rx.bin`. That has no timing of its own, so the
  rate is the first-to-last data segment at the guest NIC. SHA-256 matched.

| Direction | MiB/s |
| --- | --- |
| Send | 1.659, 1.654, 1.649 |
| Receive | 4.31, 2.53, 2.16, 3.76 |

These are nested-VM numbers, about eighteen times below the native send. They
show packet and call counts, and which costs grow with them. They do not show
native rates.

### Send

- **Segments.** The guest advertises an MSS of 536. Every 4 KiB write leaves
  as seven 536-byte segments and one 344-byte tail. The host advertises a
  65,535-byte window and acknowledges each segment within 6 µs.
- **Not window-bound.** GDB reads of the lwIP control block mid-transfer showed
  `cwnd` 65,535, a 65,535-byte send window and only about 4.4 KiB queued out of
  the 16 KiB send buffer. Segments leave one at a time, about 0.22 ms apart.
- **Per packet and per call.** Varying the write size separates the two:

  | `ttcp -l` | 1 KiB | 2 KiB | 4 KiB | 8 KiB (two 4 KiB calls) |
  | --- | --- | --- | --- | --- |
  | MiB/s | 1.03 | 1.27 | 1.53 | 1.56 |

  A linear fit gives about 0.3 ms per packet, a data segment and its ACK, and
  about 0.36 ms per call.
- **Where the time goes.** Sampling all four CPUs through the GDB stub during
  a send found them halted about 95% of the time, the BSP included.
  `perf kvm stat` in the VM over 4 s of sending counted 364,209 HPET counter
  reads at about 4.8 µs each in that layer alone, about 27 per data segment.
  Backtraces put nearly all clock reads in timer handling: the timer
  interrupt, re-arming the one-shot timer and expiring timed waits, which the
  worker's wake and sleep cycle drives about once per packet. Each
  `arch_monotonic_ns` on QEMU's 64-bit HPET is three MMIO reads.

### Receive

QEMU sends 1440-byte segments despite the guest's MSS of 536. The guest's
advertised window has a median of about 4.4 KiB of 16 KiB and reaches zero;
0.7–4.4 s of each 16 MiB run were gaps over 2 ms. The receiving program reads
4 KiB per call and writes into a growing RAM file, so these numbers measure the
consumer as much as the stack.

### Local experiments

Temporary local builds, reverted afterwards, changed the configuration:

| Build | Send MiB/s | Receive MiB/s |
| --- | --- | --- |
| Main | 1.649–1.659 | 2.16–4.31 |
| `TCP_MSS 1460` | 3.246, 2.599, 2.625 | 3.95, 4.73, 4.76 |
| `TCP_MSS 1460`, both buffers 65,535 | 3.203, 3.150, 3.202 | 4.20, 4.39, 4.30 |

With 1460 a 4 KiB write leaves as three segments instead of eight, and send
throughput roughly doubles. Larger windows change nothing in QEMU because it is
not window-bound there. Their effect can only be judged natively.

### What carries over to native

- **Packet count.** At 536 bytes a gigabit link would need about 200,000 data
  segments a second. Whatever each costs the worker, fewer and larger segments
  divide it by 2.7.
- **Clock reads per packet.** The worker's wake and sleep cycle reads the clock
  several times per packet. The ThinkPad uses the extended-HPET path, and each
  read there is an MMIO access to the chipset. Its native cost is unmeasured.
- **The window cap.** The 16 KiB window is invisible in QEMU but matches the
  native figure.
- **The 4 KiB call extent.** It holds natively too.

## Native measurement plan

The owner runs this on the ThinkPad wired, on AC, from the local Development
shell so the remote terminal's traffic stays out. `DESKTOP` is the desktop's
address, `192.168.0.213` in the RTL8111 qualification; the ThinkPad's wired
address is `192.168.0.50`. Run it once on current main as the baseline, and
again after any accepted change.

On the desktop, open the ports temporarily and start a capture and a sink:

```sh
sudo ufw allow from 192.168.0.50 to any port 5001:5002 proto tcp
sudo tcpdump -i any -s 128 -w /tmp/pyxis-net.pcap 'host 192.168.0.50 and tcp portrange 5001-5002' &
socat -u TCP4-LISTEN:5001,reuseaddr,fork OPEN:/dev/null &
head -c 67108864 /dev/urandom > /tmp/rx64.bin && sha256sum /tmp/rx64.bin
```

On the ThinkPad:

```text
ping -c 10 DESKTOP
ttcp -t -p 5001 -n 8192 -l 8192 DESKTOP
ttcp -t -p 5001 -n 8192 -l 8192 DESKTOP
ttcp -t -p 5001 -n 8192 -l 8192 DESKTOP
ttcp -t -p 5001 -n 32768 -l 2048 DESKTOP
ttcp -t -p 5001 -n 32768 -l 2048 DESKTOP
ttcp -t -p 5001 -n 32768 -l 2048 DESKTOP
```

Then receive three times. Before each run, start
`socat -u OPEN:/tmp/rx64.bin TCP4-LISTEN:5002,reuseaddr` on the desktop:

```text
tcp DESKTOP 5002 > tmp://rx.bin
sha256sum tmp://rx.bin
rm tmp://rx.bin
```

Finally stop `tcpdump` and the sink, copy `/tmp/pyxis-net.pcap` to
`/shared/net-throughput/`, and remove the rule with
`sudo ufw delete allow from 192.168.0.50 to any port 5001:5002 proto tcp`.

How to read the results:

- **Window-bound:** the capture shows about 16 KiB in flight most of the time,
  and the 2 KiB-write runs match the 8 KiB ones.
- **Call-bound:** less in flight, and the 2 KiB writes are clearly slower.
- **Receive:** the rate comes from the capture, which also shows whether
  Pyxis's receive window stays open.
- **Interference:** each 64 MiB receive leaves several times its size in
  RAM-file heap pools for the rest of the boot. That is acceptable for three
  runs; see [contiguous RAM-file backing](../technical-debt.md#contiguous-ram-file-backing).

## Accepted decisions

Accepted by the owner 2026-10-08.

1. **Segment size:** 1460 bytes to on-link peers, 536 to peers behind a
   gateway. The review asked for a per-connection clamp before considering a
   second lwIP interface.
2. **Windows:** TCP send and receive buffers at 65,535 bytes, without window
   scaling.
3. **Receive benchmark:** `ttcp -r`, discarding what it receives. Its
   connection direction is an [open question](#open-questions).

Worker per-packet costs, such as the clock reads and timer re-arming per wake,
the 4 KiB call extent and batching, wait for the native results. TSC timekeeping
is already the accepted [later direction](later-os-directions.md#clock-source),
separate from this work.

## Implementation

- **Segment size.** `TCP_MSS` is 1460. A per-connection clamp,
  `tcp_connection_limit_mss`, runs in the connect and accept callbacks: when
  `net_ipv4_route` reaches the peer through a gateway, or fails, it caps
  `pcb->mss` at `TCP_ROUTED_MSS`, 536. No second lwIP interface was needed.
  - **Why not the PCB-allocation hook:** for passive opens lwIP allocates the
    PCB before parsing the SYN's options. `tcp_parseopt` then resets `mss`
    from the peer's option and `tcp_eff_send_mss` recomputes it, and active
    opens recompute it again on the SYN-ACK. A cap at allocation would be
    overwritten.
  - **Why the callbacks work:** both run right after lwIP fixes `mss` at
    establishment, before the application can send.
  - **Initial window:** lwIP has already set it for 1460, to 4380 bytes. That
    is within RFC 6928's initial window for 536, so the clamp leaves it.
  - **What peers see:** 1460 is advertised either way; the peer's own path
    handling covers what it sends.
- **Windows.** `NET_TCP_RECEIVE_BYTES` and `NET_TCP_SEND_BYTES` are 65,535.
  lwIP's `TCP_SND_QUEUELEN` formula now gives 180 pbufs, and the out-of-order
  cap follows the receive window.

### QEMU validation (2026-10-08)

Same configuration as the baseline, kernel from this branch:

| Check | Result |
| --- | --- |
| On-link send, 16 MiB | 3.256, 3.220, 3.252 MiB/s (baseline 1.649–1.659); 1460-byte segments; both SYNs show MSS 1460, window 65,535 |
| Routed send, 4 MiB to this VM's Tailscale address through the gateway | 536-byte segments although both sides advertised 1460 |
| On-link receive, 16 MiB | 4.30, 4.54, 4.36 MiB/s; SHA-256 matched; the guest window opens to 65,535 |
| Passive on-link, the remote-terminal server's own connection | its frames leave as 1460 + 1322 bytes |

A routed passive open cannot be produced with QEMU user networking, because
forwarded connections all arrive from the on-link `10.0.2.2`. That path was
checked in code only.

## Open questions

Each has a default and needs an owner decision.

1. **Nagle with request and response framing.**
   - **The problem:** each 2.8 KB `xfer` frame is now one full segment plus a
     1322-byte tail. QEMU holds its ACK of the lone full segment for about
     2.1 ms, and lwIP's Nagle holds the tail until that ACK. Under the old
     536-byte MSS a frame was five full segments and a tail, and the receiver
     ACKed promptly.
   - **QEMU, 15 MiB `xfer` download:**

     | Kernel | Seconds |
     | --- | --- |
     | Before, #554 | 52.2–56.9 |
     | This branch | 59.7, 66.3, 64.6 |
     | This branch with Nagle off (local experiment) | 41.7, 49.9, 41.4 |

   - **Natively it may be worse:** Linux can delay the ACK for a lone segment
     by up to about 40 ms, and the host cannot reply until the frame is
     complete. That is an inference, not a measurement.
   - **Default:** disable Nagle on every connection in the same established
     callbacks. Caelum's calls already hand lwIP up to 4 KiB at once, so the
     cost is extra small segments for small writes, such as terminal echo.
     `ttcp` sends were unchanged with Nagle off (3.17, 3.19 MiB/s).
   - **Alternative:** a per-stream no-delay option, which is new ABI.
   - **Alternative:** keep Nagle and accept the stall.
2. **Which way `ttcp -r` connects.**
   - **The problem:** the proposal said `ttcp -r` would accept one connection,
     like classic `ttcp`. Ordinary Pyxis sessions have connect-only TCP
     authority; listening needs the `session` handoff the echo server uses.
   - **Default:** `ttcp -r [-p PORT] HOST` connects to a host that serves data,
     for example `socat -u OPEN:FILE TCP4-LISTEN:5002`, reads until EOF,
     discards it and reports bytes and MiB/s. This works from any ordinary
     shell.
   - **Alternative:** listen as classic `ttcp` does, launched through the
     `session` handoff with LISTEN authority.

## Out of scope

Path-MTU discovery, window scaling, SACK, IPv6, other network drivers and
changes to applications beyond `ttcp`.

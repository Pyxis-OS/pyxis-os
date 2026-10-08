# Network throughput

Status: **proposal, 2026-10-08,** assigned to Claude. Measurement only; nothing
here is accepted yet. The three [decisions](#decisions-for-the-owner) have
defaults and await the owner.

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

## Decisions for the owner

1. **Segment size.**
   - **Default:** `TCP_MSS` 1460 for on-link destinations, keeping 536 for
     routed ones. The route hook would pick a second lwIP interface with a
     576-byte MTU for destinations behind a gateway, so lwIP's own
     effective-MSS calculation applies per route.
   - **Why not everywhere:** without path-MTU discovery, a routed path below
     1500 bytes that does not clamp MSS would drop full-size segments, so the
     connection would stall after the handshake. Advertising 1460 is safe either
     way: the peer handles its own path.
   - **Alternative:** 1460 everywhere, relying on the peer's MSS option and
     router clamping. It is simpler, with that black-hole risk.
   - **Alternative:** keep 536.
2. **Windows.**
   - **Default:** `NET_TCP_RECEIVE_BYTES` and `NET_TCP_SEND_BYTES` at 65,535
     bytes, the largest window without scaling. At about 0.6 ms that allows
     roughly 100 MiB/s per connection, near gigabit, if nothing else limits.
   - **Memory:** a connection that receives allocates a 64 KiB ring instead of
     16 KiB, and can hold up to 64 KiB of queued sends and 64 KiB out of order.
     That is about 192 KiB at worst, and 6 MiB across the 32 transport records.
   - **Alternative:** 32 KiB, a cap of about 55 MiB/s and half the memory.
   - **Window scaling:** not now. A LAN at this round trip fits in 64 KiB;
     scaling matters for longer paths and is a later decision.
3. **A receive benchmark.**
   - **Default:** add `ttcp -r [-p PORT]` to the Pyxis `ttcp`. It would accept
     one connection, discard the data and report bytes and MiB/s, so receive
     can be timed without a RAM file or a pipe in the way.
   - **Alternative:** keep timing receive from host captures.

Changes to the worker's per-packet cost wait for the native results. Those are
the clock reads and timer re-arming in its wake and sleep cycle, the 4 KiB call
extent and batching. TSC timekeeping is already the accepted
[later direction](later-os-directions.md#clock-source), separate from this work.

## Out of scope

Path-MTU discovery, window scaling, SACK, IPv6, other network drivers and
changes to applications beyond `ttcp`.

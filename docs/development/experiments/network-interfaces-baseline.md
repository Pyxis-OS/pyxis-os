# Multiple-interface pre-change baseline

2026-10-18, task 1 of [multiple interfaces](../../devices/network-interfaces.md).
No runtime changes, native run or NCM TX A/B. These controls precede the
interface-owned core; they do not qualify multiple interfaces.

## Inputs

Main `d4dfc3b254db2b1850fc3b80eefea8cadb4b2903`, userland `214d0a59`,
ports `7604c85d`, fs `25337999`, lwIP `a1aadb91`. Reused the clean successful
[main workflow](https://git.internal/PyxisOS/pyxis-os/actions/runs/3082)
artifacts (API run 3182), built with `pyxis-llvm23.1.3-3d69e83`. The running
kernel reported `d4dfc3b254db`; its ELF matched the kernel bundle byte-for-byte.
No source rebuild or personal data override. Kernel log ceiling debug, ordinary
info console, packaged configuration/Kconfig, no profiler or attached debugger
during timing.

QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), Q35 nested KVM with kernel IRQ chip,
host CPU, four CPUs, 2 GiB, standard VGA, IDE CD, VirtIO RNG and one modern
VirtIO NIC. Raw OVMF CODE/VARS pair, fresh writable VARS copy, UTC RTC.
User networking supplied DHCP `10.0.2.15/24`, gateway `10.0.2.2`, DNS
`10.0.2.3`; TCP host loopback 24231 forwarded to guest 2323. No disk or HOST
export. Ordinary live spaces plus one plain Remote session, 120×40, quiet
machine input. Host client built from the same task checkout, whose only changes
were docs. Host load was low (sampled load averages 0.43 and 1.18); other agents
share this VM. No competing build was observed. This is an emulation/NAT control,
not a NIC ceiling or native latency measurement.

| Artifact | SHA-256 |
| --- | --- |
| ISO | `774f7b33e23ab60d7d95b41ec6437f262f11e94d99c16fb832e5e90ced925b93` |
| Kernel ELF | `2d540247bd1a42b5fb843b9f32f6f4b22b72ad33ef5c3b6fe329aea2b1a6776d` |
| Effective Kconfig | `0376b6f280a6feadd01c6d13c6e046073b44aacb649535f78ef9be6eb4ac0132` |
| OVMF CODE | `904bfa3e0d966372b43b804c4fe323ae63751566687c2bfdf52ca947f47eb13a` |
| Original OVMF VARS | `6ed987af3a3c155be71665f510eae3e007eda9b8b94afd59d45e91c4a11565cc` |
| Host client | `25a1c5bf72bbb77e147309ad6f320cd2899abdae1d3f02d650d5a6c96a45bbc9` |

## Commands and observations

TCP used existing `ttcp` and host `socat`; each peer was ready before its
corresponding command. The TX sink counted bytes with `wc -c`. RX served a
16 MiB zero-filled file, reproducible with `truncate -s 16777216 source16.bin`.
Order was TX1/RX1, RX2/TX2, TX3/RX3, then hash and three UDP echoes:

```sh
# Host peers, one at a time; followed by the corresponding guest command.
socat -u TCP4-LISTEN:25001,bind=127.0.0.1,reuseaddr STDOUT | wc -c
socat -u OPEN:source16.bin TCP4-LISTEN:25002,bind=127.0.0.1,reuseaddr
socat -T2 UDP4-RECVFROM:25003,bind=127.0.0.1,reuseaddr EXEC:/bin/cat
```

```text
ttcp -t -p 25001 -n 2048 -l 8192 10.0.2.2
ttcp -r -p 25002 10.0.2.2
tcp 10.0.2.2 25002 | sha256sum
udp-send 10.0.2.15 10.0.2.2 25003 interfaces-control
```

| Control | Samples / result |
| --- | --- |
| TCP TX, 16 MiB, guest MiB/s including closure | 3.660, 3.535, 2.979; each host count exactly 16,777,216 |
| TCP RX, 16 MiB, guest MiB/s through EOF | 5.648, 5.828, 5.419; each guest count exactly 16,777,216 |
| Hash | `080acf35a507ac9849cfcba47dc2ad83e01b75663a516279c8b9d243b719643e`, matching the host file |
| UDP echo | All three returned the expected 18-byte payload; Remote command wall 79.072, 71.041, 75.231 ms, including launch/transport/cleanup, not isolated UDP RTT |

After closing that Remote session, three consecutive five-second idle windows
consumed 67, 66 and 96 QEMU user+system ticks at 100 ticks/s: 13.4%, 13.2% and
19.2% of one host CPU. This includes the whole emulator and live spaces, not
network-worker CPU time. Host RSS was 327.37 MiB. A subsequent separate GDB
snapshot showed 15,188 allocated PMM frames (59.328 MiB), five retained TCP
records, and the network worker ready/unlocked, with its wait published,
notification clear and local queue empty. PMM is global allocator backing,
excluding permanently reserved frames; it is not process RSS. `fastfetch --json` inside the earlier Remote session reported 62.805 MiB allocated, and
62.918 MiB after transfers, including its own query process. Those snapshots
do not isolate network allocation or establish a leak bound.

Then run three fresh plain machine sessions, each supplied 1,024 `echo -n`
lines followed by `exit`, through the existing client:

```sh
/usr/bin/time -f %e pyxis-remote --machine --no-shell-echo \
  --columns 120 --rows 40 127.0.0.1 24231 < launch-input.txt
```

Whole-client wall times were **15.256, 15.145, 15.323 s**. Every run reported
1,024 successful child exits, builtin exit success, client status zero and
normal/drain-complete FINAL. These times include session startup/closure,
Remote framing and scheduler work, rather than isolated loader cost.

Finally, each fresh session ran one existing IPC workload, alternating CALL and
SEND, three invocations per mode:

```text
session boot://ipcbench.pxe call --size 64 --messages 8 --rounds 5
session boot://ipcbench.pxe send --size 64 --messages 8 --rounds 5
```

All warmups and 15 measured passes per mode verified content/counts and cleanup;
all clients exited zero with normal/drain-complete FINAL. CALL completion
medians per invocation were 3.077, 2.345 and 5.367 ms; combined pass range
2.303–7.844 ms. SEND completion medians were 1.221, 0.933 and 0.517 ms;
combined pass range 0.395–1.499 ms. These are eight-message batch timings,
not individual latency percentiles or sustained throughput. Variation is
material; repeat matched controls and interleave images before attributing a
small change. No guest CPU-placement claim was made.

Raw events, peer counts, exact QEMU argv, hashes, workflow provenance, idle
windows and debugger snapshot are retained under
`/shared/present/network-interfaces/task1-baseline/`. QEMU and peers were stopped
after capture. No native connection, PXE staging or NCM TX A/B occurred.

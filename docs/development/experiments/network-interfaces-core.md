# Interface-owned network core controls

2026-10-18, task 2 of [multiple interfaces](../../wip/network-interfaces.md).
Internal ownership changes only; public configuration/ABI remain `net0` plus
loopback. No native hardware, NCM TX A/B or PXE operation was performed.

## Inputs and method

A is exact main `0149d770adfbf996f620be8eaa279e003595f5be`, after the
[task-1 baseline](network-interfaces-baseline.md). B is source commit
`43de8ed8a97f1033ac33b1b3395cffc6a6e58aff`; subsequent commits only update docs.
Unchanged pins: userland `214d0a59`, ports `7604c85d`, fs `25337999`,
lwIP `a1aadb91`. A used the successful main workflow artifacts (API run 3189,
UI [run 3089](https://git.internal/PyxisOS/pyxis-os/actions/runs/3089)). B used
`make -j16` and `make -j16 image PREBUILT="sdk userspace ports"` in the owner
builder `pyxis-llvm23.1.3-3d69e83`, with those verified component bundles.
No compiler/container rebuild, pin or game-data override. Initrd bytes are
identical, SHA-256 `404a8b8a6785f43a5ef1050a3342fdfc1da2f939603c9168d6074219373c8926`.

| Artifact | SHA-256 |
| --- | --- |
| A ISO | `2819fefbe6b0cda805923051ba10d3a55487c0b5a5cf87f071f74f8602477c3e` |
| A ELF | `a949c38fa5f2ebbf0719510fadf4fe70b73fe2991085e0a799766488722ceef2` |
| B ISO | `cc92d7b980a7666a763c932b2471d0d42bfc2aeaffd94fd38424ba78f66525f9` |
| B ELF | `a1fa619237d12694817f861d47a11c31447a152dc6675beb01ed54cd38469509` |

Use the task-1 configuration, OVMF pair, Kconfig and frozen host client: QEMU
10.2.2, Q35 nested KVM/kernel IRQ chip, host CPU, four CPUs, 2 GiB, standard
VGA, IDE ISO, VirtIO RNG and one modern VirtIO NIC with user networking. Fresh
writable OVMF VARS and boot per sample; no disk/HOST export. Remote is plain
machine mode, 120×40, no shell echo. No profiler or attached debugger during
timing. Other agents share the VM; no competing build was observed.

Order: **A1/B1/A2/B2/A3/B3**. After eight seconds of warmup, run task 1's exact
16 MiB `ttcp` TX/RX, `tcp | sha256sum` and host UDP echo commands. Reverse
TX/RX order for pair 2. Close Remote, sample five seconds of whole-QEMU idle,
then take a separate GDB snapshot. Run a fresh session with 1,024 `echo -n`
launches, then fresh CALL and SEND `ipcbench` sessions, each `--size 64
--messages 8 --rounds 5`. Host `socat` peers, payload, port forwards and client
commands are the same as task 1. No debugger overlaps these measured workloads.

## Results

All TCP byte counts were 16,777,216 and all hashes matched task 1's source.
UDP returned the expected payload. Every launch session had 1,024 successful
children; all IPC warmups/passes verified content and cleanup. Remote clients
exited zero with normal, drain-complete FINAL.

| Pair | TX A / B, MiB/s | RX A / B, MiB/s | 1,024 launches A / B, s | UDP command A / B, ms |
| --- | --- | --- | --- | --- |
| 1 | 3.616 / 3.719 | 5.499 / 5.697 | 16.933 / 15.005 | 109.318 / 82.363 |
| 2 | 3.549 / 3.718 | 5.192 / 5.598 | 14.658 / 14.524 | 83.055 / 88.954 |
| 3 | 3.728 / 3.714 | 6.010 / 5.822 | 14.662 / 13.757 | 75.016 / 69.075 |

TX includes closure; RX runs through EOF. UDP is whole Remote command wall,
including launch/cleanup, not isolated RTT. Task 1's earlier TX range was
2.979–3.660 MiB/s, RX 5.419–5.828 and launch 15.145–15.323 s. The new
interleaved controls show no repeated slowdown; nested variance prevents a
small speedup claim.

| IPC, five-pass completion medians per invocation | A, ms | B, ms |
| --- | --- | --- |
| CALL | 3.791, 3.291, 2.350 | 3.209, 3.275, 2.353 |
| SEND | 0.535, 1.613, 0.545 | 0.425, 0.586, 0.535 |

Combined CALL pass ranges were A 2.297–6.050 ms, B 2.270–8.135 ms; SEND
A 0.404–1.901 ms, B 0.388–1.186 ms. These are eight-message batch timings,
not individual-message latency percentiles.

Idle CPU, as percentage of one host CPU for the whole emulator, was
A 14.6/9.4/9.6%, B 9.4/9.0/8.2%. Global allocated PMM backing was
A 15,116/15,052/15,052 pages, B 15,244/15,116/15,052: **58.797–59.047 MiB
versus 58.797–59.547 MiB**. One candidate snapshot is 512 KiB above the
highest control; these snapshots do not isolate its cause or prove leak freedom.
Host QEMU RSS ranges overlap (A 326.96–327.20 MiB, B 327.08–327.39 MiB).
B has seven fewer PMM-managed frames (28 KiB reserved-capacity difference);
page-rounded kernel LOAD memory grows by one page. Do not attribute all seven
frames to runtime network allocations.

GDB `sizeof` measurements show the fixed metadata cost; pool counts stay global:

| Structure | A → B, bytes | Existing count |
| --- | --- | --- |
| Native interface | 16 → 80 | 2 |
| Software packet metadata | 16 → 32 | 32 |
| ARP neighbor / pending | 48 → 56 / 40 → 56 | 16 each |
| UDP endpoint | 168 → 184 | 16 |
| TCP record | 152 → 168 | 32 |

## Functional checks and limits

Interactive VirtIO QEMU checks passed: passive TCP through Remote; own-address,
loopback and gateway ICMP (3/3 each); own-address and loopback UDP echoes; and
an honest connection-refused result for a closed loopback TCP port. GDB verified
both netif/native-context associations, assigned address/epochs, empty local
queue and the worker's published idle wait. Address-change retirement and
other hardware paths were source-reviewed, not claimed as native results.

A separate static-profile TAP boot used the same ELF, `DEBUG_NET=eps-core2`
and `DEBUG_WAIT=1`. Pyxis's UDP debugger bridge established an image-bound
four-CPU checkpoint; GDB read the retained VirtIO context and netif association,
then continued. Ordinary Remote worked afterward; separate QEMU-stub inspection
confirmed the driver's emergency gate returned to state zero. This checks the
stopped-network handoff in QEMU, not on RTL/NCM hardware. USB NCM's existing
unsupported stopped/fatal transport remains unchanged.

Raw captures, exact argv, hashes and per-boot summaries are under
`/shared/present/network-interfaces/task2-controls/`; exact-main component
provenance is in `task2-inputs/` beside it. All owned QEMU, debugger, bridge,
peer and TAP resources were stopped/removed. Dual-interface isolation, public
scoped factories and native management/data separation belong to tasks 3–4.

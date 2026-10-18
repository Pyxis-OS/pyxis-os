# Multiple-interface task-3 checkpoint

2026-10-18. [Task 3](../../wip/network-interfaces.md) is in progress, not delivered
multiple-interface support. This record starts with controls captured before its
first source change. No native run, PXE staging or personal data override.

## Fresh main control

Main `fed2ce888506dd1e30d076d6ea1411a5102f797e` (task 2 merged), userland
`a6c39a12`, ports `7604c85d`, fs `25337999`, lwIP `a1aadb91`. All four clean
component bundles came from successful main [CI run 3112](https://git.internal/PyxisOS/pyxis-os/actions/runs/3112)
(API3212), with provenance/hash verification before reuse. ISO SHA-256:
`7e4f7b6bdc20aa59678650acaa089493f61e297230784d29009175774dd8ef61`;
ELF: `852d9c996237675f812dc198b04089cde0fa20d534ffd859d5024fb40cb1c2d1`.

Use the [task-1 configuration and commands](network-interfaces-baseline.md):
QEMU10.2.2, Q35 nested KVM/kernel IRQ chip, host CPU, four CPUs,2GiB, raw OVMF
pair/fresh VARS, standard VGA, IDE ISO, VirtIO RNG and one modern VirtIO NIC,
user networking/DHCP, no disk or HOST export. Same frozen120×40 machine client.
No competing build was observed. Timings exclude attached-debugger intervals.

One fresh boot,8s warmup: TX/RX/hash/UDP, one CALL and SEND invocation; three
fresh1024-launch sessions; RX/TX/UDP then TX/RX/UDP; two more CALL/SEND pairs;
three5s idle windows; a separate final GDB snapshot. Peers and payloads match
task1. A launch sample accidentally overlapped GDB and is explicitly excluded
in the raw capture, replaced by all three listed clean samples.

| Control | Three valid samples |
| --- | --- |
| TCP TX16MiB, MiB/s including closure | 3.365,3.411,3.376 |
| TCP RX16MiB, MiB/s through EOF | 4.895,4.878,5.367 |
| 1024 launches, whole-client seconds | 14.796,14.997,14.947 |
| CALL five-pass medians, ms | 3.298,4.679,3.173 |
| SEND five-pass medians, ms | 0.758,0.809,0.446 |
| Idle whole-QEMU CPU, percent of one CPU | 10.2,8.8,9.8 |

All byte counts/hash/UDP payloads matched; every launch had1024 successful child
exits and normal drain-complete FINAL. All IPC warmups and passes verified.
CALL pass range2.581–8.967ms; SEND0.439–1.469ms. Idle host RSS341.797MiB;
final global PMM allocation19464pages (76.031MiB), empty deferred queue,
worker wait published and notification clear. That final retained backing is
a global post-workload snapshot, not network RSS or a leak bound. Small nested
variations do not establish a native throughput change.

Raw argv, events, summaries and the excluded sample:
`/shared/present/network-interfaces/task3-baseline/`. Exact main components and
workflow records: `task3-inputs/` beside it. Baseline QEMU/peers stopped before
source builds. Repeat matched/interleaved controls when the public cut is ready;
these numbers do not qualify the pending ABI or dual-interface isolation.

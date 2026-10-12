# Prepared HTTP transactions: QEMU qualification

2026-10-12, nested KVM, Q35 `-cpu max -smp 4 -m 2G`, HPET, UTC RTC,
VirtIO net/user networking and entropy, no HDA. Disposable local fixtures at
`10.0.2.2:18080` and HTTPS `10.0.2.2.sslip.io:18443` with a temporary public CA;
no paid API or real credentials. These are QEMU results, not native measurements.

Baseline parent `4366eb28`, userland `21cf935c`; implementation kernel `62e57184`,
userland `705b148` (parent integration #765). Both use fs `30f625a2`, ports
`fd1eb30f`, lwIP `a1aadb91`, LLVM23.1.3-2d23443. Original pre-edit
`9bd3d66d`/`62da3399` measurements are retained locally; the table refreshes the
comparison after the managed-libc merge. No compiler-container rebuild.

## Matched cost runs

Separate baseline/initial implementation/baseline/final implementation boots
(A1/B1/A2/B2), same configuration, fresh OVMF variables. No task-owned build or
GDB inspection during timing; other host workloads were not controlled. Each GET
and RAM row is three measured runs after warmup, byte verified; POST is three
runs, with the server independently checking 1 MiB length and SHA-256.

Commands: `iobench read http://10.0.2.2:18080/fixed --rounds 3`;
`cp boot://share/iobench.bin tmp://post-body` then
`iobench read tmp://post-body --rounds 3`; and three
`boot://httpget.pxe http://10.0.2.2:18080/echo --post boot://share/iobench.bin`
requests through the existing direct-libhttp exercise. Helpers/captures
stay local, outside ordinary images. Ranges are minimum–maximum, milliseconds.

| Workload | A1 baseline median (range) | A2 baseline median (range) | B2 final median (range) |
| --- | --- | --- | --- |
| Legacy GET OPEN | 181.085 (173.891–203.013) | 206.450 (201.070–222.588) | 205.986 (189.458–218.587) |
| Legacy GET whole 1 MiB | 201.135 (193.201–223.228) | 228.087 (222.600–242.751) | 391.975 (377.939–404.238) |
| Legacy FILE payload reads | 19.864 | 21.344 | 184.572 (184.120–187.094) |
| RAM 1 MiB | 0.454470 (0.449720–0.474210) | 0.451840 (0.436230–0.485500) | 0.436210 (0.435150–0.469330) |
| Direct fixed-file POST 1 MiB | 406.776 (363.155–414.899) | 371.750 (354.165–398.086) | 349.994 (337.492–377.185) |

B1 (`edb6541`) GET median was 607.908 ms (607.116–883.026): an unnecessary
zero-deadline WAIT_MANY poll submitted a BSP readiness request before every IPC,
including immediately after a wait woke. Direct TRY collection, one bounded
engine step and parking only when neither progresses restored OPEN time. Lost
wakeups remain covered by wait registration before the locked readiness scan.

Final legacy payload reading remains slower: B2 whole GET is 1.72 times A2,
2.551 MiB/s versus 4.384 MiB/s. RAM and direct POST overlap; clock controls are
about 35–42 microseconds/read. Source inspection shows readiness/BSP/timer work
when the next READ is not queued at TRY, replacing the old blocking RECEIVE loop;
this is not a measured per-function attribution. The cooperative loop is retained
for deadline/network/status progress. No generic timekeeping or wait-worker
optimization is included. Revisit after native evidence, without inferring native
throughput from this nested-QEMU cost.

## Qualification and limits

Manual native-grant qualification is in progress. This record will be completed
with actual chunk/framing, ownership, cancellation and storage results before
#765 becomes ready. Ordinary kernel/SDK/C++ runtime/ports/userland/image builds
passed; focused review-fix image builds passed. Dependent userland #240 has no
independent CI workflow; the parent builds its published head.

The [interface](../../interfaces/http-transactions.md#service-and-limits) retains
synchronous DNS randomness (3 s) and TLS entropy callbacks (5 s), additionally
bounded by the request deadline. STATUS/ABORT cannot run during those calls,
capability/service handoffs, allocation or cryptography. No native run or
multi-GB transfer is claimed in task 3; final end-to-end multi-GB shell/Lua delivery
belongs to the later adapter/coordinator tasks.

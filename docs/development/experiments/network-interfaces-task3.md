# Multiple-interface task-3 checkpoint

2026-10-18. [Task 3](../../wip/network-interfaces.md) is in progress, not delivered
multiple-interface support. This record starts with controls captured before its
first source change. No native run, PXE staging or personal data override.

## Fresh main control

Main `fed2ce888506dd1e30d076d6ea1411a5102f797e` (task 2 merged), userland
`a6c39a12`, ports `7604c85d`, fs `25337999`, lwIP `a1aadb91`. All four clean
component bundles came from successful main [CI run 3112](https://git.internal/PyxisOS/pyxis-os/actions/runs/3112)
(API 3212), with provenance/hash verification before reuse. ISO SHA-256:
`7e4f7b6bdc20aa59678650acaa089493f61e297230784d29009175774dd8ef61`;
ELF: `852d9c996237675f812dc198b04089cde0fa20d534ffd859d5024fb40cb1c2d1`.

Use the [task-1 configuration and commands](network-interfaces-baseline.md):
QEMU 10.2.2, Q35 nested KVM/kernel IRQ chip, host CPU, four CPUs, 2 GiB, raw OVMF
pair/fresh VARS, standard VGA, IDE ISO, VirtIO RNG and one modern VirtIO NIC,
user networking/DHCP, no disk or HOST export. Same frozen 120×40 machine client.
No competing build was observed. Timings exclude attached-debugger intervals.

One fresh boot, 8 s warmup: TX/RX/hash/UDP, one CALL and SEND invocation; three
fresh 1024-launch sessions; RX/TX/UDP then TX/RX/UDP; two more CALL/SEND pairs;
three 5 s idle windows; a separate final GDB snapshot. Peers and payloads match
task1. A launch sample accidentally overlapped GDB and is explicitly excluded
in the raw capture, replaced by all three listed clean samples.

| Control | Three valid samples |
| --- | --- |
| TCP TX 16 MiB, MiB/s including closure | 3.365, 3.411, 3.376 |
| TCP RX 16 MiB, MiB/s through EOF | 4.895, 4.878, 5.367 |
| 1024 launches, whole-client seconds | 14.796, 14.997, 14.947 |
| CALL five-pass medians, ms | 3.298, 4.679, 3.173 |
| SEND five-pass medians, ms | 0.758, 0.809, 0.446 |
| Idle whole-QEMU CPU, percent of one CPU | 10.2, 8.8, 9.8 |

All byte counts/hash/UDP payloads matched; every launch had 1024 successful child
exits and normal drain-complete FINAL. All IPC warmups and passes verified.
CALL pass range 2.581–8.967 ms; SEND 0.439–1.469 ms. Idle host RSS 341.797 MiB;
final global PMM allocation 19464 pages (76.031MiB), empty deferred queue,
worker wait published and notification clear. That final retained backing is
a global post-workload snapshot, not network RSS or a leak bound. Small nested
variations do not establish a native throughput change.

Raw argv, events, summaries and the excluded sample:
`/shared/present/network-interfaces/task3-baseline/`. Exact main components and
workflow records: `task3-inputs/` beside it. Baseline QEMU/peers stopped before
source builds. Repeat matched/interleaved controls when the public cut is ready;
these numbers do not qualify the pending ABI or dual-interface isolation.

## First internal checkpoint

Kernel source `1510a376` plus documentation commit `c19c5173` builds with the
owner builder `pyxis-llvm23.1.3-3d69e83`; image assembly reuses the verified main
SDK/userland/ports bundles because the public ABI is unchanged. A bounded
independent source review found no concrete defect in scope routing or factory
reference ownership. This does not qualify the pending public ABI or dual NICs.

Interactive four-CPU VirtIO QEMU passed Remote/passive TCP; own-address,
loopback and gateway ICMP, three replies each; own-address and loopback UDP
with exact echo payloads; 16 MiB TCP TX/RX byte counts and source hash. A closed
loopback TCP port returned connection refused. These were functional runs,
not matched timing measurements of the unfinished public cut.

A host-local HTTP fixture exercised the existing file-provider consumer of
native completion CONNECT. A QEMU GDB breakpoint at the native reserve entry
confirmed the resolved TCP factory held exactly the `lo` and `net0` contexts;
continuing produced the exact response body. After stopping the fixture, the
next fetch failed honestly and ordinary Remote commands still worked. Final
GDB inspection found all eight TCP control slots free with no retained factory
pointer, both projection/context associations intact and the local queue empty.
This is an observed collection checkpoint, not a proof of every cancellation
or failure path. No forced-failure infrastructure was added.

Raw first-core argv/events, GDB observations and build logs are under
`/shared/present/network-interfaces/task3-work/first-core/` and `task3-baseline/`.
Background UDP server launches are recorded as `launched`, which the reused
foreground-only capture helper labels FAILED; both counted one-shot servers
returned the expected datagrams. Closed-port/fetch failures were intentional.
All owned QEMU, GDB, HTTP and transfer peers were stopped after qualification.


## Read-only inventory and scope checkpoint

Inventory source `760abed0`, userland `2f12d8b`, passed all four checks in
[CI run 3123](https://git.internal/PyxisOS/pyxis-os/actions/runs/3123)
(API 3223). Kernel, SDK/runtime and `net` builds also passed locally with the
same owner builder. The exact CI image booted in the same four-CPU VirtIO
configuration. Two `net` invocations traversed `lo` ID 1, `eth0` ID 2 and the
end record; loopback had no fabricated hardware/carrier facts. Loopback and
gateway ICMP each returned four replies. GDB confirmed those context identities.
The initial unqualified GDB expression `calls` selected an unrelated RNG array;
it is not evidence about the network configuration slots. The later capture
below uses source-qualified names.

Scope source `c8ec5e3f`, userland `9874a79`, passed all four checks in
[CI run 3125](https://git.internal/PyxisOS/pyxis-os/actions/runs/3125)
(API 3225). This userland branch is based on main `692185c` (the merged font
example); unchanged ports/fs/lwIP pins match the control. Kernel and SDK/runtime
builds passed locally, including the typed scope helpers. Independent bounded
source reviews found no concrete inventory, publication, scope or lineage defect.

The exact CI image booted in four-CPU VirtIO QEMU. A disposable manual C program
was written to RAM `home://` and compiled with the image's TCC/SDK, without a
build-system or installed-test addition. The observed interface IDs were passed
as arguments, not assumed to be a public numeric convention. For each TCP, UDP
and echo factory it checked ascending scope/end, one-interface derivation, refusal
of scope/rights widening and zero rights, copying followed by closing the first
holder, and ancestor CUT propagation through a newly derived factory. The source
factory retained its original scope. Scoped TCP/UDP/echo attempts to the physical
local address were denied; loopback UDP open and echo exchange succeeded. Both
ordinary invocations exited zero; the second checked continued ordinary use
after the first process released its references.

Source-qualified final GDB inspection found eight configuration, eight TCP
control and eight UDP control slots free, all sixteen echo entries free, no
retained factory pointers in those controls, no queued BSP request and no pending
retired-object list. This is an observed ordinary completion/collection check,
not a proof of every cancellation, copyout failure or fault path. Those cases and
dual-interface lifecycle qualification remain open.

Raw CI-image hashes, argv, manual source/commands, output and GDB captures are in
`/shared/present/network-interfaces/task3-work/inventory/` and `factory/`.
These functional checks do not supply after-cost or native results. Refresh and
interleave matched controls at the complete public cut if intervening main or
dependency changes affect the workload. The task remains draft and unchecked.

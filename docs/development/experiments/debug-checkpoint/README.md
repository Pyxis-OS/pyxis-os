# Debug checkpoint qualification

2026-10-09: baseline `807cba49`, final checkpoint implementation `5cc7b59f`, option
absent in both measured images. Ordinary kernel/image builds used the installed
LLVM 23.1.3 fork `49e2c1a` builder and independently verified unchanged SDK,
userland and ports bundles. No compiler rebuild or new qualification infrastructure.

QEMU 10.2.2, Q35, nested KVM, CPU max, four cores/one socket/no SMT, 512 MiB,
standard VGA/display none, VirtIO net/RNG, user networking, UTC RTC and fresh
`/usr/share/edk2/ovmf/OVMF_VARS.fd` paired with `OVMF_CODE.fd`. No disks, host
filesystem export, profiling or trace. Default init/config/network except the
explicit launch profile below. Same dependency pins and effective kernel config.

| Workload | Baseline median (range) | Option off median (range) |
| --- | --- | --- |
| HPET epoch to ready log, 3 boots, ms | 218.801 (215.608–437.608) | 233.598 (219.322–236.634) |
| One remote echo session, 3 runs, s | 0.03 (0.02–0.04) | 0.03 (0.03–0.03) |
| Boot FILE complete read, 5 samples, ms | 4.050 (3.987–4.606) | 4.001 (3.938–4.045) |
| RAM FILE complete read, 5 samples, ms | 9.765 (9.312–10.285) | 9.258 (9.244–9.285) |
| 100 sequential echo launches, 3 runs, s | 0.51 (0.50–0.52) | 0.64 (0.51–0.73) |
| Fresh old-kernel 100-launch control, 3 runs, s | 0.52 (0.51–0.52) | — |

Build: `make -j16 image PREBUILT="sdk userspace ports"` in the existing builder.
Boot timing: QEMU's existing GDB with one hardware breakpoint on the existing
`Caelum ready` log call, read HPET counter at `0xfffffe80402020f0`, multiply ticks
by its 10 ns period, then detach. Excludes firmware/early adapter work.

Read commands: `iobench read boot://share/iobench.bin --buffer 64 --rounds 5`;
prepare the RAM copy with `cat boot://share/iobench.bin > home://checkpoint-baseline.bin`,
then the same read against that path. Each has a verified untimed warmup and five
verified 1 MiB passes; setup/verification excluded. These payload paths are mostly
local and do not establish IPI-heavy traffic.

Launch commands use the existing machine remote client, `--no-shell-echo`,
80 columns/24 rows, loopback forwarding to guest 2323. Host `/usr/bin/time -f %e`
includes connection, shell/application startup, launch/wait/output/cleanup and
explicit `exit`/FINAL; 0.01 s resolution. The 100-launch profile changes only
staged Remote configuration to `launch=true`, identically in both images:
`lua -e 'for i=1,100 do assert(pyxis.run{"boot://echo.pxe","ipi-baseline"} == 0) end'`.
Every run printed 100 lines and exited 0 with complete session drain. Separate
GDB inspection observed 1,268 reschedule IPI sends, including background/setup/
cleanup; its timing is excluded and the count is not a workload contract.

Default boot/one-launch ranges overlap; both read medians are lower. The 100-launch proxy remained
**23.1% higher in median** than the fresh old-kernel control (25.5% versus the
original baseline), with overlapping, widely varying ranges. An earlier candidate
(`b7975848`) had a 3.8% median difference versus that control; its raw captures
remain local. Final-source samples are retained without selecting faster runs. **Zero measurable option-off cost is not established.**
No ordinary timer/scheduler/IPI hook was added; the cause of this end-to-end proxy
difference remains unassigned in shared nested KVM. Do not infer isolated IPI
latency or native performance. This qualification limit remains for owner review;
no unrelated optimization was added.

The default baseline preceded implementation. The additional old-kernel launch
profile followed implementation start, using the unchanged baseline image.
After samples were repeated with other task-owned qualification VMs stopped;
concurrent-VM/read-client observations are excluded. Fresh old-kernel controls
followed the accepted after samples. Raw captures and commands remain local/PR
material, not repository fixtures. All owned processes were stopped.

Enabled behavior: interactive one/four-CPU QEMU/GDB confirmed all ACKs/IST stacks,
unmapped guards, mismatched/matching release, unmodified BSP return registers,
and fixed 30-second expiry. Holding APs using GDB reached terminal INCOMPLETE;
late ACKs and matching release did not resume, even past 30 seconds. Normal
network/remote echo worked after complete release. Disabled inspection confirmed
no snapshot/stack/IDT allocations, original NMI gate/IST0 and unused IST2. Common
ISR/exception reporting is unchanged. A monitor NMI did not produce delivered
fault output under this KVM configuration; no delivered-fault pass is claimed.
Actual syscall-window entry and native 32-bit HPET wrap were code-inspected,
not exercised; QEMU used direct 64-bit HPET. No native qualification is claimed.

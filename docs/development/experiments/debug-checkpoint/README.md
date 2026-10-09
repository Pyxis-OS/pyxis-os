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

## Interleaved review follow-up

The earlier sequential launch blocks were inconclusive. The review requested
fresh boots in **A B A B A B A B A B** order, five per image, using the same
100-launch profile. A is baseline `807cba49`; B is final kernel `5cc7b59f`
(the code reviewed at PR head `b9bf312a`). No code changed for this rerun.

| Pair, A then B | Baseline A, s | Option off B, s |
| --- | --- | --- |
| 1 | 0.52 | 0.50 |
| 2 | 0.52 | 0.49 |
| 3 | 0.49 | 0.49 |
| 4 | 0.49 | 0.52 |
| 5 | 0.52 | 0.49 |
| Median (range) | 0.52 (0.49–0.52) | 0.49 (0.49–0.52) |

Every trial used fresh firmware variables and exactly one timed 100-launch
command after normal network/remote startup. Same QEMU/settings/input/client as
above; no debugger attached, no profiling, warmup or sample exclusion. All ten
runs produced exactly 100 echo lines, Lua exit 0, shell exit 0 and complete FINAL
drain. Both ISOs' extracted kernel hashes match their recorded revisions; boot
command lines are identical with the option absent. All 886 initrd payload
entries have identical contents/modes; archive metadata differs.

The host was at low load (preflight load average 0.58), with no concurrent build
processes observed before, during or after the trials; load averages were
0.68 mid-run and 1.02 at the end. One unrelated SDL QEMU
remained active, about 0.12 CPU in the preflight interval; it was left untouched.
This is a recorded low-load window, not a fully idle host. Only one task-owned
VM ran at a time. All ten were stopped after their sample.

**The review criterion is met:** each median falls within the other image's
range. The prior slowdown was not reproduced, so the option-off qualification
gate is closed. This establishes no measurable slowdown in this run; it does
not establish a speedup, isolated IPI latency, exact zero machine cost or native
performance. Inspection likewise finds no new timer/scheduler/IPI/syscall/launch
work when disabled. No bisect is needed under the review's stated criterion.

Reproduction: select the archived A/B profile ISO alternately with `-cdrom`,
copy the same OVMF variables for every boot, and use the unchanged Lua input:

```sh
/usr/bin/time -f '%e' -o sample.seconds \
  build/tools/pyxis-remote --machine --no-shell-echo --columns 80 --rows 24 \
  127.0.0.1 24569 <lua100.input >sample.events.jsonl
```

A profile ISO SHA-256: `7ed724f4ef893399fe281a7ff5da171258ccf008272a11185f0f01e131e04a99`;
B: `28fc88eea807cc777eb4a93aed1d3f1439e653a6af86bf50b7a9c1dcaa42b8d0`.
Raw events, boot logs, host observations and original sequential captures remain
local/PR material. No measurement framework or kernel mechanism was added.

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

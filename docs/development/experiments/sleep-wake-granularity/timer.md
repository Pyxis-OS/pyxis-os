# Per-CPU deadline timer qualification

Measured on 2026-10-08 after the accepted
[sleep wake decisions](../../../kernel/timekeeping.md). Compare the
[original baseline](README.md) and separately measured [expiry IPI step](ipi.md).
The owner authorized implementation; the owner's native observation is in
[technical debt](../../../technical-debt.md#sleep-wake-granularity).

The measured source was `e253f22`: the locally integrated architecture/scheduler
change following the IPI step. Its ordinary kernel and image builds passed with
the same published LLVM 23.1.3 / `49e2c1a` builder and effective Kconfig. Later
commits integrate documentation and the SDL2 timer-comment dependency; their
submitted-head CI is reported separately from these identified artifacts.

| Artifact | SHA-256 |
| --- | --- |
| Measured kernel ELF | `134660a4918c3ded05584911538f8a8bf103ab1c6943fd0969ac76cdc4716ced` |
| Matched measurement ISO | `693179978e0ea2318913308495ac6e8f5057d295d5c4f60a8fde8122cbc869c5` |
| Unchanged measurement initrd | `7de899d4f50579a605d815830f4a970bed6a8f4f02d14837ad6cc0321b95de8c` |

Only `boot/caelum.elf` was replaced in the baseline image tree. The original
workload/library bytes, raw firmware, device order, four CPUs/8 GiB, standard
VGA 1280x800 and stock QEMU 10.2.2 nested KVM configuration remained fixed.
No other task-owned guest or build ran during timing. The additional one-CPU
and short-sleep checks below are separate qualification configurations.

## Matched SDL_Delay(16), unprofiled

Five individual Development invocations used the original 640x480 texture,
software render/present loop, ten warm-up frames and 300 measured frames.
Remote read the redirected summary afterward. GDB was detached throughout.
All 1,500 measured sleeps were at least 16 ms.

| Run | Mean work ns | Mean frame ns | Mean sleep ns | Minimum sleep ns | Maximum sleep ns |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 866112 | 17229145 | 16363032 | 16222270 | 17384300 |
| 2 | 1141495 | 17657000 | 16515504 | 16203340 | 18011750 |
| 3 | 988704 | 17414577 | 16425873 | 16274430 | 17284270 |
| 4 | 995757 | 17398561 | 16402804 | 16231800 | 17631760 |
| 5 | 983857 | 17430315 | 16446457 | 16259360 | 17379980 |

Median per-run mean frame time: **17.414577 ms**, range
**17.229145–17.657000 ms**. Median mean work: **0.988704 ms**; median mean
sleep: **16.425873 ms**, range **16.363032–16.515504 ms**. The coarse tick wait
is removed in this workload; syscall, calibration, scheduling and host variation
remain. The measurements are no native or maximum-latency guarantee.

## Matched Quake cap, debugger-assisted

The same packaged `quake +map e1m1`, no input and `cls.timedemo=false` used
the original verified P1F/debugger ELF pair. Five manual `Host_Frame` boundary
windows sampled completed frames and the 10 ns HPET counter, with the hardware
breakpoint disabled between boundaries. No sampling loop or inferior call.
All four deadline modes were active; ordinary migration remained enabled.
See the sanitized [raw transcript](quake-timer-gdb.txt).

| Window | Completed frames | HPET ticks, 10 ns | Frames/s |
| --- | ---: | ---: | ---: |
| 1 | 1134 | 1614026487 | 70.259070 |
| 2 | 1129 | 1603231385 | 70.420278 |
| 3 | 1385 | 1970386549 | 70.290776 |
| 4 | 1093 | 1551360206 | 70.454302 |
| 5 | 1187 | 1709130280 | 69.450528 |

Median capped rate: **70.290776 FPS**, range **69.450528–70.454302 FPS**.
This approaches the nominal 72 Hz cap while retaining interrupt/scheduling
overhead. It is not `timedemo` throughput or native performance.

| Stage | SDL median mean frame ms | Quake median capped FPS |
| --- | ---: | ---: |
| Original baseline | 25.565202 | 49.222714 |
| Expiry IPI only | 24.243215 | 59.594522 |
| Local one-shot deadlines | 17.414577 | 70.290776 |

SDL runs were unprofiled; Quake windows were debugger-assisted and differently
sized. These distinctions and the recorded variation apply to the comparison.

## Idle and short-sleep interrupts

Read-only GDB confirmed all four CPUs had entered deadline mode. CPU 1 had
`ready_head`, `current_task` and `timed_waits` all NULL at three observed stops.
Its two manual windows measured:

| Window | IRQ delta | HPET ticks, 10 ns | IRQ/s |
| --- | ---: | ---: | ---: |
| 1 | 2258 | 1880588638 | 120.068789 |
| 2 | 2519 | 2098683101 | 120.027650 |

This is consistent with retained nominal 120 Hz preemption and shows no material
idle interrupt increase. Endpoint/count differences and unobserved intermediate
state do not establish exact absence of every premature/stale IRQ or native
power behavior. See [idle transcript](idle-timer-gdb.txt).

For the short-sleep check, the same SDL measurement source changed only
`DELAY_MS` from 16 to 1. Its separately compiled/uploaded P1F SHA-256 was
`3b4202a3729c225e3e8c2e7e418f18044446c744bf225ba05ef082330d5fb9da`.
The matching debugger ELF's entry/loadable bytes were verified identical.
Hardware breakpoints at the first `SDL_Delay(1)` and final texture destruction
bounded the run; the initial breakpoint was disabled while it executed.

The 300 measured frames averaged work 899188 ns, frame 2232852 ns and sleep 1333664 ns.
Sleep ranged 1183510–2000880 ns, with zero returns below 1 ms. Including warm-up
and final output, the boundary interval was 104642625 HPET 10 ns ticks
(1.04642625 s), with timer IRQ deltas 192/127/126/430 on CPUs 0/1/2/3. The APs
carried approximately their nominal preemption plus 310 sleep events, with some
deadline/preemption coalescence; BSP also serviced kernel deadlines. No IRQ
storm was observed. This checks the real positive-duration path, not an injected
zero-count or saturation fault. See [short-sleep transcript](short-sleep-gdb.txt).

## One CPU, cancellation and resource wakes

The same measurement ISO booted with `cpus=1,sockets=1,cores=1,threads=1`; other
devices/firmware/memory were unchanged. All four ordinary spaces/networking
started and BSP deadline mode was active. One original 300-frame SDL run reported
mean work 1057881 ns, frame 17507071 ns, sleep 16449189 ns, range 16267830–18049560 ns,
and zero early returns. It is a functional one-CPU check, not a matched
four-CPU performance sample.

An ordinary `log -f | log -f` pipeline created simultaneous 100 ms user sleepers.
At deadline-service entry, GDB observed sorted timed deadlines
306203587560, 306209185800, 306222168660, 307138811970, 322369354370 ns;
the first was a kernel waiter, followed by the two user waiters on CPU 0.
BSP's separate kernel sleeper deadline was 306207443668 ns. Ctrl+C released
both logger tasks and returned to the shell. See [one-CPU transcript](onecpu-gdb.txt).

The existing `session ipcbench call --messages 4 --rounds 1` then completed a
warm-up and timed pass with four verified round trips each, no failed call/delivery
and 256 confirmed request/reply bytes. Session handoff supplies the endpoint
service omitted from ordinary command grants; the first ordinary-command attempt
correctly refused missing grants. This exercises resource completion/removal of
timed endpoint waits without changing authority. It is no new performance claim.

## Limits

SDL2 retains upstream's `SDL_WaitEvent` polling loop with a 1 ms delay.
Deadline sleeps make this about 1 ms instead of the old 8.33 ms tick, so an
idle program waiting for an event wakes about 1000 times a second instead of
about 120, increasing its CPU wake cost. This is the expected polling rate,
not a measured `SDL_WaitEvent` run. Revisit a blocking wait on the input and
display handles when a consumer waits for events; see
[SDL2 port limits](../../../technical-debt.md#sdl2-port-limits).

Sorted-list insertion/removal, all-due-prefix expiry, pre-parking notification,
startup periodic-to-one-shot transition, arithmetic bounds and nominal-only HPET
maintenance were also source-reviewed. Large simultaneous expiry batches,
backend faults and terminal clock saturation were not injected. ThinkPad LAPIC
rate/power states, actual delay/cap latency and sustained 32-bit HPET behavior
with this timer remain unqualified. All task-owned measurement processes stopped.

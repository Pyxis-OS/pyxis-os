# Per-CPU deadline timer qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Measured on 2026-10-08 after the accepted
[sleep wake decisions](../../../kernel/timekeeping.md). Compare the
[original baseline](README.md) and the separately measured [expiry IPI step](ipi.md).
The owner authorized implementation; the owner's native observation is in
[technical debt](../../../technical-debt.md#sleep-wake-granularity).

The measured source was `e253f22`, the locally integrated architecture/scheduler change
after the IPI step. Its ordinary kernel and image builds passed with the same LLVM
23.1.3 / `49e2c1a` builder and Kconfig. Only `boot/caelum.elf` was replaced in the
baseline image tree; workload and library bytes, firmware, device order, four CPUs/8 GiB,
VGA 1280x800 and the stock QEMU 10.2.2 nested-KVM configuration stayed fixed, and no
other task-owned guest or build ran during timing. The one-CPU and short-sleep checks
below are separate configurations.

## Matched results

Five individual Development invocations used the original SDL workload (640x480 texture,
software render/present, ten warm-up and 300 measured frames) with GDB detached; all
1,500 measured sleeps were at least 16 ms. Median per-run mean frame time was
**17.414577 ms** (range **17.229145–17.657000 ms**), median mean work **0.988704 ms**,
median mean sleep **16.425873 ms** (range **16.363032–16.515504 ms**). The coarse tick
wait is gone in this workload; syscall, calibration, scheduling and host variation remain.

The same packaged `quake +map e1m1` (no input, `cls.timedemo=false`) was sampled at five
manual `Host_Frame` boundary windows (1,093–1,385 frames) with the original verified
P1F/debugger ELF pair and all four deadline modes active. The median capped rate was
**70.290776 FPS**, range **69.450528–70.454302 FPS**: close to the nominal 72 Hz cap
with interrupt/scheduling overhead, and neither `timedemo` throughput nor native
performance.

| Stage | SDL median mean frame ms | Quake median capped FPS |
| --- | ---: | ---: |
| Original baseline | 25.565202 | 49.222714 |
| Expiry IPI only | 24.243215 | 59.594522 |
| Local one-shot deadlines | 17.414577 | 70.290776 |

SDL runs were unprofiled; Quake windows were debugger-assisted and differently sized.

## Idle and short-sleep interrupts

Read-only GDB confirmed all four CPUs had entered deadline mode, and CPU 1 had
`ready_head`, `current_task` and `timed_waits` all NULL at three stops. Two manual windows
on CPU 1 measured **120.068789** and **120.027650 IRQ/s** (2,258 and 2,519 IRQs), consistent
with the retained nominal 120 Hz preemption and no material idle interrupt increase.
Endpoint differences and unobserved intermediate state do not establish the absence of
every premature or stale IRQ, or native power behavior.

For the short-sleep check the SDL source changed only `DELAY_MS` from 16 to 1, with a
matching debugger ELF verified identical in entry/loadable bytes, and breakpoints at the
first `SDL_Delay(1)` and final texture destruction. The 300 measured frames averaged work
899188 ns, frame 2232852 ns and sleep 1333664 ns; sleep ranged 1183510–2000880 ns with
zero returns below 1 ms. The boundary interval was 1.04642625 s (warm-up and final output
included) with timer IRQ deltas 192/127/126/430 on CPUs 0/1/2/3: the APs carried about
their nominal preemption plus 310 sleep events, with some deadline/preemption
coalescence, and the BSP also serviced kernel deadlines. No IRQ storm was observed. This
checks the real positive-duration path, not an injected zero-count or saturation fault.

## One CPU, cancellation and resource wakes

The same ISO booted with one CPU; all four spaces and networking started and BSP deadline
mode was active. One original 300-frame SDL run reported mean work 1057881 ns, frame
17507071 ns, sleep 16449189 ns (range 16267830–18049560 ns) and zero early returns: a
functional check, not a matched four-CPU sample.

An ordinary `log -f | log -f` pipeline created simultaneous 100 ms user sleepers. At
deadline-service entry GDB saw sorted timed deadlines 306203587560, 306209185800,
306222168660, 307138811970 and 322369354370 ns (the first a kernel waiter, then the two
user waiters on CPU 0), and the BSP's separate kernel sleeper deadline was
306207443668 ns. Ctrl+C released both loggers and returned to the shell. The existing
`session ipcbench call --messages 4 --rounds 1` then completed a warm-up and timed pass
with four verified round trips each, no failed call or delivery, and 256 confirmed
request/reply bytes (session handoff supplies the endpoint service that ordinary command
grants omit; the first ordinary-command attempt correctly refused for missing grants).
This exercises resource completion and removal of timed endpoint waits without changing
authority, and makes no new performance claim.

## Limits

SDL2 retains upstream's `SDL_WaitEvent` polling loop with a 1 ms delay. Deadline sleeps
make this about 1 ms instead of the old 8.33 ms tick, so an idle program waiting for an
event wakes about 1000 times a second instead of about 120, raising its CPU wake cost.
This is the expected polling rate, not a measured `SDL_WaitEvent` run. Revisit a blocking
wait on the input and display handles when a consumer waits for events; see
[SDL2 port limits](../../../technical-debt.md#sdl2-port-limits).

Sorted-list insertion/removal, all-due-prefix expiry, pre-parking notification, the
startup periodic-to-one-shot transition, arithmetic bounds and nominal-only HPET
maintenance were source-reviewed. Large simultaneous expiry batches, backend faults and
terminal clock saturation were not injected. ThinkPad LAPIC rate/power states, actual
delay/cap latency and sustained 32-bit HPET behavior with this timer remain unqualified.

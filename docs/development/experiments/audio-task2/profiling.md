# Audio BSP cost: measurement follow-up

Raw counters, accounting, calibration and caller resolution were kept only in
local probe worktrees. This record keeps the summaries.

Measured **2026-10-09**, following the [early review of #557](https://git.internal/PyxisOS/pyxis-os/pulls/557).
Candidates 1 and 2 were accepted and implemented; candidate 3 (request batching)
remains deferred. The owner accepts task 2 delivery with the recorded
nested-QEMU eight-session limit; native eight-session playback remains mandatory for
milestone closure. Eight sessions, eight 10 ms DMA periods, 80 ms session queues,
4096-byte writes and the safety thresholds are unchanged.

## Method

Implementation under review: `62a63e4b`, userland `052ac5ea`. A local probe branch
(`cc6af6be`) adds only temporary counters; it changes no accepted tuning. Both
repeats used one image: QEMU 10.2.2, Q35, **nested KVM**, `-cpu max`, four CPUs, 8 GiB,
OVMF, VGA 1280×800, VirtIO SCSI/RNG/network, `intel-hda` + `hda-output`, WAV 48 kHz S16
stereo, LLVM 23.1.3/`49e2c1a`. The ordinary nine-shell layout was the only userland
policy change; it launches no audio.

Commands were `pcm 1000 500 600` entered manually (Remote alone for one session;
Remote, Development and Audio 1–6 for eight) with HMP `sendkey` and an ordinary Ctrl+C
to stop. Counters start at the first mix with the target number of acquired sessions
and freeze when that count falls or the engine fails; eight source-loop samples per
mix confirm simultaneous participation. This is a workload comparison, not an
isolated presenter test.

Serialized RDTSCP intervals are calibrated against HPET (about 3.187 GHz) and read by
read-only GDB. Clock timing includes only calls entered with interrupts disabled.
Timings include probe overhead, VM exits and host descheduling, so they are **not
exclusive guest CPU time**, and nested rows overlap (do not add them). Registered-waiter
wake counts mean a notification detached a waiter; they are not unique scheduler
transitions. Four windows of about 15.018 s were quiet (no debugger, sampler or other
owned guest or build); in eight-1, GDB attached after the counters froze but before the
other producers stopped, and that later engine inspection is excluded. These short
profiles do not establish long-duration qualification. Counter intervals were
41.702/16.132 s for one and 15.970/20.570 s for eight (idle 26.856 s); counts below
are normalized to **10 ms elapsed**.

## Per-period work

Ranges span two repeats. Clock counts are for the BSP; staging/setup runs on APs. The
idle scene makes 13.80 clock calls / 41.41 HPET reads per 10 ms.

| Work per 10 ms | One session | Eight sessions | Observed elapsed cost |
| --- | ---: | ---: | --- |
| Mixer passes | 1.002–1.006 | 1.000 | 2.42–2.61 / 4.78–4.86 µs per 10 ms |
| Source accumulation loops | 1.002–1.006 | 8.000 | 0.49 / 3.11–3.20 µs per 10 ms; included in mix |
| Output saturation/DMA stores | 1.002–1.006 | 1.000 | 0.31–0.33 µs per 10 ms; included in mix |
| Accepted queue-copy writes | 0.470–0.471 | 3.751 | 0.48–0.51 µs per write; 0.24 / 1.80–1.90 µs per 10 ms |
| Caller staging/setup, APs | 0.939–0.940 | 5.108–5.193 | 0.10–0.14 µs staging + 0.67–0.71 µs setup per attempt |
| HDA IRQ entries | 1.001–1.002 | 1.000 | 80–83 / 71–72 µs per 10 ms |
| HDA position observations | 4.711–4.726 | 7.362–7.426 | about 61–62 µs per observation |
| HDA MMIO reads | 25.795–25.810 | 39.801–40.288 | included in position/IRQ timings |
| Audio worker loops / sleeps | 2.71–2.72 / 2.70–2.72 | 5.36–5.43 / 5.03–5.07 | drive observations and wait/timer work |
| Audio notifications / waiter wakes | 1.93–1.94 / 1.91 | 6.08–6.17 / 5.02–5.07 | consequences appear in scheduler/clock rows |
| Readiness notifications / waiter wakes | 1.945 / 1.47–1.49 | 6.11–6.20 / 3.37–3.39 | 0.36–0.84 µs per notification; downstream scans separate |
| Network notifications / waiter wakes | 1.95 / 1.87–1.91 | 6.11–6.20 / 3.96–3.97 | 0.08–0.31 µs per notification; downstream service separate |
| BSP scheduler loops | 12.94–13.06 | 23.57–23.88 | each dispatch services deadlines and rearms the timer |
| BSP timer re-arms | 22.36–22.53 | 37.80–38.15 | about 40–41 µs each, including one clock read |
| BSP `arch_monotonic_ns` calls | 130.98–131.59 | 246.86–249.62 | 35.36–36.32 µs per IF=0 call |
| BSP physical HPET counter reads | 392.93–394.78 | 740.58–748.85 | three per clock call, plus rare retries |
| Codec/CORB 1 ms polling sleeps | 0 | 0 | activation/shutdown polling lies outside the phase |

Fixed output work is one 480-frame/1920-byte period and about one IRQ per 10 ms,
independent of session count. Mixing stays below **0.05% of elapsed time**, with no
per-sample calls or whole-queue copies; eight sources submit **1.536 MB/s PCM**.
The expensive growth is downstream: successful writes and period consumption notify
readiness, which also wakes networking, readiness checks every active request's
deadline, and every audio-worker iteration observes HDA position first. The
running-only 5 ms watchdog remains; steady playback has no codec polling.

## Clock callers

| Share of BSP clock calls | One session | Eight sessions |
| --- | ---: | ---: |
| Timer arm + timed-wait expiry + BSP sleeper checks | 40.31–40.51% | 34.57–34.65% |
| Broader timer/sleep/shared deadline helpers (includes the row above) | 52.17–52.35% | 45.61–45.62% |
| Readiness request deadline scans | 28.33–28.36% | 34.32–34.51% |
| Network service | 14.48–14.68% | 16.26–16.43% |
| Direct audio observation/IRQ/start | 4.35–4.36% | 3.38–3.39% |

This is the same wake/sleep → timer handling → HPET traffic pattern as the merged
[send investigation](../../network-throughput.md#qemu-results-2026-10-08), which found
about 27 physical counter reads per TCP segment. Audio's strict timer group accounts
for about **158–160 / 256–259 HPET reads per 10 ms** with one/eight sessions, the
broader group **205–207 / 338–342**, and readiness scans another **111–112 / 254–258**.
Unlike the send result, audio has a large readiness-list component that grows with
active waiters. Shared deadline helpers can serve several workers, so the broad group
is not exclusively audio. Native costs are unmeasured.

## Guest versus host cost

A control with the unchanged kernel and the same layout separated Linux `/proc`
`guest_time` from `utime` and `stime` over 2026-10-08 uninstrumented 20.02 s windows:

| BSP thread, % of one host CPU | One session | Eight sessions |
| --- | ---: | ---: |
| Linux-accounted guest execution | 24.57 | 42.36 |
| QEMU userspace, `utime - guest_time` | 12.39 | 21.18 |
| Host-kernel handling, `stime` | 20.73 | 35.72 |
| Total BSP thread | 57.68 | 99.26 |

So the earlier 99% BSP-thread figure includes substantial host exit/emulation work and
is not 99% Pyxis arithmetic; host userspace and kernel (KVM) are not solely HDA. The
main QEMU thread adds about 2.85%. One-session cost is still disproportionate and
remains a fix target, and native acceptability cannot be inferred from nested KVM.

The counter-enabled windows repeat the scaling: BSP guest **23.11–23.44% /
42.42–42.48%**, host-side **30.96–31.36% / 57.13%**, total **54.41–54.47% /
99.55–99.61%**; all QEMU threads **67.72–68.79% / 125.11–126.51%**; idle BSP 7.33%.
Software IP samples on the unchanged kernel (1529/2607 samples) put **26.42% / 25.43%
of whole-VM samples** at the BSP HPET load/retry instructions; a pilot 20 s KVM exit
trace counted 785,109 HPET accesses against 52,426 HDA accesses (HPET about 86% of
exits), though the trace adds substantial overhead. Guest-cycle PMU events were
unavailable and IP samples are statistical.

## Owner decisions

1. **Accepted and implemented: suppress successful-WRITE readiness notifications and
   notify consumption on a maximum-write writable threshold crossing.** A write
   reduces free capacity and cannot raise writable readiness. Dropping it alone
   predicts **0.470–0.471 / 3.751 fewer readiness and network notifications per
   10 ms**, about **24% / 61%** of those measured. Release, failure, generation and
   mixed audio/TCP-wait notifications are preserved.
2. **Accepted and implemented: amortize readiness deadline checks with one fresh, lazy
   HPET observation per service scan, skipping expiry work already superseded by
   readiness or caller stop.** This targets the **28–35% clock-call component**.
   Readiness still precedes timeout, and both workers recheck their earliest absolute
   deadline before parking, so an aged snapshot cannot add a blocking wait.
3. **Deferred: bound audio request batches and schedule routine position observations
   from IRQ/watchdog work**, keeping fresh before/after DMA-commit checks and all
   thresholds. It targets the growth from **4.7 to 7.4 position observations per
   period** (about half a millisecond at eight); any proposal needs owner agreement on
   its budget, fairness and cleanup handling, and new measurements.

Notification/scan work is implemented at `9026ac89`; batching is not. TSC/cheaper clock
sources, scheduler timer-rearm caching and network optimization belong to separate
kernel work. Capacity, queue/DMA tuning and safety-threshold changes need the owner.

## Repeat after the accepted fixes

The after probe is `7ff4de02` (the same instrumentation plus `9026ac89`), with the same
initrd, compiler, configuration, layout, devices, four CPUs and commands. Counter phases
were 32.583/20.194 s for one and 42.090/36.061 s for eight (3.187062 GHz); four quiet
15.000 s host windows; all producers stopped before GDB; no run ended with a controller
failure; each eight-session run had exactly eight source loops per mix.

| Counts per 10 ms | One before | One after | Eight before | Eight after |
| --- | ---: | ---: | ---: | ---: |
| Accepted PCM writes | 0.470–0.471 | 0.470–0.471 | 3.751 | 3.750 |
| Request attempts | 0.939–0.941 | 0.939–0.941 | 5.110–5.195 | 7.499 |
| Audio worker loops | 2.708–2.724 | 2.875–2.904 | 5.362–5.426 | 8.525–8.565 |
| HDA position observations | 4.711–4.726 | 4.878–4.905 | 7.362–7.426 | 10.525–10.565 |
| HDA MMIO reads | 25.795–25.810 | 26.742–26.871 | 39.801–40.288 | 55.878–56.003 |
| Readiness notifications | 1.945 | 0.942–0.944 | 6.111–6.197 | 4.752–4.753 |
| Readiness waiter wakes | 1.472–1.485 | 0.940–0.941 | 3.373–3.394 | 3.723–3.791 |
| Network waiter wakes | 1.873–1.906 | 0.939–0.940 | 3.958–3.972 | 3.642–3.758 |
| Audio waiter wakes | 1.906–1.912 | 1.915–1.920 | 5.020–5.066 | 8.283–8.300 |
| BSP scheduler loops | 12.941–13.063 | 12.298–12.341 | 23.566–23.878 | 32.174–32.257 |
| BSP timer re-arms | 22.359–22.535 | 20.746–20.758 | 37.800–38.153 | 50.387–50.517 |
| BSP clock calls | 130.976–131.592 | 84.854–85.090 | 246.861–249.617 | 217.118–219.090 |
| BSP physical HPET reads | 392.929–394.778 | 254.561–255.270 | 740.583–748.852 | 651.356–657.269 |
| Readiness deadline clock calls | 37.109–37.322 | 4.271–4.272 | 84.734–86.153 | 16.018–16.336 |

| Host CPU, % of one CPU | One before | One after | Eight before | Eight after |
| --- | ---: | ---: | ---: | ---: |
| BSP guest execution | 23.11–23.44 | 17.93–19.80 | 42.42–42.48 | 41.73–43.40 |
| BSP QEMU userspace | 11.12–11.19 | 8.33–8.87 | 21.17–21.51 | 19.00–19.73 |
| BSP host kernel | 19.78–20.24 | 14.20–16.07 | 35.62–35.96 | 33.20–33.27 |
| BSP total | 54.41–54.47 | 40.47–44.73 | 99.55–99.61 | 93.93–96.40 |
| All QEMU threads total | 67.72–68.79 | 57.40–63.33 | 125.11–126.51 | 136.26–144.20 |

Fixed output is unchanged (one mix, one output loop, about one IRQ per 10 ms); after-mix
elapsed is 1.98–2.66 / 5.40–6.22 µs per 10 ms and queue copies stay below 2.1 µs with
eight sources. Readiness scan clock calls fall about 89% / 81%, and their share of BSP
clock calls from 28% / 34% to 5% / 7%. Timer arm/expiry/sleeper checks now account for
61% / 54% of BSP clock calls, the broader timer group for 76% / 69–70%.

One-source BSP cost falls visibly; eight-source BSP cost falls only modestly while
whole-VM CPU rises. The change increases retry/wait pacing: nonaccepted requests rise
from 1.36–1.44 to 3.749 per 10 ms with eight sources, because `pcm` submits WAIT_MANY
after a blocked WRITE and that submission itself notifies readiness and networking,
offsetting much of the direct saving (inferred from source and aggregate counts;
crossings and individual WOULD_BLOCK outcomes were not counted). Every forwarded audio
request wakes the worker, so more retries mean more worker loops, HDA observations,
dispatches and timer arms. Eight-session IF=0 clock elapsed rises from 4.29–4.32 to
5.81–5.92 ms per 10 ms while per-call cost stays about 35–36 µs. An unrelated pointer
QEMU/debugger was present; the one-2 helper thread exited between host snapshots, so
that sample's whole-VM figure omits its unobserved runtime. These are nested-QEMU
measurements on a shared host, not a native comparison or a precise speedup.

## Current-main uninstrumented qualification

The ordinary build used `c61fe9d7`, main `52451d3a`, userland `50bf2be`, the same
compiler and unchanged filesystem/ports/lwIP pins, with a local nine-shell
`config/live.lua` restored after assembly. Same four-CPU devices and `pcm 1000 500 600`.

The planned five-minute eight-session check **failed closed**. The WAV holds 112.227 s of
output from the first RUN (the exact failure time was not timestamped). Frozen reason:
`output DMA commit exceeded clock limit`. The maximum HPET-measured commit window was
**922,780 ns**, below 1 ms, so the rejecting condition was the codec WALCLK delta
reaching its 1 ms bound, which includes the post-copy position observation after the
recorded HPET endpoint; the exact delta was not retained. No guard was relaxed. The
early quiet 15.000 s window cost BSP guest 42.86%, QEMU userspace 19.87%, host kernel
33.93%, total 96.66% (a separate observation, not the matched comparison).

Ctrl-C/exit cleanup left all eight slots empty. The controller was stopped, failed and
shut down (IRQ masking, stream reset, CORB/RIRB stop, link reset and BME-off all
succeeded, DMA backing retained); the shell stayed serviceable and a new producer
received UNAVAILABLE until reboot. No debugger, screenshot or task-owned build ran
before the failure. A live serial filter missed the `hda:` failure wording, so results
rest on the capture and frozen state, not on a matching live line.

**Sustained eight-session playback remains unqualified in nested QEMU.** The owner
accepted task 2 delivery with this limitation on 2026-10-09; see
[technical debt](../../../technical-debt.md#hd-audio-sustained-eight-session-playback).
Batching stays deferred and milestone closure requires native eight-session playback.
The likely trigger was a transient nested-host scheduling/VM-exit delay rather than
mixer throughput, but no coincident host trace proves it. AMD/ALC257 remains unbound at
this record; the branch could give a ThinkPad availability check, not native listening
evidence.

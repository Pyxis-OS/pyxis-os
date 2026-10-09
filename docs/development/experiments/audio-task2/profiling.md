# Audio BSP cost: measurement follow-up

Measured **2026-10-09**, following the [early review of #557](https://git.internal/PyxisOS/pyxis-os/pulls/557).
**Candidates 1 and 2 accepted 2026-10-09 and implemented; #557 stays draft
for owner review.** Candidate 3 (request batching) remains deferred until the
owner reviews the repeat measurements. Eight sessions, eight 10 ms
DMA periods, 80 ms session queues, 4096-byte writes and the existing safety
thresholds remain unchanged. Earlier eight-session failures remain a delivery
issue to resolve, rather than an accepted capacity limitation.

## Matched inputs and method

Implementation under review: `62a63e4bd972229858490b354a4fcfd39c355539`, userland
`052ac5ea0c1b045924aee439470f66e7f3382c1c`. The unmerged local measurement branch
`probe/audio-task2-cost`, `cc6af6be548cb5887f4ec33029122e3099e7a8f9`, adds only
temporary counters. The instrumentation stays on local probe branches; it changes no accepted tuning. Its ordinary image build
passed using LLVM 23.1.3/49e2c1a and verified SDK/userspace/ports inputs.

Both repeats use the same ELF/ISO/initrd: QEMU 10.2.2, Q35, **nested KVM**,
`-cpu max`, four CPUs, 8 GiB, OVMF, VGA 1280×800, VirtIO SCSI/RNG/network,
`intel-hda` + `hda-output`, WAV 48 kHz S16 stereo. The ordinary
nine-shell layout is the sole userland policy
change; it launches no audio. No ThinkPad, SDL2 or other port was changed.

| Artifact | SHA-256 |
| --- | --- |
| Probe ELF | `008a7487d7829b7b7b89a6d8f015f3e340eb114a76c853dfcb554186cf63eca3` |
| Probe ISO | `1a52a8384b1e85ce3fd5d5e5d34e22a55c58413d90572a998a99b246654eb33f` |
| Shared initrd | `9b05e83cb852eb0c8ce2653408d4123217e58fe918032f41423824a074125690` |

Manually enter `pcm 1000 500 600`: Remote alone for one session; Remote,
Development and Audio 1–6 for eight. Local commands use ordinary HMP `sendkey`.
One-session physical selection stays Caelum; eight-session selection ends at
Audio 6. Both are static console scenes, with the same framebuffer size and
background spaces. This is a workload comparison, not an isolated presenter test.
Stop with ordinary Ctrl+C. Counters start at the first mix with the target number
of acquired sessions and freeze when that count falls or the engine fails.
Eight source-loop samples per mix confirm simultaneous participation.

Serialized RDTSCP intervals use the existing HPET timestamps for calibration
(about 3.187 GHz). TSC is measurement only; it does not provide kernel time.
Read-only GDB retrieves the frozen counters. Clock timing includes only calls
entered with interrupts disabled; IF-enabled calls can include preemption and
would double-count elapsed work. Timings include probe overhead, VM exits/device
handling and host descheduling, and are **not exclusive guest CPU time**. Nested
rows overlap: timer rearming includes its clock read, and mixing includes its
source loops, output and notification. Do not add the rows.

Registered-waiter wake counts mean a notification detached a waiter and called
`task_wait_wake`; a timeout might already have made it runnable. They are not
unique scheduler transitions. In eight-1, GDB attached after the counters froze
but before the other seven producers stopped; that later engine inspection is
excluded. Eight-2 stopped all producers before GDB and ended with no failed
controller. No debugger, sampler, exit tracing, other owned guest or build ran
during the four approximately 15.018 s host-accounting windows. These short
profiles do not establish long-duration qualification.

Raw counters, accounting, calibration and caller resolution are retained locally
under the probe worktree's `build/`; repository documentation retains these
summaries. The submitted evidence was also archived locally before trimming.
Frozen counter intervals were 41.702/16.132 s for one, 15.970/20.570 s for eight;
the idle counter interval was 26.856 s. Counts below normalize each interval to
**10 ms elapsed**, rather than assuming an IRQ is a period.

## Per-period work

Ranges span the two repeats. Clock counts are for the BSP; staging/setup runs
on APs. The idle scene makes 13.80 clock calls / 41.41 HPET reads per 10 ms.

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
| Audio notifications / waiter wakes | 1.93–1.94 / 1.91 | 6.08–6.17 / 5.02–5.07 | wake consequences appear in scheduler/clock rows |
| Readiness notifications / waiter wakes | 1.945 / 1.47–1.49 | 6.11–6.20 / 3.37–3.39 | 0.36–0.84 µs per notification; downstream scans separate |
| Network notifications / waiter wakes | 1.95 / 1.87–1.91 | 6.11–6.20 / 3.96–3.97 | 0.08–0.31 µs per notification; downstream service separate |
| BSP scheduler loops | 12.94–13.06 | 23.57–23.88 | each dispatch services deadlines and rearms the timer |
| BSP timer re-arms | 22.36–22.53 | 37.80–38.15 | about 40–41 µs each, including one clock read |
| BSP `arch_monotonic_ns` calls | 130.98–131.59 | 246.86–249.62 | 35.36–36.32 µs per IF=0 call |
| BSP physical HPET counter reads | 392.93–394.78 | 740.58–748.85 | three per clock call, plus rare retries/in-flight edges |
| Codec/CORB 1 ms polling sleeps | 0 | 0 | activation/shutdown polling lies outside the phase |

Fixed output work is one 480-frame/1920-byte period and about one IRQ per 10 ms,
independent of session count. Input accumulation/copies grow with the sources;
all mixing stays below **0.05% of elapsed time**. There are no per-sample calls
or whole-session-queue copies per period. Eight sources submit **1.536 MB/s PCM**;
multiple memory passes increase traffic, but their measured arithmetic/copy cost
is small.

The expensive growth is downstream: successful writes and period consumption
notify readiness, which also wakes networking. Readiness checks every active
request's deadline; the worker services all interests, not just audio. Every
audio-worker iteration also observes HDA position before servicing one request.
The running-only 5 ms watchdog remains; steady playback has no codec polling.

## Clock callers and the network comparison

| Share of BSP clock calls | One session | Eight sessions |
| --- | ---: | ---: |
| Timer arm + timed-wait expiry + BSP sleeper checks | 40.31–40.51% | 34.57–34.65% |
| Broader timer/sleep/shared deadline helpers, including the row above | 52.17–52.35% | 45.61–45.62% |
| Readiness request deadline scans | 28.33–28.36% | 34.32–34.51% |
| Network service | 14.48–14.68% | 16.26–16.43% |
| Direct audio observation/IRQ/start | 4.35–4.36% | 3.38–3.39% |

This is the **same wake/sleep → timer handling → HPET traffic pattern** as
[#559's send measurements](https://git.internal/PyxisOS/pyxis-os/src/branch/net/throughput-baseline/docs/wip/network-throughput.md#send),
which found about 27 physical counter reads per TCP segment. That work is now
merged; its [send investigation](../../../wip/network-throughput.md#send)
retains the measurements. Audio's strict
timer group accounts for about **158–160 / 256–259 HPET reads per 10 ms** with
one/eight sessions; the broader group is **205–207 / 338–342 reads**. Readiness
scans add another **111–112 / 254–258 reads**. Unlike the send result, audio has
a large additional readiness-list component that grows with active waiters.
Shared deadline helper PCs can serve several workers; the broad group does not
attribute every helper call exclusively to audio. Native costs are unmeasured.

## Guest versus host cost

The unchanged-kernel control uses the same four-CPU/device/space layout and
initrd. Its ELF is `01f3455065a0f5f354187abdd0fee06a99ba7125c79bbef1b92dd8d0d3b2a365`,
ISO `40cef99c8eea97282e5b19f50595025a8c80fbf17462c719a8a549ae7deb5fed`.
The 2026-10-08 uninstrumented 20.02 s windows separate Linux `/proc` field 43
`guest_time` from `utime` (which includes it) and `stime`:

| BSP thread, % of one host CPU | One session | Eight sessions |
| --- | ---: | ---: |
| Linux-accounted guest execution | 24.57 | 42.36 |
| QEMU userspace, `utime - guest_time` | 12.39 | 21.18 |
| Host-kernel handling, `stime` | 20.73 | 35.72 |
| Total BSP thread | 57.68 | 99.26 |

Thus the prior 99% BSP-thread figure includes substantial host exit/emulation
work. It is not 99% Pyxis arithmetic. Host userspace includes all device/dispatch
handling on that thread, and host kernel includes KVM; neither is solely HDA.
The main QEMU thread costs another approximately 2.85% total, likewise not an
isolated audio-backend measurement. One-session cost is still disproportionate
and remains a fix target; native acceptability cannot be inferred from nested KVM.

The new approximately 15.018 s counter-enabled windows repeat the scaling:
BSP guest **23.11–23.44% / 42.42–42.48%**, host-side **30.96–31.36% / 57.13%**,
total **54.41–54.47% / 99.55–99.61%**. All QEMU threads cost **67.72–68.79% /
125.11–126.51%**. Idle BSP is 7.33%. Similar scale to controls does not establish
zero probe overhead or a speedup across separate host windows.

Separate unchanged-kernel software samples (1529/2607 samples) put **26.42% /
25.43% of whole-VM samples** at the BSP HPET load/retry instructions. Host
`native_write_msr_safe` and `vmx_flush_pml_buffer` are major hotspots. A pilot
20 s KVM exit trace counted 785,109 HPET accesses versus 52,426 HDA accesses:
HPET explains about 86% of all exits. That trace itself adds substantial
overhead; it is corroborating exit attribution, not a quiet timing window.
Hardware guest-cycle PMU events were unavailable. Software IP samples are
statistical; they do not isolate each function's CPU cost. Preserve that limit
when interpreting the Linux accounting categories and overlapping TSC intervals.

## Owner decisions and implementation

1. **Accepted 2026-10-09 and implemented: suppress successful-WRITE readiness notifications and
   notify consumption on a maximum-write writable threshold crossing.** A write
   reduces free capacity, so it cannot raise writable readiness. Dropping that
   notification alone predicts **0.470–0.471 / 3.751 fewer readiness and network
   notifications per 10 ms**, about **24% / 61%** of the measured notifications.
   Threshold gating may save more; crossings were not counted. Preserve release,
   failure, generation and mixed audio/TCP-wait notifications. Notification
   savings do not imply an equal reduction in actual dispatches or CPU time.
2. **Accepted 2026-10-09 and implemented: amortize readiness deadline checks with one fresh, lazy HPET observation per
   service scan; skip expiry work already superseded by readiness or caller stop.**
   This targets the **28–35% clock-call component** above. Scan invocations and
   ready/blocked partitions were not counted, so precise savings remain unknown.
   Preserve readiness-before-timeout precedence and absolute-deadline parking;
   a snapshot ages during one scan and must not introduce an extra blocking wait.
3. **Deferred pending repeat results and owner decision: bound audio request batches and schedule routine progress observations from
   IRQ/watchdog work.** Keep fresh before/after DMA-commit checks and all existing
   service/commit thresholds. This targets the growth from **4.7 to 7.4 position
   observations per period**, approximately half a millisecond of observed
   elapsed work at eight. Savings depend on batching and watchdog cadence;
   bounding work must preserve request fairness, cleanup and error handling.

Notification/scan work is implemented at `9026ac89`; batching is not implemented.
The writable threshold, release/failure/exit notifications and mixed audio/TCP
notification routing remain intact. Each readiness scan samples only after a
blocked request needs expiry checking. Current readiness still precedes timeout;
both workers recheck their earliest absolute deadline before parking, so an aged
scan snapshot cannot cause an additional blocking wait.
**TSC/cheaper clock sources, general scheduler timer
rearm caching and broad network optimization belong to separate kernel work.**
Capacity, queue/DMA tuning and safety-threshold changes require the owner and are
not proposed as an escape from this overhead. The repeated matched runs below measure the accepted fixes. Longer uninstrumented
qualification is separate; #557 remains draft pending owner review.


## Repeat after accepted fixes (2026-10-09)

The isolated after probe is `7ff4de02`, the same `cc6af6be` instrumentation plus
implementation `9026ac89`. It retains the identical initrd hash above, compiler,
configuration, nine-shell layout, QEMU devices, four CPUs and manual commands.
The after ELF SHA-256 is
`8289db1363a0af1c1d11b83dd9c03782fc8af1f7e66a014d37aa012680ad76bd`;
ISO `f3ee4810f209b0cf6ab73d7a7fc5a16a19cd08006e908b85cff90692f6fda3a9`.
Only the two accepted runtime changes differ. Counter phases are 32.583/20.194 s
for one and 42.090/36.061 s for eight, calibrated at 3.187062 GHz. Four independent
quiet host-accounting windows last 15.000 s each. All producers stopped before
GDB inspection; all four runs ended with no controller failure, and each eight
run had exactly eight source loops per mix.

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

Fixed output remains one mix, one output loop and approximately one IRQ per
10 ms; source loops remain one/eight. After mix elapsed is 1.98–2.66 / 5.40–6.22 µs
per 10 ms. Queue copies remain below 2.1 µs per 10 ms with eight sources.
Readiness scan clock calls fall about 89% / 81%; their BSP clock share falls from
28% / 34% to 5% / 7%. Timer arm/expiry/sleeper checks now account for 61% / 54%
of BSP clock calls, with broader timer/shared helpers 76% / 69–70%. The earlier
network wake/sleep/HPET comparison still applies; cheaper clocks remain separate.

| Host CPU, % of one CPU | One before | One after | Eight before | Eight after |
| --- | ---: | ---: | ---: | ---: |
| BSP guest execution | 23.11–23.44 | 17.93–19.80 | 42.42–42.48 | 41.73–43.40 |
| BSP QEMU userspace | 11.12–11.19 | 8.33–8.87 | 21.17–21.51 | 19.00–19.73 |
| BSP host kernel | 19.78–20.24 | 14.20–16.07 | 35.62–35.96 | 33.20–33.27 |
| BSP total | 54.41–54.47 | 40.47–44.73 | 99.55–99.61 | 93.93–96.40 |
| All QEMU threads total | 67.72–68.79 | 57.40–63.33 | 125.11–126.51 | 136.26–144.20 |

One-source BSP cost falls visibly. Eight-source BSP cost falls only modestly,
while whole-VM CPU rises. The change increases retry/wait pacing: nonaccepted
requests rise from 1.36–1.44 to 3.749 per 10 ms with eight sources. `pcm` submits
WAIT_MANY after a blocked WRITE; submitting that wait itself notifies readiness
and networking. These extra submissions offset much of the direct WRITE-notify
saving. Staggered threshold crossings can also keep the mix notifying each pass.
That attribution is inferred from source and aggregate counts; crossings and
individual WOULD_BLOCK outcomes were not counted separately.

Every forwarded audio request wakes its worker. More retries therefore cause
more worker loops, HDA observations, scheduler dispatches and timer arms. Eight
IF=0 clock elapsed rises from 4.29–4.32 to 5.81–5.92 ms per 10 ms, while its
per-call elapsed cost stays about 35–36 µs. Removed readiness reads were IF=1,
whose elapsed timing is excluded. The nested timings remain non-additive.

**Candidate 3 remains deferred:** a bounded pending-request batch could reduce
request-driven observations and sleep/rearm churn. Any proposal must retain fresh
before/after DMA-commit checks, all guards, cleanup/generation handling and request
fairness. Its count/time budget and actual benefit need owner agreement and new
measurements; no batching has been implemented.

An unrelated pointer QEMU/debugger was present during these repeats. No task-owned
build, debugger or sampler ran during the quiet windows. The one-2 helper thread
exited between host snapshots; BSP accounting is unaffected, while that sample's
whole-VM figure omits its unobserved runtime. These are nested-QEMU measurements
on a shared host, not a controlled native comparison or a precise uninstrumented
speedup. Raw snapshots, counters and derived analysis remain in local `build/`.


## Current-main uninstrumented qualification

The ordinary source build uses `c61fe9d7`, main `52451d3a`, published userland
`50bf2be`, the same compiler and unchanged filesystem/ports/lwIP pins. The
source-built nine-shell qualification image changes only local `config/live.lua`,
restored after assembly; production configuration is unchanged. Its hashes are:

| Artifact | SHA-256 |
| --- | --- |
| ELF, no probes | `969d503a19e63459b7998b4f3ee6d04f9aedb931b46025c16c2bcf0f66e44a0c` |
| ISO | `e2ca494758f8dd91e0eeccea86fb3a286982c9ea396f0c0eb5de6c22b0f5e6d0` |
| Initrd | `d32aaaf860fa3b03f1278e2e977af59deaf6b75fc0ec8addbbe66bc6dc495486` |

Same four-CPU/device configuration and manual `pcm 1000 500 600` commands. The
planned five-minute eight-session check **failed closed**. WAV contains 112.227 s
of output from the first RUN; the exact eight-only failure time was not timestamped.
Frozen reason: `output DMA commit exceeded clock limit`. Maximum HPET-measured
commit window is **922,780 ns**, below 1 ms, so the rejecting condition was the
codec WALCLK delta reaching its 1 ms bound. That check includes the post-copy
position observation, which lies after the recorded HPET endpoint; the exact
rejecting WALCLK delta was not retained. No guard was relaxed. The early quiet
15.000 s window cost BSP guest 42.86%, QEMU userspace 19.87%, host kernel 33.93%,
total 96.66%; it is a separate current-main observation, not the matched probe
comparison above.

Later Ctrl-C/exit cleanup left all eight slots empty. The controller is stopped,
failed and shut down; IRQ masking, stream reset, CORB/RIRB stop, link reset and
BME-off all succeeded, with DMA backing retained. The shell remained serviceable;
a new producer received UNAVAILABLE as required until reboot. No debugger,
screenshot or task-owned build ran before the failure. An overly narrow live
serial filter missed the `hda:` failure wording; capture, complete logs and frozen
state determine the result, not the absence of a matching live line.

**Sustained eight-session playback remains unqualified.** The owner reviews these
numbers before deciding on batching or other separately scoped work. AMD/ALC257
remains unbound; the current branch can provide a ThinkPad availability check,
not native listening evidence. PR instructions distinguish that gate from the
later one/eight listening checks. All task-owned guests and debugger jobs stopped.

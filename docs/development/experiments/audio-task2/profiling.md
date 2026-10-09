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
not proposed as an escape from this overhead. After an accepted fix, repeat
matched one/eight runs and sustained eight-session qualification before delivery.
All task-owned guests, clients, debuggers and profilers are stopped.

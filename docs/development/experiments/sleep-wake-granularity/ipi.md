# Deadline expiry reschedule IPI

This is the separately measured first implementation step from
[proposal #532](https://git.internal/PyxisOS/pyxis-os/pulls/532), whose three
defaults the owner accepted on 2026-10-08. It fixes AP notification without
changing deadline ownership, timer programming, ABI or ordinary resource wakeups.
The one-shot step follows separately.

## Publication

BSP expiry removes each due timed record and publishes its wake under the existing
queue lock. For a parked waiter it records only the destination CPU index; after
unlocking it sends one existing reschedule IPI per affected remote CPU. No task
or wait pointer survives publication/unlock. A not-yet-parked waiter receives
notification only, preserving the saved-stack handshake. No self-IPI is sent.
The temporary destination array uses the existing boot CPU count and stack
storage, adding no timer quota or heap allocation.

## Matched measurements

The source base was `92ae8783b520a007826633fe418b9bf51e97e5cf`, whose kernel
source matches the original `9acf597` baseline, plus the 12-line change recorded
in `a82fca6`. The measured ELF was built before committing that change and
embeds `92ae8783b520`; no claim is made that its revision string identifies the
later submitted documentation head. The existing builder, effective Kconfig,
guest devices/order, firmware, four CPUs/8 GiB, standard VGA 1280x800 and nested
KVM match the original baseline. Only `boot/caelum.elf` changed in the copied
image tree; all workload/library/initrd bytes were held constant.

| Artifact | SHA-256 |
| --- | --- |
| Measured kernel ELF | `b82f3618b054ed47aff4fe3e39d3d871f0375b2643f4e894213c6e426d491926` |
| Measured ISO | `a200eaec93d6b77acfbab9b3bcb3eef894a6a8ef1abf8fc150965da0762681ba` |
| Unchanged measurement initrd | `7de899d4f50579a605d815830f4a970bed6a8f4f02d14837ad6cc0321b95de8c` |

Five individually entered, unprofiled `sleep-baseline > sN` runs in Development
used the same real SDL workload: ten warm-up frames and 300 measured
render/present/SDL_Delay(16) frames. Remote read each result afterward. Every
run reported zero sleeps below 16 ms.

| Run | Mean work ns | Mean frame ns | Mean sleep ns | Minimum sleep ns | Maximum sleep ns |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 944024 | 24300948 | 23356924 | 16178880 | 24226440 |
| 2 | 931481 | 24239490 | 23308009 | 16178700 | 24248440 |
| 3 | 1038164 | 24067987 | 23029822 | 16102030 | 24203860 |
| 4 | 1399695 | 24243215 | 22843520 | 16430050 | 24222740 |
| 5 | 954514 | 24325201 | 23370686 | 16197950 | 24276990 |

Median per-run mean frame time was **24.243215 ms**, range
**24.067987–24.325201 ms**, versus baseline median 25.565202 ms. Mean sleep
median was 23.308009 ms; the largest observed sleep was 24.276990 ms,
versus baseline 32.356390 ms. Render/host variation remains; this is no maximum
latency guarantee. The IPI alone does not remove BSP tick quantization.

After SDL completed, the same packaged `quake +map e1m1` ran with no input,
`cls.timedemo=false`, and its ordinary 72 Hz cap. The matching debugger ELF
retained the original verified P1F entry/loadable-byte identity. Manual
`Host_Frame` boundary samples used `host_framecount` and the 10 ns HPET counter;
the hardware breakpoint was disabled between boundaries. No sampling loop,
inferior function call or guest state substitution occurred. The numerical
readings are in the table below.

| Window | Completed frames | HPET ticks, 10 ns | Frames/s |
| --- | ---: | ---: | ---: |
| 1 | 1104 | 1852679356 | 59.589372 |
| 2 | 917 | 1538732025 | 59.594522 |
| 3 | 1197 | 2008204928 | 59.605471 |
| 4 | 907 | 1522197366 | 59.584915 |
| 5 | 896 | 1503382667 | 59.598931 |

Median capped rate was **59.594522 FPS**, range **59.584915–59.605471 FPS**,
versus baseline median 49.222714 FPS. These debugger-assisted windows include
scheduling, display, host and profiling effects; they are neither native
performance nor isolated CPU cost. No other task-owned VM or build ran during
sampling. Ordinary kernel build and manual boot passed. Queue/parking ownership
was source-reviewed. All measurement guest/debugger/client processes stopped.

# Cheaper timekeeping

Status: **proposal, 2026-10-09,** assigned to Claude. Nothing here is accepted
yet; the [decisions](#owner-decisions) below need the owner. No code exists for
it.

## Why

Every monotonic clock read is an HPET counter read. In QEMU that is an MMIO
exit to the device model, and the kernel reads the clock several times for
each wake and sleep:

- **Network.** About 27 HPET counter reads per TCP data segment in QEMU,
  nearly all from timer handling in the network worker's wake and sleep cycle
  ([network throughput](../development/network-throughput.md#qemu-results-2026-10-08)).
- **Audio.** #557's [profiling](https://git.internal/PyxisOS/pyxis-os/pulls/557)
  counted about 394 HPET reads per 10 ms on the BSP with one playback session and
  745 with eight. Timer re-arm, expiry and sleeper checks made 40% and 35% of
  the clock calls; readiness scans made another 28% and 34%. With interrupts
  off, one clock call took about 36 µs of elapsed time in nested QEMU and a
  timer re-arm about 40 µs, including its clock read. #557's accepted fix takes
  one clock reading per readiness scan, after which timer re-arm, expiry and
  sleeper checks are 61% and 54% of the remaining clock calls.

Natively the cost is smaller but unmeasured: the ThinkPad reads a 32-bit HPET
in the chipset, extended in software. The
[accepted direction](../technical-debt.md#wall-clock-time-and-clock-source-performance)
is TSC with extended-HPET fallback, with frequency discovery and cross-CPU
qualification, keeping the clock protocol. This proposal makes that concrete
and adds a second, independent change: fewer clock reads in the timer path.

## Current behaviour

- **Clock.** `arch_monotonic_ns` reads the HPET: three MMIO reads on QEMU's
  64-bit counter (high, low, high), or one read plus a shared compare-and-swap
  on the ThinkPad's software-extended 32-bit counter
  ([timekeeping](../kernel/timekeeping.md)).
- **Timer interrupt.** `arch_timer_interrupt` reads the clock to advance the
  120 Hz preemption occasion. `task_timer_interrupt` then reads it again in
  `expire_timed_waits` when the CPU has timed waits, again in `wake_sleepers`
  on the BSP when kernel tasks sleep, and once more in `arch_timer_arm`. That
  is up to four clock reads per interrupt.
- **Timed waits.** Every timed `sleep_wait` re-arms the local timer, and each
  re-arm reads the clock to turn the absolute deadline into a LAPIC countdown,
  even when the new deadline is later than the one already armed.
- **Timer hardware.** Each LAPIC runs one-shot countdowns calibrated against
  PIT channel 2. TSC-deadline mode is not used.

## Proposed work

Two tasks, each measured on its own.

### Task A: fewer clock reads per timer event

This is independent of the clock source and also helps the HPET fallback.

1. **One reading per timer interrupt.** `arch_timer_interrupt` reads the clock
   once and passes that reading on: to expiry, BSP sleeper wakeup and the
   re-arm. Expiry against a reading taken a few microseconds earlier can only
   leave a just-due wait for the next interrupt, which the re-arm then
   schedules at the minimum count. No deadline is ever treated as passed early.
2. **Re-arm only for an earlier target.** Each CPU remembers the absolute time
   its armed countdown targets, cleared when the timer interrupt runs. A
   re-arm whose new target is not earlier than the armed one writes nothing
   and reads no clock. A later deadline leaves the earlier interrupt armed;
   that interrupt finds nothing due and re-arms, which is already the accepted
   behaviour when a minimum is removed.

Kernel workers that call `task_deadline_expired` repeatedly in one pass, such
as ARP and IPv4, are left alone here. With a TSC each call is cheap; with the
HPET fallback, taking one reading per pass in those workers is subsystem
follow-up work, as #557 did for readiness.

### Task B: TSC as the clock source, extended HPET as fallback

**Detection, on every CPU:**
- TSC present (`CPUID.1:EDX[4]`) and invariant (`CPUID.80000007:EDX[8]`). Check
  the maximum extended leaf first.
- `RDTSCP` (`CPUID.80000001:EDX[27]`) for the ordered read below.
- Under a hypervisor (`CPUID.1:ECX[31]`) the same bits decide; see
  [decision 3](#owner-decisions). kvmclock is not used.
- **TSC-deadline mode is not used.** The ThinkPad's Zen 2 CPU lacks it (no
  `tsc_deadline_timer` in the [inventory](../targets/t14-gen1-amd/thinkpad-inventory-docked.txt)),
  while QEMU offers it. Using it only where present would make QEMU and native
  take different timer paths. The LAPIC countdown stays; with a TSC clock its
  "now" is cheap. Revisit with an Intel target.

**Ordered reads.** `RDTSC` alone can execute out of order. The read is
`RDTSCP` followed by `LFENCE`, which needs `LFENCE` to be dispatch-serializing:
- on Intel it always is;
- on AMD it is when `CPUID.80000021:EAX[2]` says so, or after setting
  `DE_CFG` (MSR `C001_1029`) bit 1 on each CPU, as Linux does on Zen 2. A
  native Caelum boot must do this itself and not assume Linux's MSR state.
  Under KVM, the implementation checks whether the guest can set or read that
  bit;
- where it can't be established, the read falls back to `CPUID; RDTSC`. Under
  KVM, `CPUID` exits to the hypervisor's kernel, not to QEMU, which is still
  far cheaper than an HPET MMIO exit.

**Frequency.**
- **CPUID `0x15`** gives an exact ratio to the crystal when its denominator,
  numerator and crystal frequency are all nonzero. Recent Intel CPUs report
  it. AMD CPUs are not expected to, the ThinkPad's included; the
  implementation logs what it finds.
- **CPUID `0x16`** reports nominal base MHz. It is not a measurement and is
  never used.
- **Proposal: calibrate against the HPET everywhere** in this task, and log the
  `0x15` result where present for comparison only. Using `0x15` as the source
  on Intel targets can follow once one exists.
- **Method:** during BSP clock initialization, before APs start, with
  interrupts off:
  - bracket an HPET read between two TSC reads at the start and the end of a
    100 ms interval;
  - repeat each bracket a few times and keep the narrowest;
  - reject the calibration if no bracket is narrow enough or the HPET's
    progress disagrees with the TSC by more than a sanity bound.
- **Accuracy.** The error bound is the two bracket widths plus one HPET period
  (70 ns natively), divided by the interval. With brackets of 1 µs it is about
  20 ppm, or 1.7 s a day; narrower brackets give less.
  - The HPET's own crystal error passes through unchanged, as it does today.
  - The klog line reports the bound actually achieved.
  - A native check: Linux's refined calibration on the ThinkPad was
    2,096.061 MHz.
- **Cost:** 100 ms is added to boot.

**Conversion.** One shared, immutable record holds:
- the TSC value at the switch;
- the nanosecond reading at the switch;
- a multiplier, `(10^9 << 32) / hz`, computed once in 64-bit arithmetic.

A reading is `base_ns + ((tsc - tsc_base) * mult >> 32)`, with a 128-bit
product and no 128-bit division helper. It saturates at `UINT64_MAX` like
today's conversion.

**Cross-CPU agreement.** Invariance promises a constant rate, not equal values
across CPUs.
- **Warp check.** During the existing serialized AP startup, each AP runs a
  bounded warp check with the BSP, about 2 ms per AP. Each side repeatedly
  takes a shared lock, reads its TSC, and fails the check if the value is below
  the last one either CPU stored. Linux uses the same check. On the ThinkPad's
  12 CPUs this adds about 25 ms to boot.
- **Limits.** A finite check cannot prove agreement for the rest of the boot.
  With no shared nondecreasing floor, a later warp would go unnoticed. A floor
  would put one contended cache line on every read, which
  [decision 2](#owner-decisions) weighs.

**Switching and fallback.**
- **One switch.** Boot keeps running on the extended HPET exactly as today,
  through calibration and AP startup. After the last AP passes, and before the
  scheduler starts, the BSP switches once:
  1. it reads HPET nanoseconds and the TSC as one bracketed pair;
  2. it publishes the conversion record;
  3. it release-stores the source selection, which readers acquire.

  The epoch does not change, so the reading never steps backwards across the
  switch.
- **Fallback.** Any failure leaves every CPU on the extended HPET:
  - an invariant TSC missing on any CPU;
  - ordered reads that can't be established;
  - a calibration outside its bounds;
  - any AP warp.

  No CPU runs on a different source from the others.
- **The HPET stays initialized** either way. It is the calibration reference
  and the fallback, and its existing software-extension maintenance keeps
  running so the fallback stays valid.

**Reporting.** The existing HPET initialization lines stay, because the HPET is
initialized either way. The decision adds one line, for example:

```text
clock: TSC selected, 2096.061 MHz calibrated against HPET (±6 ppm), 12 CPUs agree, RDTSCP+LFENCE
clock: HPET kept: CPU 7 TSC behind BSP during startup check
```

## Measurement

Measure three revisions in the same configurations: main before task A, after
task A, and after task B. Temporary counters may be used locally, as #557 did,
and are not committed. No new benchmark tools are needed.

**QEMU** (nested KVM, 4 CPUs, 8 GiB, VirtIO net, QEMU 10.2.2):
- `ttcp -t -n 2048 -l 8192 10.0.2.2` and `ttcp -r -p 5002 10.0.2.2`, three
  runs each, as in [network throughput](../development/network-throughput.md#qemu-results-2026-10-08);
- #557's `pcm 1000 500 600` with one and with eight sessions, using its
  layout and commands, once #557 is merged;
- the clock-call loop that `iobench` and `allocbench` already print, in ns per
  read.

For each run, record:
- throughput;
- HPET MMIO exits from `perf kvm stat` (for the network runs, exits per data
  segment);
- the BSP thread's host CPU split into guest, QEMU userspace and host kernel
  time from `/proc`, as #557 recorded.

Task B can only select the TSC in QEMU if the guest sees an invariant TSC;
see decision 3.

**Native ThinkPad, run by the owner** (wired, on AC, PXE builds):
- the clock klog lines;
- `iobench` clock-call loop, giving the native cost per read;
- the `ttcp` send and `ttcp -r` runs from
  [repeating the measurement](../development/network-throughput.md#repeating-the-measurement),
  three of each, with the capture optional;
- for task B, the calibrated frequency against Linux's 2,096.061 MHz;
- a 15-minute `date -u` check against a stopwatch, as was done for the
  extended HPET. Native audio is not bound yet, so the audio workload stays in
  QEMU.

Results go in this document and then the timekeeping reference. Nested and
native results are kept apart.

## Owner decisions

1. **Order: task A first, then task B.** Default: yes, as separate PRs, each
   measured. Task A is small, independent of the clock source, and also
   benefits the HPET fallback. Its measurement shows how much of the cost is
   the number of reads rather than the cost of each read.
2. **Cross-CPU policy: a startup check, no shared floor, no runtime watchdog.**
   Default: yes, accept invariant TSC plus the per-AP warp check, and fall
   back to HPET for all CPUs on any failure.
   - The risk is a warp appearing after boot, which would go unnoticed.
   - The alternatives are a shared nondecreasing floor (a contended cache
     line on every read) or a periodic TSC-against-HPET watchdog that switches
     back at runtime (a second switch path).
   - Either can be added later if a target shows warps.
3. **Hypervisors: same rule as hardware, no kvmclock.** Default: under a
   hypervisor, use the TSC only when the invariant-TSC bit is set.
   - **Consequence:** QEMU on my development VM falls back to HPET. On
     2026-10-09, `-cpu max,+invtsc` there gave "host doesn't support requested
     feature: CPUID[eax=80000007h].EDX.invtsc", because the VM on the desktop
     doesn't expose invariant TSC to its own guests; the VM itself runs on
     kvm-clock.
   - **To measure task B in QEMU**, the owner exposes invariant TSC to that VM,
     for example libvirt's `host-passthrough` with
     `<feature policy='require' name='invtsc'/>`, which gives up live
     migration. `make run` would then pass `+invtsc` to `-cpu`. QEMU on the
     ThinkPad can expose it directly, because that host has it.
   - **Alternative:** trust the TSC under KVM when kvmclock reports its
     stable-TSC flag. That means implementing kvmclock's shared page, a second
     source this proposal avoids.

## Out of scope

- kvmclock and other paravirtual clocks;
- the TSC-deadline timer;
- switching clock source at runtime, suspend and resume, and VM migration;
- a userspace mapping of the counter;
- changes to the clock protocol, `clock_now` or the wall clock;
- tickless scheduling;
- the per-pass clock reads in network workers;
- changes to #557's audio batching.

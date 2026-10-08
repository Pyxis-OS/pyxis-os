# Sleep wake granularity

Status: **proposal awaiting owner decisions, 2026-10-08**. Assigned to Codex
through the orchestrator. Baseline/investigation are authorized; implementation
has not started. Base: `9acf597fec897119256a0c0044785771b19f9132`.

## Goal and evidence

Make deadline sleepers runnable at their deadline while retaining the 120 Hz
preemption schedule, existing round-robin/placement policy, non-preemptible
syscalls, clock authority and ABI. No tickless scheduler, busy-wait sleep,
userspace timer objects, HPET comparator interrupts or TSC clock-source change.

The [current-main baseline](../development/experiments/sleep-wake-granularity/README.md)
reproduces the [debt](../technical-debt.md#sleep-wake-granularity): five unprofiled
SDL_Delay(16) runs had median mean frame time 25.565 ms (24.991–25.785), with
about 0.986 ms work and 24.501 ms sleep. Quake's ordinary 72 Hz cap measured
48.189–51.482 FPS in five debugger-assisted windows. Neither is native evidence.

Inspection found two tick delays: BSP checks global timed waits, then expiry
of an AP waiter enqueues it without a reschedule IPI. A BSP one-shot alone
would leave that AP tick wait intact. See [timekeeping](../kernel/timekeeping.md)
and the [SMP contracts](../kernel/smp.md).

## Three proposed decisions

Recommendations below remain unaccepted.

1. **Ownership/timer — recommend per-CPU deadlines and calibrated LAPIC
   one-shot countdowns.** Only the executing CPU programs its LAPIC, with IF=0;
   reuse the existing PIT channel 2 calibration in `arch/x86_64/apic.c`, with
   no new calibration path. HPET stays authoritative. Arm the earlier of the
   local deadline and next nominal preemption occasion. A centralized BSP
   alternative requires remote earlier-deadline notifications plus AP wake IPIs.
   Defer TSC-deadline, which requires feature detection and qualified HPET-to-TSC
   conversion.
2. **Many sleepers — recommend sorted embedded lists, no allocation or new
   capacity limit.** Earliest lookup O(1), insertion/cancellation O(n), expiry
   O(k) for k due records. Wake all due records together; ordinary ready queues
   decide execution. A heap or fixed wake budget adds storage/capacity or
   continuation policy. Simultaneous expiries and lock contention remain real
   latency limits, without a hard upper bound.
3. **Callers — recommend the existing shared deadline mechanism.** Improve
   clock sleeps, console/endpoint timeouts and BSP kernel sleeps together,
   preserving past-deadline, resource-wakeup and stop behavior. Clock-only
   precision would require separating waits that currently share machinery.
   No SDL2/Quake workaround or ABI change.

## Handoff and timer constraints

Timed membership keeps the existing queue lock and resource/group-before-queue
lock order. Insertion is local: a blocked syscall cannot migrate; kernel tasks
stay on BSP. Earlier insertion rearms before parking. Remote wake/stop removes
membership and uses the existing runnable IPI; it never programs a remote timer.
An obsolete earlier arm can fire harmlessly and recalculate.

Preserve the saved-stack handshake: early expiry marks `notified`; only a parked
context is published runnable. Remove membership before enqueueing and retain no
wait/task pointer after publication and unlock. Resource-pointer detachment stays
with the resumed caller. Timer service allocates nothing, logs nothing and
acquires no resource lock.

Check expiry and rearm even while idle or when context switching is forbidden;
rearm before EOI/possible stack switch. A due sleeper uses ordinary reschedule
rules without making syscalls or AP kernel execution preemptible. HPET confirms
expiry, so premature/stale interrupts never complete sleep early.

Extra wake interrupts neither reset the absolute preemption phase nor advance
BSP HPET's nominal-tick maintenance countdown. Late service skips missed
preemption occasions rather than issuing catch-up interrupts. Retain periodic
boot timers for BSP's interrupt-disabled AP-startup timeout; transition locally
only after acquiring initialized scheduler state. Use checked upward count
rounding, a positive minimum and the next preemption occasion to bound the
hardware horizon. Calibration is approximate; early interrupts recheck HPET.
See [Intel SDM 3A, section 11.5.4](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf#page=408).

## Work and qualification

- [x] Investigate and record a fresh baseline/proposal.
- [ ] Owner settles the three decisions and authorizes implementation.
- [ ] Proposed review follow-up: fix the missing expiry reschedule IPI as a
  separate measured step. Collect destination CPU indices under the queue lock
  and notify after unlocking, without retaining published task/wait pointers.
  Repeat the baseline before timer changes to distinguish AP wake delay from
  BSP tick quantization. This sequencing suggestion is not implementation
  authority.
- [ ] Implement deadline ownership/dispatch and timer multiplexing together;
  update clock/scheduler contracts in the implementation PR.
- [ ] Repeat identical baseline workload/library bytes with only the kernel
  changed. Check ordinary one-/four-CPU boots, earlier insertion, simultaneous
  sleepers, resource wake/stop, idle wake, stack ordering and ongoing preemption.
  Include a roughly 1 ms sleep loop to inspect deadline interrupt rate and
  positive-minimum rearming, plus an idle CPU with no sleepers to verify it
  receives no more than the nominal preemption interrupts. Use the existing
  workload and debugger; these are qualification checks, not latency guarantees.
  Publish exact-head existing CI; no new workflow or benchmark framework.
- [ ] Record owner native evidence or an explicitly accepted qualification limit
  before closing this WIP.

QEMU/nested KVM can establish removal of tick quantization, not native latency.
IF=0 intervals, runnable load, host descheduling and firmware stalls still delay
execution. ThinkPad checks should repeat SDL delay/Quake cap and verify LAPIC
operation, sustained 32-bit HPET extension and power-state behavior. Hardware
availability and results are not assumed from screenshot qualification.

Branch: `kernel/sleep-deadline-proposal`. No dependency changes or compiler
rebuild. Baseline consumer is a measurement artifact, not a normal application.
All task-owned guest/debugger/client/build jobs stopped. Implementation waits
for owner decisions; opening/reviewing the proposal does not start it.

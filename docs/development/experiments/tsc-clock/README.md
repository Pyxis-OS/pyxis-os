# TSC clock source

Measured on 2026-10-09 for task B of the cheaper-timekeeping plan (#565):
the TSC as the clock when every CPU qualifies, with the extended HPET as the
fallback. See [TSC selection](../../../kernel/timekeeping.md#tsc-selection)
for the behaviour. Main here is `e34b9ce`, which already includes
[task A](../timer-clock-reads/README.md).

## Configurations

| Configuration | Clock | Status |
| --- | --- | --- |
| Development VM, main `e34b9ce` | HPET | measured |
| Development VM, task B | HPET, `clock: HPET kept: CPU 0 has no invariant TSC` | measured: the fallback path |
| Development VM, task B with a local forced patch | TSC, 3187.062 MHz (+/-250 ppm), RDTSCP+LFENCE | measured, not a supported configuration |
| Desktop host QEMU, `-cpu host,+invtsc` | TSC, 3187.061 MHz (+/-66 ppm), RDTSCP+LFENCE | [measured](#desktop-host-runs) by luna |
| ThinkPad, native | TSC | [owner-reported](#native-result): 2096.063 and 2096.064 MHz |

**Development VM.** QEMU 10.2.2 with the local AHCI fix, nested KVM on the
development VM, q35, `-cpu max`, 4 CPUs, 8 GiB, VirtIO net with user
networking. The development VM exposes no invariant TSC and runs on kvm-clock
itself, so task B correctly keeps the HPET there.

**The forced patch.** To exercise calibration, the warp checks, the switch
and TSC reads before the desktop runs, a temporary local patch was used and
then removed. It is not committed. It changed only two things:
- it skipped the invariant-TSC requirement;
- it raised the calibration bound from 100 to 2000 ppm.

The nested VM's HPET reads are exits through two hypervisors. They widen the
calibration brackets to a measured +/-250 ppm, which the committed 100 ppm
bound rejects ("calibration too uncertain"). Results from this configuration
show the TSC path working and what it costs. They do not qualify it: Linux
itself does not trust the TSC in this VM.

**Host load.** In the first forced set, another agent's unrelated QEMU guest
used about a quarter of a host CPU; later sets ran without it. The measurement
helpers picked their QEMU process by forwarded port.

## Workloads

The same existing tools as task A:
- **Send:** `ttcp -t -n 2048 -l 8192 10.0.2.2` into a host `ttcp -r -s`,
  16 MiB, three runs per set.
- **Receive:** `ttcp -r -p 5002 10.0.2.2` from a host `socat`, 16 MiB, three
  runs per set.
- **Clock loops:** the clock-call loop of `iobench read
  boot://share/iobench-small.bin --bytes 32768 --rounds 5` and `allocbench
  pages --rounds 3 --live 64 --size 65536 --profile`, three each.
- **Sleeps:** the [SDL_Delay(16) workload](../sleep-wake-granularity/sdl-delay.c)
  from `host://`, five runs.
- **Host CPU:** the BSP vCPU thread's `/proc` times.
- **KVM exits:** `perf kvm stat` counts of HPET counter reads and
  `APIC_WRITE` exits, in separate profiled windows.

## Results

| Measure | Main `e34b9ce` | Task B, fallback | Task B, forced TSC |
| --- | --- | --- | --- |
| Send, MiB/s | 3.348, 3.400, 3.535 | 3.690, 3.678, 3.704 | 48.180, 46.048, 41.766; 52.934, 53.755, 51.295 |
| Receive, MiB/s | 5.422, 5.054, 5.345 | 5.179, 5.144, 5.235 | 70.938, 69.209, 68.460; 77.603, 77.615, 78.569 |
| Clock loop, ns per call | 35,804–39,087 | 32,830–47,846 | 104–204 |
| Idle HPET counter reads per 10 s | 29,823 | 31,890 | 0; 0 |
| HPET counter reads in one profiled 16 MiB send | 384,984 | 411,171 | 0; 0 |
| Idle BSP vCPU thread, % of a host CPU | 7.1 | 5.1 | 2.9; 2.8 |
| BSP vCPU thread while sending, % of a host CPU | 99.9–100.2 | 99.9–100.2 | 74.2–80.2 |

Two forced sets on separate boots are separated by semicolons. Both
calibrated 3187.062 MHz, the same frequency #557 measured independently
against the HPET with `RDTSCP` (3.187062 GHz). In the second forced set, the
host's `ttcp -r -s` sink measured 52.96 MiB/s for the guest's 52.934, so the
guest's TSC-based timing agrees with the host.

**Sleeps,** SDL_Delay(16), five runs:

| | Main `e34b9ce` | Task B, forced TSC |
| --- | --- | --- |
| Mean sleep per run, ms | 16.217, 16.206, 16.264, 16.274, 16.261 | 16.058, 16.062, 16.058, 16.065, 16.063 |
| Longest sleep per run, ms | 16.530, 16.628, 17.170, 17.681, 16.666 | 16.190, 16.172, 16.197, 17.316, 16.217 |
| Mean frame per run, ms | 17.175, 17.165, 17.397, 17.497, 17.363 | 16.984, 17.024, 16.985, 17.001, 16.987 |

No sleep ended early.

- **Fallback.** It matches main: the same HPET reads and the same clock-loop
  cost. Its send is slightly higher, within the run-to-run spread seen in
  task A.
- **Nested-VM caveat.** In nested QEMU almost all of the old cost was HPET
  exits, so the forced TSC's 14–16x send and receive gains are an upper bound
  on what the change can do, not a native prediction.

## Desktop host runs

Run by luna on 2026-10-09 on the desktop host. Configuration:
- **Host:** i9-12900K, whose Linux uses the TSC.
- **QEMU:** 11.1.1 directly under KVM; q35, 4 vCPUs, 8 GiB, VirtIO net with
  user networking.
- **CPU model:** a wrapper appended `-cpu host,+invtsc`.
- **Builds:** clean, of main `e34b9ce` and of this branch at `cc9d0e3`. The
  committed code was used, with no forced patch.
- **Host load:** the owner's two other VMs kept running, and CPUs were not
  pinned.

| Measure | Main `e34b9ce` | Task B `cc9d0e3` |
| --- | --- | --- |
| Clock line | HPET, direct | `TSC selected on 4 CPUs, 3187.061 MHz calibrated against HPET (+/-66 ppm), RDTSCP+LFENCE` |
| Send, MiB/s | 18.696, 19.050, 18.648 | 84.834, 87.729, 96.059 |
| Receive, MiB/s | 29.882, 29.902, 28.554 | 123.865, 169.448, 148.149 |
| `iobench` clock loop, ns per call | 11,206, 7,240, 7,263 | 103, 104, 103 |
| `allocbench` clock loop, ns per call | 7,280, 7,239, 7,266 | 107, 110, 113 |

Single-level KVM HPET exits fit the 100 ppm calibration bound. The
frequency agrees with the forced nested runs and with #557's measurement.
These are host-KVM results, not native ones.

## Native result

Owner-reported on the ThinkPad: two cold boots selected the TSC and calibrated
2096.063 and 2096.064 MHz, within 12 ppm of Linux's refined 2,096.061 MHz; a clock
read costs about 130 ns; and the 15-minute `date -u` check against a stopwatch held
with no drift or step. The same batch's network results are in
[technical debt](../../../technical-debt.md#tcp-throughput-limits). These are owner
numbers, not agent measurements, and the individual lines are not recorded here.

## Native steps for the owner

On the ThinkPad, wired, on AC, from the local Development shell. Use PXE
builds of main `e34b9ce` and of this branch, in the same order for both.

1. **Clock lines.** Photograph or note the `clock:` lines. Expected with this
   branch: `clock: TSC selected on 12 CPUs, about 2096 MHz ..., RDTSCP+LFENCE`.
   Compare the frequency with Linux's refined calibration on this machine,
   2,096.061 MHz; the difference should be within the logged bound. Any
   `HPET kept` line and its reason is a result too.
2. **Clock loop,** three times each:

   ```text
   iobench read boot://share/iobench-small.bin --bytes 32768 --rounds 5
   allocbench pages --rounds 3 --live 64 --size 65536 --profile
   ```

3. **TCP,** three runs each, following
   [repeating the measurement](../../network-throughput.md#repeating-the-measurement):

   ```text
   ttcp -t -p 5001 -n 8192 -l 8192 DESKTOP
   ttcp -t -p 5001 -n 32768 -l 2048 DESKTOP
   ttcp -r -p 5002 DESKTOP
   ```

4. **15-minute date check,** on this branch:
   - run `date -u` at the prompt and start a stopwatch, noting the offset from
     an external clock;
   - leave the shells at their prompts, then run `date -u` again after about
     15 minutes.

   The offset should stay the same to within a second. A drift of more than
   about a second is evidence against the calibration. A jump or a backward
   step is evidence against cross-CPU agreement. The previous HPET check
   used the same procedure.
5. **Feel.** Optionally, play capped Quake briefly and say whether it feels
   the same.

## Limits

The development-VM TSC numbers come from an unsupported local patch. The committed
code selected the TSC on the desktop host and natively on the ThinkPad (above). The
warp check and the switch ran on four vCPUs of one VM in the agent's runs; the owner's
native run covered the ThinkPad's twelve CPUs. Cross-CPU agreement is checked only at
startup, so a later warp would go unnoticed.

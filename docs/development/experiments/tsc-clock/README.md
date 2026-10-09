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
| Desktop host QEMU, `-cpu host,+invtsc` | expected TSC | [to run](#desktop-host-runs) |
| ThinkPad, native | expected TSC | [owner's batch](#native-steps-for-the-owner) |

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

To be run on the desktop host (i9-12900K; Linux reports `constant_tsc` and
`nonstop_tsc` on all 24 CPUs and uses the TSC), by luna or the owner, with
ordinary builds of main `e34b9ce` and of this branch. These runs replace the
forced patch as the QEMU evidence for the TSC path.

1. Boot each build with KVM and 4 CPUs, adding `-cpu host,+invtsc` after the
   run script's `-cpu max`, for example through a wrapper given as `QEMU=`.
   QEMU applies the last `-cpu`. Keep VirtIO net with a forwarded remote port.
2. Record the `clock:` lines. Expected with this branch: `clock: TSC selected
   on 4 CPUs`, a frequency close to the host TSC, and an error bound under
   100 ppm. A "calibration too uncertain" line would mean the host's HPET
   exits are also too slow, which is worth reporting with its ppm.
3. Run the workloads above: send, receive and the clock loops three times each.
   Optionally run `perf kvm stat` HPET counts.

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

These are nested-VM measurements, and the TSC numbers come from an
unsupported local patch. The supported TSC path awaits the desktop host and
ThinkPad runs above. The warp check and the switch ran on four vCPUs of one
VM; twelve CPUs on real hardware are the owner's run.

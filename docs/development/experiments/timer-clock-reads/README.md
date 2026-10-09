# Fewer clock reads per timer event

Measured on 2026-10-09 for task A of the cheaper-timekeeping plan (#565):
- each timer pass reads the clock at most once;
- a rearm reads it only for an earlier target.

See [timekeeping](../../../kernel/timekeeping.md#scheduler-timing) for the
behaviour. The clock source is unchanged: every read is still an HPET read.

## Configuration

- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, `-cpu max`, 4 CPUs, 8 GiB, VirtIO net with user networking, display
  off. The remote shell was reached through a forwarded port.
- **Two matched pairs.** Each pair differs only in the timer change:
  - **Network, clock loops and sleeps:** main `26770a0`, then the same tree
    with the change.
  - **Audio:** main `32ee113`, which adds #557's audio sessions, then the
    rebased change `302c49e`, with an `intel-hda` + `hda-output` device writing
    a WAV file.
- **Repeats.** Main was measured in three sets over two boots, and the change
  in four sets over three boots.
- **Quiet host.** No other task-owned VM or build ran during the sets. The VM's
  load average was 0.4–2.4, from processes outside this task.

**Workloads and counters,** all existing tools:
- **Send:** `ttcp -t -n 2048 -l 8192 10.0.2.2` into a host `ttcp -r -s`,
  16 MiB, three runs per set.
- **Receive:** `ttcp -r -p 5002 10.0.2.2` from a host `socat` serving 16 MiB,
  three runs per set.
- **Clock loops:** the clock-call loop printed by
  `iobench read boot://share/iobench-small.bin --bytes 32768 --rounds 5` and by
  `allocbench pages --rounds 3 --live 64 --size 65536 --profile`, three of each.
- **Sleeps:** the [SDL_Delay(16) workload](../sleep-wake-granularity/sdl-delay.c)
  from the sleep-wake measurements, built against the same SDK and run from
  `host://` in Development, five runs.
- **Audio:** `pcm 1000 500 600` in Remote: one session, stopped with Ctrl+C.
- **Host CPU:** the BSP vCPU thread's `utime`, `stime` and `guest_time` from
  `/proc`, split into guest, QEMU userspace and host kernel, as #557 did.
- **KVM exits:** `perf kvm stat record` on the QEMU process, counting HPET
  counter reads (MMIO at `0xfed000f0`/`0xfed000f4`, three per clock read on
  QEMU's 64-bit HPET) and `APIC_WRITE` exits. These were recorded in separate
  windows, never during the unprofiled timings.

## Results

| Measure | Main | With the change |
| --- | --- | --- |
| Send, MiB/s | 3.305, 3.276, 3.264; 3.242, 3.243, 3.273 | 3.675, 3.671, 3.641; 3.686, 3.640, 3.671; 3.605, 3.645, 3.622; 3.688, 3.695, 3.735 |
| Receive, MiB/s | 4.213, 4.360, 4.254; 4.440, 4.535, 4.473; 4.447, 4.514, 4.488 | 5.711, 6.096, 5.865; 5.398, 6.030, 5.680; 6.001, 5.937, 5.852; 6.068, 6.043, 5.990 |
| Idle, HPET counter reads per 10 s | 85,788; 85,746; 85,854 | 31,953; 31,695; 31,938; 31,947 |
| Idle, `APIC_WRITE` exits per 10 s | 11,367; 11,367; 11,379 | 5,344; 5,339; 5,339; 5,344 |
| Idle, BSP vCPU thread, % of a host CPU | 13.0, 9.7, 9.3 | 5.2, 5.4, 5.8, 5.2 |
| One profiled send, HPET reads | 529,483; 527,625; 498,335 | 387,633; 385,446; 387,923; 379,827 |
| The same, per data segment | 43.1, 42.9, 40.6 | 31.5, 31.4, 31.6, 30.9 |
| One profiled send, `APIC_WRITE` exits | 31,803; 31,646; 31,668 | 8,137; 8,137; 8,147; 7,882 |
| `iobench` clock loop, µs per read | 32.9–39.6 | 33.3–42.3 |
| `allocbench` clock loop, µs per read | 33.4–44.1 | 35.0–42.9 |

- **Excluded runs.** The first main set's sends (2.560, 1.768, 2.988 MiB/s)
  varied far more than any later set and are left out of the send row.
- **Per-segment figures** divide by the 12,288 data segments of a 16 MiB send
  (three per 4 KiB call). They include background timer reads during the
  profiled window, about 6.5 s on main and 5.4–5.9 s with the change.
- **BSP CPU during sends** stayed at 99–100% of a host CPU in both builds,
  about 43% guest, 20% QEMU userspace and 36% host kernel. The same CPU now
  moves 12% more data.
- **Clock loops** time `clock_now` calls, whose cost the change does not
  touch. They serve as a control and stayed within the same noise.

**Sleeps,** SDL_Delay(16), 300 frames per run, five runs:

| | Main | With the change |
| --- | --- | --- |
| Mean sleep per run, ms | 16.292, 16.359, 16.463, 16.336, 16.322 | 16.212, 16.227, 16.213, 16.204, 16.254 |
| Longest sleep per run, ms | 17.070, 17.323, 17.323, 17.560, 17.128 | 16.531, 16.644, 16.552, 16.483, 16.853 |
| Mean frame per run, ms | 17.167, 17.335, 17.664, 17.247, 17.236 | 17.126, 17.226, 17.170, 17.120, 17.340 |

No sleep ended early in either build. Reusing a slightly older reading for
the rearm did not make wakes later. They came slightly earlier and with less
spread, probably because each wake path now spends less time in clock exits
before it runs.

**Audio,** one session, two playback runs per build:

| Measure | Main `32ee113` | With the change |
| --- | --- | --- |
| BSP vCPU thread over 15 s, % of a host CPU (guest / QEMU userspace / host kernel) | 41.3 (18.6 / 8.4 / 14.3); 40.9 (18.7 / 8.5 / 13.7) | 26.9 (12.1 / 6.0 / 8.9); 27.0 (12.4 / 5.7 / 8.9) |
| HPET counter reads per 10 s, all CPUs | 315,901; 316,978 | 160,164; 159,363 |
| `APIC_WRITE` exits per 10 s | 33,771; 33,959 | 9,564; 9,489 |

Both captured WAV files hold a 1000 Hz left and 500 Hz right tone, counted by
zero crossings over one second. Eight sessions were not run; that needs #557's
nine-space layout.

**Limits.**
- These are nested-VM results, where every HPET read is an exit to QEMU and
  one clock call costs about 36 µs (the clock loops above).
- Natively each read is a chipset MMIO access of unmeasured cost, so the
  native effect will be smaller and needs the owner's run below.
- No debugger or temporary counters were used, so clock calls are inferred
  from HPET exits rather than counted per call site.

## Native steps for the owner

On the ThinkPad, wired, on AC, from the local Development shell, PXE builds of
main before the change and of the change. Run each block on both builds:

1. **Clock path.** Note the `clock:` lines in the boot log; they should still
   say 32-bit, software-extended.
2. **Clock loop,** three times each:

   ```text
   iobench read boot://share/iobench-small.bin --bytes 32768 --rounds 5
   allocbench pages --rounds 3 --live 64 --size 65536 --profile
   ```

   Record each "Clock-call loop" line. This gives the native cost of one read,
   which the change should not move.
3. **TCP.** Follow
   [repeating the measurement](../../network-throughput.md#repeating-the-measurement)
   with the desktop's sink and source. Send with 8 KiB and 2 KiB writes and
   receive with `ttcp -r`, three runs each:

   ```text
   ttcp -t -p 5001 -n 8192 -l 8192 DESKTOP
   ttcp -t -p 5001 -n 32768 -l 2048 DESKTOP
   ttcp -r -p 5002 DESKTOP
   ```

   Native send was bound by per-segment and per-call cost (70.5 MiB/s with
   8 KiB writes, 44 MiB/s with 2 KiB), so it is where fewer clock reads per
   wake would show. The desktop capture is optional.
4. **Sleeps.** Optionally play capped Quake briefly, as in the sleep-wake
   check, and say whether it feels the same.

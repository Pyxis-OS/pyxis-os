# HD Audio timing recovery

Owner decisions, **2026-10-13**: replace fail-closed-until-reboot for timing
faults with [stream recovery](../../../devices/hda.md#timing-recovery), and
derive the native service horizon from the ring. Bounded teardown is a separate
change; precedence for BSP kernel workers is held for the BSP-pinning
inventory.

## Native evidence before the change

ThinkPad, owner session 2026-10-13, image `54a02657` (#803 head) and the
diagnostic branch `diag/audio-horizon` (`e83ae1fc`, trace logging, not
merged), which added the gap at a trip and each process teardown's duration.

| Run | Result |
| --- | --- |
| Chocolate Quake (#787), quit | old 20 ms horizon tripped at the quit |
| EDuke32 bundle `-d1:1` | tripped; the profile is silent by design, so it was not placed in time |
| DIAG: EDuke32 capped, then uncapped (`r_maxfps 0`), ~20 s each | sound throughout, no trip |
| DIAG: EDuke32 console quit | teardown **21.8 ms**, then a **28.8 ms** observation gap and the trip, 41.5 s after RUN |
| DIAG: Chocolate Quake quit | teardown **39.7 ms** (audio had already failed at boot, see below) |
| DIAG: every other process exit | about 0.45 ms |

The teardown runs on CPU 0. `reap_completed()` runs only in the BSP scheduler
loop with interrupts off, and `process_task_reclaimed()` asserts CPU 0. The
`CPU N` printed by the diagnostic, and by the existing `userspace: CPU N
exited` line, is the CPU the task last ran on. With the 5 ms watchdog, a
21.8 ms teardown explains the 28.8 ms gap. Natively, the trips seen so far are
quits, not play.

The second DIAG boot failed audio at boot with `native codec
identity/graph/idle setup failed`. The four identity verbs succeeded, but the
codec's 100 ms setup budget had expired before its subsystem read, on a busy,
trace-logging CPU 0. That setup path is handled separately.

## Nested-QEMU validation of recovery

Horse VM, KVM, default logging, current main plus this change, with
EDuke32 data. `pcm 1000 500 40` in the Read-only space, while EDuke32
(`eduke32 -v1 -l1 -s2`, silent on main) started and quit in Development.
QEMU's WAV capture was analysed for gaps of 1 ms or more.

An earlier revision failed the engine after more than 3 recoveries in 10 s, as
first proposed. In one 4-CPU run, EDuke32's startup caused four restarts within
about 130 ms (mostly QEMU's 1 ms commit guard), so that rule disabled audio
after a single load burst. The rule now fails only on failed recoveries that
persist for 2 s ([timing recovery](../../../devices/hda.md#timing-recovery)).
Results with the final rule:

| CPUs | Restarts | Gaps after startup silence | pcm at release |
| --- | --- | --- | --- |
| 4 | 4, all horizon | 10.0, 13.3 ms at startup; 4.0 ms at quit | full 40 s played |
| 4 | 3, all horizon | 14.0, 12.7 ms at startup | `discontinuity=3 state=0` |
| 1 | 5 (horizon and commit) | 5 gaps, largest 20.0 ms (at startup and at 33 s) | `discontinuity=5 state=0` |

Every capture starts with the documented 58.7 ms of startup silence. Two
restarts can fall within one audible gap, and the log line is limited to one
per 10 s, so the 1-CPU run logged 4 of its 5 restarts. Before this change the same
reproduction disabled audio until reboot on 3 of 3 boots (EDuke32 sound
record, pyxis-os #802).

Failure path: a GDB breakpoint in `recover_output` marked the stream unproven
with a streak older than 2 s. That recovery failed the engine with `hda:
output kept missing service after repeated recovery; engine unavailable until
reboot`, and `pcm` received UNAVAILABLE (status 6). Pausing the VM from GDB does not cause a trip
because QEMU stops the guest clocks too.

The native horizon (`horizon-ns` in trace logs) is not exercised in QEMU.

## Native result

Owner session on the ThinkPad, 2026-10-13, AC power, PXE image of this change
at `df7db1bc` (main plus #807; later commits change only documentation),
default logging. `pcm` played a test tone in the Read-only space while
Chocolate Quake (sound bundle from #787) was opened and closed in Development.

- **Passed.** The tone stopped briefly when Chocolate Quake was **opened**,
  then resumed. Closing it caused no audible stop. The engine never failed:
  no `engine unavailable` line.
- Log: `audio: output observation exceeded service horizon; output restarted
  (1 since boot)`, then `(2 since boot)`, then `(4 since boot)`. The rate
  limit hid one line, so there were at least four restarts.
- With this build, the native trigger is the game's **startup**: Chocolate
  Quake's large private-memory setup runs in a non-preemptible syscall,
  starving the BSP audio worker, as seen at EDuke32's startup in nested QEMU.
  No stop was heard at the quit. The log has no timestamps, so an inaudible
  restart there cannot be ruled out. The longer native horizon (about 50 ms)
  probably covers the 39.7 ms teardown measured in the trace build plus the
  5 ms watchdog, where the old 20 ms guard did not. That is an inference;
  `horizon-ns` is logged only at trace level.

Startup and teardown both stall the BSP; recovery now turns each stall into a
short gap. Removing the stalls is separate, paused work: bounded teardown and
the BSP-pinning inventory.

For later native audio checks, use `pcm 440 220 SECONDS` (A4 left, A3 right)
at moderate volume, not the 1000/500 Hz tones of the nested runs above (owner
request).

# Vela precision input: task 4

## Pre-code baseline, 2026-10-16

Main `4677e89a122b930ee0e770fc940bb44da724677f`, successful CI2894
(Forgejo API run2994), df87791 builder. Matching artifacts and clean component
metadata verified before use. Kernel SHA256
`b9aad646b0712e73cffa677c3d1fe5e31358a1399a774fd50f744a6f7867c385`,
ISO `c0296d1ab64fecf4d3b50528e530b647cd05c674443116ba056ff6d834966e4c`.
Userland `909d4c40`, ports `0adce1de`, fs `25337999`, lwIP `a1aadb91`.
Ordinary configuration: polling4ms, measurement off.

Nested KVM Q35/max, 4 CPUs,2GiB, OVMF, standard VGA, modern VirtIO-net/rng,
xHCI hub/keyboard, stock live spaces. Existing IPC CALL/SEND size64/4096,
256messages/5rounds plus ten Lua `pyxis.run` echo launches. All IPC samples
verify; each Remote session exits0/draincomplete. No build/GDB overlap with
measurements; read-only GDB ran21seconds after the last timed launch, showing
LPSS absent, transportNULL, no PS2 filter initialization. No actual I2C device
is emulated. Shared-host load uncontrolled; these are nested controls.

| Workload | Median ms (range) |
| --- | --- |
| CALL64 completion | 101.921 (96.707–107.815) |
| CALL4096 completion | 99.668 (78.909–111.597) |
| SEND64 completion | 15.074 (12.852–17.045) |
| SEND64 admission | 1.253 (1.215–1.540) |
| SEND4096 completion | 17.280 (12.653–24.746) |
| SEND4096 admission | 1.472 (1.353–1.841) |
| Echo launch | 7.848 (2.520–9.716) |

Raw local records: task worktree `build/task4-baseline/`; repeat matched controls
as the stack lands. The [task3 native4ms sweep](../vela-hid-i2c-polling/README.md#native-sweep-2026-10-16)
is a reference for pre-filter service/rate, not a fresh native A/B. It measured
146.3validreports/s and14.367% whole-poll service estimate; native4ms idle0.9%
is extrapolated from8ms idle, not a separate measurement. No scheduler probe.

## Native gates still due

The [accepted task4 contract](../../../wip/vela-i2c-touchpad.md#accepted-task-4-decisions-owner-2026-10-16)
requires source-owned filter state, honest contact metadata, bounded client
reinitialization and lift-tail classification before pointer publication.
Session(a) uses one never-merged build, pointer publication disabled, combining
bounded lift/contact/rejected-frame capture with one announced real clientRESET.
Session(b) checks the ordinary build's gestures and the coordinated release
checks. Neither native session is claimed yet; GPIO interrupts and non-spinning
I2C completion remain deferred outside tasks3/4.

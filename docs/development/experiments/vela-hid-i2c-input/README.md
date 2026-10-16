# Vela precision input: task 4

## Pre-code baseline, 2026-10-16

Main `4677e89a122b930ee0e770fc940bb44da724677f`, successful CI 2894
(Forgejo API run 2994), df87791 builder. Matching artifacts and clean component
metadata verified before use. Kernel SHA256
`b9aad646b0712e73cffa677c3d1fe5e31358a1399a774fd50f744a6f7867c385`,
ISO `c0296d1ab64fecf4d3b50528e530b647cd05c674443116ba056ff6d834966e4c`.
Userland `909d4c40`, ports `0adce1de`, fs `25337999`, lwIP `a1aadb91`.
Ordinary configuration: polling 4 ms, measurement off.

Nested KVM Q35/max, 4 CPUs, 2 GiB, OVMF, standard VGA, modern VirtIO-net/rng,
xHCI hub/keyboard, stock live spaces. Existing IPC CALL/SEND size 64/4096,
256 messages/5 rounds plus ten Lua `pyxis.run` echo launches. All IPC samples
verify; each Remote session exits 0/drain complete. No build/GDB overlap with
measurements; read-only GDB ran 21 seconds after the last timed launch, showing
LPSS absent, transport NULL, no PS2 filter initialization. No actual I2C device
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

Raw records: `/shared/present/vela-touchpad-input-qualification/qemu-source-state/`;
repeat matched controls as the stack lands. The [task 3 native 4 ms sweep](../vela-hid-i2c-polling/README.md#native-sweep-2026-10-16)
is a reference for pre-filter service/rate, not a fresh native A/B. It measured
146.3 valid reports/s and 14.367% whole-poll service estimate; native 4 ms idle 0.9%
is extrapolated from 8 ms idle, not a separate measurement. No scheduler probe.

## Native gates still due

The [accepted task 4 contract](../../../wip/vela-i2c-touchpad.md#accepted-task-4-decisions-owner-2026-10-16)
requires source-owned filter state, honest contact metadata, bounded client
reinitialization and lift-tail classification before pointer publication.
Session (a) uses one never-merged build, pointer publication disabled, combining
bounded lift/contact/rejected-frame capture with one announced real client RESET.
Session (b) checks the ordinary build's gestures and the coordinated release
checks. Neither native session is claimed yet; GPIO interrupts and non-spinning
I2C completion remain deferred outside tasks 3/4.

## Source-owned filter slice

`abb0f2cb`: ordinary build and interactive QEMU/GDB passed, 1/4 CPUs with the
configuration above. After commands are retained; the exact baseline command
was not retained, so matching is limited to its recorded configuration,
8-function PCI inventory, memory/configuration counters and dependency pins.

| Workload | After median ms (range) |
| --- | --- |
| CALL64 | 108.330 (97.483–118.383) |
| CALL4096 | 94.384 (92.176–99.440) |
| SEND64 completion / admission | 18.325 (13.972–25.106) / 1.314 (1.254–2.206) |
| SEND4096 completion / admission | 13.273 (12.122–17.664) / 1.284 (1.274–1.330) |
| Echo launch | 8.811 (3.072–10.294) |

All samples verified, exit 0/drain complete. One nested A/B pair with material
variation supports no performance conclusion. Read-only GDB and normal input
confirmed PS/2 keyboard/mouse delivery, USB mouse movement and held-button
unplug, and held-Shift USB keyboard unplug: removed sources cleared their own
holds/fractions while PS/2 remained live. LPSS was absent and the touchpad
context uninitialized. No positive Synaptics filter or actual I2C/recovery
qualification is claimed. All QEMU/GDB/client jobs stopped.

## Precision integration controls

`3200daf1`: ordinary build, interactive 1/4-CPU QEMU/GDB and existing PS/2/USB
input/loss checks passed with the same limits as above. The later measurement
change invalidates phases interrupted by recovery and excludes sleeping retries
from service; final ordinary build at `7ef22d68` also passed. No actual I2C
controller/recovery or native contact behavior is emulated.

| Workload | Integration median ms (range) |
| --- | --- |
| CALL64 | 99.502 (78.203–112.498) |
| CALL4096 | 90.874 (89.463–101.169) |
| SEND64 completion / admission | 16.719 (12.003–18.795) / 1.256 (1.194–1.600) |
| SEND4096 completion / admission | 14.053 (13.602–16.954) / 1.343 (1.284–1.357) |
| Echo launch | 9.194 (3.968–11.572) |

All samples verified, exit 0/drain complete; controls preceded GDB/other builds.
These are nested controls with uncontrolled host load, not a native performance
comparison. Raw text records remain under the packet's `qemu-precision/`.
The two native sessions remain due; no performance or gesture pass is inferred.

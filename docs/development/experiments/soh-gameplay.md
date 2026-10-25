# SoH task 6: gameplay and audio qualification

Baseline: main `fc69c6cd`, userland `88872fe`, ports `e6c51d9`, with the exact
private task-5 target/image. After sources are in parent #1237 and ports #182.
Rebasing over the later LuaLS/Rust work changes neither HDA nor LUS; userland's
intervening change is Neovim configuration, not the SDK/runtime used here.

Nested Q35/KVM, four CPUs, 4 GiB, 1280×800 VirtIO GPU, Intel HDA/WAV 48 kHz
stereo, softpipe GL3.3 compatibility, 320×240 internal 4:3, MSAA off. These are
functional checks, not native or frame-rate measurements.

## HDA

| Ordinary workload | Before | After |
| --- | --- | --- |
| `boot://pcm.pxe 440 220 10` | completes; starvation 3, discontinuity 1 | same observed counts |
| Longer SDL game output | terminal unstable-position refusal | about twenty minutes of wall time, normal Quit; no terminal shutdown |

The long after-run changes only the kernel, retaining the task-5 P1F/initrd.
Parked GDB finds the engine available, stream stopped and sticky stream errors
zero; 450 bounded recoveries occurred across the tone/game workloads. Frequent
nested timing recoveries prevent any smooth/audible native-output claim. The
rare movement-only branch was not separately identified in the rate-limited log.

Before-run GDB was attached after shutdown: last LPIB reads 1124→1324 were in
range/aligned, with no recorded FIFO/descriptor errors. Cached RUN did not prove
RUN at the final refusal. The [accepted rule](../../devices/hda.md#timing-recovery)
requires fresh validation before treating movement alone as recoverable. Three
scans and all ring/timing/burst/repeated-recovery bounds stay unchanged; failed
scans never supply accepted progress. Observation time follows the status scan
that can create its notification timestamp, preventing unsigned age underflow;
commit duration includes both scans.

## Input, persistence and lifecycle

Normal mirror-only LUS recipe builds and both private game links pass. A
separate explicit-Null run keeps debugger pauses out of real audio playback.

| Check | Observed result |
| --- | --- |
| Held W+D, switch space, release hidden, return | stick 69/68 becomes 0/0, no buttons |
| Keyboard play | opens a slot, completes the opening, moves in Link's house; real read shows A=32768 and stick right=85 |
| Create and normal pause save | actual worker completion precedes `Game saved` |
| Cold QEMU restart on the same npfs home | slot A retained; loads the house without replaying the opening |
| Read-only home at startup | visible `cannot prepare home://soh: Permission denied`, exit 1; no runtime-save claim |
| Full 128 MiB scratch pool | ordinary `cat` writes reach ENOSPC; save failure modal, question retained, no success acknowledgement |
| Free filler, manual retry | `Game saved` after completion; preserved prior-save copy is byte-identical to the committed before-file |
| Configuration on the full pool | visible write failure, then a distinct publication/durability-uncertain modal when parent sync returns NO_SPACE |
| Detached pool copies after save and full/retry | committed replay plus fsck both exit 0 |
| Null-worker shutdown inspection | producer running=false, processing=false, thread handle=0 before save shutdown; failure session exits 1 |

The owner captures immutable JSON; the real save worker performs file sync,
checked close, replacement and parent sync. Owner-frame completion gates hooks,
metadata and UI. Pre-replacement failure and uncertain publication are distinct.
Shutdown joins/drains without UI acknowledgement waits. LUS configuration has
checked persistence and an exception-safe C bridge; sticky failures remain
visible to application UI and exit status after Context destruction. A session
with a persistence failure conservatively exits 1 even after a later retry.

Native backend selection now obeys the menu's relaunch contract: saving SDL
while Null is live leaves the actual Null player intact, verified in GDB. The
next launch used SDL, loaded the retained slot, moved and saved in the house,
and quit normally with exit 0. Its WAV contains 735 seconds of output. Parked
inspection finds available=true, stream_running=false and no sticky stream
errors (46 bounded recoveries). No real playback was paused for debugger
inspection.

## Limits

The play check covers the opening and movement in the first room, not a full
playthrough. Game-over/alternate save menus, scheduled autosave intervals and
optional export tools were source-reviewed, not individually played. No host
power-loss or native audibility result is claimed. Native vela audio remains a
coordinated follow-up. The selected link omits the legacy raw-SRAM bridges;
modern JSON saves use the checked path. Task 6 is QEMU-qualified; task 7 and
native audio remain open.

Private source adaptations, targets, pool copies, WAV and raw evidence remain
under `/shared/oot/soh-9.3.1-task6/`; no ROM or extracted asset bytes/hashes are
published.

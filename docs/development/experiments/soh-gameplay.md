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

### First native vela check

On 2026-10-25, main `12d12f75` with the unchanged private task-6 packet failed
on first native launch. FIFOS was `0xc0` (conservative FIFO 196 bytes), reserve
4,040 bytes, commit bound 20 ms / 480,000 WALCLK ticks and service horizon
48,958,333 ns. RUN read back in 1,635 ns; the subsequent
`position outside aligned PCM ring` refusal shut down HDA until reboot.
This is a failed native check, not successful gameplay or audible qualification.

The old failure line does not distinguish range from alignment or print the
retained `start_lpib[0..1]`; their actual values remain unknown. This boot has no
configured network debugger and its USB-NCM link has no stopped-debug transport,
so those kernel RAM values cannot be retrieved through that interface. A later
live MMIO read would not recover the pre-reset observation.

HDA 1.0a §3.3.37 defines LPIB in bytes, without a PCM-frame alignment guarantee.
The focused correction accepts native in-range byte positions and byte progress
without rounding; QEMU keeps its qualified frame checks. Complete-period
reclamation, native CBL normalization, three scans, all timing/reserve/recovery
bounds and terminal rejection of out-of-range positions remain. Terminal failure
diagnostics now include the retained raw pair, ring length and cached stream
state before shutdown. The corrected native run below passes; the specification
error and successful rerun still do not recover the original pair or establish
which operand caused that particular refusal. The raw native record stays private under
`native/results/crash-log-20261025b.txt` in the task-6 area.

### Corrected native vela check

Chronium attended the 2026-10-25 run with #1240 kernel `319813e7` (SHA-256
prefix `3c765c4e`) and the same private packet. Launch, slot load, play past
Saria's cutscene, save, reload without repeating the cutscene and normal Quit
to the prompt passed. The save/reload check used RAM home in the same boot;
it does not establish reboot durability. No HDA shutdown recurred.

| Observation | Native result |
| --- | --- |
| Owner session snapshot after producer join, before SDL release | generation 3, READY, starvation 1, discontinuity 0 |
| Controller counters after quit | recoveries 0, starts 2, stops 2; maximum commit 90,575 ns |
| Native model | FIFO 196 bytes, reserve 4,040 bytes, 20 ms / 480,000 WALCLK commit bound, 48,958,333 ns horizon |
| Observed softpipe FPS | title/menu about 4; Link's house up to 5.2; Kokiri Forest 4.9–5.4 |
| Chronium's listening report | clean sound without clicks, but in chunks with audible gaps at the low game-loop rate |

`starvations` counts kernel mixer periods lacking queued PCM, coalescing a
continuing shortage into one episode; it measures neither duration nor audible
gaps. LUS uses SDL queued audio, whose drain callback pads a shortage with
silence before the Pyxis backend writes a complete buffer. Thus game-side
starvation can be audible while the kernel queue remains supplied. SoH wakes
its audio producer from graphics/game updates; this source path is consistent
with the reported chunks at softpipe speed. No per-gap timing was measured,
and the single kernel starvation cannot identify every audible interruption.
Zero discontinuities/recoveries establishes no engine recovery in this run,
not uninterrupted application sound. The private result is
`native/results/collect-20261025T210640` in the task-6 area.

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

QEMU covers the opening and first room; native play extends past Saria's cutscene
into Kokiri Forest, not a full playthrough. Game-over/alternate save menus,
scheduled autosave intervals and optional export tools were source-reviewed,
not individually played. No host power-loss result is claimed. Native listening
is reported above, with audible application gaps and no continuous-sound claim.
The selected link omits the legacy raw-SRAM bridges; modern JSON saves use the
checked path. Task 6 has QEMU and native functional evidence; task 7's remaining
resource/performance qualification is not implied by this attended run.

Private source adaptations, targets, pool copies, WAV and raw evidence remain
under `/shared/oot/soh-9.3.1-task6/`; no ROM or extracted asset bytes/hashes are
published.

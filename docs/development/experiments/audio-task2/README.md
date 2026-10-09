# Audio task 2: historical baseline and runtime evidence

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory
were removed from the tree; Git history keeps them at `d6733033`.

Measurements belong to their named revisions, before the accepted BSP cost
follow-up. The owner accepted [#557](https://git.internal/PyxisOS/pyxis-os/pulls/557)
delivery with the recorded nested-QEMU eight-session limitation on **2026-10-09**.
The [profiling report](profiling.md) records the later cost analysis and the
matched remeasurement of the accepted fixes. This historical record claims no
results for later fixes or native qualification; the
[engine reference](../../../devices/hda.md#qualification-and-remaining-scope) records
the subsequent native closure.

The [session reference](../../../interfaces/audio.md) and
[engine reference](../../../devices/hda.md) describe the implemented behavior.

Accepted tuning is eight 10 ms DMA periods, 80 ms session queues and 4096-byte
atomic writes. IRQ hints and a running-only 5 ms watchdog supply observations; the
progress horizon is 20 ms and a commit of 1 ms or more fails. A reclaimed-period
commit needs a fresh position and more than 8192 bytes plus one frame of headroom.
These guards do not prove absolute progress: modulo LPIB and QEMU's adaptive WALCLK
can hide whole laps, and a post-copy check cannot undo consumed PCM. The 8192-byte
bound is a QEMU 10.2.2 source assessment, not a measured burst or native result. No
hard real-time, uninterrupted-output or audible-drain guarantee follows.

## Revisions and configuration

| Observation | Kernel/task revision | Userland revision |
| --- | --- | --- |
| Original parked-engine baseline | `780f5d22` | `a5a48b4b` |
| First public playback | modified tree later committed as `e004fc95` | `97770cd` |
| Clean one-CPU duplex | `e004fc95` | `97770cd` |
| Single producer and matched after | `8fa7e8d9` | `052ac5ea` |
| Eight-session capacity | scene `672fe6e0`; kernel matches `0070d33f` except build identity | `052ac5ea`, local space layout |
| Matched parked-engine control | `dac5e2e9`, main `01791631`, matching audio SDK | `052ac5ea` |
| Bluetooth compatibility integration | `0e3c266e`, main through `114f2acb` | `052ac5ea` |

First playback recorded an earlier HEAD and a modified tree; it is not a clean
`e004fc95` build. Compatibility integration moved audio's protocol tag to 45 and
object type to 47, so older measurements are not exact-head evidence for it or for
later changes. Ordinary `make -j16 image` passed for the original baseline and the
integration with LLVM 23.1.3/`49e2c1a`, matching SDK and pinned sources (baseline
also ports `a642f073`, filesystem `b427df29`, lwIP `a1aadb91`; info logging, flush
interval 30 s, HPET maintenance 120 ticks, XHCI enabled). Vendor warnings remain.

Stock QEMU 10.2.2: Q35, nested KVM, `-cpu max`, normally four CPUs and 8 GiB,
fresh OVMF variables, static Caelum console, VirtIO SCSI read-only CD-ROM, RNG and
network, and `intel-hda` + `hda-output` writing a 48 kHz S16 stereo WAV
(`-audiodev wav,...,out.frequency=48000,out.channels=2,out.format=s16`). The
display was 1280×800 (1280×768 for the matched idle runs). The duplex run used one
CPU and `hda-duplex` for playback only; its unavailable ADC warning establishes no
recording support. The absent-device run used one CPU without HDA. Callback-driven
compatibility mode is unqualified. Commands were entered manually.

## No-playback observations

Original baseline: codec `1af40012`, pin 3 → DAC 2, 22 commands/responses, live
untimed parked worker, command/stream DMA and BME off (`PCI command=0x402`), 20 KiB
DMA retained, WAV with zero frames.

Presenter timing used eight manual hardware-breakpoint/finish pairs around
`space_present` with the [timing method](../../screenshot-qualification.md#matched-presenter-cost).
Separate debugger-free windows used host monotonic time and sequential `/proc`
snapshots (`CLK_TCK=100`); percentages mean one host CPU including QEMU/KVM overhead.

| Observation | Original baseline | Matched control | Matched after |
| --- | ---: | ---: | ---: |
| Accounting window | 157.492567112 s | 76.922345279 s | 132.721176347 s |
| QEMU process CPU | 19.4485% | 19.565186% | 19.032381% |
| Presenter median | 1.162955 ms | 0.742190 ms | 0.703230 ms |
| Presenter range | 0.745350–1.293590 ms | 0.658790–0.893530 ms | 0.613040–0.793690 ms |

Original BSP/AP costs were 9.1560% / 3.2192%, 4.2605%, 2.7938%. The matched runs
used an identical initrd. Single windows establish neither a stable improvement nor a
regression threshold, and idle cost does not predict eight-session BSP cost or native
performance.

## Manual playback and failures

Evidence came from interactive boots, terminal commands, read-only debugger
inspection and offline WAV analysis; no tests, fault injection or automation were added.

| Run | Observed behavior | Maximum commit |
| --- | --- | ---: |
| First four-CPU playback | 240,000 independent left/right PCM frames exact; normal start/stop, empty sessions/FIFO, parked worker | 340,040 ns |
| Clean one-CPU duplex | 250 ms pauses, two repetitions/150 ms gap, Ctrl+C and fresh acquisition; 389,824 nonzero frames exact, six starts/stops, zero reported discontinuities | 483,350 ns |
| Eight-session capacity | eight generation-1 acquisitions, ninth CALL_LIMIT (11); hidden-space playback and saturated mix, then terminal failure | 803,090 ns |
| Compatibility integration | first command and repeated run 1 complete; repeated run 2 fails at the 20 ms guard | 815,270 ns |

Unprimed startup emitted **1024 tone frames / 21.333 ms, then 2816 zero frames /
58.667 ms**, then the remaining PCM; capacity and all five integration RUN epochs
kept this gap. Exact PCM totals do not establish gap-free startup. Ctrl+C
discarded queued PCM.

- **`8fa7e8d9`:** a two-minute producer completed without a reported discontinuity;
  acquisition in an already owned space returned BUSY (9). A later refill fault,
  associated with a monitor capture (causation unknown), retained DMA, left the
  shell responsive, and later acquisition returned UNAVAILABLE (6). A separate
  four-vCPU single-producer window: 56.487179845 s, QEMU 72.9369%.
- **Capacity:** every eight-source sum/final-clip sample matched for **591,872
  frames / 12.330667 s** (frames 1,359,360–1,951,231), accounting for the first
  source's startup phase. Left reached −32768/32767 with 123,305 negative and
  123,310 positive clipped samples; right reached ±27306 without clipping. Later
  starvation changed phase and no later match is claimed. Its 53.523698770 s window
  measured QEMU 132.6889%, BSP 99.1150%, APs 10.3132%, 13.6949%, 6.5765%, with an
  unrelated pointer VM running, so the failure's exact placement is unproven. Progress
  exceeded the 20 ms horizon before any screenshot, debugger or Ctrl+C. All producers
  and fresh acquisitions then returned UNAVAILABLE; shutdown masked IRQs, reset
  stream/link, stopped CORB/RIRB, cleared BME and retained DMA, slots/FIFO emptied and
  the worker parked. This does not qualify sustained eight-session refill, normal
  release or recovery without reboot.
- **Compatibility integration:** `pcm 1000 500 5` completed 240,000 frames in
  4.885213970 s (235 writes, 229 full/writable waits, starvation 2, discontinuity 0).
  `pcm --pause-ms 250 --repeat 2 --gap-ms 150 1000 500 2` completed its first
  96,000-frame run in 2.051498010 s (94 writes, 86 waits, starvation 4,
  discontinuity 0); run 2 (generation 5) resumed after its pause, then failed on an
  expired completion notification at the 20 ms guard, with no screenshot or debugger
  attached during playback. **386,962 captured PCM frames** across ten regions
  (240,000 first-command, 96,000 repeated-run-1, 50,962 partial run-2) matched their
  channel functions (WAV 412,787 frames). Agreement does not undo the failure or prove
  absolute progress. Afterward: five starts, four normal stops, 863 IRQs, 868 refills,
  audio availability false, empty slots/FIFO, parked worker, BME off; fresh acquisition
  returned UNAVAILABLE and shell echo worked.
- **Absent controller:** the boot stayed quiet: no audio/HDA serial line, ACQUIRE
  UNAVAILABLE, shell echo successful, parked worker, empty slots, `prepared=false`, no
  controller claim or DMA allocation.

## Delivery limits

The parent pinned published userland
[#168](https://git.internal/PyxisOS/pyxis-userland/pulls/168) at
`052ac5ea0c1b045924aee439470f66e7f3382c1c`. No compiler-container rebuild was needed;
manual refused-authority and malformed-call qualification was not added.

The accepted BSP follow-up needs matched one/eight measurements and sustained
eight-session qualification. Native AMD binding, speaker/headphone/jack behavior,
latency and refill qualification remain open, and eight periods remain starting
tuning. Those were the limits at this revision; subsequent native qualification
is recorded in the engine reference linked above.

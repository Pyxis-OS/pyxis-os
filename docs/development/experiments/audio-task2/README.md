# Audio task 2: historical baseline and runtime evidence

Measurements below belong to their named revisions, before the accepted BSP
cost follow-up. [#557](https://git.internal/PyxisOS/pyxis-os/pulls/557) remains
draft; sustained eight-session playback and repeat qualification remain delivery
requirements. The [profiling report](profiling.md) records the later cost analysis.
This summary claims no results for subsequent fixes and no native qualification.

The [session reference](../../../interfaces/audio.md) and
[engine reference](../../../devices/hda.md) describe implemented behavior:
per-space grants, up to eight process-owned copied PCM queues, S32 accumulation
with one final S16 clip, and IRQ-driven refill. Hidden spaces continue playing;
release/exit invalidates the generation and discards pending PCM. First queued
frames start output without priming; depletion stops and parks the engine after
internal zero-tail handling. Starvation supplies zeros and counts once per empty
episode. Unsafe or ambiguous progress fails closed until reboot, returns
UNAVAILABLE/WAIT_ERROR and retains DMA.

Accepted tuning remains eight 10 ms DMA periods, 80 ms session queues and
4096-byte atomic writes. IRQ hints and a running-only 5 ms watchdog supply
observations; the progress horizon is 20 ms and a commit of 1 ms or more fails.
Fresh position and more than 8192 bytes plus one frame of headroom are required
before a reclaimed-period commit. These conservative guards do not prove absolute
progress: modulo LPIB and QEMU's adaptive WALCLK can hide whole laps, and a
post-copy check cannot undo consumed PCM. The 8192-byte bound is QEMU 10.2.2
source assessment, not a measured burst or native result. No hard real-time,
uninterrupted output or audible-drain guarantee follows.

## Revisions and configuration

| Observation | Kernel/task revision | Userland revision |
| --- | --- | --- |
| Original parked-engine baseline | `780f5d22` | `a5a48b4b` |
| First public playback | Modified tree subsequently committed as `e004fc95` | `97770cd` |
| Clean one-CPU duplex | `e004fc95` | `97770cd` |
| Single producer and matched after | `8fa7e8d9` | `052ac5ea` |
| Eight-session capacity | Scene `672fe6e0`; kernel matches `0070d33f` except build identity | `052ac5ea`, local space layout |
| Matched parked-engine control | `dac5e2e9`, main `01791631`, matching audio SDK | `052ac5ea` |
| Bluetooth compatibility integration | `0e3c266e`, main through `114f2acb` | `052ac5ea` |

First playback recorded an earlier HEAD and modified-tree digest; it is not a
clean `e004fc95` build. Compatibility integration updated audio's protocol tag to
45 and object type to 47. Older measurements are not exact-head evidence for
that integration or subsequent changes.

Ordinary source-built `make -j16 image` passed for the original baseline and
compatibility integration with LLVM 23.1.3/`49e2c1a`, matching SDK and pinned
sources. Original baseline additionally pinned ports `a642f073`, filesystem
`b427df29` and lwIP `a1aadb91`, with info logging, flush interval 30 seconds,
HPET maintenance 120 ticks and XHCI enabled. Vendor warnings remain.

| Artifact SHA-256 | Original baseline | Compatibility integration |
| --- | --- | --- |
| ELF | `561e0df274bd422a66b2264cbe353d719d3cad78b08c240a0563c15137d02a64` | `01f3455065a0f5f354187abdd0fee06a99ba7125c79bbef1b92dd8d0d3b2a365` |
| ISO | `9fe9346e1490e33447628bcb3c5d3540616bf01864ac5c2e5d9f2beff86f0cb8` | `2e06028c6a36d5719d96c9808658e5df8cc31b8bcc891e8e85c5a1db78a01ad5` |
| Initrd | `acc72d85e0bdebe6e7336065f4eaf64ec233908c836163c07408c9a1337c25fb` | `e32737615ecea925bb6bf460261cb7ec352b7ca1c49f2c8d3de113959de8473e` |

Stock QEMU 10.2.2: Q35, nested KVM, `-cpu max`, normally four CPUs/8 GiB,
fresh OVMF variables, static Caelum console, modern VirtIO SCSI read-only CD-ROM,
RNG and network; `intel-hda` + `hda-output`, WAV 48 kHz S16 stereo. Original
baseline/capacity used 1280×800; matched idle used 1280×768. Duplex used one CPU
and `hda-duplex` for playback only; its unavailable ADC warning establishes no
recording support. The absent-device run used one CPU without HDA.
Callback-driven compatibility mode is unqualified.

The manually launched baseline command, with matching worktree artifacts:

```sh
cp /usr/share/OVMF/OVMF_VARS.fd build/audio-task2-baseline-vars.fd
qemu-system-x86_64 -name audio-task2-baseline -machine q35 -accel kvm -cpu max -smp 4 -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=build/audio-task2-baseline-vars.fd \
  -display none -serial file:build/audio-task2-baseline-serial.txt -monitor stdio \
  -pidfile build/audio-task2-baseline.pid -gdb tcp:127.0.0.1:1243 \
  -drive if=none,id=audio_cd,format=raw,media=cdrom,readonly=on,file=build/pyxis.iso \
  -device virtio-scsi-pci,id=audio_scsi,disable-legacy=on \
  -device scsi-cd,bus=audio_scsi.0,drive=audio_cd,bootindex=1 \
  -object rng-random,id=audio_rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=audio_rng,disable-legacy=on \
  -device virtio-net-pci,netdev=audio_net,disable-legacy=on \
  -netdev user,id=audio_net,hostfwd=tcp:127.0.0.1:2351-10.0.2.15:2323 \
  -audiodev wav,id=audio,path=build/audio-task2-baseline.wav,out.frequency=48000,out.channels=2,out.format=s16 \
  -device intel-hda,id=hda -device hda-output,bus=hda.0,audiodev=audio
```

## No-playback observations

Original baseline: codec `1af40012`, pin 3 → DAC 2, 22 commands/responses,
live untimed parked worker, command/stream DMA and BME off (`PCI command=0x402`),
20 KiB DMA retained, WAV zero frames.

Presenter timing used eight manually entered hardware-breakpoint/finish pairs
around `space_present`, matching ELF, `set may-call-functions off` and 10 ns HPET
ticks, following the [timing method](../../screenshot-qualification.md#matched-presenter-cost).
Separate debugger-free CPU windows used monotonic host time, sequential `/proc`
snapshots and `CLK_TCK=100`. Percentages mean one host CPU, including QEMU/KVM
overhead; presenter elapsed includes preemption, device waits and host variation.

| Observation | Original baseline | Matched control | Matched after |
| --- | ---: | ---: | ---: |
| Accounting window | 157.492567112 s | 76.922345279 s | 132.721176347 s |
| QEMU process CPU | 19.4485% | 19.565186% | 19.032381% |
| Presenter median | 1.162955 ms | 0.742190 ms | 0.703230 ms |
| Presenter range | 0.745350–1.293590 ms | 0.658790–0.893530 ms | 0.613040–0.793690 ms |

Original BSP/AP costs: 9.1560% / 3.2192%, 4.2605%, 2.7938%. No owned build,
second VM, debugger or remote client ran during that original CPU window.
Matched runs used identical initrd
`28039e4d3283eb5702274c973709b575c54b5c11e0aef00a19f085e11c20138a`
and stopped transport/stream with parked workers. An unrelated pointer VM and
nested-host scheduling remained later noise sources. Single windows establish
neither a stable improvement nor a regression threshold; idle cost does not
predict eight-session BSP cost or native performance.

## Manual playback and failures

Evidence came from interactive boots, terminal commands, read-only debugger
inspection and offline WAV analysis. No tests, fault injection, boot/output
automation or new benchmark infrastructure was added. Large WAVs and raw records
remain local recovery evidence; this report retains the summary.

| Run | Observed behavior | Maximum commit |
| --- | --- | ---: |
| First four-CPU playback | 240,000 independent left/right PCM frames exact; normal start/stop, empty sessions/FIFO, parked worker | 340,040 ns |
| Clean one-CPU duplex | 250 ms pauses, two repetitions/150 ms gap, Ctrl+C and fresh acquisition; 389,824 nonzero frames exact, six starts/stops, zero reported discontinuities | 483,350 ns |
| Eight-session capacity | Eight generation-1 acquisitions, ninth CALL_LIMIT (11); hidden-space playback and saturated mix, then terminal failure | 803,090 ns |
| Compatibility integration | First command and repeated run 1 complete; repeated run 2 fails at 20 ms guard | 815,270 ns |

Unprimed startup emitted **1024 tone frames / 21.333 ms, then 2816 zero frames /
58.667 ms**, then remaining PCM. Later preparation ordering changed, but capacity
and all five compatibility-integration RUN epochs retained this gap. Exact PCM
totals do not establish gap-free startup. Ctrl+C discarded queued PCM.

At `8fa7e8d9`, a two-minute producer completed without reported discontinuity;
acquisition in an already owned space returned BUSY (9). A later refill fault was
associated with a monitor capture; causation is unknown. It retained DMA, left the
shell responsive and later acquisition returned UNAVAILABLE (6). A separate
four-vCPU single-producer CPU window: 56.487179845 s, QEMU 72.9369%.

Capacity matched every eight-source sum/final-clip sample for **591,872 frames /
12.330667 s**, frames 1,359,360–1,951,231 inclusive, accounting for the first
source's startup phase. Left reached −32768/32767, with 123,305 negative and
123,310 positive clipped samples; right reached ±27306 without clipping. Later
starvation changed phase; no later match is claimed. Its 53.523698770 s CPU
window measured QEMU 132.6889%, BSP 99.1150%, APs 10.3132%, 13.6949%, 6.5765%.
An unrelated pointer VM ran concurrently; unsynchronized clocks leave exact
failure placement within this window unproven.

Eight-session progress exceeded the 20 ms horizon **before** screenshots, debugger
inspection or Ctrl+C. All producers and fresh acquisitions returned UNAVAILABLE.
Shutdown masked IRQs, reset stream/link, stopped CORB/RIRB and cleared BME,
retaining DMA; slots/FIFO emptied and worker parked. This does not qualify
sustained eight-session refill, normal release or recovery without reboot.

At compatibility integration, `pcm 1000 500 5` completed 240,000 frames in
4.885213970 s (235 writes, 229 full/writable waits, starvation 2, discontinuity 0).
`pcm --pause-ms 250 --repeat 2 --gap-ms 150 1000 500 2` completed its first
96,000-frame run in 2.051498010 s (94 writes, 86 full/waits, starvation 4,
discontinuity 0). Run 2 acquired generation 5 and resumed after its pause, then
failed on an expired completion notification at the 20 ms guard. No screenshot
or debugger attachment occurred during playback; attachment followed failure.

Every nonzero captured frame matched its channel function: **386,962 PCM frames**
across ten regions (240,000 first-command, 96,000 repeated-run-1, 50,962 partial
run-2). WAV: 412,787 total frames, SHA-256
`5deafb9a4abe45b3b4a2f5f64f310bb724494cf4750d3dbdd9ead2ab8c675861`.
Agreement does not undo failure or prove absolute progress. Afterward: five starts,
four normal stops, 863 IRQs, 868 refills; file-qualified audio availability false,
empty slots/FIFO, parked worker, BME off. Fresh acquisition returned UNAVAILABLE;
shell echo worked. Unqualified GDB `available` resolved another static symbol
and was not audio evidence.

Absent-controller boot stayed quiet: no audio/HDA serial line, ACQUIRE UNAVAILABLE,
shell echo successful, parked worker, empty slots, `prepared=false`, no controller
claim or DMA allocation.

## Delivery limits

The parent pins published userland
[#168](https://git.internal/PyxisOS/pyxis-userland/pulls/168),
`052ac5ea0c1b045924aee439470f66e7f3382c1c`; merge userland before the parent.
Userland has no standalone Actions tasks; empty/unparsable status is not a pass.
Parent CI must be checked at the exact submitted revision. No compiler-container
rebuild is needed. Manual refused-authority/malformed-call qualification was not
added. All historical task-owned qualification guests, clients and debuggers stopped.

The accepted BSP follow-up needs matched one/eight measurements and sustained
eight-session qualification. Native AMD binding, speaker/headphone/jack behavior,
latency and refill qualification remain open; eight periods remain starting
tuning. SDL2, Quake and later native tasks are separately assigned. Milestone
closure needs the owner's QEMU/ThinkPad qualification decision.

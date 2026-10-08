# Audio task 2: sessions, mixing and IRQ refill

Owner assignment **2026-10-08**, after [#553](https://git.internal/PyxisOS/pyxis-os/pulls/553)
merged. Task branch **`audio/sessions-mixer`** starts from fresh main
**`780f5d22dce25aaa79fe9a0d842db6cd9e9883f3`**. This assignment combines the
[periodic refill and session/mixing steps](../../../wip/hda-playback.md#proposed-task-sequence-and-review-gates)
of the accepted milestone. The private engine is already merged; this task adds
its public per-space grant, at most eight process-owned sessions, copied queues,
kernel mixing and owned interrupt delivery. SDL2, Quake and native AMD binding
remain separately assigned work. Public sessions, mixing and IRQ refill are now
implemented; qualification and remaining delivery checks are recorded below.

## Accepted task-specific policies

Accepted by the owner through the orchestrator on **2026-10-08**, before
implementation. Hidden-space playback also carries forward the proposal policy.

1. Continue playback in hidden spaces independently of keyboard/pointer focus.
   Provision a named audio grant alongside each space's display/keyboard grants;
   shell delegation is independent of stdin/input focus. Initial controls remain
   PCM/session-only, without volume or pause UI.
2. Start on the first queued frames without a 20 ms priming threshold. Stop
   hardware after queued and mixed PCM is consumed, then park without idle IRQs.
   Preserve acquired sessions for subsequent writes and count producer starvation
   once per empty episode. The public interface still makes no audible-drain promise;
   internal stopping must account for the codec/backend tail and be qualified.
3. Fail closed until reboot when refill progress is ambiguous or unsafe, or a
   FIFO/descriptor fault occurs: stop the engine, report WAIT_ERROR and
   CALL_UNAVAILABLE, and retain DMA. Ordinary producer starvation supplies zeros
   and is not terminal.

The already accepted ACQUIRE/WRITE/STATUS/RELEASE, atomic copied writes of at
most 4096 bytes, maximum-write writable waits and admission errors remain the
[session contract](../../../wip/hda-playback.md#accepted-session-call-contract).
No new call semantics, resampler or consumer backend is proposed here.

## No-playback baseline

Captured before task 2 implementation from clean main **`780f5d2`**, with the
merged task 1 engine prepared and parked. Ordinary `make -j16 image` passed in
the existing LLVM 23.1.3/49e2c1a builder, rebuilding the pinned sources. The
streaming-era userspace pin is **`a5a48b4b1e1cbff8131915244dbfb6018729789f`**;
older task 1 bundles were not substituted. [Kernel](baseline-kernel.txt),
[SDK](baseline-sdk.txt), [userspace](baseline-userspace.txt),
[ports](baseline-ports.txt) and [configuration](baseline-kernel.config) record
provenance. Existing vendor build warnings are not a kernel warning-free claim.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `561e0df274bd422a66b2264cbe353d719d3cad78b08c240a0563c15137d02a64` |
| ISO | `9fe9346e1490e33447628bcb3c5d3540616bf01864ac5c2e5d9f2beff86f0cb8` |
| Initrd | `acc72d85e0bdebe6e7336065f4eaf64ec233908c836163c07408c9a1337c25fb` |

Stock QEMU 10.2.2, Q35, `-cpu max`, four cores/one thread each, 8 GiB,
**nested KVM**, fresh OVMF variables and default VGA 1280×800. Static Caelum
TTY/cursor selected; three user spaces and network ready. Modern VirtIO SCSI
read-only CD-ROM, NIC and RNG; `intel-hda` plus `hda-output` with a 48 kHz S16
stereo WAV backend. No remote client during the idle cost window. The command
shape follows the [investigation](../audio-investigation/README.md#guest-configuration-and-commands),
with matching task 2 ELF/ISO, local filenames and a host accounting pidfile.
The manually launched baseline command, from the baseline worktree:

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

The [serial record](baseline-serial.txt) has the established single
`audio: ready ...; output idle` summary. [GDB](baseline-presenter-gdb.txt)
confirms 22 commands/responses, discovered codec `1af40012`, pin 3 → DAC 2,
command/stream DMA off, PCI command `0x402` (BME clear, INTx disabled), retained
20 KiB DMA backing and a live untimed parked worker. The WAV contains zero
frames. No native hardware was accessed.

### Presenter elapsed observations

Eight individually entered hardware-breakpoint/`finish` pairs around
`space_present`, matching ELF, `set may-call-functions off`, HPET counter
`0xfffffe80402020f0`, 10 ns period. GDB confirmed Caelum and 1280×800. This follows
the [existing timing method](../../screenshot-qualification.md#matched-presenter-cost)
and adds no guest instrumentation, inferior calls or guest state edits.

HPET ticks: **121997, 124578, 129359, 88096, 96148, 74535, 115815, 116776**.
Median **1.162955 ms**, range **0.745350–1.293590 ms**. These are debugger-qualified
elapsed observations including preemption/device waits and nested-host variation,
not isolated CPU time or native performance.

### Host cost with idle guest

Separate debugger-free window: two manual process/thread `/proc` stat snapshots,
[start](baseline-cpu-start.json) and [end](baseline-cpu-end.json), with monotonic
timestamps and `CLK_TCK=100`. Monitor `info cpus` mapped CPU0/1/2/3 to host
threads **462225/462226/462227/462229**. Elapsed **157.492567112 s**; QEMU process
CPU **30.63 s**, or **19.4485% of one host CPU**.

| Guest vCPU thread | Host CPU seconds | Percent of one host CPU |
| --- | ---: | ---: |
| CPU0 / BSP | 14.42 | 9.1560% |
| CPU1 | 5.07 | 3.2192% |
| CPU2 | 6.71 | 4.2605% |
| CPU3 | 4.40 | 2.7938% |

This measures nested KVM execution/exits and QEMU overhead, not guest-idle
percentages. Thread reads are sequential and jiffy-rounded. No owned build,
second VM, debugger or remote client ran during this window. A single window
is not a stable regression threshold. Repeat matched no-playback observations
with the same scene/configuration and report actual elapsed windows; measure
active playback and eight-session cost separately. Check ABI/bundle inputs
before reuse when this task introduces public headers; do not bypass bundle
validation to force a kernel-only comparison.

## Implemented behavior

The [public session reference](../../../interfaces/audio.md) records exact reply
layouts, validation order, authority, lifetime and readiness. The
[engine reference](../../../devices/hda.md) records BSP ownership, single-message
MSI, immutable BDL, observation/headroom guards and retained failure ownership.
The worker mixes signed 32-bit sums and clips once to S16. A session owns copied
PCM, not DMA; release/exit invalidates its generation and discards pending PCM.
Hidden spaces continue playing. Hardware starts with the first queued frames,
then stops and parks after source depletion and its internal zero-tail handling.
Producer starvation is separate from terminal hardware failure.

The independently published userspace dependency is
[#168](https://git.internal/PyxisOS/pyxis-userland/pulls/168), branch
`audio/session-producer`, head **`052ac5ea0c1b045924aee439470f66e7f3382c1c`**.
It provides libpyxis helpers, optional grant forwarding independent of input
focus and the ordinary native `pcm` producer. It is published before the parent
pin; merge userland before the parent implementation. Existing build/CI evidence
must be checked for the submitted dependency revision, rather than inferred from
an earlier successful build.

## Accepted DMA tuning and progress limits

The owner accepted **eight 10 ms hardware periods (80 ms)** on **2026-10-08**,
keeping the **80 ms session queue** and **4096-byte atomic WRITE** unchanged.
The payload grows from 7680 to 15,360 bytes. This is starting native tuning to
revisit during latency/refill qualification.

Stock QEMU 10.2.2's timer-driven codec can request **8192 bytes**, exceeding the
original **7680-byte / 40 ms** ring. Controller transfer walks at most the
configured descriptor count; a whole lap can leave LPIB unchanged with coalesced
BCIS. The codec adjusts or resets its timer origin, so WALCLK since RUN is not
an absolute byte counter. A small new observation gap cannot exclude catch-up
from an earlier lag. The 8192-byte bound is a **source assessment**, not a dynamic
burst measurement or a native hardware result. See QEMU's
[codec timer](https://gitlab.com/qemu-project/qemu/-/blob/v10.2.2/hw/audio/hda-codec.c)
and [controller transfer](https://gitlab.com/qemu-project/qemu/-/blob/v10.2.2/hw/audio/intel-hda.c).
Callback-driven compatibility mode is not qualified.

The implementation refuses stale or ambiguous observations, checks fresh position
and more than 8192 bytes plus one frame of headroom before committing a reclaimed
period, and rejects a commit of 1 ms or more. IRQ hints plus a running-only 5 ms
watchdog supply observations; a 20 ms observation/progress horizon fails closed.
These checks and the larger ring reduce exposure; modulo LPIB and adaptive WALCLK
still do not prove absolute progress. Multiple catch-up callbacks can hide whole
laps, and a post-copy check cannot undo PCM consumed during a host stall or racing
DMA. No hard real-time, uninterrupted cyclic-output or audible-drain guarantee
follows from this qualification.

## Manual QEMU qualification

These are ordinary interactive boots, terminal commands, read-only debugger
inspection and offline captured-WAV inspection. No tests, self-tests, fault
injection, boot/output automation, new benchmark script or CI workflow was added.
Large WAVs and screenshots stay as local evidence; compact records below retain
hashes, output measurements, process accounting and debugger state.

### First output and one-CPU duplex

The first public producer run used the working tree subsequently committed as
**`e004fc9539f96f3ac1e31d2253fcf1bea9408869`** and userspace **`97770cd`**.
Its [retained provenance and runtime record](first-playback.txt) records an earlier
HEAD plus a modified-tree digest; this is not presented as a clean e004 build.
QEMU `hda-output`, four CPUs, Q35, nested KVM and 8 GiB captured all **240,000**
requested independent left/right PCM frames exactly. One normal start/stop left
all session slots empty, the FIFO empty, worker parked and stream stopped.
The observed maximum IF=0 mix/commit interval was **340,040 ns**.

Unprimed startup produced **1024 frames / 21.333 ms of tone, then 2816 zero
frames / 58.667 ms**, then the remaining tone. Exact total PCM does not mean
zero-gap startup. Later startup preparation ordering was corrected before the
capacity run; that record independently retains the same measured initial gap.

The clean e004 [one-CPU `hda-duplex` run](duplex-qualification.txt), with the
same format and 8 GiB, exercised ordinary 250 ms producer pauses, two repetitions
with a 150 ms gap, Ctrl+C process exit and fresh acquisition. Six starts and six
normal stops captured **389,824 nonzero frames**, each matching its channel's
triangle function exactly. This includes complete repeated and reacquired runs
and a partial Ctrl+C run; exit discarded its queued PCM. All reported hardware
discontinuities were zero. Maximum commit was **483,350 ns**; final command DMA,
RUN, interrupt delivery and PCI BME were off, all slots/FIFO empty and the worker
parked. `hda-duplex` qualifies playback only; its unavailable ADC backend warning
is expected and establishes no recording support.

### Later producer and fail-closed observation

At clean **`8fa7e8d9ca6077ef7b7fa04ba600b9f879c7fc3d`**, with userspace
**`052ac5e`**, an ordinary two-minute producer completed without a reported
hardware discontinuity. The [terminal record](single-producer.txt) also records
**BUSY (9)** for simultaneous acquisition of an already owned space, Ctrl+C
cleanup and later **UNAVAILABLE (6)** after failure. A generic refill fault was
observed in association with an actual monitor capture during a later run; the
causal relation is unknown. It failed closed, retained DMA and left the shell
responsive. This is an observed guard outcome, not an injected failure test.

A separate [steady single-producer accounting window](single-producer-cpu.json)
lasted **56.487179845 s**, with process CPU **72.9369% of one host CPU**.
This was one producer in the four-vCPU guest, not a one-vCPU configuration.
Sequential `/proc` snapshots are jiffy-rounded nested-host CPU consumption,
including QEMU/KVM overhead; they are not native playback measurements.

### Eight-session admission and saturated mix

The [capacity record](capacity-qualification.txt) used frozen scene revision
**`672fe6e0`**, whose kernel matches task branch **`0070d33f`** except build
identity, and userspace **`052ac5e`** with only an ordinary local space layout
change. Stock QEMU 10.2.2, Q35, nested KVM, four CPUs and 8 GiB ran eight manually
started producers. All eight acquired generation 1; a ninth space received
**CALL_LIMIT (11)**. This establishes bounded admission and playback from hidden
spaces while other spaces were selected.

The [captured sample analysis](capacity-wave-analysis.json) matches **every
sample** of the signed 32-bit sum followed by final S16 clipping over frames
**1,359,360 through 1,951,231 inclusive: 591,872 frames / 12.330667 s**.
The eight-source formula accounts for the first producer's measured startup
phase offset. Left output reaches **−32768/32767**, with 123,305 negative and
123,310 positive clipped samples; right output reaches **±27306**, without
clipping. Later producer starvation changes phase, so later periodic output is
not claimed to match that original eight-source pattern.

The [CPU window](capacity-cpu.json) lasted **53.523698770 s**: QEMU process
**132.6889% of one host CPU**, BSP **99.1150%**, APs **10.3132%, 13.6949%,
6.5765%**. An unrelated pointer-task VM ran concurrently. This is a costly
nested-host workload observation, not a physical-host latency or capacity
promise. Guest and host clock origins were not synchronized; the exact failure
placement within this accounting window versus the brief end-to-stop gap is
unproven. The window belongs to an eight-admitted run that subsequently failed;
it is not certified failure-free.

The engine exceeded its **20 ms service horizon before screenshots, debugger
inspection or local Ctrl+C**. All eight producers had returned UNAVAILABLE; fresh
acquisition also returned UNAVAILABLE. Shutdown confirmed IRQ masking, stream
reset, CORB/RIRB stop, link reset and BME off, with DMA retained. The post-failure
worker remained parked/serviceable, all eight slots and request FIFO were empty.
Maximum recorded commit was **803,090 ns**. This run does **not** establish
failure-free sustained eight-session refill, successful normal release or
post-failure reacquisition. Recovery requires reboot.

## Matched no-playback observations

The original `780f5d2` / `a5a48b4` baseline above remains historical. It is not
used as the direct comparison for later images with a changed SDK/userland.
A new control **`dac5e2e9`** on main **`01791631`** adds matching audio SDK
declarations and the same **`052ac5e`** userspace pin, while retaining the parked
task 1 engine. The after image is **`8fa7e8d9`**. Both used identical initrd
SHA-256 **`28039e4d3283eb5702274c973709b575c54b5c11e0aef00a19f085e11c20138a`**.

Both runs used stock QEMU 10.2.2, Q35, nested KVM, `-cpu max`, four CPUs,
8 GiB, fresh OVMF variables, static Caelum at **1280×768**, `hda-output` and
48 kHz S16 stereo WAV. The [GDB records](matched-idle-gdb.txt) confirm parked
workers, stopped stream/command transport and matching scene; CPU windows were
separate from debugger observations and had no remote client. The unrelated
pointer VM and nested-host scheduling remain uncontrolled noise sources.

| Observation | Matched control | After |
| --- | ---: | ---: |
| Idle accounting window | 76.922345279 s | 132.721176347 s |
| QEMU CPU, percent of one host CPU | 19.565186% | 19.032381% |
| Presenter median, eight observations | 0.742190 ms | 0.703230 ms |
| Presenter range | 0.658790–0.893530 ms | 0.613040–0.793690 ms |

[CPU snapshots](matched-idle-cpu.json) use monotonic host time and `CLK_TCK=100`.
Presenter observations use the same manually entered hardware-breakpoint/finish
method and 10 ns HPET ticks as the original baseline. These are single windows
and debugger-qualified elapsed observations; they establish neither a stable
improvement nor a regression threshold. An IRQ-driven parked engine's low idle
cost does not predict the eight-session BSP cost.

## Current integration and delivery status

Main Bluetooth changes through **`114f2acb`** were integrated at
**`0e3c266efd9ed9421c1a26ace90b2a486cb457e8`**. Audio now uses protocol tag
**45** and object type **47**, with in-tree consumers updated. Measurements above
belong to their named older revisions and are not exact-head evidence for this
compatibility integration.

A full source-built ordinary image passed at that compatibility integration,
with userspace **`052ac5e`** and matching SDK. Artifact SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `01f3455065a0f5f354187abdd0fee06a99ba7125c79bbef1b92dd8d0d3b2a365` |
| ISO | `2e06028c6a36d5719d96c9808658e5df8cc31b8bcc891e8e85c5a1db78a01ad5` |
| Initrd | `e32737615ecea925bb6bf460261cb7ec352b7ca1c49f2c8d3de113959de8473e` |

The [current four-CPU `hda-output` record](current-qualification.txt) completes
`pcm 1000 500 5`: 240,000 frames, 235 writes, 229 full/writable waits,
4.885213970 s producer time, starvation 2 and discontinuity 0. A subsequent
`pcm --pause-ms 250 --repeat 2 --gap-ms 150 1000 500 2` completed its first
96,000-frame run (2.051498010 s, 94 writes, 86 full/waits, starvation 4,
discontinuity 0). The second run acquired generation 5 and resumed after its
pause, then failed closed on an **expired completion notification at the 20 ms
guard**. No debugger attachment or screenshot occurred during playback; GDB was
started and loaded symbols, but attached only after the failure. This is an ordinary
nested-QEMU failure; the current image did not complete both repetitions.
Historical duplex repetitions above remain evidence for their named revision.

The [independent current WAV inspection](current-wave-analysis.json) matches
**every nonzero captured stereo frame** to the producer functions: **386,962**
PCM frames across ten regions, comprising 240,000 first-command frames, 96,000
complete repeated-run-1 frames and 50,962 partial repeated-run-2 frames. Five RUN
epochs each retain the 1024-tone/2816-zero-frame startup gap. The WAV has 412,787
total frames and hash `5deafb9a4abe45b3b4a2f5f64f310bb724494cf4750d3dbdd9ead2ab8c675861`.
Waveform agreement does not undo the terminal failure or prove absolute progress.

After failure: starts 5, normal stops 4, IRQ 863, refills 868 and maximum commit
815,270 ns. File-qualified audio availability was false; the session slots and
FIFO were empty, worker parked and BME off. Fresh acquisition returned
UNAVAILABLE and a shell echo completed. The retained unqualified GDB expression
`available` resolves another static symbol; it is not audio availability evidence.

The [absent-controller check](absent-qualification.txt) used the same ELF/ISO,
one CPU and no HDA device. Serial had no audio/HDA line; a one-second producer
returned ACQUIRE UNAVAILABLE and shell echo succeeded. Read-only debugger
inspection confirmed audio unavailable, a live parked worker, empty slots, no
controller claim/DMA allocation and `prepared=false`. Absence stays quiet.

Delivery: parent [#557](https://git.internal/PyxisOS/pyxis-os/pulls/557) pins the
published userspace [#168](https://git.internal/PyxisOS/pyxis-userland/pulls/168)
revision above. Merge userland first, then the parent. The parent ordinary source
build compiles that dependency against its matching SDK; userspace has no
standalone Actions tasks, and its empty/unparsable status is not a CI pass.
The parent PR records existing CI for its exact submitted revision. No compiler
container rebuild is needed. Source review of request/exit/readiness/grant and
Bluetooth integration found no blocking issue; manual refused-authority or
malformed-call qualification was not added.

All task-owned qualification guests, clients and debuggers were stopped.
Native AMD binding, physical speaker/headphone/jack behavior, latency and refill
qualification remain open. Eight periods remain starting tuning. SDL2, Quake and
later native tasks do not start from this task; milestone closure still requires
the owner's confirmation of QEMU closure with a later ThinkPad batch or native
qualification first.

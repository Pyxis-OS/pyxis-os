# SDL audio callbacks qualification

Threads task 9, second SDL step: [audio output](../../sdl2.md#audio) through the
space's native PCM session. The owner accepted the contract on 2026-10-13.
These are manual nested-QEMU results; native qualification will be the owner's
ThinkPad check. There are no kernel, ABI, SDK or libc changes.

## Revisions and configuration

- Matched pair: Pyxis main `6bc3a009` (ports `9f40774`, userland `b38b0e0`),
  builder `pyxis-llvm23.1.3-4c23a20`, SDL 2.32.10 `5d249570`.
  - Baseline: unchanged ports.
  - Changed: this task's ports change on the same base.
  - Both images share the kernel (`caelum.elf` SHA-256 prefix `34c6b03d`).
- An earlier baseline at main `32e0a054` (ports `c09023d`) gave the same
  refusals. It provided the native `pcm` reference before main moved.
- QEMU 10.2.2 with the local AHCI fix, nested KVM in Claude's Fedora VM, Q35,
  `-cpu max`, 2 GiB, standard VGA at 1280x800, `intel-hda` + `hda-output`
  writing a 48 kHz S16 stereo WAV, VirtIO RNG, no NIC or disk,
  `-display none`. Input arrived through QMP. One and four CPUs.

Workloads were staged as `.pxb` bundles declaring `memory`, `clock`, `threads`,
`pipe`, `display`, `keyboard`, optional `pointer` and `audio`, so baseline and
changed images grant the same authority:

- a local `sdl-audio` probe: triangle tones through an SDL callback, callback
  intervals, the native STATUS counters read before close, close time, a
  second-open attempt and an optional one-second pause;
- `cquake`: the ordinary Chocolate Quake executable wrapped with those grants,
  running `+timedemo demo1` (969 frames);
- the native `pcm` tool as the reference producer.

The probe, bundles, images and WAV files stay local. No test recipe, library
or CI change was added.

## Before

`SDL_INIT_AUDIO` failed with "SDL not built with audio support". Chocolate Quake
logged "Couldn't init SDL audio" and ran silent. Native `pcm 1000 500 10`
produced the same results on one and four CPUs:

- 469 writes, 465 queue-full retries, two starvation episodes, no
  discontinuities;
- a continuous WAV apart from the documented 58.7 ms startup gap at 21 ms.

## Probe results (changed image)

| Run | CPUs | Callbacks | Interval mean (min–max) ms | Before close | Close |
| --- | --- | --- | --- | --- | --- |
| 48 kHz, 1,024 samples, 10 s | 4 | 473 | 21.209 (1.288–32.392) | free 448/3840, starvations 1, state READY | 62.457 ms |
| 48 kHz, 1,024 samples, 10 s | 1 | 473 | 21.210 (1.554–33.648) | free 448/3840, starvations 1, READY | 59.948 ms |
| 44.1 kHz, 512 samples, 10 s | 4 | 870 | 11.510 (0.052–31.910) | free 448/3840, starvations 1, READY | 62.953 ms |
| 44.1 kHz, 512 samples, 10 s | 1 | 868 | 11.516 (0.054–34.278) | free 992/3840, starvations 1, READY | 52.220 ms |

- **48 kHz runs.** The nominal callback interval is 21.333 ms, and no interval
  exceeded twice nominal. The queue stayed near full, as accepted.
- **44.1 kHz runs.** These used SDL's conversion stream. Callbacks run in
  irregular bursts to fill each 1,024-frame device buffer, so the probe's
  "more than twice nominal" count (62–64) reflects batching, not lateness.
- **Starvation.** The single episode in each run is counted at stream start;
  the captured WAV has no gap of 1 ms or more inside any SDL run.
- **Close time.** It includes SDL's two-buffer drain delay.
- **Second open.** Every run reported "Audio device already open": SDL's core
  refuses it before the native BUSY check.
- **Pause** (6 s, one CPU and four): the callback count stayed at 144 for the
  one-second pause, then resumed. Starvations stayed at 1, because SDL writes
  silence while paused.
- **Plain launch** (no threads grant): the open failed with "Couldn't create
  audio thread". A following `pcm` acquired the session, so it had been
  released. Plain `chocolate-quake` logs "Couldn't open SDL audio: Couldn't
  create audio thread" and plays silently.

Whole-QEMU host CPU over 6 s during playback, on the same image and boot:

| CPUs | `sdl-audio` 48 kHz | Native `pcm` | `sdl-audio` 44.1 kHz | Paused SDL |
| --- | --- | --- | --- | --- |
| 4 | 35.32% | 36.65% | 46.48%¹ | 31.65%¹ |
| 1 | 29.31% | 31.15% | 29.15% | 21.32% |

¹ Different boot from the 4-CPU 48 kHz/`pcm` pair. The SDL path costs no more
than the native producer here.

The four-CPU 48 kHz WAV contained 10.080 s of tone with no gap of 1 ms or more.
QEMU records only while the HDA stream runs, so idle time between runs is
absent from the files.

## Chocolate Quake

`+timedemo demo1` frames per second, all runs recorded and median. Runs that
failed the HDA engine were repeated after a reboot and are excluded, along with
one run whose failure was missed by an earlier check (below).

| Configuration | 4 CPUs | 1 CPU |
| --- | --- | --- |
| Baseline (no SDL audio) | 432.6, 457.2, 468.3, 473.5, 499.9 → **468.3** | 282.5, 283.7, 402.6, 408.8, 425.6 → **402.6** |
| Changed, `-nosound` | 464.0, 506.3, 509.0, 513.2, 538.1 → **509.0** | 429.1, 436.0, 436.1, 443.8, 448.3 → **436.1** |
| Changed, with sound | 371.0, 404.8, 412.2, 424.3, 428.7, 453.9, 454.4, 455.3, 462.6 → **428.7** | 363.5, 376.6, 379.8, 399.9, 409.6 → **379.8** |

- **No regression without sound.** Without sound, the changed image is not
  slower than the baseline.
- **Cost of sound.** With sound, the uncapped timedemo ran about 16% (four
  CPUs) and 13% (one CPU) below `-nosound`. This includes Quake's own mixing at
  44.1 kHz, SDL's conversion to 48 kHz, the callback thread and native writes.
  It is not attributed further.
- **Driver.** Quake reported "SDL audio driver: pyxis" with a 44.1 kHz,
  1,024-sample spec.
- **Captured output.** A separate four-CPU run captured 24.5 s of stream with
  game sound present. Its peak was 668 because the boot master volume is 50%.

## Nested-QEMU engine failures

Starting a stream often tripped the HDA engine's existing QEMU guards, which
disable audio until reboot. Each boot's serial log names the guard:

- "output DMA commit exceeded clock limit" (the 1 ms commit bound);
- in two later four-CPU Quake starts, "output completion notification
  expired".

Fresh-boot first starts, counting only the commit-limit message:

| Boots | SDL first | Native `pcm` first |
| --- | --- | --- |
| Four CPUs, alternating | 4 of 6 | 4 of 6 |
| One CPU, alternating | 2 of 3 | 2 of 3 |
| Four CPUs, earlier ad hoc | 3 of 4 | 2 of 4 |
| Four CPUs, earlier kernel (`32e0a054`) | — | 1 of 6 |

The native producer trips as often as SDL. The guard window holds two device
register reads around the period mix; each read exits to QEMU. That is
consistent with the recorded host-stall explanation, not with anything the SDL
path does. Trips were far more frequent than during this morning's baseline
boots on the same host; host load could not be observed from the VM.

Early runs checked only the commit-limit message. One Chocolate Quake "with
sound" run started after an unnoticed engine failure, so it opened no audio;
it is excluded above. Later runners checked for any "engine unavailable until
reboot" line. The guard bounds are unchanged, and this remains the
[recorded QEMU limit](https://git.internal/PyxisOS/pyxis-os/issues/1391);
it is not a native result.

## Debugger inspection

GDB was attached to the four-CPU changed image, using a debugger ELF of the
probe linked with `-Wl,--oformat=elf` from the same inputs, with
`may-call-functions off`:

- **Callback thread.** A hardware breakpoint at `PYXIS_WaitDevice` stopped in
  that thread (`fs_base` `0x117000`), called from `SDL_RunAudio` and
  `SDL_RunThread`.
- **Close.** A breakpoint at `PYXIS_CloseDevice`, conditioned on the same CR3,
  then stopped in the main thread (`fs_base` `0x4000`), called from
  `close_audio_device` and `main`. Libc's record for the callback thread was
  already `THREAD_FREE`, with its stack, TLS context and completion observer
  released. The thread was joined and collected before the session release.

## Limits

These are nested-VM timings and engine behaviour. Output latency is calculated,
not measured, and its tuning is [deferred](https://git.internal/PyxisOS/pyxis-os/issues/1397)
until the native check. Audible quality was not judged by listening; WAV files
were checked for gaps and content only. Game recipes are unchanged, so ordinary
images stay silent; a Chocolate Quake bundle with sound is a later follow-up.

# Raylib audio bridge qualification

Raylib 6.2/rlsw, custom miniaudio backend over SDL2 2.32.10, builder
`pyxis-llvm23.1.3-3d69e83`. The parent is based on task-2 `85e2a804`,
userland `6d8fd881`, SDL geometry-reuse `024b262`; this task changes no kernel,
SDK ABI or libc. The pinned cache archive and ordered audio patch are in the
port metadata. Raw probes, output and captures stay at
`/shared/present/raylib/task3/`.

## Builds and behavior

The ordinary selected recipe and a forced whole-archive link with section
collection disabled passed against the real SDK. No additional standard libc
operation was needed. The selected decoder is WAV (dr_wav, MIT-0); miniaudio
uses its MIT-0 alternative. Generated Sound and AudioStream are included;
other codecs remain disabled, with no broader format claim.

Four-CPU Q35 nested-KVM QEMU, `-cpu max`, 4 GiB, ordinary Bochs 1920x1080,
VirtIO RNG/network, `intel-hda`/`hda-output` and a WAV sink:

- Eight-second float-stereo AudioStream callback at 48 kHz ran, reported ready
  and closed cleanly. A generated quiet 440 Hz signal used amplitude 0.02.
- A 22.05 kHz mono S16 WAV loaded through app://, converted to the mixer format,
  played, stopped/unloaded and closed; immediate reopen and close passed.
- The stream and WAV/close/reopen checks also passed with one CPU.
- A four-CPU boot with no HDA device also refused initialization.
- Missing audio authority refused context initialization. Missing threads
  refused device initialization. Both returned failure; a subsequent ordinary
  WAV/open/close/reopen succeeded, establishing observed session reuse.
- The custom backend is callback-only: SDL owns the joinable worker. Source
  review confirmed join before converter/mixer destruction, cleanup of the
  initialization mutex/events, and no null-backend fallback. Allocation failure
  was inspected, not injected; these are not a heap-leak proof.

QEMU's default WAV sink records at 44.1 kHz, separately from the 48 kHz guest
session. The combined four-CPU capture had equal stereo channels, peak 19/32768
at boot volume 50%, and 440 positive crossings in a representative second.
A separate one-CPU capture containing only raylib also had equal stereo
channels and 440 crossings/s (peak 13). This establishes captured
tone/conversion, not native listening or a gap-free latency guarantee.

## Matched cost

One boot and unchanged kernel/archive/SDL; alternate the direct-SDL baseline
and raylib AudioStream three times each. Both use the same 48 kHz F32 stereo,
1,024-frame SDL callback, quiet 440 Hz generator and eight-second playback.
Read whole-QEMU `/proc/PID/stat` CPU ticks over five seconds starting one second
into each run. No build ran during these cohorts. An earlier functional cohort
that overlapped compilation is excluded. The direct-SDL first playback was
captured before running the new bridge.

| Whole-QEMU host CPU | Median | Range, 3 samples |
| --- | ---: | ---: |
| Direct SDL callback | 13.40% | 10.20–18.60% |
| Raylib AudioStream → miniaudio → SDL | 14.00% | 10.00–14.60% |

Ranges overlap widely; this does not establish a measurable added cost or an
improvement. These nested results do not predict native cost. Commands use the
ordinary ports runner, a local manually launched .pxb probe and QEMU's WAV
backend; probe sources/captures remain outside the repository.

Native listening, hardware loss, wider codecs and audio latency beyond SDL's
existing contract remain unqualified. Coordinate a quiet native check before
making a hardware claim; start at a low master volume, listen for a clean
440 Hz tone, stop/reopen and confirm normal input/Ethernet remain usable.

## Task-1 review follow-up

After task 1 merged, its three minor follow-ups were folded into this task.
Fresh SDL/raylib recipes stage `sdl2.pc` and `raylib.pc` independently; host
`pkg-config --static` emits canonical `sdk://include`, `sdk://include/SDL2`,
`sdk://lib` and both actual libraries. Overriding `prefix` with a relocated
combined development directory (ending in `/`) compiled/linked a real SDK
consumer using those flags. The native SDK view itself still awaits integration.

Four-CPU QEMU checked existing-file extra-parent behavior against upstream,
directory and missing-file paths, scheme-root boundaries, successful existing
root mkdir, missing-root errno and `ENOTDIR` for a TEXT-backed FILE root.
All passed. No new timing claim; the path lookup added for existing files is
intentional upstream behavior, and the metadata changes affect build discovery.
Raw commands/output stay in `task3/review-followup/` beside the earlier record.


## SDK-view restack (2026-10-23)

Ports `2b6e7de` on drawing backend `913e115c`; kernel/SDK `8bc5af9e`
(userland `6de2b65f`, main `69cceab3` with #1147). The selected audio recipe and
existing manual fixture were rebuilt with that SDK and SDL2 prefix; all three
parent tasks have the same kernel/public-header/runtime inputs. No compiler
rebuild. The shared `.pc` files retain explicit `sdk://include`/`sdk://lib`;
relocated host consumers can override `includedir` and `libdir` directly.

Four-CPU Q35/nested-KVM QEMU, 4 GiB, supported `intel-hda` controller and
`hda-output`, WAV backend. The 8-second float-stereo callback, 22050-Hz mono WAV
conversion and close/reopen passed. Removing declared audio or threads grants
refused initialization; a subsequent ordinary open/stream/close passed
(`ready=1 closed=1`). Captured 44.1-kHz S16 stereo channels matched, peak 13,
about 440 Hz in the first tone window (439.5-Hz crossing estimate). The initial
`ich9-intel-hda` fixture was outside the driver's QEMU controller profile and
correctly refused; it is not positive playback evidence. No native listening,
new timing comparison, fault injection or heap-leak proof. Raw commands/capture
remain at `/shared/present/raylib/sdk-view-20261023/audio/` and its parent.


The final stack check used the development packages built at descendant
`d07b90f1` (ports `cd89600c`, including audio `9d733e0`), on the same kernel/SDK.
Published Clang `785717a1...` compiled the existing audio fixture against the
installed `sdk://` libraries with only `-lraylib -lSDL2`; its declared bundle
passed the stream and WAV/reopen checks again. The final 44.1-kHz capture had
matching stereo channels, peak 13 and the same approximately 440-Hz tone. The
test image additionally admitted `d07b90f1256a`; this is not a committed ABI
promise. The covered private miniaudio adapter is now staged under package
`share/raylib` and included with the public shapes program's sources.


## Native vela and fixture correction (chronium, 2026-10-23)

Full-distribution image `6ea0659991d65f863f630b66aff7c736a0242e3d`,
userland `5bb8336b`, ports `ee3fceb9`, builder
`pyxis-llvm23.1.3-3d69e83`. The initial quiet stream and WAV were inaudible;
the same-boot `pcm 440 220` control was audible. The original stream peak .02
was 21.94 dB below pcm's left peak .25; the WAV peak 399/32768 was 26.25 dB
below 8192/32768. The old QEMU capture peak 13/32768 (-68.03 dBFS) was nonzero.

An app-only comparison matched pcm's 440/220 Hz triangle amplitudes
8192/4096 through SDL S16, SDL F32 and a raylib stream, followed by a mono
440 Hz WAV at peak 8192. It printed read-only worker-confirmed volume/mute
state without changing controls. QEMU captured peaks 250, 249, 171 and 171,
respectively, at master 50%, space 100%, unmuted; raylib's centred panning
accounts for its lower level. These are QEMU sample measurements.

Chronium heard all four checks on vela. The corrected raylib stream and WAV
checks were also clean, stopped normally and passed the close/reopen check.
The earlier apparent silence was fixture attenuation, not a backend defect;
SDL, miniaudio, raudio and kernel code are unchanged. PXE was restored and the
fixture HTTP server stopped. Native listening qualifies these playback paths,
not hardware-loss behavior, other hardware or a latency/performance bound.
Records: `/shared/present/raylib/native-audio-levels-20261023/`.

The corrected [stream fixture](audio-check.c) uses peak .25 and the
[authored WAV generator](tone.py) uses peak 8192, bounded by pcm's existing
left-channel amplitude. They are manual fixtures, not ordinary boot programs:

```sh
make -f docs/development/experiments/raylib-audio/fixture.mk
```

`SDK`, `RAYLIB`, `SDL2` select the existing SDK and development prefixes;
`OUTPUT` selects the temporary bundle directory (default
`build/raylib-audio.pxb`). Run that bundle locally with no argument for the
stream, or `wav` for file playback/reopen. Use moderate volume and send the
`RAYLIB_AUDIO_DONE` or `RAYLIB_WAV_REOPEN_DONE` line plus listening results.

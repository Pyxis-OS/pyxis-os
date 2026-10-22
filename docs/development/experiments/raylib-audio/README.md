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

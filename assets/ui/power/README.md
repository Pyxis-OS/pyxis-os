# Power overlay sound

Original sound made by the Pyxis owner with jsfxr and supplied on 2026-10-19
as `/shared/present/overlay-sound/ctrl-alt-del-button.{wav,sfxr.json}`.
The owner licensed both files under MPL-2.0; see [LICENSE](../../../LICENSE).
The jsfxr JSON is the editable preset; the WAV is the supplied export. Both
files are retained unchanged.

The WAV contains 26,031 unsigned PCM8 mono frames at 44,100 Hz (about 0.590 s).
`scripts/audio-ui-sound.py` uses Python's standard-library WAV reader and
integer linear interpolation to generate `build/kernel/ui_sound.h`: signed
PCM16 stereo at 48,000 Hz, with the mono sample duplicated into both channels.
The output contains 28,334 frames, rounding duration up to a complete frame
and holding the last source sample at the end. Interpolated samples are rounded
to the nearest integer, then a 5 ms linear fade at each endpoint makes the
first and last output frames zero. There is no normalization. The converter
rejects files above 64 KiB or durations above one second before reading sample
data or generating output. The original assets remain authoritative and
unchanged; the faded header is a build artifact.

Source SHA-256:

- WAV: `5acc4984fff930e495d882ffe8ecbeb595f9b0cd60b573c5afa5148dece2da89`.
- jsfxr JSON: `858f346d0b99f1ffaf3846fa8e9e09ff3a0cd5817a3e094d77474453147b9df5`.

# ALC257 headphone stop

Owner baseline, **2026-10-09**: ThinkPad main `11d35fa6`, AMD `1022:15e3`,
Realtek `10ec0257`, headphone pin 33, speaker pin 20. `pcm 1000 500 2` ended
with a very loud pop about 80% of the time on the owner's 300-ohm planar
headphones; the speaker never popped. Master 70–80% was a listening level.
This is an owner observation, not a measured ten-run baseline.

Implementation base `b0a050b780b2cafc79c6d254ccbeb5e37d0d93c8` has the same
HDA/PCM source as that image. Userland remains pinned to
`0c6902898c424276554736a657ca015b505b9887`. Source inspection found an 80 ms
zero ring already preceding stream reset, but no settling between pin mute
and pin/EAPD disable. The generated two-second triangles have zero mean;
their final samples are nonzero. An endpoint click remains possible, while
the headphone analog power transition is the leading hypothesis, not a proven
native cause. Stop does not intentionally enter D3 or resample jack presence.

The [accepted stop sequence](../../../devices/hda.md#codec-and-stream-activation)
adds two interruptible 75 ms worker phases while preserving fully disabled
idle and software gain/ramp/mute. Source review covers worker-only waits,
restart before/after pin disable and exit after activation but before RUN.
The baseline kernel build passed with builder `pyxis-llvm23.1.3-49e2c1a`;
the implementation `make -j16 image` build passed in the same builder.
Build results do not establish an acoustic fix. QEMU has no HDA here; no
playback, capture, native timing or pop reduction has been measured locally.

## Owner native result

On **2026-10-09**, main `11d35fa6` plus #628: no end-of-tone headphone pops
across many runs after the change, against about 80% beforehand. The speaker
never popped. No exact post-fix run count was supplied; this is owner listening
evidence, not a measured electrical transient. Only the planar headphones and
speaker were available. [Volume listening](../../../userland/audio-volume.md#native-qualification)
was comfortable at 70–80% on headphones and 70% on the speaker (80% was loud).

This supports the staged stop fix without proving the analog cause. Rapid
restart overlapping either settle was not separately reported; retain it in the
[next native regression](../../../technical-debt.md#hd-audio-volume-native-regression).

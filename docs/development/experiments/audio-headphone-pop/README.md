# ALC257 headphone stop recheck

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

## Owner native recheck

1. Record the PR revision. Plug in the planar headphones before starting;
   leave the space unmuted at 100%. Set master to **1%, unmuted**. Run
   `pcm 1000 500 2` ten separate times, allowing each stop to finish. Count
   start and end pops separately out of ten, and note their severity.
2. If the low-level check is comfortable, set master to **about 70%** and
   repeat ten runs with the same counts. A very loud analog pop at 1% is
   reason to stop before this level.
3. Unplug headphones before a fresh start. Repeat through the speaker, first
   at 1%, then a comfortable level, counting pops out of ten at each level.
4. Check quick restart with `pcm --repeat 3 --gap-ms 0 1000 500 2` on the
   headphones at 1%. Listen for an added gap, new start pop or missing audio;
   restart must not wait for a pending settle. This does not prove which phase
   overlapped without debugger inspection. Keep PCM's final STATUS and the
   log; report failed state/discontinuities and whether later playback works.

Only the planar headphones and speaker are available. Native results remain
pending; the transducer's faithful transient response does not establish the
electrical cause by itself.

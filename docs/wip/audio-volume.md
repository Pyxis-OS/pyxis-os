# Volume control milestone

Decisions and implementation of delivery steps 1–3 accepted **2026-10-09**,
after #616. The **−60 dB floor, Q16 table, unmuted 50% boot default** are accepted.
The [interface](../interfaces/audio.md#user-volume-controls),
[bar usage](../userland/audio-volume.md) and
[owner artwork](../../assets/ui/volume/README.md) describe implemented behavior.
Native listening remains required before closure; players are separate work.

## Delivery

1. [x] BSP worker/mixer gain, retained per-space state, ordered bounded intents,
   bar-only setters and read-only 88-byte STATUS; SDK/libpyxis consumers updated.
2. [x] Import original MPL-2.0 assets, host conversion of four masks and bar
   widgets/popups; preserve application capture, lock, battery and screenshot composition.
3. [x] Baseline and matched QEMU gain/UI/cost checks, with the existing nested
   guard limit recorded in the [experiment](../development/experiments/audio-volume/README.md): 0/1/10/25/50/100%,
   unity, combined gains, ramps/mute, space isolation, hidden playback,
   reacquisition, eight sources, readiness and repeated input. Record revisions,
   configuration, commands and ranges. Eight-source attempts fail closed; sustained
   eight-session playback and a new ninth-session capacity check are not qualified.
4. [ ] Owner native listening on speaker and wired headphones. Boot has no
   autoplay and reports master50/unmuted, space100/unmuted. **Lower master to1%**
   before brief `pcm 1000 500 2` tones and raise only as comfortable; do not
   repeat painfully loud full-scale speaker tests. Check each output at a fresh
   physical playback start (jack routing remains sampled then).

For step4, check icon mute and Super+M, both sliders and keyboard increments,
master/per-space isolation including hidden playback, retained level after release,
no pops/dropouts during changes, and the expected published-ring mute delay.
Warm/cold reboot must restore50% master rather than the prior setting. Observe
media-key input without claiming decoder support. A later eight-session regression
run is two minutes of silent `pcm 0 0 120`, retaining each final STATUS/log;
ten minutes was the one-time playback closure evidence. Fold this remaining
checklist into references on closure; keep persistence/media-key and ring-delay
limits as short debt. No MIDI/SPC/piano or shared icon pool is assigned.

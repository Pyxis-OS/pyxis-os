# Renoir flip polling qualification

Owner authorized the poll-cost follow-up on 2026-10-10, including delayed
layout-loss detection during the bounded, write-free PENDING interval.
Implementation is in draft #658. The [reference](../kernel/renoir-flip.md#pending-poll-validation)
records the light observations and full action guards. No default enablement,
new GPU write authority or presentation-timing step 3 is authorized.

The earlier owner-reported native `9254f5c8` run measured 369 µs mean full
validation elapsed, 3.9 ms maximum and about 8 polls per flip: roughly 3 ms
cumulative validation elapsed per frame, including preemption/clock overhead.
The [poll-cost record](../development/experiments/renoir-poll-cost/README.md)
records baseline/candidate revisions, sealed matched inputs, builds and
interleaved QEMU unavailable-path checks. QEMU ordinary compose means ranged
0.354–0.449 ms and copy means 0.294–0.347 ms; it has no DCN and does not establish
native poll-cost improvement. Native A–B–A–B results have not been supplied.

- [x] Capture the pre-change baseline and prior native metrics.
- [x] Implement light pending observations with full validation at every guarded action.
- [ ] Qualify interleaved native A–B–A–B cost, confirmations and both games.

Use the existing **Flip: on + metrics** entry with info/UDP logging and
`display.timing=off`. Ask the orchestrator to have Luna stage the sealed A/B sets;
Alpha does not stage PXE. Compare the same idle, native Quake and unchanged 72 Hz
Chocolate Quake workloads, repeated warm windows, full/light/submission costs,
polls per confirmation and BSP observation elapsed per frame. Require matching
confirmations, no timeout/FAILED and no visual/input regression. Exact commands,
input manifests and steps are in the record. Retain the
[cost debt](../technical-debt.md#renoir-steady-state-validation-cost) until measured
results establish the outcome; trim this WIP when qualification is complete.

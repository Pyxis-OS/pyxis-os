# Renoir pending-poll cost

Owner authorized the [guard split](../../../kernel/renoir-flip.md#pending-poll-validation)
on 2026-10-10. Implementation and local builds/review pass; native interleaved
qualification is pending. Default boot and the GPU write allowlist are unchanged.

## Baselines and revisions

Fresh main `7766dae0` includes #647. Before edits, a full source image was built
with info logging, UDP logging, `display.flip=1 display.flip.metrics=1
 display.timing=off`, sealed as `build/poll-baseline-main/`, and booted in QEMU.
Pins: userland `89c520b6`, ports `9397093c`, fs `b427df29`, lwIP `a1aadb91`.
Existing LLVM23.1.3 builder; no toolchain or dependency change.

The PR was rebased onto main `4236efc7` after #661 moved the reference. The
sealed A/B sets below retain their original revisions and inputs; the rebase
does not relabel them or turn earlier measurements into rebased-head results.

The earlier owner-reported native `9254f5c8` run had 3120 submissions and
confirmations, zero timeouts/FAILED, full-poll validation mean 369 µs/max 3.9 ms
and about 8 polls per flip. About 3 ms cumulative polling validation elapsed per
frame is an estimate; it excludes submission validation and is not separately
profiled CPU time. See the [normal-flip native result](../renoir-flip-backend/README.md#native-success--2026-10-10).

Native A is `4e2c8ebc`: original full validation on every poll, with expanded
metrics only. Native B is `bc4dc9eb`: the light pending path, same accounting.
Sealed sets: `build/native-poll-before/` and `build/native-poll-after/`, each
with kernel/initrd/limine.conf/REVISION/INPUTS/SHA256SUMS. B deliberately uses
the exact A initrd; the code delta is private kernel C with no SDK/ABI change.
Both use identical options and consumer binaries. A full rebuild changed some
application payloads/SDK provenance despite unchanged pins; those rebuilt
payloads are preserved locally and excluded from the native comparison.

## Guards and metrics

Two observations read flip control and earliest low/high (six MMIO reads).
A known-owned tuple with unchanged non-status control can only keep PENDING
waiting. Possible completion, 50 ms/50-poll expiry or an unexpected observation
enters full validation. Unexpected observations remain latched for FAILED even
if later full samples recover. READY/fallback and submission retain full checks;
no surface write, retirement, capture publication or fallback is authorized by
light reads. Primary and earliest must both confirm the requested surface.

Existing `validation-*` fields keep their full-poll meaning, including READY
and completion/fallback checks. `submit-validation-*` covers the full check
before GPU submission. `light-*` covers the pending observations. All report
count, mean, maximum and cumulative total at 120 confirmations. The per-frame
fields divide cumulative totals by confirmations:

- `poll-observation-per-frame`: full-poll validation plus light observations.
- `bsp-observation-per-frame`: the above plus submission validation.

These are elapsed intervals on the BSP, including clock/interrupt/preemption
cost; sleeping between polls is excluded. They do not measure all presenter CPU
execution. Compose/copy mean/max remain in `display-flip-metrics`. Preserve
submitted/confirmed/poll counts, wait mean/max, timeouts and FAILED output too.
Baseline A's light count is zero; B's full-check count should fall, not disappear.

## Local qualification

Full existing-builder images pass for clean main, accounted A and B. Source
review confirms full validation before first back stores, submission, completion,
retirement/capture and both initial/later fallback copies. No new tests, synthetic
DCN device, fault injection or boot automation was added. QEMU has no DCN2.1:
it checks the unavailable path and ordinary presentation, not poll-cost reduction.

Interleaved clean-main/B/clean-main/B QEMU 10.2.2: Q35, nested KVM, max CPU,
four CPUs, 2 GiB, fresh OVMF variables per boot, standard VGA 1280×800, relative
PS/2, VirtIO-SCSI CD/RNG, no NIC/disk/USB/audio. Idle cumulative compose/copy
means are sampled at frames 1200–2400 (11 metric lines per run), before keyboard
interaction; ranges include nested-host scheduling variation. Results below
are ordinary-path costs, not Renoir validation timings.

| Run | Compose mean range | Copy mean range |
| --- | --- | --- |
| A1 clean main | 0.355–0.361 ms | 0.294–0.302 ms |
| B1 candidate | 0.375–0.382 ms | 0.304–0.308 ms |
| A2 clean main | 0.421–0.449 ms | 0.344–0.347 ms |
| B2 candidate | 0.354–0.362 ms | 0.303–0.309 ms |

Both revisions refuse absent Renoir and remain OFF/unprepared with no register
mappings; shell/tab/ls/caret/I-beam checks pass. New private polling code does
not execute on this path; these varying costs do not establish a native gain.

## Native A–B–A–B: Luna stages, existing flip-on entry

1. Ask the orchestrator to have Luna stage A first, then B, then A, then B.
   Alpha does not stage PXE. Reuse **Flip: on + metrics** with info logging,
   UDP capture, `display.flip=1 display.flip.metrics=1 display.timing=off`.
   Keep the same panel/GOP mode, power setting, game binaries/data and workloads.
   Verify each set's manifest and INPUTS; retain prior rollback files.
2. Per boot, retain startup, discard the first 120 confirmations as warm-up,
   then capture at least 60s idle and equal-duration moving native Quake and
   unchanged 72 Hz Chocolate Quake scenes/play. Check keys/pointer and the same
   space/layer/lock/unlock interactions for perceived latency, stale frames,
   tearing, hiccups and freezes. Retain camera clips/owner judgment when possible.
3. Record per-run validation/light/submission mean/max/count/total, polls per
   confirmation, both observation-per-frame fields, compose/copy and wait costs.
   Compare the same workloads/windows. Cumulative totals permit subtraction of
   warm-up/previous windows; divide delta total by delta confirmations rather
   than treating cumulative means as per-window measurements. Keep repeated-run
   ranges; do not infer improvement from fewer calls alone.
4. Require many matching confirmations, no timeout/FAILED and no visual/input
   regression. Unexpected tuples and full-check failure retain the existing
   pinned FAILED behavior; timeout fallback still requires full validation.
   Native timeout/panic injection is not authorized. A safe refusal/failure is
   not qualification success; preserve its diagnostics and restore baseline.
5. Send the masked log locations, manifests/revisions and owner observations.
   Update this record with actual interleaved results before claiming cost
   reduction or completing the follow-up. Native results are not yet available.

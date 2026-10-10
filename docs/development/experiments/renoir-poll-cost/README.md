# Renoir pending-poll cost

Owner authorized the [guard split](../../../kernel/renoir-flip.md#pending-poll-validation)
on 2026-10-10. The owner qualified the interleaved native comparison on the
ThinkPad the same day. Default boot and the GPU write allowlist are unchanged.

## Baselines and revisions

Fresh main `7766dae0` includes #647. Before edits, a full source image was built
with info logging, UDP logging, `display.flip=1 display.flip.metrics=1
 display.timing=off`, sealed as `build/poll-baseline-main/`, and booted in QEMU.
Pins: userland `89c520b6`, ports `9397093c`, fs `b427df29`, lwIP `a1aadb91`.
Existing LLVM23.1.3 builder; no toolchain or dependency change.

The PR was rebased onto main `4236efc7` after #661 moved the reference; the
poll code was unchanged. Final delivery rebases onto main `036f3287`; native
results retain the exact A/B revisions below. The earlier owner-reported `9254f5c8` run estimated
roughly 3 ms polling validation elapsed per frame (369 µs mean, about 8 polls).
The accounted A/B comparison below measures cumulative totals directly.

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

## Native A–B–A–B — owner-run, 2026-10-10

ThinkPad on AC, existing **Flip: on + metrics** entry made the default for the
session, info/UDP logging, `display.flip=1 display.flip.metrics=1
 display.timing=off`; remote reboots. Luna built both kernels. Every boot used
the same A initrd: B's rebuild differed in 15 payload files, so it was excluded
and only the kernel varied. Native inputs:

| Input | Revision | SHA-256 |
| --- | --- | --- |
| A kernel | `5f187a971bacd1abb06cc3f8adf83c6518b6beea` | `418152769590902970979a23a268214cf3c81967b61f658db98c85256e0edd7e` |
| B kernel | `f4f71bc6cf165bf0f1e4002f5da56c25a73dd101` | `6585ab1b2445b3c11bbb325dc6869a37080692c65ad98c0699f42d000581f888` |
| Shared A initrd | A build | `9cde80a0f2069bfb400365f946577635bb65f9f489f8a78203ff02b1730c7ded` |

Per boot the owner spent about one minute idle at the prompt, one minute moving
in native Quake and one minute in Chocolate Quake. These are **whole-boot
cumulative totals**, including warm-up; no per-workload or warm-window splits
were supplied. Final metric files inspected locally:
`/shared/present/batch2/poll-ab/ab-{A1,B1,A2,B2}.txt`. Raw logs stay outside Git.

| Run | Submitted = confirmed | Polls / per flip | Wait mean / max (ms) | Full validation count | Full mean / max (µs) | Poll observation / frame (ms) | BSP observation / frame (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 | 11280 | 122546 / 10.86 | 12.858 / 29.661 | 133826 | 265.742 / 25996.542 | 3.152770 | 3.414532 |
| B1 | 11040 | 145167 / 13.15 | 12.833 / 28.741 | 22080 | 350.164 / 19156.383 | 0.796039 | 1.116441 |
| A2 | 11400 | 121146 / 10.63 | 12.599 / 27.824 | 132546 | 268.806 / 8494.945 | 3.125376 | 3.392792 |
| B2 | 11280 | 149156 / 13.22 | 12.917 / 48.121 | 22560 | 349.866 / 9687.901 | 0.796159 | 1.116199 |

| Run | Submission validation mean / max (µs) | Light count | Light mean / max (µs) |
| --- | --- | --- | --- |
| A1 | 261.761 / 610.177 | 0 | 0 / 0 |
| B1 | 320.402 / 628.359 | 145167 | 7.278 / 2278.966 |
| A2 | 267.416 / 2181.136 | 0 | 0 / 0 |
| B2 | 320.039 / 2715.298 | 149156 | 7.292 / 1546.898 |

All runs had zero timeouts. The owner reported no FAILED or timeout-fallback
lines and no tearing. A1/A2 felt the same as before; B1/B2 felt more responsive.
Poll-observation elapsed per frame fell about 74.5–74.8%; including submission
validation, BSP observation elapsed fell about 67.1–67.3% (3.393–3.415 ms to
1.116 ms). This is a matched repeated whole-boot result, not task CPU profiling
or a separately measured improvement for either game. B performs more polls
per flip because the intervening checks cost less; full poll validation falls
to two checks per confirmed frame rather than one per pending poll plus READY.
Mean confirmation wait stayed 12.60–12.92 ms across all four boots.

B2's 48.121 ms wait maximum is below, but close to, the unchanged 50 ms deadline.
The interval starts after the flip address write and ends after full completion
validation. A producer/game stall before submission is excluded. It includes
sleep wakeup scheduling, input service between polls, preemption, register reads
and final validation; delayed observation or hardware completion can increase
it. Only final cumulative maxima were retained, so the event cannot be placed
in a workload, attributed to a scheduling or hardware cause, or counted. A
maximum does not establish that it happened only once. No timeout occurred;
this is not a pixel-to-photon measurement or evidence for relaxing the bound.

B's full-validation mean is about 350 µs versus A's 266–269 µs with identical
check code. The samples differ: A includes every pending poll, whereas B keeps
READY and candidate-completion checks. Less frequent checks may also leave
colder caches; B's submission-check mean rises too. Cache state was not profiled,
so colder caches are plausible rather than established. The relevant cumulative
cost still falls substantially despite the higher mean per remaining full check.

- [x] Capture the baseline before optimization.
- [x] Implement light waits with full guards and qualify the QEMU unavailable path.
- [x] Qualify repeated native costs, matching confirmations and both games.

This completes the poll-cost follow-up. Timeout recovery, native panic and other
unreported scenarios remain in the separate
[backend qualification debt](../../../technical-debt.md#renoir-flip-backend-qualification).

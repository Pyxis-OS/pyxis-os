# Retained FILE accounting

## Before-code baseline — 2026-10-15

Parent `11e6ae8c`: main `b030c7b1` plus the unchanged handoff #874 source;
userland `7795dac6`, ports `0e760128`, fs `101e1f68`, lwIP `a1aadb91`.
Separate worktree/branch: `pyxis-retained-values`, `kernel/retained-values`.
The handoff review branches are untouched. Ordinary complete `make -j16 image`
passes using the existing `pyxis-llvm23.1.3-df87791` image.

Nested q35/KVM, QEMU 10.2.2, four CPUs, host CPU, 2 GiB, UTC RTC, fresh OVMF,
standard VGA, VirtIO net/RNG; loopback 24489 forwards guest 2323. Plain Remote
machine/no-echo is 80×24. The exact existing Lua OPEN/launch and 1 MiB iobench
read/write/pipe commands are in the
[handoff baseline](capability-output.md). Three warmed cohorts each contain ten
measured samples. All nine sessions exit zero with complete FINAL drain and
verified transfers. No owned build or debugger runs during timing; shared-host
load is uncontrolled. GDB afterward sees all CPUs idle, no retired objects and
zero retired heap bytes. No native cost or leak-freedom claim.

| Nested elapsed ms | Cohort medians | All-sample range |
| --- | --- | --- |
| Pipe | 0.74533 / 0.57960 / 0.74835 | 0.52043–5.30215 |
| Native OPEN | 0.03681 / 0.04047 / 0.03981 | 0.03653–0.43160 |
| Text OPEN | 1.60748 / 2.31484 / 1.78297 | 0.95586–4.60099 |
| Launch | 8.73048 / 7.37991 / 9.90250 | 2.94560–16.35479 |
| Boot read | 0.26838 / 0.25731 / 0.26599 | 0.25461–0.32602 |
| RAM read | 0.44273 / 0.44218 / 0.44866 | 0.43184–0.49143 |
| RAM write | 9.73688 / 9.76820 / 9.72725 | 9.46488–10.09453 |

Raw JSONL, input commands, image, summary and GDB inspection remain in ignored
`build/retention-baseline/`. All owned baseline jobs are stopped. Repeat matched
workloads after implementation with the preserved image interleaved as control.

Baseline ELF SHA256
`6817d22e43d3c80df3fe13676c959ab50b4b78b96318e11f7f0239385546a31b`;
ISO `765bdda6d3f37f8943809077805b41336168539026e32f5940b9069028901590`.

## Implemented qualification

Parent `4b0efd1d`, userland `9437cb69`; other pins and the compiler image above
are unchanged. The ordinary complete image build passes. The private SDK client
extends the existing handoff demonstration, using the actual exported SDK;
no repository test infrastructure or injected allocation failures were added.
Interactive one/four-CPU QEMU uses the configuration above. All demonstration
sessions exit zero with complete Remote drain.

- With the configured defaults, a sparse write ending at exactly 64 MiB succeeds.
  Overwrite adds no charge; shrink releases it; regrowth restores it. COPY and an
  exported environment holder preserve the charge after public handles close;
  removing the final holder returns it through deferred retirement.
- A prefix followed by growth past the value limit returns FILE_TOO_LARGE.
  Retry, shrink-to-zero and zero writes cannot repair it. TAKE transfers an
  immutable TOO_LARGE FILE; DESCRIBE succeeds, while READ (including zero bytes)
  and SIZE fail. The discarded prefix has no remaining charge.
- Eight sparse 64 MiB results fill exactly 512 MiB. The next byte produces a
  collectible failed FILE without disturbing the existing charge. Closing the
  eight final holders drains the account to zero. The bounded ABORTED constructor
  retains its declared outcome/detail and refuses reads. A second one-CPU run
  with authored 4 KiB/32 KiB limits passes the same boundary checks.
- Existing handoff, mixed/zero/second-value, CUT, close/cancel and collector
  authority checks still pass. Real echo collects bytes; real cat moves a sparse
  64 MiB+1 ordinary FILE with identical object identity and no second charge.
  Ordinary child lookup finds neither account nor factory.
- Remote `--mux` connects, splits and runs the demonstration in the new pane;
  number focus returns to the live original shell. Disconnect completes cleanup.
  Read-only GDB sees account usage zero afterward and only the session owner
  reference, with no retired objects or retired heap bytes.

Read-only GDB also stops at the real account reservation boundary on an AP:
the counter is exactly 512 MiB, the next one-byte reservation returns false,
and no allocation follows. No debugger calls or target writes are used.
Source inspection found and corrected a SIZE-after-wait failure recheck; the
first runtime run found and corrected dispatch of message headers instead of
captured payloads. Independent source review found no further confirmed account,
startup privacy or final-holder lifetime issue.

## Matched ordinary workloads

Three changed-image cohorts repeat the baseline commands. A fresh preserved
baseline boot is interleaved between changed cohorts 2 and 3. All twelve sessions
exit zero with complete drain and verified transfers. No owned build/debugger
runs during timing; shared-host load remains uncontrolled.

| Nested elapsed ms | Changed cohort medians | Changed range | Interleaved control median |
| --- | --- | --- | --- |
| Pipe | 1.57668 / 0.96269 / 1.41810 | 0.52912–4.85464 | 0.61333 |
| Native OPEN | 0.03977 / 0.03997 / 0.03624 | 0.03604–0.05082 | 0.04001 |
| Text OPEN | 4.27198 / 2.06089 / 1.93938 | 1.11182–5.55512 | 2.16292 |
| Launch | 7.93881 / 7.50564 / 8.71387 | 2.19585–10.08179 | 9.24199 |
| Boot read | 0.27182 / 0.27319 / 0.28019 | 0.25213–0.64847 | 0.27607 |
| RAM read | 0.44222 / 0.44802 / 0.45813 | 0.42965–0.51617 | 0.44782 |
| RAM write | 9.80254 / 9.75863 / 9.79734 | 9.71866–12.10572 | 9.71351 |

OPEN, launch and ordinary FILE samples overlap. Pipe medians rise in all three
changed cohorts; their fastest sample remains close to baseline, and ranges are
wide. This increase is observed, not dismissed or attributed. Source inspection:
iobench workers use ordinary read/write PIPE calls; their implementation has no
new account guard or quota operation. The PIPE constructor change only removes
unaccounted output creation. Native cost and the cause of these nested pipe
medians remain unmeasured; no performance improvement is claimed.

## Reproducibility and limits

Raw commands/JSONL and GDB captures remain in ignored
`build/retention-{baseline,after,control,qualification}/`. Ordinary final ELF
SHA256 `0652463648f2878992986dd251f81483abff0fd31fedd6760bf3c833d1c06f36`;
ISO `facebb519ad8f2a965e3bf468f2ebd84d12e305ad27eb5c81a9f9d9dec155e38`.
Private demonstration source SHA256
`7397eb496c3b5d5569b0dfef6cc48ef61301102f1a68732961d68aa04cbf67b6`.
All owned QEMU/GDB/client/build jobs are stopped.

Sparse boundaries do not measure dense-data throughput or physical memory use.
Allocation/capacity failure rollback, cancellation during growth, concurrent
SIZE/failure and late reply-copy races are source-inspected, not fault-injected
or runtime race-qualified. Cross-process survival after the exporting shell exits
is source-inspected; the runtime final-holder check uses COPY/environment/unset.
No native hardware claim. General C9 pipelines and A5 shell substitution remain
separate; the direct SDK's interim consumer-size policy is not the hard retention
limit and must be reconciled explicitly by its consumer.

## Rejected write correction — 2026-10-15

Daybreak reported the admitted-error fix in #874 qualified at `6a694379`.
At retention `a663997e` / userland `9437cb69`, its remaining Low finding was an
overflow rejected before admission leaving a successful prefix collectible.
`c75b8643` now latches any failed WRITE/RESIZE reaching the WRITE-authorized
producer, including malformed requests, overflow and bad buffers. The original
producer status stays unchanged; TAKE returns IO. Typed quota TOO_LARGE remains
collectible. A failed concurrent TAKE cannot restore temporary grants into an
already-failed cell. That cleanup race is source-reviewed, not runtime-qualified.

The ordinary full image build passes. Existing positive SDK demonstrations run
again at one/four CPUs with exact default boundaries, permanent TOO_LARGE,
COPY/environment final-holder drainage, handoff/mixed/zero/close/CUT behavior and
real echo/cat; both exit zero with complete Remote drain. Read-only GDB afterward
sees idle CPUs, no retired objects and zero retired heap bytes. No new negative
program, allocation injection, test infrastructure or timing run was added;
Daybreak independently reruns its overflow check. All owned jobs are stopped.

Correction ELF SHA256
`7231bf5b98a5cdd3dc33f4485ac6393191454c9e66867fa3c314d620cc630388`;
ordinary ISO `7722e962d5fdcf3f95eb83ccbcf768b5154406f4914be0ef343734975b10255d`.
Raw captures stay in ignored `build/retention-rejected-write/`; the existing
demonstration source/hash above is unchanged. Daybreak separately reported the
64/512 MiB boundaries, final-holder release, shared charge across two Remote
connections without factory authority, and 1,800 overlapping release/write
attempts passing at the earlier head; those are its results, not rerun here.

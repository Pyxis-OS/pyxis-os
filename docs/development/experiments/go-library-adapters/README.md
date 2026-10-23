# Go standard-library adapters

2026-10-23. Kernel/image source `59e8d64d`; userland `96d2430b` and existing
ports/fs/lwIP pins unchanged. Baseline fork `ce725f7680`, adapters `3b1344ea9c`;
the timing run used `730eafcd1b` before the lookup-refusal addition.
[fork PR #7](https://git.internal/PyxisOS/pyxis-go/pulls/7). No new kernel ABI,
compiler container, libc, merged test framework or native/PXE run.

The ordinary compiler recipe fetched the exact published internal fork commit
and built a fresh host prefix. Its recipe-built hello is byte-identical to the
qualified adapter hello (SHA-256 `f5beafb003eec0bab5d269701cd64cd4f9f0c945a648407a3bfb984fe1ffe978`). Target
callers use `GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0`, `-trimpath`,
`GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off`. Sources, build commands, image and
executable hashes and raw captures stay at `/shared/present/go-stdlib-adapters/`.
The private image adds only qualification bundles to the normal live archive.

## Runtime checks

Nested QEMU/KVM q35, four vCPUs `max,+invtsc`, 2 GiB, OVMF, VirtIO RNG/network,
read-only VirtIO-SCSI boot CD. The guest selected stable KVM clock. Signal
callers selected four Ps; no debugger ran during startup measurements.

| Check | Observed |
| --- | --- |
| Random / paths / users | Complete fills of 0/1/255/256/257/8193 bytes; native-root cleaning/join/list spelling; all user/group lookups explicitly unsupported. Target calls passed; host lexical cases separately checked root/relative/boundary helpers. No entropy-quality claim. |
| Signal | First Ctrl+C delivered to one/two channels and NotifyContext; 800 concurrent Notify/Stop cycles followed by real delivery; last Stop/Reset restored forceful first press. Ignore stayed true across Notify, discarded first press, then the second forced termination. Actual process results retained. |
| Crash output | Private output survived caller close; reinstall/replacement/disable and cursor handback passed. Read-only/directory replacements refused without replacing the working target. Supplied FILE append preserved existing bytes after original stdout close; stdout/stderr alias retained shared positions. |
| Cleanup | After process/group retirement, debugger observation found no closing readiness accounts or queued work/source/timer entries. |
| Build | Selected Pyxis/Linux packages built. Existing go/build TestDependencies passed after replacing a pre-existing syscall-to-strings dependency with lower-layer helpers. |

Public-root parsing accepted the pinned 121-root bundle and rejected empty,
malformed PEM and malformed DER cases in a private host caller. Host x509 built.
Target x509 still fails on missing native `netFD`; no target TLS/root-loading
qualification is claimed. Under the accepted 2026-10-23 limit, `os/exec` builds.
QEMU calls to
`LookPath` (empty, bare, native and absent names) returned unsupported with the
requested name preserved. Bare command launch and explicit-path launch with
supplied standard streams also returned unsupported. The fresh final-pin recipe
produced the identical qualified caller. No subprocess support is claimed.
Random transport/fatal-resource failures, hardware operation and broader signal
supervisors are source-inspected or unqualified, not inferred from successful calls.

## Startup

The existing native launch probe captured a fresh group and output pipe before
timing, then measured LAUNCH to first output and confirmed process retirement.
Each batch discarded one warmup and retained 11 samples. Baseline before code
changes: 33 samples, first output 4.047–4.781 ms, p50 4.323 ms. A matched single-
boot interleaved comparison used the same kernel/archive/probe, with only the
raw executable selected before the timed window differing:

| Batch | First output min / p50 / max (ms) | Retirement p50 / max (ms) |
| --- | --- | --- |
| A1, baseline | 4.128 / 4.612 / 5.034 | 4.815 / 5.204 |
| B1, adapters | 4.401 / 4.693 / 5.401 | 4.878 / 5.674 |
| A2, baseline | 4.389 / 4.656 / 5.602 | 4.818 / 5.830 |
| B2, adapters | 4.432 / 4.623 / 5.588 | 4.823 / 5.793 |

All 44 measured launches printed the expected hello and exited zero. Variation
exceeds the p50 difference; no material startup regression is established, and
all samples retain clear margin inside the 250 ms nested engineering budget.
These are warm nested-VM hello results, not native hardware, cold media or a
Caddy startup claim. Debugger cleanup inspection was separate and afterward.

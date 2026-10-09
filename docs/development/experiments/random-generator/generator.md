# Kernel random generator qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Measured 2026-10-08 after the defaults accepted in
[PR #546](https://git.internal/PyxisOS/pyxis-os/pulls/546). Compare the
[raw-source baseline](README.md) and the [implemented construction](../../../devices/random-generator.md).
The ordinary build passed with the same LLVM 23.1.3 / `49e2c1a` builder and Kconfig; the measured
source and embedded revision is `15b03656f101` (later documentation and notice-packaging changes do
not alter these generator or service routines). Only `boot/caelum.elf` was replaced in the baseline
image tree; consumer, libraries, firmware, device order, four cores/8 GiB and the stock QEMU 10.2.2
q35 nested-KVM configuration were fixed. The CPU case omitted the same RNG device, and no debugger or
other task-owned guest or build ran during timing. The one-CPU/debugger and cancellation/HTTPS checks
below are separate from the matched samples.

## Matched random_read runs

Each source had one fresh boot and five individually completed invocations of the unchanged
`random-baseline` (16 warm-up and 512 measured reads at extents 0/1/32/256), so each nonzero extent has
2560 measured successful reads per source, with the original clock boundaries, deadlines and throughput
calculation and no control subtraction. All five command completions per source exited zero.

| Source | Bytes/read | Before median mean µs | After median mean µs | After run-mean range µs | Before loop MiB/s | After loop MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| VirtIO | 0 | 71.856 | 71.678 | 71.634–71.891 | — | — |
| VirtIO | 1 | 909.705 | 561.285 | 533.590–590.953 | 0.000999 | 0.001586 |
| VirtIO | 32 | 916.745 | 554.805 | 524.040–603.622 | 0.031585 | 0.051342 |
| VirtIO | 256 | 895.204 | 548.759 | 531.182–559.566 | 0.259850 | 0.415204 |
| CPU | 0 | 65.190 | 72.414 | 71.999–73.443 | — | — |
| CPU | 1 | 540.680 | 572.748 | 535.223–577.556 | 0.001663 | 0.001557 |
| CPU | 32 | 688.805 | 563.404 | 551.851–617.900 | 0.042066 | 0.050887 |
| CPU | 256 | 1804.430 | 580.078 | 548.513–592.890 | 0.131983 | 0.394567 |

At 256 bytes loop throughput improved about 1.60 times on VirtIO and 2.99 times on CPU entropy. CPU
one-byte median latency rose 5.9%, but its 535.223–577.556 µs range overlaps the baseline's
536.792–641.127 µs and the zero-length control median also rose 11.1%. The runs include host/scheduler
variation and erasure/dispatch costs; they establish no universal latency improvement, raw ChaCha
throughput, saturation limit or native prediction, and the global generator still pays worker handoff.

## RFC vector and erasure

In a separate one-CPU guest GDB stopped at entropy-worker entry before live seeding and checked the
compiled core against [RFC 8439 section 2.3.2](https://www.rfc-editor.org/rfc/rfc8439#section-2.3.2),
using four observed FREE call-slot buffers as temporary scratch for the public key 00..1f, nonce, state
and block, with the counter and leading nonce word set explicitly. All 64 output bytes matched the RFC
and the counter advanced from 1 to 2. Scratch was erased and registers and stack restored before
resuming; this never seeds the generator. GDB's normal inferior-call trampoline tried to execute on the
non-executable kernel stack, so that disposable guest was restarted; the successful calls used a
kernel-text return breakpoint with interrupts disabled and a corrected stack-alignment cast. This is a
debugger-only observation, not a boot self-test or fault-injection facility.

At natural first seeding the worker's tentative extent was 40 bytes with `generator.seeded=false` and
`seed_required=true`; after return seeded was true, available output zero and the budget within
[1 MiB, 2 MiB). Stepping the cleanup confirmed zeroed seed storage and a cleared latch, and later
inspection found the 856-byte reserved/consumed prefix erased with 168 future bytes still buffered
(not printed). Disassembly confirms `memzero_explicit` keeps volatile byte stores in the optimized
kernel. The transcripts held public RFC bytes and metadata only (one automatically rendered seed
argument was redacted), and kernel code logs no seed, key, output or CPU health-history words.

## Natural reseeding and existing consumers

Normal consumer requests alone showed both reseed triggers: the first reseed had age of at least
60 seconds while its output budget still exceeded the request (time trigger), and after renewal repeated
invocations reached a remaining budget no larger than the next request with age under 60 seconds (output
trigger), without modifying state, budget or clock. Both commits had a full tentative extent and the
required latch, and after cleanup the seed bytes were erased and output permitted.

The one-CPU run's ordinary unprofiled consumer after vector inspection detached gave a mean 256-byte
latency of 524.969 µs with every read successful (its later invocations were boundary qualification, not
matched samples). Existing `cat https://example.com/` succeeded with packaged trust on one-CPU VirtIO
and four-CPU VirtIO/CPU sources (577-byte body, CRC32 2118444194). On the four-CPU VirtIO guest Ctrl+C
terminated an active consumer, the next HTTPS command succeeded and session FINAL confirmed cleanup and
drain.

## Limits

Source review covered failed-attempt completion, the required-reseed latch for later smaller reads,
cancellation/park ordering, stale DMA exclusion, watchdog/quarantine, healthy CPU survivor refill and
explicit erasure. Temporary CPU supply exhaustion, permanent hardware faults, stalled or short DMA
failures and whole-VM cloning were not induced. Native ThinkPad performance, seed supply under load and
source quality remain unqualified; RFC matching and the existing health checks do not certify entropy or
exclude compiler or hardware side channels.

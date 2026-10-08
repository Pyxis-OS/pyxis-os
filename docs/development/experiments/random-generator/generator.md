# Kernel random generator qualification

Measured 2026-10-08 after the defaults accepted in
[PR #546](https://git.internal/PyxisOS/pyxis-os/pulls/546). Compare the
[raw-source baseline](README.md) and [implemented construction](../../../devices/random-generator.md).
The ordinary kernel/image build passed with the same LLVM 23.1.3 / `49e2c1a`
builder and effective Kconfig. The measured source/embedded revision is
`15b03656f101`; later documentation and notice-packaging changes do not alter
these generator/service routines. Submitted-head CI is reported separately.

| Artifact | SHA-256 |
| --- | --- |
| Measured kernel ELF | `ae519d93150a31758f68f100bdd919d0ead1f7c5a72012c3e2a6b4d7043f6f0a` |
| Matched measurement ISO | `686ce2cdb045d73f707b9656533a418347f14e09643d6c619ff8401646293918` |
| Unchanged measurement initrd | `3c5b5b7d026c547d9d0270715ac195af9178008668ebc78a72c5d8359da78e9a` |
| Unchanged consumer P1F | `cec74bc730dc027294abc2f5f95afd39673204e0a893282b79d300d3ae3f9813` |

Only `boot/caelum.elf` was replaced in the baseline image tree. Consumer/library,
firmware, device order, four cores/8 GiB, stock QEMU 10.2.2 q35 nested KVM and
source configuration stayed fixed. The CPU case omitted the same RNG object/
device lines. No debugger or task-owned build/other guest ran during timing.
The one-CPU/debugger and additional cancellation/HTTPS checks below are separate
qualification; they are excluded from the matched timing samples.

## Matched random_read runs

Each source had one fresh boot and five individually completed invocations of
unchanged `random-baseline`: 16 warm-up and 512 measured reads at each extent
0/1/32/256. Each nonzero extent therefore has 2560 measured successful reads per
source. The original clock boundaries, deadlines, stdout timing and throughput
calculation stayed unchanged. No control subtraction or raw entropy output.
Raw after samples: [VirtIO](virtio-after-samples.txt), [CPU](cpu-after-samples.txt).
All five command-complete events per source report normal exit zero.

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

At 256 bytes the loop throughput improved about 1.60 times on VirtIO and
2.99 times on CPU entropy. CPU one-byte median latency increased 5.9%; its
535.223–577.556 µs after range overlaps the baseline 536.792–641.127 µs range,
and the zero-length control median also increased 11.1%. These runs include
host/scheduler variation and erasure/dispatch costs. They establish no universal
latency improvement, raw ChaCha throughput, saturation limit or native prediction.
The global generator still pays the existing worker handoff cost.

## RFC vector and erasure

The complete [debugger record](rfc-vector-gdb.txt) checks the compiled core
against [RFC 8439 section 2.3.2](https://www.rfc-editor.org/rfc/rfc8439#section-2.3.2).
In a separate one-CPU guest, stop at entropy-worker entry before live seeding.
Four observed FREE call-slot byte buffers serve only as temporary debugger
scratch for public key 00..1f, the RFC nonce, state and block. Set the RFC counter
and leading nonce word explicitly. All 64 observed output bytes match the RFC,
and the counter advances from 1 to 2. Erase scratch and restore saved registers/
stack contents before resuming; this never supplies a seed to the generator.

GDB's normal inferior-call trampoline attempted execution on the non-executable
kernel stack; that disposable guest was stopped and restarted. The successful
manual core calls use a kernel-text return breakpoint, with interrupts disabled
and a corrected integer cast for stack alignment. The record retains that
correction. This is a debugger-only vector observation, not an added boot
self-test or a fault-injection facility.

At natural first seeding, the worker's tentative extent was 40 bytes while
`generator.seeded=false` and `seed_required=true`. After return, seeded became
true, available output was zero and the budget was within [1 MiB, 2 MiB).
Stepping the cleanup confirmed zeroed seed storage and a cleared latch. Later
read-only inspection found the entire 856-byte reserved/consumed prefix erased;
168 future bytes remained buffered and were not printed. Disassembly confirms
`memzero_explicit` retains volatile byte stores in the optimized kernel.

Automatic debugger rendering of one seed argument was redacted from the retained
record; subsequent seed frames suppress arguments. The retained transcripts
contain public RFC bytes and metadata only. Kernel code logs neither seed/key/
output material nor CPU health-history words.

## Natural reseeding and existing consumers

The [reseed transcript](reseed-gdb.txt) uses only normal consumer requests:

- The first observed reseed had age at least 60 seconds while its output budget
  still exceeded the request; the time trigger was independently true.
- After renewing the seed, repeated unchanged consumer invocations reached a
  remaining budget no larger than the next request while age was below 60 seconds.
  This establishes the output trigger without modifying state/budget/clock.
- Both commits had a full tentative extent and the required latch; after cleanup
  the seed bytes were erased and output was permitted again.

The [one-CPU record](onecpu-results.txt) includes an ordinary unprofiled consumer
run after vector inspection detached: mean 256-byte latency 524.969 µs and every
read successful. Its later repeated invocations were boundary qualification,
including the debugger stops, not matched performance samples. One-/four-CPU
boots started the ordinary spaces and prepared TCP identity.

Existing `cat https://example.com/` succeeded with packaged trust on one-CPU
VirtIO and four-CPU VirtIO/CPU sources, returning a 577-byte body, CRC32
2118444194. On the four-CPU VirtIO guest, Ctrl+C terminated an active consumer;
the following HTTPS command succeeded and session FINAL confirmed complete
cleanup/drain. [Consumer checks](consumer-checks.txt) retain statuses and body
identity without the payload. Timing records end after their five completed
commands; an excluded follow-up session was disconnected by host SIGINT, then
reconnected with terminal signal handling disabled to send Ctrl+C to the guest.

## Limits

Source review covered failed-attempt completion, the required-reseed latch for
later smaller reads, cancellation/park ordering, stale DMA exclusion, watchdog/
quarantine, healthy CPU survivor refill and explicit erasure. Temporary CPU
supply exhaustion, permanent hardware faults, stalled/short DMA failures and
whole-VM cloning were not induced. Native ThinkPad performance, seed supply
under load and source quality remain unqualified. RFC matching and the existing
health checks do not certify entropy or exclude compiler/hardware side channels.
All task-owned QEMU, debugger, client and build jobs stopped.

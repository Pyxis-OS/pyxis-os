# Shared PMM after caller-CPU MEMORY

Measured 2026-10-26 after [#1254](https://git.internal/PyxisOS/pyxis-os/pulls/1254)
removed the CPU0 MEMORY dispatcher. This is task 2's required measurement and
[decision input](../../../wip/bsp-removal.md#task-2-measured-allocation-decision-proposed),
not an allocator implementation or native result.

## Method and provenance

Source `e13b8f3511dce20b7c6ff5ab2462e27ae6ff7dbc`, including merged task 1.
The measured PMM, paging and MEMORY sources are unchanged at the documentation
base `aa48e42b`. Clang 23 builder `pyxis-llvm23.1.3-3d69e83`; configuration SHA-256
`a874bdf4b456b0d062896143932eeefea466fa840983db9482828942d49572ea`.
Nested KVM, q35, 8 vCPUs, `host,+invtsc`, 4 GiB, eager XSAVE `0x7`/832 bytes;
the kernel selects its **KVM shared monotonic-floor clock**, HWP is unavailable.
No NIC, persistent disk, concurrent benchmark or debugger during timing. These
are virtual-machine elapsed measurements, not bare-metal CPU costs.

The existing allocbench pages body is unchanged; a private ready/release PIPE
barrier was added before PROFILE/timing. One independent process on each of dense
CPU1 through CPU N starts after all clients are ready. The controller waits for
actual retirement. Each case has three fresh boots per variant, interleaved
control/observation, observation/control, control/observation. `live=1` throughout:
4 KiB × 20,000 rounds, or 256 KiB × 512 rounds per client. Archives/programs are
byte-identical between variants for each case. No single-process sibling gate
serializes these independent address spaces. Warmup, preparation, output and
retirement are excluded from the per-process PROFILE window; aggregate elapsed
includes controller scheduling and final observation.

Private, never-merged counters record per-CPU PMM allocation/free counts, sizes,
failures, bitmap words examined, lock wait/hold, and zero/scratch-map elapsed;
MEMORY bodies have a matching elapsed-TSC denominator. There is no allocation,
logging or extra lock in the counter path. GDB reads counters only after DONE.
The disabled control keeps the same counter storage and returns no observer;
it is not claimed to be a byte-identical stock kernel.

The initial full-observation matrix was too intrusive for attribution: four
256 KiB clients took median **86.41 ms** against **56.87 ms** disabled, and
reported about 30% lock wait. We therefore repeated the entire matrix with only
one in **61** PMM and zero operations timed (prime stride avoids always sampling
the same position within 64-page runs). Counts remain exact. Sample sums are
expanded separately for each CPU/path by actual calls / sampled calls, then
divided by the sum of MEMORY-body elapsed ticks. No timestamp cost is subtracted.
Empty timestamp-pair minima were recorded too; virtualized scheduling outliers
remain in all data. Sampling estimates are not exact CPU-time attribution.

## Matched elapsed results

Milliseconds, median (min–max), three fresh boots per cell. Aggregate wall time
covers all clients, not a sum of per-client durations.

| Size | Clients | Disabled control | Sparse observation |
| --- | ---: | ---: | ---: |
| 4 KiB | 1 | 195.14 (191.99–203.36) | 197.62 (196.34–242.06) |
| 4 KiB | 2 | 317.99 (317.37–321.49) | 328.31 (314.24–336.14) |
| 4 KiB | 4 | 395.50 (395.06–403.50) | 399.32 (395.28–402.13) |
| 256 KiB | 1 | 32.46 (31.68–33.19) | 34.58 (33.54–34.59) |
| 256 KiB | 2 | 47.65 (45.97–47.93) | 48.05 (48.02–48.11) |
| 256 KiB | 4 | 57.16 (56.70–58.31) | 58.32 (57.86–59.24) |

Sparse-observation median overhead versus the adjacent disabled-control medians
is about 0.8–6.5%; the one-client 4 KiB observation has a 242.06 ms outlier.
Ranges and the observer comparison are retained rather than treating the
instrumented maximum as a production worst case. Four clients do four times the
work: disabled aggregate grows 2.03× for 4 KiB and 1.76× for 256 KiB, rather than
4×. This establishes overlap, not linear scaling or a native speedup.

## Allocation path attribution

Estimated percentage of summed **MEMORY-body elapsed TSC ticks**, median
(min–max). Lock wait includes acquiring even an uncontended lock. Hold includes
the in-lock observation timestamp. Zero includes scratch mapping, 4 KiB memset,
withdrawal and local invlpg; it does not isolate memset from scratch traffic.
These percentages are not fractions of end-to-end application time.

| Size | Clients | PMM lock wait % | PMM lock hold % | Zero + scratch % |
| --- | ---: | ---: | ---: | ---: |
| 4 KiB | 1 | 0.69 (0.67–1.76) | 1.53 (1.47–4.94) | 13.07 (12.86–25.24) |
| 4 KiB | 2 | 0.75 (0.74–0.82) | 1.92 (1.74–2.00) | 9.12 (9.00–10.05) |
| 4 KiB | 4 | 0.63 (0.59–0.63) | 1.79 (1.74–1.81) | 8.41 (8.26–9.17) |
| 256 KiB | 1 | 4.40 (4.19–4.44) | 4.04 (3.91–4.32) | 85.44 (84.57–87.90) |
| 256 KiB | 2 | 6.02 (5.36–6.14) | 6.15 (5.51–6.21) | 80.98 (79.84–82.65) |
| 256 KiB | 4 | 7.92 (7.41–10.31) | 6.78 (6.77–7.19) | 77.43 (76.04–77.58) |

All 72 matrix boots completed with zero allocation failures, zero child failures
and confirmed retirement; four earlier pilots were not substituted into the
matrix. Every observed PMM call requests/frees **one frame**. Per client:

- 4 KiB: 20,000 allocations + 20,000 frees, 20,000 zero calls.
- 256 KiB: 32,768 allocations + 32,768 frees, 32,768 zero calls, despite only
  512 MEMORY allocate/release pairs. No multi-frame PMM calls.
- Exactly two bitmap words examined per sampled one-frame allocation in this
  unfragmented warm case. It is not the former whole-bitmap quadratic scan.
  Fragmentation, low-memory full scans and long contiguous DMA requests are not
  characterized by these workloads.

Source matches the counts: [back_run and free_backing](../../../../kernel/mm/vm.c)
use existing 64-frame stack vectors, but call PMM separately for every frame.
[PMM](../../../../kernel/mm/pmm.c) owns one bitmap/hint/statistics lock, with
pressure notification after unlock. Caller-CPU execution is already present;
no BSP refill service or per-CPU retained free pool exists.

At 4 KiB the observed lock sections are a small fraction of the body; the
remaining end-to-end slowdown cannot be assigned to PMM. At 256 KiB the shared
lock has measurable cost, but zero/scratch is the largest measured interval.
The untimed remainder includes page-table work, metadata, checks and observer
cost; it is **not** a measured map-only bucket. In particular, the existing
adjacent scratch PTEs and flags permit cache-line sharing. The earlier
[scratch experiment](../smp-task7-pmm/README.md#remaining-concurrency-cost-scratch-slot-false-sharing)
is historical supporting evidence, not a fresh proof of today's slowdown.
No scratch layout or recursive-walk experiment was performed here.

## Decision and limits

The recommended first change is bounded transaction-owned frame batches, using
the existing 64-frame VM work buffers. It can reduce per-frame lock traffic
without introducing a hidden free pool, remote-free queue or new exhaustion
policy. It does not promise to remove the larger zero/scratch cost. Approval is
still required; no allocation or scratch behavior has changed.

The task 1 native packet is sealed at `/shared/present/bsp-removal/native/`:
pre-task1 `742817b0` versus merged `c7028007`, identical archive, Remote-driven
single runs, owner steps zero. Both images passed the end-to-end collector in
QEMU; neither has PMM counters. It has not been staged or run natively by this
agent. AC/HWP/CPU/clock matching is checked before reporting native comparisons.
The fixed CPU1 comparison changes service CPU0 → CPU1 and on hybrid hardware
cannot isolate the routing benefit from core differences.

Evidence, private patches/sources, ELF/config/archive hashes, per-boot commands,
raw logs, counter captures and reproducible `analyze.py` are retained outside Git
at `/shared/present/bsp-removal/pmm-measure/`. No counter or workload helper is
being merged. Native PMM attribution, fragmented/pressure performance, more than
four concurrent mutators and shared kernel VM remain unmeasured here.

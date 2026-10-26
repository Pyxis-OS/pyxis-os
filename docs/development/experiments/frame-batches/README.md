# Caller-owned PMM frame batches

Task 2 implementation of [decision 4](../../../wip/bsp-removal.md#task-2-measured-allocation-decision-accepted-2026-10-26),
accepted by chronium on 2026-10-26. The
[pre-change measurement](../caller-cpu-pmm/README.md) prompted this bounded
alternative to retained per-CPU free caches.

## Implemented behavior

`pmm_alloc_frames` claims a prefix of up to 64 arbitrary single frames in
first-fit order under one PMM lock. Each returned frame is globally accounted
as allocated immediately, owned by that call's transaction and initially
unzeroed. The caller zeroes/maps after unlock. Exhaustion can return a short
prefix; existing VM mapping/unwind handles it and public MEMORY still either
publishes the complete allocation or returns NO_MEMORY with old regions intact.
Pressure notification happens only after unlock, on a short claim or the
unchanged low-free threshold.

`pmm_free_frames` checks shape, alignment, range and duplicates in the
caller-owned kernel vector before locking. Under the lock it validates every
frame against immutable allocatability and current allocation bits before
clearing any of them, updates the first-candidate hint and exact global counts,
and unlocks. Duplicate checking is bounded by the 64-entry cap and stays outside
the lock. A CPU may free another CPU's allocation: there is no CPU-owned free
list or remote-return queue. Caller ownership remains the precondition, as with
the scalar PMM; the bitmaps are not an allocation-owner registry.

The existing 64-frame VM stack vectors now batch allocation, backing release and
unpublished rollback, covering ordinary, guarded and sparse backing. No new heap
buffer, worker, wait, ABI, grant, per-CPU reserve or default log is introduced.
Scalar/contiguous/DMA PMM calls stay unchanged. All frames stay immediately
available to any CPU until an active transaction claims them. Zero-before-
publication, per-process quiescence, existing step sizes, synchronous stop and
actual retirement retain their contracts. The existing large eager release
interrupts-off tail is unchanged.

## Matched measurements

Recorded 2026-10-26, **nested KVM**, q35/8 vCPUs, `host,+invtsc`, 4 GiB,
KVM shared monotonic-floor clock, HWP unavailable, XSAVE `0x7`/832 bytes, RAM only.
No network/disk or debugger during timing; this collector ran no build or
second benchmark concurrently. The shared host was not isolated from other
agents: a controller-correctness QEMU started during the first repetition and
remained active during later pairs and stock controls. Its CPU load was not
measured. Close interleaving and ranges reduce, but do not eliminate, that
uncertainty; particularly the small-request percent differences are not an
isolated-host regression estimate.
Clang23 builder `pyxis-llvm23.1.3-3d69e83`, same tracked configuration as the
pre-change record. Separate processes fixed on dense CPU1 through CPU N;
ready/release PIPE barrier before PROFILE/timing, actual child retirement waited.
The unchanged allocbench body uses `live=1`, 20,000 rounds for 4 KiB or 512 rounds
for 256 KiB. The programs were built once with userland `e80f50f8`; archive bytes
match across every variant of each case.

A disabled/sampled ELF is the recorded baseline `e13b8f35`; its complete
kernel/arch/include/Makefile trees equal the build base `aa48e42b`. A separate
stock A was built at `aa48e42b`. B is code commit `6c4eb628` (later documentation
only). Before editing allocator code, four fresh A cases passed. The subsequent
matrix interleaves A/B disabled and A/B sampled (reversed for repetition 2),
three fresh boots each; the independent stock matrix interleaves A/B, B/A, A/B.
60 matrix/control boots plus four initial baselines all complete with zero
allocation/child failures and actual retirement.

### Disabled-counter elapsed

Aggregate milliseconds, median (min–max), three fresh boots per cell; N clients
do N times the work. The null observer remains in both disabled kernels. This
is not a native speed estimate.

| Size | Clients | A, per-frame locks | B, bounded batches |
| --- | ---: | ---: | ---: |
| 4 KiB | 1 | 189.56 (189.36–189.93) | 191.84 (191.31–193.13) |
| 4 KiB | 4 | 385.61 (385.10–386.41) | 387.25 (384.12–388.41) |
| 256 KiB | 1 | 33.98 (32.43–39.56) | 32.23 (31.62–33.02) |
| 256 KiB | 4 | 55.26 (54.96–56.05) | 50.81 (49.98–51.65) |

The four-client 256 KiB median improves about 8.1%. One-client 4 KiB is about
1.2% slower; four-client 4 KiB about 0.4% slower with overlapping ranges. These
small-request costs are retained in the record, not discarded. One-client large
ranges overlap, so do not infer a precise single-client improvement.

### Stock-kernel large-allocation control

To check that fewer calls into the disabled counter predicate do not account for
the result, separate completely uninstrumented kernels repeat the 256 KiB case:

| Clients | A stock, ms | B stock, ms |
| --- | ---: | ---: |
| 1 | 34.03 (32.37–34.10) | 32.53 (31.76–32.63) |
| 4 | 56.86 (55.91–57.66) | 51.25 (49.42–53.34) |

Four-client stock median improves about 9.9%; ranges do not overlap. Nested
scheduling/frequency and the fixed KVM clock remain limits on generalization.
Neither matrix is a bare-metal performance claim.

### Sampled lock cost

A timestamps one in 61 single-frame lock calls. B timestamps a call when
`requested_pages % 61 < count`, so every 64-frame batch is timed: comparable
numbers of timed lock intervals per frame workload, not 61 times fewer B
observations. Zero/scratch is sampled one in 61 calls in both. Totals expand by
actual/timed calls separately for each CPU/path and use summed MEMORY-body
elapsed TSC ticks; no clock cost is subtracted. The full-instrumentation build
previously inflated contention and is not used. Sampling and outliers remain
visible; these are estimated elapsed fractions, not exact CPU-time buckets.

Four 256 KiB clients, median (min–max), percent of summed MEMORY bodies:

| Variant | Lock wait % | Lock hold % | Zero + scratch % |
| --- | ---: | ---: | ---: |
| A | 8.15 (7.95–9.10) | 7.27 (6.96–7.54) | 76.12 (75.83–89.48) |
| B | 0.12 (0.11–0.13) | 0.90 (0.90–0.93) | 88.42 (87.15–92.13) |

Per client, 32,768 frames still allocate/free and zero. Allocation and free lock
acquisitions each fall **32,768 → 512** for this aligned 64-page workload. Partial
runs remain possible for other layouts. Mean observed hold per acquisition grows
from median 188 to 1,317 raw TSC ticks, but far fewer acquisitions reduce total
hold and wait; contention has not merely moved into a larger aggregate hold.
Fragmented searches still have the existing first-fit cost under the lock;
64 bounds frames per acquisition, not a proven worst-case elapsed deadline.

The larger remaining zero/scratch interval rises proportionally to about 88%
after removing lock traffic. It includes scratch map/unmap, invlpg and memset;
it does not establish which dominates. The later scratch/page-table task must
attribute those separately before proposing a change. No scratch layout,
recursive walking or zeroing rule changed here.

## Correctness and ordinary QEMU/GDB evidence

- 1- and 4-vCPU functional runs: large/self-contained/guarded/sparse operations,
  idempotence, zeroed recommit and preserved old pages, failed pin unwind, pinned
  release refusal until actual retirement, mapping withdrawal/address wait,
  staged pipe I/O, batch-launch refusal and waited process stop. On 4 CPUs two
  independent processes run concurrently. Existing Go parallel/copied-stack/GC
  and 128 locked-native-thread retirements pass in each process.
- A 256 MiB guest requests 512 MiB through the real MEMORY service. Read-only
  GDB sees a **64-frame batch return four frames with free=0**, then detaches.
  Sparse commit returns NO_MEMORY; old bytes survive and subsequent small
  commits return zero. Oversized eager allocation also rolls back, followed by
  successful zeroed allocation/release. No fault hook, memory edit or fake error.
- After exhaustion/unwind a stopped, lock-free snapshot recounts immutable
  eligible and allocated bitmap bits: **50,805 total = 2,780 allocated + 48,025
  free**, exactly equal to all three PMM statistics.
- Cross-CPU test uses a newly created space's existing setup affinity: allocate
  on dense CPU1, narrow/move to CPU2 before sibling launch, then free. Read-only
  GDB correlates actual physical frame allocations with completed frees and
  confirms **64 frames CPU1 → CPU2**; the full 129-page release then completes.
  This tests a different executing CPU, without a new placement API.
- The same fixture makes holes with 65-page allocations, allocates 130 pages
  through the holes, checks every page zeroed and a neighboring allocation's
  contents preserved, and repeats 32 times. A sibling release/reallocation and
  actual guarded-stack pin retirement pass too.

Private helper corrections (reserved stream name in a new-space request, a
CPU0-only initial space, and pre-paging software breakpoints) were corrected;
those early runs are not counted as qualifying evidence. No production code
change was needed after the measured implementation. No private instrumentation
or test helper is merged.

Evidence, exact ELF/config/archive/source hashes, commands, counter patches,
workload sources, GDB transcripts and `analyze.py` are outside Git under
`/shared/present/bsp-removal/frame-batches/`. Public 0.0.8/0.0.9 ABI checks pass.
Native batch qualification remains open; the sealed task 1 native A/B packet is
unchanged and still tests task 1, not these batches. Shared live-root mutation,
invalidation, launch/reaper ownership and JIT remain later work.

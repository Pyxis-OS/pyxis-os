# USB storage read throughput

Owner-assigned scoped freeze exception, 2026-10-13. All three defaults below
were **accepted on 2026-10-13**, with implementation authorized. The bounded npfs
change is being qualified; native comparison remains pending. The
[native baseline](../development/experiments/usb-read-throughput.md) separates
ordinary launch timings, instrumented attribution and QEMU functional checks.

## Evidence and smallest change

The ThinkPad's existing BOT/xHCI path reads 32 MiB at 16–22 MB/s with 4-KiB
requests and 113–115 MB/s with 64-KiB requests. It can already move larger
requests efficiently without UAS or a controller rewrite. Clang's 90,347,866-byte
npfs capture issues 22,102 4-KiB READ commands on the first launch, taking 4.344 s;
warm launches still issue 21,035 and take 4.078–4.104 s. Eager image loading
then takes about 128 ms. The dominant measured interval is npfs capture reads.

At the measured baseline, `kernel/fs/npfs_store.c` reads one cache page per miss and waits for its block
request before the next. The file's misses form 45 contiguous logical/physical
runs (43 warm); splitting these at the existing 16-page device cap gives
1,379 data commands (1,316 warm), plus metadata reads. This is a grouping lower
bound, not a completed implementation. BOT still serializes three synchronous
CBW/data/CSW transfers per command. The raw curve shows the combined cost of
small commands; it does not isolate controller, device, interrupt or scheduler
CPU time. No FULL retries were observed.

Recommend bounded aggregation of consecutive clean npfs read misses within a
multi-page read, up to the device's reported cap and a 64-KiB workspace limit.
Keep existing cached/dirty-page precedence, metadata/journal overlays and sparse
zero handling. Use one BSP-worker-owned temporary staging buffer to scatter a
successful run into the ordinary cache entries; do not publish entries before
the transfer succeeds. Stop at cached pages, holes, physical discontinuities,
caller-request boundaries, EOF or the cap. Preserve cancellation, deadlines,
error propagation, provisional capture cleanup and unpublished-launch rollback.
If staging allocation fails, use the existing single-page path. Release the
workspace on every exit. No new public ABI, authority or dependency pin is needed.

This serves existing internal bulk reads, especially selected-image capture.
Ordinary small FILE/raw-disk calls retain their current ABI limits; do not claim
that every installer copy immediately benefits. Firmware/UEFI/Limine boot-archive
reads happen before Caelum and are outside this change. Do not change USB transfer
sizes, concurrency, interrupt waits, copies or architecture boundaries until a
remaining cost requires it. No image reuse or prefetch beyond the current read.

## Expected gain, not a promise

At the warmed 64-KiB raw rate, moving about 90 MB would cost roughly 0.8 s,
versus the measured 4.1–4.3 s capture interval. This extrapolates a repeated
32-MiB region to a different, larger extent. The initial full-region warmup
took 0.740 s (about 45 MB/s), so it does not demonstrate that cold 90-MB rate.
Metadata/cache lookup, mapping,
scatter copies, shorter tail runs and scheduling add cost. The useful target is
a several-fold reduction in capture time and an optimistic roughly 1–2 s warm
ordinary Clang startup;
that range is an engineering estimate, not a native result or acceptance gate.
An ideal 16-fold command reduction does not imply a 16-fold speedup. Keep the
ordinary baseline's 4.30–4.56 s version and 4.30–4.31 s hello compile as the
end-to-end comparison. Native decides whether the gain and other-path costs
justify merging.

## Owner decisions (accepted 2026-10-13)

1. **First path. Accepted:** aggregate misses in the existing multi-page npfs read
   path, retaining ordinary cache semantics. A capture-only direct-read path
   would avoid cache scatter/copies, but creates a second read/coherency path and
   does less for later bulk consumers; defer it unless measured costs justify it.
2. **Bound and lifetime. Accepted:** at most 64 KiB of reclaimable worker-owned
   staging for one read, bounded also by the device cap; fall back to single-page
   reads if staging cannot be allocated. No new read-ahead, retained image cache,
   cache-size change or asynchronous requests in flight.
3. **Qualification. Accepted:** keep BOT/xHCI unchanged and qualify the npfs change
   first in QEMU, then on the same native stick/port with interleaved ordinary
   before/after version/compile samples, separating the first post-boot launch
   from repetitions rather than claiming forced cold media. Use an unmerged probe only for command
   size/count attribution, kept separate from uninstrumented timings. Include
   fragmented, sparse, cached/dirty, unaligned and failed reads, capture rollback,
   small-read latency/throughput and shared-worker responsiveness. A Fedora
   reference remains optional; it is not required to establish this bottleneck.

After this first task the owner can: launch existing USB-backed Clang/LLD bundles
through the same authority and loader path with fewer storage commands. Implementation is authorized; native qualification is
required before merging. Image reuse, UAS and device classes remain out of scope.

## Implementation qualification

Aggregation covers aligned interiors containing at least two full requested
pages; partial heads/tails and ordinary FILE reads keep the original path. Runs
stop before metadata that is not resident, without speculative backing I/O or
publishing a future error. Cache slots retain their existing selection policy;
mandatory cache storage precedes optional staging, and each page is consumed
before another reservation can evict it. Staging is released on every exit.
A failed aggregated ticket contributes no confirmed bytes within that ticket;
earlier successful chunks retain their known progress. No failed-run retry.

- [x] Implement bounded synchronous aggregation, with allocation fallback.
- [ ] Complete QEMU byte/error/cleanup qualification and record its limits.
- [ ] Native interleaved comparison on the same stick/port, separately from
  debugger/probe attribution. The owner runs the native packet before merge.

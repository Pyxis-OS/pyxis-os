# Writable filesystem per-call latency

Status: **owner direction, 2026-10-02.** The decisions under
[owner direction](#owner-direction) are accepted. Everything under
[proposed criteria](#proposed-acceptance-criteria) and
[candidate directions](#candidate-directions) is a proposal for review; it does
not authorize implementation. Baseline: Pyxis `b7b9e72`, filesystem pin
`a250731`, with measurements from the
[overflow-split record](../../fs/docs/overflow-split-measurements.md)
(measured revision `d6175e8`).

## Owner direction

- **Write amplification is acceptable for now.** At the current pin, all nine
  matched RAM-only histories submit fewer bytes than Btrfs. ext4 remains the
  longer-term reference. No further write-amplification work is assigned: the
  [write-efficiency plan](filesystem-write-efficiency.md) is paused, not closed,
  and its remaining proposals stay unassigned.
- **Per-call latency is unacceptable, and it is now the blocking problem** for
  the writable filesystem. At the measured cost, every individually durable call
  takes tens of milliseconds, growing with the amount of data already stored. On
  a real disk with a realistic population, anything issuing synchronous writes
  (the shell, editors, builds, package steps) would bring the whole system to a
  crawl.
- **Latency is a primary metric from now on.** Filesystem changes report per-call
  latency and its growth with population next to submitted bytes. A write saving
  does not justify a latency regression, and the reverse needs an explicit
  decision too.

## Evidence

Measurement windows only (512 individually durable calls each, three windows per
case), both repetitions, from the recorded
[measurement JSON](../../fs/docs/measurements/overflow-split.json). All storage is
RAM-backed. "Planning reads" counts blocks read during planning per publication.
`J` is the largest source allocation map, in nodes, seen in those windows.

| Background | Case | Pyxis ms/call | Planning ms/call | Worst planning ms | Planning reads/publication | Max J | Btrfs ms/call | ext4 ms/call |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 MiB | append | 24.0 | 18.8 | 39 | 68 | 105 | 0.15 | 0.05 |
| 1 MiB | compiler | 13.4 | 8.4 | 7–8 | 29 | 27 | 0.13 | 0.04–0.05 |
| 1 MiB | overwrite | 25.5 | 17.2 | 22 | 63 | 66 | 0.13 | 0.02 |
| 8 MiB | append | 44.0–44.4 | 37.4–37.8 | 70–71 | 125 | 133 | 0.13 | 0.05 |
| 8 MiB | compiler | 43.7–43.9 | 38.4–38.5 | 38–41 | 106 | 104 | 0.12–0.13 | 0.04 |
| 8 MiB | overwrite | 49.2 | 40.3 | 52 | 131 | 133 | 0.13–0.14 | 0.02 |
| 20 MiB | append | 77.5–77.7 | 71.8–72.0 | 187–288 | 191 | 198 | 0.12–0.16 | 0.05–0.08 |
| 20 MiB | compiler | 72.2–73.0 | 66.4–67.1 | 58–74 | 174 | 172 | 0.12–0.13 | 0.05 |
| 20 MiB | overwrite | 79.7–79.9 | 70.4–70.6 | 165–166 | 197 | 198 | 0.12–0.13 | 0.02 |

What this shows:

- **Hundreds to thousands of times slower than the reference filesystems.** A
  durable 4 KiB call takes 13–80 ms in Pyxis, against 0.12–0.16 ms for Btrfs and
  0.02–0.08 ms for ext4. The worst single planning step takes 288 ms.
- **Cost grows with population, not with the change.** The calls are the same
  size at every population, yet latency rises 3–5x from 1 MiB to 20 MiB of
  background data.
- **Planning dominates.** It is 63–93% of each call (most of it at 8 and 20 MiB).
- **Every publication reads the whole allocation map.** Planning reads per
  publication track `J` almost exactly (191 vs 198, 174 vs 172).

Code inspection agrees with the measurements. Each publication in
[`build_publication`](../../fs/core/writer.c):
- reloads and validates every source map node from media
  ([`pfs_incremental_map_load`](../../fs/core/incremental_map.c)), including
  source-load duplicate detection that the
  [write-efficiency plan](filesystem-write-efficiency.md) records as `O(J²)`;
- rebuilds the block-sorted descriptor index;
- regenerates the complete candidate record stream from the immutable source on
  every closure pass;
- seals by iterating all candidate records and claims.

None of this work is proportional to the size of the change.

Limits of this evidence:

- **Instrumented runs.** The comparison harness sets `collect_map_metrics`, so
  each local publication also counts a bulk-reference map. That inflates planning
  by an unrecorded amount, though the counting is itself population-sized.
- **Not like-for-like.** Pyxis runs as a host library through callbacks, while
  Btrfs and ext4 run in the kernel. Everything is RAM-backed, so these are not
  NVMe latencies.
- **No per-call distribution.** Only window totals and per-phase planning maxima
  are recorded, with no p50 or p99.
- **Small populations.** The largest population is 20 MiB. Extrapolating the
  trend to gigabytes predicts seconds per call. That is an extrapolation, not a
  measurement.

None of these limits plausibly accounts for a factor of 100 or more, or for the
growth with population.

## Goal

Per-call work, including planning reads, CPU and writes, must be proportional to
the size of the change and to tree height, not to the filesystem's population.
Population-sized work belongs at mount, in scrub/check, or in explicitly
scheduled maintenance. It must never be on every durable call's critical path.

All existing correctness requirements are preserved unless a separate decision
changes them:
- individually durable completed calls;
- both retained states and their payloads;
- failure provenance and the READABLE_STOPPED/ACCESS_STOPPED distinction;
- funded admission, memory ceilings and the RAM-only rule for write-heavy
  workloads.

Write amplification must stay Btrfs-comparable while latency is fixed.

## Proposed acceptance criteria

These are proposals for the owner to accept, change or replace:

1. **Population independence.** Planning reads per publication and median
   per-call latency stay flat, within measurement noise, from 1 MiB to 20 MiB
   background. They must also stay flat on at least one larger populated case
   that fits the RAM budget.
2. **Absolute latency.** On the matched RAM matrix, uninstrumented median
   per-call latency is within an owner-chosen factor of Btrfs (for example 10x),
   with p99 and maximum reported.
3. **No hidden costs.** Submitted bytes stay Btrfs-comparable, and any new
   population-sized work (mount, scrub, checkpoint) is reported with its own
   cost and frequency.

## Candidate directions

Proposals only. Choosing among them, or rejecting them, is an owner decision
that follows the first task below.

- **A. Keep validated state across publications.** The writer produced and
  checksummed the previous state itself and already holds its admitted map
  records in memory. Keep the source topology, index and validation results
  across publications, and validate only blocks newly read from media. This
  needs an explicit integrity decision: today every publication re-validates its
  source from media, which detects media corruption early. Moving full validation
  to mount and scrub changes when corruption is detected, not whether it is.
- **B. Make candidate construction incremental.** Apply each call's deltas to the
  touched leaves and paths instead of regenerating the full canonical record
  stream from the source on every closure pass. Sealing and admission would then
  account for the change, not the population.
- **C. Separate per-call durability from tree publication (intent log).** A
  completed call becomes durable by appending one compact logical record and
  flushing. The copy-on-write tree publication happens per batch, by count, time
  or space pressure, and recovery replays the log over the last published state.
  This is the Btrfs log-tree / ZFS intent-log approach. It moves planning off
  the per-call critical path entirely and also reduces writes per call. Unlike
  A and B, it changes the durability and recovery contract: individually durable
  calls stay, but publication is amortized across them. It needs its own design
  and recovery qualification.

A and B attack the population scaling directly and keep the current contract.
C is the larger and longer-term lever. They are not exclusive.

## Out of scope

- Further write-amplification tuning: map-editor trials, neighbour repair, split
  or merge solving, placement or fill policy.
- Device-backed workloads, which still need an agreed budget.
- Native mounting, FUSE and installation.

[Task 7 and writable deployment](writable-filesystem-core.md#focused-tasks)
remain open, now blocked on latency.

## Focused tasks

1. [ ] **Latency baseline and breakdown (measurement only).** Using the existing
   comparison harness and RAM launcher, with no new framework:
   - run the matched matrix (or at least every 20 MiB case) with
     `collect_map_metrics` disabled;
   - record per-call p50/p99/max;
   - break planning down per publication: source load/validation, index build,
     candidate regeneration per closure pass, closure, encoding,
     admission/sealing and I/O callbacks;
   - add one larger populated case within the RAM budget.

   The output names which components grow with population, and by how much.
2. [ ] **Design proposal with a cost model.** For the candidate directions (and
   any better ones), predict per-call reads, CPU work and writes as a function of
   change size and population. Also give the effect on the durability and
   recovery contract and the expected latency against the accepted criteria. The
   owner chooses the direction and the criteria.
3. [ ] **Implement the accepted direction.**
4. [ ] **Matched validation.** Repeat the task-1 measurements, the quick and
   extended suites, and the matched write comparison. Report latency, its growth
   with population, and submitted bytes together.

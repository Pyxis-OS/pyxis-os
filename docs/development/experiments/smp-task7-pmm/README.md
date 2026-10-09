# SMP task 7: PMM word search

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Matched before/after record for the PMM follow-up between SMP tasks
[7a](../smp-task7a/README.md) and 7b, recorded on 2026-10-05. The owner chose it
after 7a measured page clients serializing on the PMM's bit-by-bit scan. These
are nested-VM measurements: the development host is itself a KVM guest (Fedora
44, Linux 6.19.10, 8 vCPUs, virtualized i9-12900K), and its own KVM has
`halt_poll_ns=200000` and EPT for its guests. They are not owner-host or
ThinkPad results.

## Change

The PMM bitmaps are 64-bit words. A search:

- skips fully allocated words whole;
- takes the lowest clear bit of the first word that has one (`__builtin_ctzll`);
- stops checking a run once it has seen enough frames.

Every frame below a first-candidate hint is unavailable, so searches start
there. Allocation advances the hint and freeing lowers it. Placement is still
first fit, under the same lock.

Splitting a word into 32-bit halves was considered and not used. Once a word has
a clear bit, one count-trailing-zeros instruction finds it; halves would only
add a branch.

## Configuration

| Item | Before | After |
| --- | --- | --- |
| Pyxis | main `f713b81` (task 7a) | this PR's code commit `564b8db` |
| Kernel ELF SHA-256 | `83c6a57a…fc1c90c1d1` | `1fff98ca…7eaace1746` |
| Pins | userspace `76eef93`, fs `d352c7e`, ports `36d952e`, lwIP `a1aadb9` | same |

The setup and workloads are those of the [7a record](../smp-task7a/README.md#configuration):

- `P` is `allocbench pages --rounds 2048`.
- `H` is `allocbench heap --rounds 262144`.
- The mixed set is H, P, a RAM-file write and ttcp.

## Results

Every sample exited 0 with no failures. Values are medians with min–max ranges.

### Private memory, five fresh processes per row, ms

| Workload | 4 CPUs before | 4 CPUs after | 1 CPU before | 1 CPU after |
| --- | ---: | ---: | ---: | ---: |
| `allocbench pages` | 11.598 (11.491–11.892) | 4.997 (4.941–5.146) | 13.283 (12.227–14.198) | 5.798 (4.810–6.518) |
| `allocbench growth` | 27.049 (25.848–27.785) | 8.415 (7.955–10.191) | 30.088 (28.855–33.987) | 9.607 (9.193–12.332) |
| `allocbench heap` (control) | 12.844 (12.653–13.042) | 13.087 (12.606–13.203) | 15.044 (14.035–15.853) | 14.447 (13.429–14.615) |

### Concurrent sets, 4 CPUs, s

| Set | Before | After |
| --- | ---: | ---: |
| P alone, internal elapsed | 0.372 (0.370–0.380) | 0.156 (0.152–0.160) |
| 2 × P, aggregate | 0.725 (0.707–0.728) | 0.348 (0.340–0.351) |
| 4 × P, aggregate | 1.403 (1.333–1.434) | 0.563 (0.562–0.563) |
| Mixed, aggregate | 1.274 (1.267–1.292) | 1.279 (1.264–1.302) |

The mixed set is unchanged at 4 CPUs because its P client was already a small
part of it. On 1 CPU the mixed set took 2.515 s (2.510–2.537), against 2.823 s
(2.820–2.827) before.

### PMM lock

The same counter build as in 7a, ported to the new code
(a throwaway patch, not kept in the tree):

- **Two P together:** 133,548 lock calls spent about 150 cycles each waiting.
  That is negligible next to the 4.5 G wait cycles in 7a.
- **Holding the lock** fell from about 36,700 cycles per call in 7a to about 75
  per call.

The lock no longer serializes the clients.

## Remaining concurrency cost: scratch-slot false sharing

Two P clients still each took about 290 ms against 156 ms alone, so something
else is shared. Sizes isolate it:

| Workload | Alone | Two together | Two together, slots spread |
| --- | ---: | ---: | ---: |
| `allocbench pages --size 4096 --rounds 20000` | 104.4, 104.7 | 179–183 | 140–142 |
| `allocbench pages --size 262144 --rounds 500` | 150.6, 149.3 | 283–287 | 211–217 |
| `allocbench pages` | 151.6, 152.0 | — | 216–223 |
| `allocbench heap --rounds 262144` | about 810 | 804–818 | — |

Values are ms; each cell holds the two repetitions, or both clients' range.

- **Per-page cost:** the slowdown follows pages, not syscalls, and pure userspace
  computation scales perfectly.
- **Where it comes from:** each page maps and unmaps scratch slots about a dozen
  times, for zeroing and for each page-table level read. Every CPU's slot PTEs
  share one 64-byte cache line of the scratch page table, and the `scratch_busy`
  flags share another.
- **Experiment:** a throwaway build (64 CPUs at most) gave each CPU's PTEs and flags their own line. That removed
  roughly half of the paired slowdown.
- **What remains:** after that, two clients still take about 1.4 times as long as
  one. The remaining shared state is the PMM and heap locks, their counters, and
  bitmap words that both clients' adjacent frames share. None of it was traced
  further.

This PR does not change the scratch layout; the
[technical-debt entry](../../../technical-debt.md#scratch-slot-false-sharing)
records the options.

## Correctness

The task-5 stress patch ran on this PR at 4 and 12
CPUs. Every AP allocated, zeroed, pattern-checked and freed frames, built and
walked private roots, and made oversized requests that scan the whole bitmap
and fail. All APs reported no failures and no mismatches. Each AP's bitmap
recount matched the statistics, and every oversized request was refused. The
remote workloads ran normally afterwards.

## Not exercised

- No native ThinkPad run; the false-sharing cost in particular may differ
  natively. Task 8 covers native validation.
- Multi-page runs were exercised only with 1–4 frames in the stress patch and
  with DMA and boot allocations; no workload asks for long contiguous runs.

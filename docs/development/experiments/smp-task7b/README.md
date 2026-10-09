# SMP task 7b: userspace on the BSP

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Matched before/after record for SMP task 7b
([milestone](../../../wip/scheduling-and-threads.md)), recorded on 2026-10-06.
These are nested-VM measurements: the development host is itself a KVM guest
(Fedora 44, Linux 6.19.10, 8 vCPUs, virtualized i9-12900K), and its own KVM has
`halt_poll_ns=200000`. They are not owner-host or ThinkPad results.

7b lets user tasks run on the BSP of a multicore boot: in placement, in idle
pulls and in affinity sets. Placement ties go to the parent's CPU, then the
lowest AP, then the BSP. The BSP also runs the kernel workers, so it gets user
work only when it is no busier than the APs.

## Configuration

| Item | Before | After |
| --- | --- | --- |
| Pyxis | main `ac12d20`, built as `564b8db` (same code) | this PR's code |
| Kernel ELF SHA-256 | `1fff98ca…7eaace1746` | `d49c91af…72daea90` |
| Userspace | `76eef93` | `2d17795` (shell: no CPU-0 affinity message) |
| Other pins | fs `d352c7e`, ports `36d952e`, lwIP `a1aadb9` | same |

The setup matches the [7a record](../smp-task7a/README.md#configuration): a
default image on QEMU 10.2.2 with KVM, Q35, `-cpu max`, 256 MiB and 4 CPUs.

- `H` is `allocbench heap --rounds 262144`.
- `P` is `allocbench pages --rounds 2048`.
- `W` is `iobench write home://…bin --rounds 2`, a RAM file.
- `X` is `ttcp -t -n 192 10.0.2.2`.

Concurrent sets use one remote session per client. The remote server admits
four clients, so the loaded sets use a small driver script instead: one
session starts four background `allocbench heap --rounds 1000000` jobs
(about 3 s each), then runs X or W in the foreground and collects every result.
Each set ran three times.

## Results

Every sample exited 0 with no failures. Values are medians with min–max ranges.

| Set | Before | After |
| --- | ---: | ---: |
| H alone, ms | 819 (818–826) | 834 (833–836) |
| P alone, ms | 152.5 (152.1–154.5) | 151.9 (151.9–152.4) |
| W alone, transfer ms | 6.7 (6.5–8.1) | 7.7 (6.8–8.3) |
| X alone, MiB/s | 1.300 (1.294–1.324) | 1.293 (1.285–1.310) |
| 2 × H, aggregate s | 0.859 (0.847–0.888) | 0.846 (0.845–0.849) |
| 4 × H, aggregate s | 1.267 (1.260–1.268) | 0.931 (0.930–0.932) |
| 4 × H, each client, ms | 794–1195 | 793–885 |
| 4 × P, aggregate s | 0.546 (0.537–0.553) | 0.473 (0.466–0.476) |
| Mixed H + P + W + X, aggregate s | 1.208 (1.202–1.212) | 1.213 (1.205–1.217) |
| X under 4 background H, MiB/s | 1.280 (1.267–1.286) | 1.256 (1.249–1.267) |
| W under 4 background H, transfer ms | 8.4 (4.8–9.9) | 7.2 (4.7–14.1) |
| Background H medians during the X / W sets, ms | 3900 / 3895 | 3536 / 3205 |

- **Compute:** four compute clients now use four CPUs. Before, two of them
  shared an AP.
- **Page clients** also spread further, but they are still limited by the
  [scratch-slot false sharing](../../../technical-debt.md#scratch-slot-false-sharing).
- **Unloaded rows:** H and W alone differ slightly between the boots. Neither
  depends on the BSP being eligible.

**Effect on the BSP's own work.** With every CPU busy with compute, network
transfer kept 98% of its earlier rate (1.256 against 1.280 MiB/s). The RAM-file
write median improved, but its maximum rose from 9.9 to 14.1 ms in one of the
three runs. The mixed set did not change. Display presentation and input
latency, which also run on the BSP, were not measured.

## Checks

- **A CPU-0-only space and `affinity 0`.** A 4-CPU boot used
  `SPACE_CPUS='readonly=0'`, and Development's init was replaced by
  an init that runs `affinity 0`, then
  starts three background compute jobs.
  - Both spaces started.
  - Four GDB snapshots taken while the jobs ran
    showed every Pinned and Read-only task running or queued on CPU 0, among
    kernel workers, while CPUs 1–3 were idle and pulled nothing.
  - Each job took 9.4–9.6 s, about three times one alone.
- **CPU counts.** 1, 2 and 12-CPU boots ran two H, one P and `ls`
  concurrently. On 2 CPUs the two H clients took 0.93 and 1.06 s, one per CPU.
  No boot logged a panic.

## Not exercised

- No native ThinkPad run; task 8 covers it, including interactive feel with the
  BSP loaded.
- Display smoothness and input latency under BSP load were not measured.
- No long BSP user syscall, such as a very large allocation, was timed against
  worker latency.

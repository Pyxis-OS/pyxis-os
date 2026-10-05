# SMP task 7a: local private memory

Matched before/after record for [SMP task 7a](../../../wip/scheduling-and-threads.md),
recorded on 2026-10-05. These are nested-VM measurements: the development host is
itself a KVM guest (Fedora 44, Linux 6.19.10, 8 vCPUs, virtualized i9-12900K), and
its own KVM has `halt_poll_ns=200000`. They are not owner-host or ThinkPad results.

7a moves private-memory ALLOCATE and RELEASE from the BSP request queue into the
caller's own syscall. The BSP still runs no userspace on multicore boots; that is
7b.

## Configuration

| Item | Before | After |
| --- | --- | --- |
| Pyxis | main `a87468c` (task 6) | this PR's kernel commit `e0e23ff` |
| Kernel ELF SHA-256 | `60d6fdb8…b3624ab7` | `ce28fe46…02075994` |
| Userspace | `2f01df6` | `76eef93` (allocbench prints the new profile) |
| Other pins | fs `d352c7e`, ports `36d952e`, lwIP `a1aadb9` | same |

The setup matches the [task-5 record](../smp-task5/README.md#configuration): a
default `make -j16 image`, QEMU 10.2.2 with KVM, Q35, `-cpu max` and 256 MiB, with
virtio-net, virtio-rng and a blank USB stick on `qemu-xhci`. Commands ran in
remote sessions, sequentially or, for concurrent sets, one session per client
submitted together.

- `H` is `allocbench heap --rounds 262144`, pure userspace computation.
- `P` is `allocbench pages --rounds 2048`, 64 KiB private allocate/release pairs.
- The mixed set runs H, P, `iobench write home://mixN.bin --rounds 2` and
  `ttcp -t -n 192 10.0.2.2` together, with a host `ttcp -r` receiver.

It replaces the task-1 mixed set's native-pool write with a RAM-file write,
because this setup has no pool disk. Each set ran three times.

## Results

Every sample exited 0 with no failures. Values are medians with min–max ranges.

### Private memory, five fresh processes per row, ms

| Workload | 4 CPUs before | 4 CPUs after | 1 CPU before | 1 CPU after |
| --- | ---: | ---: | ---: | ---: |
| `allocbench pages` | 37.969 (36.848–39.359) | 11.345 (11.318–11.379) | 38.092 (37.819–39.384) | 13.295 (12.170–13.356) |
| `allocbench growth` | 51.966 (50.142–54.698) | 25.235 (25.034–26.466) | 52.719 (52.507–53.849) | 29.675 (28.800–32.118) |
| `allocbench heap` (control) | 13.568 (12.971–14.294) | 12.900 (12.312–13.555) | 14.424 (13.277–14.579) | 14.937 (13.971–15.085) |

From the `--profile` runs on 4 CPUs (one process each), the address-space work
itself did not change. The time saved was the BSP handoff:

| Profile | Before: service / total mean | After: service / total mean |
| --- | --- | --- |
| `pages` allocate | 191 / 515 µs | 200 / 264 µs |
| `pages` release | 59 / 352 µs | 55 / 119 µs |
| `growth` allocate | 232 / 532 µs | 229 / 301 µs |

Before, total ran from request preparation to resumption and included BSP
queueing. After, it runs from syscall entry through reply copying.

### Concurrent sets, 4 CPUs (three application CPUs), aggregate host wall time, s

| Set | Before | After |
| --- | ---: | ---: |
| P alone, internal elapsed | 1.052 (1.051–1.056) | 0.359 (0.359–0.360) |
| 2 × P | 0.998 (0.988–1.006) | 0.706 (0.685–0.721) |
| 4 × P | 2.028 (1.986–2.069) | 1.392 (1.327–1.431) |
| Mixed | 2.333 (2.319–2.339) | 1.240 (1.238–1.260) |

Each mixed client, ranges over three repetitions, 4 CPUs:

| Client | Before | After |
| --- | --- | --- |
| H elapsed | 790–808 ms | 794–825 ms |
| P elapsed | 2258–2283 ms | 418–424 ms |
| RAM write, transfer median | 11.3–12.5 ms | 5.8–6.9 ms |
| ttcp | 0.728–0.733 MiB/s | 1.219–1.242 MiB/s |

Alone, ttcp sent at 1.293–1.300 MiB/s before and 1.304–1.316 after. Under mixed
load it now keeps about 95% of that, up from about 56%: page clients no longer
occupy the BSP, which also runs the network worker.

On 1 CPU the mixed set took 2.729 s (2.727–2.732) after, against 3.596 s
(3.591–3.608) before. Its P client fell from 3268–3514 ms to 777–890 ms, while
its H client rose from 961–1050 ms to 1245–1469 ms. Everything shares one CPU
there, so a faster P takes a larger share of it while running. Before, P's memory
work ran in the BSP executor, a kernel task. This share shift is an explanation
from inspection, not measured.

Full output: `pre4-output.txt`, `post4-output.txt`, `pre1-output.txt` and
`post1-output.txt`.

### PMM lock contention

Page clients now run in parallel, but they do not scale. Two P clients on
separate CPUs each took 628–664 ms, against 359 ms for one alone. Four clients
on three CPUs split into 935–1027 ms and 1230–1350 ms.

A throwaway build ([pmm-counters.patch](pmm-counters.patch)) counted PMM lock
calls, bitmap bits scanned, and `rdtsc` cycles spent waiting for and holding the
lock. GDB read the counters ([checks.txt](checks.txt)). `rdtsc` appears costly in
this nested VM, so the build's absolute times are inflated; only the ratios are
used.

| Interval | Lock calls | Bits scanned per call | Wait cycles | Hold cycles |
| --- | ---: | ---: | ---: | ---: |
| One P | 66,774 | about 7,670 | 4.0 M | 2.45 G |
| Two P together | 133,548 | about 7,980 | 4.50 G | 5.10 G |

Each frame allocation scans the bitmap bit by bit from frame 1, past every
allocated frame, while holding the lock. Alone, a client almost never waits. With
two, waiting is about as large as holding, so the clients largely take turns.
This is the [technical-debt entry](../../../technical-debt.md#pmm-first-fit-search-under-its-lock)
whose revisit point was task 7. 7a does not change the PMM; that awaits an owner
decision.

## Validation

- **Out-of-frames unwind off the BSP.** Three remote clients each held 64 MiB of
  1 MiB regions at once, more than the guest's free memory. Two received
  NO_MEMORY partway through, on APs, after 40 and 37 regions, and exited 1; the
  third completed. Afterwards, the PMM's free frames (31,691) and the heap's
  live allocation count (2,200) were back to exactly their earlier values.
- **Termination.** Ctrl-C through the remote terminal stopped
  `allocbench pages --rounds 1000000` after one second and `allocbench growth`
  after 10 ms. Both reported `terminated`, and the PMM and heap counters were
  unchanged afterwards. A normal run then completed. All of this is in
  [checks.txt](checks.txt).
- **Distinct roots and migration.** A GDB breakpoint on
  `private_memory_allocate()` logged the CPU, process and CR3 of 1,500 calls
  during four page clients, one of them short so that its exit freed a CPU
  ([migration.gdb](migration.gdb), [migration-gdb.txt](migration-gdb.txt)).
  - **Own root:** every call ran with CR3 equal to its own process's root.
  - **Concurrent:** different processes made calls on CPUs 1, 2 and 3 at once.
  - **Migrated:** three processes made calls first on one CPU, then on another:
    1→2, 1→3 and 3→2.

  An earlier 400-call trace without the short client showed no migration.
- **1-CPU boot:** everything runs on the BSP there; all rows passed.

## Not exercised

- No native ThinkPad run. Task 8 covers native validation.
- Concurrent callers sharing one address space cannot occur under the
  single-task process model, so they were not tested.
- Termination arriving while a memory syscall is in progress takes effect at
  syscall return. That follows from interrupts being masked in syscalls, not from
  a forced race.

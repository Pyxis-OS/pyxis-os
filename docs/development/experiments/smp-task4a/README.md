# SMP task 4a: placement and balancing

Matched before/after record for [SMP task 4a](../../../wip/scheduling-and-threads.md),
recorded on 2026-10-05. These are nested-VM measurements: the development host is
itself a KVM guest (Fedora 44, Linux 6.19.10, 8 vCPUs, virtualized i9-12900K), and
its own KVM has `halt_poll_ns=200000`. They are not owner-host or ThinkPad results.

## Configuration

| Item | Before | After |
| --- | --- | --- |
| Pyxis | main `d30e678` (task 3) | task 4a branch on `d30e678` |
| Kernel ELF SHA-256 | `30220efe…24a1239` | `04191bac…f8e48` |
| Pins | fs `d352c7e`, userspace `28b2516`, ports `36d952e`, lwIP `a1aadb9` | same |

Everything else matches the [task-1 baseline](../smp-task1-baseline/README.md):

- **Emulator:** QEMU 10.2.2 with the AHCI fix, KVM, Q35, `-cpu max`, 256 MiB.
- **Devices:** virtio-blk with a 128 MiB native pool on a `/dev/shm` disk, virtio-fs, virtio-net, virtio-rng, and `qemu-xhci` with a blank USB stick.
- **Workload commands:** unchanged.
- **Boot configuration:** the task-1 init in the space grammar: `INIT=init-baseline.sh SPACES=baseline=app://init MOUNT_DISK=…`.

Remote clients are children of that one space. Before the change they all run on its
CPU; after it, the scheduler spreads them.

Pipe and endpoint IPC need the `session` handoff, so they ran from local shells:

- **Spaces:** a separate image with six `init-readonly` spaces.
- **Driving:** the commands were typed through QEMU `sendkey`.
- **Reading results:** reports were read from `screendump` images and transcribed into [pipe-ipc.txt](pipe-ipc.txt).
- **Workloads:** `session app://iobench.pxe pipe --buffer 4096` in three spaces and `session app://ipcbench.pxe call --size 64 --messages 8` in three.

Each invocation verified one warmup and five passes.

## Results

All workloads verified every sample, with no failures. Each pool passed `fsck.npfs`
after its boot. Values are medians with min–max ranges.

### Concurrent sessions, 4 CPUs

Aggregate host wall time over three repetitions, from the first submission to the last completion.
`H` is `allocbench heap --rounds 262144`, `P` is `allocbench pages --rounds 2048`, and
the mixed set adds a native write and a TCP transfer.

| Set | Before | After |
| --- | ---: | ---: |
| 2 × H | 1.767 s (1.762–1.770) | 0.852 s (0.850–0.855) |
| 4 × H | 3.349 s (3.348–3.364) | 1.275 s (1.273–1.346) |
| 2 × P | 0.888 s (0.888–0.931) | 0.895 s (0.891–0.925) |
| 4 × P | 1.851 s (1.820–1.898) | 1.791 s (1.784–1.798) |
| Mixed | 3.404 s (3.149–3.411) | 3.382 s (3.377–3.383) |

- **Compute-bound clients** now run in parallel. Two clients take about one client's time. Four clients on three application CPUs take 1.5 times one client.
- **P and the mixed set** are bound by BSP private-memory service and by BSP workers, so they are unchanged. Task 7 moves private memory off the BSP.

### Single workloads

| Workload | 4 CPUs before | 4 CPUs after | 1 CPU before | 1 CPU after |
| --- | ---: | ---: | ---: | ---: |
| `allocbench heap`, ms | 13.198 | 12.531 | 14.240 | 14.553 |
| `allocbench pages`, ms | 35.257 | 33.131 | 35.736 | 35.962 |
| `allocbench growth`, ms | 46.757 | 44.936 | 46.915 | 47.880 |
| HOST read payload, ms | 162.131 | 143.576 | 171.530 | 171.903 |
| Native grow write transfer / sync, ms | 193.795 / 36.364 | 197.190 / 37.140 | 225.753 / 36.405 | 205.382 / 33.034 |
| TCP, MiB/s | 1.324 | 1.328 | 1.260 | 1.225 |

These differences fall within the spread seen between runs on this VM. The 1-CPU
native write was faster after the change, although that path is unchanged; it is recorded,
not attributed. The full per-sample output is in `pre4-output.txt`, `post4-output.txt`,
`pre1-output.txt` and `post1-output.txt`.

### Pipe and endpoint IPC, 4 CPUs

Pipe completion is the producer start through the consumer's acknowledgment, in ms:

| Run | Before | After | After, each space limited to one CPU |
| --- | --- | --- | --- |
| 1 | 1.042 (1.017–1.132) | 2.940 (0.854–3.949) | 1.039 (1.028–1.120) |
| 2 | 1.027 (1.017–1.036) | 1.088 (0.768–4.145) | 1.004 (0.995–1.015) |
| 3 | 1.033 (1.029–1.155) | 0.841 (0.805–1.755) | 1.056 (1.034–2.738) |
| Repeat | 1.030, 1.019, 1.133 | — | 1.052, 1.044, 1.155 |

- **Bimodal pipe passes:** after the change, each pass's coordinator and fresh workers are spread across idle CPUs. Passes then split two ways:
  - Some are faster than before, down to 0.77 ms.
  - Some are three to four times slower, up to 4.1 ms.
- **The cause is placement:** limiting each space to one CPU (`SPACE_CPUS`) restores the old numbers on the new kernel, so the scheduler bookkeeping costs nothing measurable.
- **Probable mechanism (not directly measured):** with producer and consumer on different CPUs, each blocking 4 KiB hand-off wakes another vCPU, often a halted one. That wake is expensive in this nested VM.
- **On real hardware**, the cost of these cross-CPU wakes has not been measured.

The endpoint CALL medians changed little:

| Set | Medians, ms |
| --- | --- |
| Before | 2.045, 2.044, 2.032 |
| After | 2.049, 1.960, 2.018 |
| After, limited to one CPU | 2.071, 2.080, 2.041 |

Every set, including both before-runs, has occasional 2.6–4.8 ms passes.

### Placement check

A conditional GDB breakpoint ([batch-gdb.txt](batch-gdb.txt)) fired just after a two-stage
publication had released the queue lock and before any notification. The command was
`ls app:// | head -n 1` from a remote session, on 4 CPUs.

- **Destinations:** the batch had two distinct destinations, CPUs 2 and 1.
- **Queues:** each of those CPUs' queues held one task whose `cpu_index` matched, and CPU 3's queue was empty.

So the members were spread across queues before either could run.

### Migration check

Four longer heap clients ran (`--rounds 1000000`). Read-only GDB snapshots, about 0.6 s
apart, recorded each CPU's running and queued task ([migration-gdb.txt](migration-gdb.txt)).

- **Before:** CPU 2 ran task `…a020` with `…e240` queued behind it.
- **After CPU 1's client exited:** CPU 1 was running `…a020`, which an idle pull had moved
  mid-computation after a user-mode preemption.

## Not exercised

- No native ThinkPad run yet. [thinkpad-check.md](thinkpad-check.md) gives the
  owner-run commands and the staged PXE builds.
- The preemption push (move at a load gap of two) was not caught in the debugger. Its
  locals are optimized out at the requeue site, so a `dprintf` could not be attached.
- The 1-CPU control was not run under the debugger.

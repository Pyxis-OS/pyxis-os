# SMP task 8a: final validation

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Final matched record for the [runtime SMP milestone](../../../wip/scheduling-and-threads.md),
recorded on 2026-10-06. It compares main `7748db5` (SMP tasks 1–7) with main
`83c08d6`, the pre-milestone revision of the [task-1 baseline](../smp-task1-baseline/README.md).

There are two parts:

- **Owner-run native check:** on the ThinkPad T14 Gen 1, by PXE.
- **QEMU runs:** in the nested VM. The development host is itself a KVM guest
  (Fedora 44, Linux 6.19.10, 8 vCPUs, virtualized i9-12900K), and its own KVM
  has `halt_poll_ns=200000`.

## Native ThinkPad check (owner-run)

The ThinkPad is a T14 Gen 1 with a Ryzen 5 PRO 4650U: 6 cores and 12 threads,
with SMT siblings adjacent as Pyxis CPUs. The owner booted two PXE entries built
with `make image` and the default configuration:

| Entry | Source | Kernel SHA-256 |
| --- | --- | --- |
| Baseline | main `83c08d6` | `d0d0fde7…603f3406` (the task-1 baseline kernel) |
| Final | main `7748db5` | `e4427b95…fecb4c` |

A check script ran from the desktop. Each batch opened one remote
session, started N background clients and waited for all of them.

| Batch | Baseline: wall, each client (s) | Final: wall, each client (s) |
| --- | --- | --- |
| heap × 1 | 1.310; 1.294 | 1.296; 1.289 |
| heap × 4 | 5.197; 4.770–5.164 | 2.352; 1.296, 1.445, 2.255, 2.258 |
| heap × 8 | 10.374; 8.402–9.776 | 3.598; 2.184–3.328 |
| heap × 11 | 14.258; 10.140–12.482 | 4.432; 2.754–4.086 |
| pages × 1 | 1.391; 1.375 | 0.100; 0.092 |
| pages × 2 | 2.543; 2.486, 2.498 | 0.163; 0.105, 0.115 |
| pages × 4 | 5.069; 4.948–4.998 | 0.263; 0.105–0.117 |
| pages × 8 | 10.456; 10.171–10.308 | 0.423; 0.106–0.137 |

`heap` is `allocbench heap --rounds 262144` (computation only). `pages` is
`allocbench pages --rounds 2048` (private memory). No client failed.

- **Computation** runs in parallel. On the baseline, every remote client shared
  the remote space's CPU. With four clients, two finished at single-client speed
  and two at about 2.25 s, which fits two clients on SMT siblings of one core
  ([CPU topology](../../../targets/t14-gen1-amd/notes.md)). With 11 clients, each
  took about three times its solo time. That fits SMT plus the 15 W processor's
  lower all-core clock; neither was measured separately.
- **Private memory** is about 15 times faster for one client natively, because it
  no longer waits for the BSP. Page clients barely slow each other: two took 1.15–1.25
  times one alone, and eight up to 1.5 times. In the nested VM the same pair took
  about 1.9 times one alone, so the
  [scratch-slot false sharing](../../../technical-debt.md#scratch-slot-false-sharing)
  costs little on this machine.
- **Feel under load.** While `smp8-check.py --load 120` kept 11 compute and
  4 page clients running, the owner ran Quake's `timedemo demo1`, switched tabs
  and typed:
  - Quake reported 261.0 fps under load, against 622.5 fps idle.
  - Everything ran well, the console behaved, and no keystrokes or frames
    dropped.
  - The cursor blink was visible (single-buffered scanout) but not distracting.

## QEMU: task-1 workloads, matched

Both revisions ran the [task-1 workload set](../smp-task1-baseline/README.md#configuration)
in one session on the same host:

- **Init:** the task-1 baseline init,
  written in each revision's grammar.
  - `83c08d6`: `INIT_PRIMARY=app://init INIT_CPUS=`
  - `7748db5`: `SPACES=baseline=app://init`
- **Devices:** the task-1 set: a 128 MiB native pool on a GPT disk, virtio-fs,
  network, RNG and a blank USB stick on `qemu-xhci`. QEMU 10.2.2 with KVM,
  256 MiB, 4 and 1 CPUs.

The kernels are the same ones staged for the ThinkPad, and the 1-CPU pair was
repeated in reverse order. Full output is in the `qemu-*-output.txt` files:
`b`/`f` for baseline/final, `4`/`1` for the CPU count, and `r` for the repeat.
Every workload exited 0 and verified every sample, and every pool passed
`fsck.npfs`.

### 4 CPUs

| Workload | Baseline | Final |
| --- | ---: | ---: |
| `allocbench heap`, ms | 13.290 (13.094–13.336) | 12.555 (12.416–12.857) |
| `allocbench growth`, ms | 48.400 (47.623–65.894) | 8.366 (8.273–24.677) |
| `allocbench pages`, ms | 35.819 (35.298–36.456) | 4.909 (4.526–4.961) |
| HOST read payload, ms | 158.459 (153.647–164.289) | 158.371 (146.207–160.706) |
| Native grow write transfer / sync, ms | 188.991 / 36.214 | 192.082 / 36.600 |
| TCP 16 MiB, MiB/s | 1.334 (1.331–1.341) | 1.336 (1.326–1.338) |
| H alone, internal, ms | 810.9–812.8 | 793.1–794.6 |
| P alone, internal, ms | 1013.6–1015.9 | 152.7–153.1 |
| 2 × H, aggregate s | 1.669 (1.667–1.670) | 0.850 (0.849–0.851) |
| 4 × H, aggregate s | 3.390 (3.386–3.392) | 0.927 (0.916–0.935) |
| 2 × P, aggregate s | 0.946 (0.943–0.951) | 0.340 (0.338–0.341) |
| 4 × P, aggregate s | 1.938 (1.937–1.942) | 0.472 (0.450–0.475) |
| Mixed, aggregate s | 3.461 (3.420–3.461) | 2.709 (2.605–2.738) |

Each mixed-set client, ranges over three repetitions:

| Client | Baseline | Final |
| --- | --- | --- |
| H elapsed | 827–872 ms | 805–887 ms |
| P elapsed | 3269–3346 ms | 154–159 ms |
| Native write transfer (median of 2) / sync | 521–537 / 87–95 ms | 379–432 / 67–74 ms |
| ttcp | 0.560–0.564 MiB/s | 0.551–0.580 MiB/s |

The mixed set is now bounded by the BSP's serial services. ttcp keeps only about
42% of its solo rate under the mixed load, as before, while the synced native
write runs. Network, the native filesystem and virtio-blk are all BSP workers.
7a's mixed set used a RAM-file write instead and kept 95%. SMP did not change
this ([remaining serial services](#remaining-serial-services)).

### 1 CPU

| Workload | Baseline | Final | Baseline, repeat | Final, repeat |
| --- | ---: | ---: | ---: | ---: |
| `allocbench pages`, ms | 32.724 | 5.590 | 34.281 | 4.781 |
| `allocbench growth`, ms | 43.487 | 8.883 | 45.839 | 9.562 |
| Native grow write transfer, ms | 202.235 | 223.527 | 224.571 | 223.856 |
| HOST grow write transfer, ms | 147.165 | 164.544 | 164.426 | 164.613 |
| 4 × P, aggregate s | 4.235 | 0.844 | 4.247 | 0.846 |
| Mixed, aggregate s | 4.716 | 4.025 | 4.735 | 3.978 |

The first 1-CPU pair seemed to show final native and HOST writes about 10% slower.
The repeat, run final first, put both revisions at 224 ms and 164 ms. The
baseline kernel itself measured 202 ms in one boot and 224–226 ms in two others,
including the task-1 record. These rows vary between boots and show no change
from the milestone.

## QEMU: lifetime scenarios

A lifetime script ran on the default final image at 2, 4 and 12 CPUs.

| Scenario | Result at 2, 4 and 12 CPUs |
| --- | --- |
| `ls app:// \| head -n 2` and a three-stage `cat \| head \| cat` | exited 0 |
| `ls app:// \| app://missing.pxe` (second image missing) | `launch_failed`; nothing left behind |
| Ctrl-C to a blocked `head -n 1`, a running pipeline, a compute job | each `terminated` |
| Session exit with four background compute jobs | `shell_exit`, drain complete |
| PMM free frames before and after | identical |
| Heap live allocations before and after | within ±4 |

The heap count moves up and down by a few allocations between reads; it does not
only grow. lwIP's PCBs come from the kernel heap (`MEMP_MEM_MALLOC`), and closed
connections linger in TIME_WAIT for 120 s, so remote sessions opened shortly
before a read show up there.

A 2-CPU rerun (`life2c`) also read the heap 30 s
and 130 s after the last session closed. The count matched the idle value right
after the scenarios, 2196. It was 2195 at both later reads: one fewer than the
idle read, which itself came shortly after a warm-up session. That is consistent
with TIME_WAIT PCBs expiring, though no PCB was identified directly. No boot
logged a panic.

## QEMU: preemption push

The #417 review asked to see the push itself: a task preempted in user mode moving
to a CPU at least two tasks lighter. A throwaway patch
counts these moves (not affinity relocations) and logs the first eight. A 4-CPU
boot of the default image ran the loaded sets from 7b and four compute clients:

```text
push 1: task 0xffff8010002a2ef0 CPU 3 (load 2) -> CPU 1 (load 0)
push 4: task 0xffff80100029d040 CPU 1 (load 3) -> CPU 2 (load 1)
push 5: task 0xffff80100029d040 CPU 0 (load 4) -> CPU 1 (load 1)
push 7: task 0xffff8010002e13c0 CPU 0 (load 3) -> CPU 1 (load 1)
```

Pushes 5 and 7 move user tasks off the BSP, once its kernel workers made it the
busiest CPU.

## Remaining serial services

These still run only on the BSP:

- the BSP request executor and its services, from capability growth and
  namespaces to RAM-file replacement, the launcher and display;
- the network, native filesystem, HOST transport, virtio-blk, USB and
  presentation workers;
- task reaping and object retirement;
- the general kernel VM.

The mixed set above shows the consequence: network and storage work share one
CPU. Moving one of these services off the BSP is the milestone's first named
follow-up.

## Not exercised

- No native run of the 1-CPU control or of the task-1 storage and network
  workloads; the native check covered computation and private memory.
- The full task-1 device set ran at 1 and 4 CPUs only. The 2- and 12-CPU runs
  used the default image without the pool disk or virtio-fs.
- Display and input latency were judged by feel natively, not measured.

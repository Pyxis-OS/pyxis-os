# CPU accounting: timer sampling against exact TSC (2026-10-29)

chronium asked for both methods to be measured before choosing. The process
listing needs per-process CPU time ([proposal](../../wip/everyday-gaps.md),
Process list row). He chose exact TSC timing (2026-10-29).

## Variants

Each variant was one experiment commit on main `6f67ace2`, never merged:

- **A, timer sampling:** at each local timer interrupt, the time since this
  CPU's previous interrupt is charged to the interrupted task, or to idle.
- **B, exact TSC:** `rdtsc` before and after the scheduler's dispatch switch.

A build-only variant of each logged a process's accounted time at exit, for
accuracy.

## Setup

- **Images:** `DISTRIBUTION=full` live images from builder
  `pyxis-llvm23.1.3-8bdf136`.
- **Workloads (existing tools only):**
  - `ipcbench call` and `send` (`--size 64 --messages 256 --rounds 50`);
  - `cpuclock --threads N --samples 5 --milliseconds 1000`;
  - `allocbench pages --rounds 5000`;
  - 500 Lua `pyxis.run{"cat","tmp://e"}` launches.
- **Runs:** each workload in its own Remote `--machine` session; 5 boots per
  image, in rotating order.
- **QEMU:** 10.2.2, q35/KVM with `-cpu host`, 1 and 4 CPUs, on a shared host
  (load 4–7).
- **Native:** vela, 16 CPUs, PXE with the standing command line. Remote was
  reached through the `vela` beacon over RTL8156 USB Ethernet.

## Cost on vela

Each figure is the median of the per-boot figures, with the range. An IPC
boot's figure is the median of its 50 passes.

| Workload | base | A | B |
| --- | --- | --- | --- |
| ipcbench call, µs/round trip | 4.875 (4.732–4.971) | 4.813 (4.760–4.855) | 4.961 (4.932–6.396) |
| ipcbench send, µs/message | 2.572 (2.518–2.594) | 2.443 (2.391–2.502) | 2.531 (2.484–2.557) |
| allocbench pages, µs/op | 15.788 (15.655–15.991) | 15.751 (15.640–15.851) | 15.719 (15.649–15.977) |
| launch, ms each | 1.075 (1.063–1.089) | 1.091 (1.077–1.097) | 1.085 (1.080–1.094) |
| cpuclock, mean GHz | 2.200 (2.199–2.200) | 2.196 (2.194–2.197) | 2.200 (2.199–2.202) |

**IPC fast path:** taking each boot's 10th-percentile pass, B's call round
trip was 4.54–4.56 µs in four boots, against 4.31–4.45 µs for base. That is
about +150 ns (+3.5%).
- **Fifth boot:** 6.18 µs throughout, unexplained.
- **Send:** about +40 ns.
- **A:** no IPC cost. Its cpuclock rate was 0.2% lower, with non-overlapping
  ranges, which is probably a layout effect.

**QEMU:** every difference was within ±2.5%, inside the host's run-to-run
noise.

## Accuracy on vela

vela's TSC is 3.6864 GHz.

| Case | Wall | A | B |
| --- | --- | --- | --- |
| one busy thread, 3×1 s | 3.00 s | 3.000 s | 3.001 s |
| two busy threads | 3.00 s | 5.992 s | 5.997 s |
| ipcbench client | 57.5 ms | 25.0 ms | 14.9 ms |
| 20 receivers, ~1.4 ms wall each | — | 0 in 17; 8.33 ms in 3 | 0.40–0.54 ms each |
| `sleep 2` | 2.00 s | 0 | 0.10 ms |

- **A is wrong per process** for short or interleaved work: a short process
  reads 0 or a whole 8.3 ms tick.
- **A can't see kernel time:** syscalls run with interrupts disabled, so the
  timer never lands in the kernel.
- **QEMU** showed the same pattern.

## The implementation on vela

The implementation reads the TSC once per dispatch return and once per idle
wake, and twice more per syscall to split user and kernel time. A `nosplit`
build of each revision left out the syscall reads. Each round had five boots
per image in rotating order, compared only within its own packet.

**First implementation (`e692411b`, base `c65430c6`):**

| Workload | nosplit | with split |
| --- | --- | --- |
| ipcbench call, median | +2.6% | +3.9% |
| ipcbench send, p10 | 0 | +3.7% |
| launch | +1.1% | +3.1% |

- **Single read:** one read per dispatch saved nothing over B. In QEMU (4
  CPUs, a counting build), 2.0 of the 2.1 dispatches per call round trip
  follow an idle wake, and the wake's read sits where B's pre-switch read was.
  One read between wake and dispatch is the minimum for exact timing.
- **Shared lines:** per-CPU user, kernel and idle totals sat in the packed,
  16-byte-aligned `schedulers` array, on cache lines other CPUs write when
  they queue work. Every syscall entry and leave updated them.
- **The fix (chronium, 2026-10-29):**
  - syscall entry and leave write only the task;
  - per-CPU mark, busy and idle totals take one cache line per CPU;
  - per-CPU user/kernel totals are dropped.

**After the fix (`9eea5040`, base `32b600d8`).** Each figure is the median of
the per-boot figures, with the range:

| Workload | base | fixed | nosplit |
| --- | --- | --- | --- |
| ipcbench call, µs/round trip | 4.967 (4.916–5.000) | 5.002 (4.813–5.301), +0.7% | 4.937 (4.902–5.137), −0.6% |
| ipcbench call p10, µs | 4.559 | 4.625, +1.5% | 4.535, −0.5% |
| ipcbench send, µs/message | 2.666 (2.586–2.693) | 2.756 (2.654–2.814), +3.4% | 2.617 (2.611–2.672), −1.8% |
| ipcbench send p10, µs | 2.496 | 2.570, +3.0% | 2.461, −1.4% |
| launch, ms each | 0.943 (0.931–0.951) | 0.950 (0.940–0.957), +0.7% | 0.941 (0.931–0.958), −0.2% |
| allocbench pages, µs/op | 15.837 | 15.867, +0.2% | 15.756, −0.5% |
| cpuclock, mean GHz | 2.204 | 2.204 | 2.207 |

- **Dispatch and idle-wake accounting (nosplit):** no measurable cost.
- **The user/kernel split** is fixed − nosplit:
  - call: +1.3% median (+65 ns), +2.0% p10 (+90 ns);
  - send: +5.3% (+139 ns) per message;
  - launch: +1.0% (+9 µs).
- **Decisions:**
  - **Merge gate** (fixed against base at or below about +1.8% on the call
    median): met, at +0.7%.
  - **Split rule (chronium, 2026-10-29):** keep the split if it costs at most
    about +1.5% on the call median. At +1.3% the split stays.
  - **Accepted cost:** about 5% per sent message, which is the split's two
    TSC reads per syscall.

## Limits

- One native machine, at one CPU count.
- IPC figures are the benchmark's batch means.

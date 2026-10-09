# SMP task 5: PMM lock and per-CPU scratch mappings

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Matched before/after record for [SMP task 5](../../../wip/scheduling-and-threads.md),
recorded on 2026-10-05. These are nested-VM measurements: the development host is
itself a KVM guest (Fedora 44, Linux 6.19.10, 8 vCPUs, virtualized i9-12900K), and
its own KVM has `halt_poll_ns=200000`. They are not owner-host or ThinkPad results.

Task 5 changes no caller's CPU. Every physical allocation, scratch mapping and
pressure notification still happens on the BSP in a normal boot. The timings below
check that the uncontended locks cost nothing measurable. The stress run checks the
new any-CPU paths, which no committed code uses yet.

## Configuration

| Item | Before | After |
| --- | --- | --- |
| Pyxis | main `b45270e` (task 4b and the T14 topology) | this PR's code, built just before committing it as `c1ca77e` |
| Pins | fs `d352c7e`, userspace `2f01df6`, ports `36d952e`, lwIP `a1aadb9` | same |

- **Build:** `make -j16 image` with the repository's default spaces (Development,
  Read-only, Remote). `allocbench.pxe` is byte-identical in both images. Port
  binaries rebuilt in between differ, but no workload uses them.
- **Emulator:** QEMU 10.2.2 with the AHCI fix, KVM, Q35, `-cpu max`, 256 MiB,
  OVMF with a fresh VARS copy per boot.
- **Devices:** virtio-net (user, port 2323 forwarded), virtio-rng and `qemu-xhci`
  with a blank 16 MiB USB stick. There is no native pool or virtio-fs, unlike the
  [task-1 baseline](../smp-task1-baseline/README.md). None of these workloads uses
  storage.
- **Driving:** each command ran in a `pyxis-remote --machine --no-shell-echo`
  session, the next one only after the previous one's typed completion. Concurrent
  sets used one remote session per client, submitted together.

## Results

Every sample exited 0 with no allocation failures. Values are medians with
min–max ranges, in ms; each row is five fresh processes.

| Workload | 4 CPUs before | 4 CPUs after | 1 CPU before | 1 CPU after |
| --- | ---: | ---: | ---: | ---: |
| `allocbench heap` | 13.136 (12.952–13.420) | 13.551 (12.876–14.632) | 14.458 (13.264–14.613) | 14.193 (13.807–14.382) |
| `allocbench heap --mixed --size 4096 --live 256` | 48.567 (48.363–49.003) | 48.958 (48.531–49.280) | 56.827 (56.246–57.895) | 56.731 (55.606–57.058) |
| `allocbench growth` | 52.961 (52.545–54.200) | 53.456 (53.055–55.905) | 53.891 (52.975–54.773) | 53.491 (52.936–55.339) |
| `allocbench pages` | 38.124 (37.606–39.055) | 38.459 (38.006–39.155) | 38.707 (37.701–38.939) | 37.436 (36.919–37.911) |

`P` is `allocbench pages --rounds 2048`. Three repetitions on 4 CPUs:

| Set | Before | After |
| --- | ---: | ---: |
| P alone, internal elapsed | 1065.3 ms (1061.3–1075.2) | 1066.9 ms (1043.9–1136.7) |
| 2 × P aggregate | 0.977 s (0.969–0.979) | 0.952 s (0.938–0.994) |
| 4 × P aggregate | 1.921 s (1.910–1.934) | 1.922 s (1.915–1.950) |

Every difference is within the spread between runs on this VM. Page work still
goes through the BSP's private-memory requests, so 4 × P still takes about twice
2 × P. Task 7 moves that work off the BSP.

## Stress run (not committed)

The stress patch is a throwaway patch on top of this PR (it applied to main at `fe1f5cd`) and is not part of the tree. When the
scheduler starts it keeps every AP busy for six seconds while the BSP continues booting (USB enumeration, display start and
network setup all allocate during that window). Each round on an AP:

1. Allocates 1–4 frames.
2. Zeroes each frame with `arch_frame_zero()`.
3. Checks that the frame is zero, writes a per-CPU pattern through the data slot,
   and reads it back through the table slot, mapped to the same frame.
4. Frees the frames.

Every 16th round, it also builds a private address space, maps, queries and
unmaps one page in it, and destroys it. All of these walks use the AP's own
scratch slots. Every 256th round, it requests more frames than are free, or a run
as long as the free count, which scans the whole bitmap and fails because free
memory is fragmented. Every 1024th round, it sends a memory-pressure notification.

At the end, each AP recounts the allocated frames in the bitmap under the PMM
lock and compares the count with the statistics.

| CPUs | Rounds per AP | Private spaces per AP | Refused large requests | Failures | Pattern mismatches | Bitmap matches stats |
| ---: | --- | --- | --- | ---: | ---: | --- |
| 2 | 89,281 | 5,581 | not in this run | 0 | 0 | yes |
| 4 | 69,662–69,681 | 4,354–4,356 | not in this run | 0 | 0 | yes, on every AP |
| 4 | 63,836–64,399 | 3,990–4,025 | 249–252, none granted | 0 | 0 | yes, on every AP |
| 12 | 21,208–39,666 | 1,326–2,480 | not in this run | 0 | 0 | yes, on every AP |

The refused requests were added for the second 4-CPU run; the other runs used the
patch without them. The 12-CPU boot ran 12 vCPUs on this 8-vCPU host, so its
round counts vary.

- **Pressure wakes:** in the first 4-CPU run, 204 of the 207 notifications from
  APs woke the filesystem worker on the BSP. The other three found no registered
  observer, because the worker was already awake.
- **Early wake:** with refused requests also notifying, the second 4-CPU run
  woke the worker 898 times. Six of those were the new path in
  `mm_pressure_wait()`: the worker registered after a notification that had
  found no observer, and its sleep returned at once.
- **After stress:** `allocbench pages` and `allocbench growth` then ran normally
  from a remote session, at 2, 4 and 12 CPUs.

**Control.** The same patch with the lock removed from `pmm_alloc()` and
`pmm_free()` panicked within seconds on 4 CPUs. Several CPUs reported
`PMM: reserved or already-free frame` at once, with interleaved panic output.
So the stress does detect a missing PMM lock.

## Scratch slots in the debugger

A hardware breakpoint in the stress build (before refused requests were added) stopped each time an AP had both of its slots mapped. GDB then read the page table covering `TEMP_MAP_BASE`, where entries
2n and 2n+1 belong to CPU index n.

- **Separate pairs:** both entries of the stopped CPU's pair always mapped the
  same frame. In one stop, CPU 1 (entries 2–3) and CPU 3 (6–7) held live
  mappings of different frames at the same moment.
- **BSP unused:** the BSP's pair (entries 0–1) was empty at every stop.

## Not exercised

- No caller in this PR uses these paths off the BSP; the evidence is the stress run
  and GDB.
- Allocation failure was exercised only in the PMM itself. Callers' unwind paths,
  such as `arch_space_create()` running out of frames, are unchanged and did not
  run.
- No native ThinkPad run. Task 8 covers native validation.

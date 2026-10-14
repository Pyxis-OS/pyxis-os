# Bounded ALLOCATE

Owner go, **2026-10-14**, after the native run of
[bounded teardown](../bounded-teardown/README.md#native-run): every remaining
audio restart there followed a large private-memory ALLOCATE of about 80 ms
with interrupts off on the BSP (128 MiB), and none followed a teardown.

## Change

- `vm_back()` is now `vm_back_begin()` plus steps. A step allocates, zeroes
  and maps at most 256 pages with interrupts off. After a failure, a step
  instead unmaps and frees at most 256 pages from the end of the mapped prefix.
  The synchronous `vm_back()` is begin plus one unlimited step, unchanged for
  kernel, loader and image callers.
- Private ALLOCATE and ALLOCATE_GUARDED run those steps and yield between them
  (`kernel_task_yield_masked()`). The result stays all-or-nothing: either the
  whole region is backed, or the stepped unwind leaves the reservation bare,
  it is released and NO_MEMORY is returned.
- An allocation larger than one step goes to the existing VM worker even when
  the process is already quiescent, entering it in the MUTATING phase. The
  request executor therefore keeps forwarding other requests, including PCM
  writes, while the allocation runs.
- Mapping installs a run of leaf entries per page-table visit
  (`arch_page_map_run()`).
- The "reservation is bare" check (`require_unmapped()`) skips absent tables
  and reads each present leaf table once (`arch_range_vacant()`). It used to
  query every page. For 128 MiB that check alone took 28 ms with interrupts off
  in nested QEMU, which is why the first stepped build still tripped.

The 256-page step is tuning, not a contract.

### Ordering within one process

The process VM transaction is taken by the executor before it decides between
inline and worker execution, and it is exclusive for the whole allocation:
admission is closed, the mutator is parked on its request and sibling threads
are held at syscall entry or return. A sibling RELEASE cannot be submitted
meanwhile; a sibling already inside a syscall gets BUSY, as for any competing
mutation. A process stop marks the process stopping, but the mutator's
synchronous call completes or unwinds first, and the process cannot retire
(and reach the reaper) while that task's request is outstanding. Moving the
allocation to the worker changes which task runs it, not where it is ordered.

## Nested method

Horse VM, nested KVM, patched QEMU 10.2.2, q35, `-cpu max`, 2 GiB, std VGA,
display none, `intel-hda` with WAV capture, info logging. Host steal is read
from `/proc/stat` per boot. Diagnostic `diag:` klog lines, never merged, carry
timestamps: BSP request and VM mutation services over 0.5 ms, every audio
recovery, session starvation edges, and (after) each stepped allocation's
total time, steps, longest step and begin time.

| Image | Revision | ISO sha256 prefix |
| --- | --- | --- |
| Base | main `3b714754` plus diagnostics | `7249030f49deaeb2`; with timestamps `03a8253bb0f3edcb` (Quake) |
| After | this change plus diagnostics | `99f93784a3189889`; with a stop-request line `ef86ed3498e70300` |

Workload per boot, as for teardown: `pcm 440 220 85 &`, then three
`lua5.1 -e "t={} for i=1,8e6 do t[i]=i end"` runs. The table grows by
doubling, so each run makes allocations up to 128 MiB.

## Nested results

| | Base | After |
| --- | --- | --- |
| 128 MiB ALLOCATE, interrupts off | one span, 103–218 ms | longest step 0.27–0.9 ms; begin ≤ 0.3 ms |
| 128 MiB ALLOCATE, total | same | 74–204 ms (4 CPUs), 75–112 ms (1 CPU) |
| Recovery right after a 25 ms+ allocation, 4 CPUs (steal 112 / 153) | every one (10 recoveries) | none inside any allocation window |
| Same, 1 CPU (steal not measured / 146) | every one (11 recoveries) | one, 24 ms after a 128 MiB allocation ended (of 3) |

Two step outliers of 4.1–4.3 ms appeared in one boot with high host steal.
Restarts away from any allocation happen in both images under host load and
are not attributed.

Failure and stop, after image, 4 CPUs:

- Growing a Lua table until a 1 GiB allocation fails: NO_MEMORY after 1445
  to 1448 steps, including the stepped unwind. Lua printed `not enough memory`,
  and a following 128 MiB run succeeded. In one boot steps grew to 13–22 ms
  near exhaustion, likely the host faulting in never-touched guest RAM
  (unverified); a later boot stayed at 0.6–1.8 ms.
- Ctrl+C during that 1 GiB allocation: the stop was requested 10 ms into it,
  the allocation completed its failure and unwind 473 ms later, and only then
  was the task terminated. A following allocation succeeded.
- threadprobe `exhaust` (guarded allocations until NO_MEMORY, then reuse) and
  `lifecycle` pass on the PR head without diagnostics.

## Chocolate Quake demo end

The owner heard a glitch when Quake's timedemo ended, on both teardown images.
Timestamped nested runs show no kernel event at the demo end: no BSP span, no
recovery and no session starvation, yet the capture has a 99.8 ms run of zeros
just before the demo's sound stops. At demo end Chocolate Quake's
`CL_Disconnect()` calls `S_StopAllSounds(true)`, which clears its own sound
buffer. The cut is the game's, and it is identical on this change. Quake's
147 ms startup allocation now runs stepped on the VM worker; it came before
the game's audio started in both images.

## Residual interrupts-off work

RELEASE frees its whole region in one step (natively about 1.3 ms per
128 MiB, from the teardown run). Image loading and launch preparation use the
synchronous backing. DISPLAY services take 7–20 ms natively at Chocolate Quake
start. Worker precedence on CPU 0 remains a separate follow-up; the native
reaper queue delay (median 4.6 ms) is its input.

## Native run

Pending: owner run on the ThinkPad with base and after diagnostic images.

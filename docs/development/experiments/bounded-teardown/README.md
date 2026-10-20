# Bounded process teardown

First slice of the accepted [BSP inventory](../../../wip/scheduling-and-threads.md#serial-services-off-the-bsp)
(decision 1, owner, 2026-10-13). Final process reclamation used to run to
completion in CPU 0's scheduler loop with interrupts off. Natively that took
21.8 ms for EDuke32 and 39.7 ms for Chocolate Quake, enough to trip the audio
worker's horizon at a quit ([record](../audio-recovery/README.md)).

## Change

- The scheduler only marks the last task's process RETIRED, detaches its
  control and group membership and queues it, without allocating.
- The process reaper, a BSP kernel task, ends device sessions, then releases
  the address space in steps of at most 256 backed pages, range records or
  page-table entries with interrupts off. It enables interrupts and yields to
  runnable BSP work between steps. It then releases the remaining resources
  and only then publishes process and group completion, so WAIT still means
  the memory is free ([SMP contract](../../../kernel/smp.md#scheduling-and-ownership)).
- `vm_space_destroy()` is retirement plus one unlimited release step. Unchanged
  callers (unpublished launch abort, loader failure) keep synchronous
  destruction.
- Page-table release walks a resumable cursor, mapping each table once
  instead of recursing. Unmapping clears a run of leaf entries per leaf-table
  visit instead of walking four levels per page, for every caller of the VM
  backing free.

The 256 budget is tuning, not a contract.

## Nested method

Horse VM, nested KVM, patched QEMU 10.2.2, q35, `-cpu max`, 2 GiB, std VGA,
display none, `intel-hda` with WAV capture, info logging. Each image adds
diagnostic `diag:` klog lines (never merged): process teardown or reaper
release time, steps and longest step, the reaper's queue delay, BSP request
and VM mutation services over 0.5 ms, and every audio recovery.

| Image | Revision | ISO sha256 prefix |
| --- | --- | --- |
| Base | main `5fab8d39` plus diagnostics | `4ff02d4eef6afb21` |
| After | `c08c58b5` plus diagnostics (the PR's code commit `c12264bf` differs only in a comment) | `1a0938c56cabc5a6`, `b4ead0c79658df89` (queue delay) |

Workload per boot, typed into the Development space:

```text
pcm 440 220 85 &
lua5.1 -e "t={} for i=1,8e6 do t[i]=i end"     (three times, about 22 s apart)
ls
```

Each Lua run grows a 128 MiB table, then exits with about 275 steps of backing
and tables. EDuke32 quits use the bundle's `-d1:1` profile demo, which exits
by itself. Restarts can happen at any time in nested QEMU (host contention), so
only a recovery logged between a process's last allocation and its teardown
or release line counts against that quit.

## Nested results

Large exits (times in ms; base is one interrupts-off span):

| Workload | CPUs | Base teardown | After release (total) | After longest step | Recovery at the quit |
| --- | --- | --- | --- | --- | --- |
| Lua 128 MiB | 4 | 50.0, 94.6, 94.9 | 38.1, 42.4, 47.0; 50.9, 39.6, 40.6 | 0.053–0.139 | base 3/3, after 1/6 |
| Lua 128 MiB | 1 | 94.5, 94.1, 94.1 | 44.5, 49.4, 54.8 | 0.056–0.077 | base 3/3, after 0/3 |
| EDuke32 profile quit | 4 | 55.7, 55.7 | 18.6, 18.6 | 0.056–0.060 | base 2/2, after 0/2 |

The one "after" recovery was logged just after the first release in one boot,
following a 301 ms allocation stall and its restarts. Without timestamps it
cannot be placed inside or after the release. Total release time is lower than
the old teardown because of the cheaper unmapping, despite the yields.

Small exits (13 per boot, mostly boot-time programs and `ls`):

| | Base teardown | After release | After queue delay |
| --- | --- | --- | --- |
| 4 CPUs | 1.08–1.21, median 1.13 | 0.30–4.29, median 0.90 | 3.70–16.21, median 4.56 (16 exits) |
| 1 CPU | 1.07–1.17, median 1.11 | 0.70–4.65, median 0.96 | not measured |

These queue delays include this record's own diagnostic lines: two `klog`
lines printed in the BSP scheduler loop between queueing a process and the
reaper running. Each line costs CPU 0 its output time
([worker precedence record](../worker-precedence/README.md#kernel-console-rendering-cost)),
so the figures overstate the delay.

Lifecycle checks with a tone playing (4 CPUs, one boot each):
`session bin://threadprobe.pxe` `lifecycle`, `stop`, `fault`, `tls-stop`,
`tls-fault`, `park-stop`, `exhaust` and `close` all passed, with the expected
`terminated`/`faulted` lines and no assertion. Ctrl+C on a running 128 MiB Lua
process printed `Process terminated`, released it in 41.2 ms (275 steps,
longest 0.058 ms) and returned the prompt. Failed launches still destroy their
unpublished process synchronously, unchanged.

An earlier revision, at trace logging, ended one boot with `output position
observation unstable; engine unavailable until reboot` during a restart storm.
It did not recur in the 8 later info-level boots of either image.

## Startup side

ALLOCATE zeroes and maps its whole region in one interrupts-off BSP service.
Nested spans of 100–300 ms appear while the Lua table grows (its 64 and
128 MiB steps), and 269–282 ms at EDuke32 startup. Most of them are followed by
a horizon recovery, in both images. A 36.8 ms DISPLAY service also appeared at
EDuke32 startup. This change does not address them; natively they are the
likely cause of the restart when Chocolate Quake starts (to be confirmed by the
native run).

## Residual interrupts-off work

- Session exit and the retirement check (one step, tens of microseconds).
- Endpoint, completion, private-record and capability-table release after the
  address space (one step, about 0.04 ms nested for these workloads).
- Object reaping in the scheduler loop and object destroy callbacks.
- Launch preparation and abort, and ALLOCATE (above).

## Native run

ThinkPad on AC power, owner session 2026-10-14, images `base` `96fd8332`
(main plus diagnostics) and `after` `33524d02` (#819 plus diagnostics).
Development space, `pcm 440 220 90 &`, three Lua 128 MiB runs, a Chocolate
Quake timedemo and quit, and EDuke32 for about 10 s and a quit.

| | Base | After |
| --- | --- | --- |
| Interrupts off at exit | one 21.8–39.8 ms span | longest step 0.012 ms |
| Largest total release | (same) | 2.5 ms (up to 284 steps) |
| Reaper queue delay | — | median 4.61 ms, max 6.67 ms (21 exits); diagnostic logging, see below |
| Audio recoveries | 4 | 6 |
| ...following a teardown | 0 | 0 |
| ...following a large ALLOCATE (about 80 ms) | 4 | 6 |

The queue delay is two or three diagnostic lines of about 2.1 ms each
(4.19–4.33 and 6.55–6.66 ms). A later native run without per-exit lines measured
tens of microseconds ([worker precedence](../worker-precedence/README.md#native-attribution)).

The owner heard each Lua run interrupt the tone and Quake glitch when its demo
ended, on both images; EDuke32 was clean start to end on `after`. The
remaining restarts are the startup side, addressed by
[bounded ALLOCATE](../bounded-allocate/README.md); the Quake demo-end cut is
the game's own (see there).

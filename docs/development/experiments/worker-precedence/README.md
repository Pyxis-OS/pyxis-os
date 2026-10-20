# CPU 0 worker precedence

Second decision of the [BSP inventory](../../../wip/scheduling-and-threads.md#serial-services-off-the-bsp).
Owner go 2026-10-14. The input was the native reaper queue delay in the
[bounded teardown](../bounded-teardown/README.md#native-run) run: median
4.61 ms, max 6.67 ms over 21 exits. **Parked 2026-10-14:** natively the reaper
waits microseconds, and that input was the diagnostic's own logging. No
scheduler change was made.

## Accepted design, not implemented

A bounded wake boost (owner, 2026-10-14):

- A BSP kernel task woken from a wait or sleep gets 2 ms of CPU time and is
  queued ahead of CPU 0's user tasks, in FIFO order among boosted workers.
- Its CPU time is charged when it leaves the CPU. With budget left, a
  preemption or yield keeps it ahead of user tasks. Once the budget is spent, it
  rejoins round-robin behind them, as today.
- The bound is per wake: at most 2 ms of CPU ahead of CPU 0's user tasks,
  enforced at the next yield or interrupt, so it can overrun by up to one tick.
  It is not a CPU share.

Strict precedence was rejected. Continuous kernel work, such as a long stepped
allocation or EC polling, would leave CPU 0's user tasks only short turns.

## Nested baseline: CPU 0 contention

Horse VM, nested KVM, q35, 2 GiB, intel-hda, main `4db9e431` plus diagnostics
that are never merged (`diag/precedence-base`). In the Development space each boot
runs an echo launch-through-exit loop: idle, with a tone, then with a tone, busy
Lua loops (8 at 4 CPUs, 2 at 1 CPU) and a Lua 128 MiB run. There were 3 boots at
4 CPUs and 2 at 1 CPU. A 10 s window counts as "load" when CPU 0 ran more than 2 s
of user work.

| | Other windows | Load windows |
| --- | --- | --- |
| Audio worker wake delay, mean, 4 / 1 CPU | 0.28 / 0.21 ms | 1.07 / 2.59 ms |
| User time ahead of each audio wake, 4 / 1 CPU | 0.05 / 0.00 ms | 2.02 / 3.13 ms |
| Audio recoveries per window, 4 / 1 CPU | 1.6 / 1.5 | 4.7 / 16.4 |

When user tasks saturate CPU 0, a woken worker waits behind them in the FIFO,
each for up to a tick. Natively that needs every CPU busy.

## Native attribution

ThinkPad with 12 CPUs, on AC power, 2026-10-14. Image `diag/precedence-native`
`1f6a5176`, run hands-free in the Remote space: echo loops idle, with a tone, and
with 14 busy Lua loops plus a Lua 128 MiB run. The diagnostic listed every CPU 0
run that overlapped a reaper wait of 1 ms or more.

- **Reaper queue delay:** means over each 25 exits ranged from 6 to 280 µs,
  median 13 µs, in every phase.
- **Waits of 1 ms or more:** only 4 in the whole run (1.1–4.8 ms). Ahead of the
  reaper in them: 2.8 ms of presenter and 1.1 ms of a user task.
- **The original 4.61 ms median was logging.** In the bounded-teardown run, each
  exit printed two diagnostic lines in the BSP scheduler loop between queueing the
  process and the reaper starting. Its queue delays of 4.19–4.33 ms are two lines
  at about 2.1 ms each, and 6.55–6.66 ms is three
  ([kernel console cost](#kernel-console-rendering-cost)). The nested
  small-exit queue delays in that record carried the same diagnostic lines.

Not measured natively: sustained CPU 0 contention. The packet's phases were sized
from nested timing, so the busy loops overlapped the measured exits for only
about 1 s. The local Development session with games and the presenter was not
reproduced either.

## Kernel console rendering cost

Replaced on 2026-10-14 by rendering in the presenter
([console rendering](../console-rendering/README.md)). This section measures
the earlier behaviour.

Every `klog` line, and every write by a program in a local space's console, is
drawn into that terminal's full-screen RAM image while the caller holds the
global output lock (`log_begin()`).

From code inspection:

- At the bottom margin, each new row moves the whole image up by one row
  (`move_rows()`, a `memmove`) and blanks the last row. The terminal is not
  redrawn per line.
- Every space keeps its own image. An off-screen space scrolls the same way.
- The cost is paid in the writer's context, on whichever CPU it runs:
  - a `klog` call runs with interrupts off;
  - a console write runs in the program's syscall, which also masks interrupts,
    and holds the lock for each chunk of up to 256 bytes;
  - other CPUs that log or write a console spin on the lock.
- The CPU 0 presenter takes the same lock with interrupts off, once per frame
  and per terminal row, so another CPU's long write stops CPU 0.
- The Remote space and Multiplexer panes use terminal objects, which are byte
  rings that the kernel does not render, so they never take this path.

Measured:

| Case | Configuration | Result |
| --- | --- | --- |
| `klog` diagnostic lines, Caelum tab | Native ThinkPad, `1f6a5176` | About 4.0–4.4 ms for a line of about 250 characters and 7.8–8.3 ms for longer ones. The per-row cost of about 2.1 ms is inferred from line length; the column count was not checked. A burst of about 30 lines stopped CPU 0 for 130 ms, and both audio recoveries in that run followed it. |
| One console write of 256 newlines, Development space | Nested, 1280×800, main `fcf3def5` | 164 ms on screen, 165 ms off screen |
| Writes of a 41-character line | Same | 453 µs on screen, 507 µs off screen; 359 µs to a `host://` file |
| Tone alone, then a tone with 20 such writes | Same, 4 and 1 CPU | Alone: no gaps. With writes: 17 gaps (0.9 s silent) at 4 CPUs and 21 gaps (1.1 s) at 1 CPU, all during the writes |
| The same writes in the Remote space | Same, 4 CPUs | 2.6 ms per 256-newline write; 2.2 ms per line, mostly network transport |

The native cost of a local console write was not measured. It is the same code
on the same full-screen image as the measured `klog` path.

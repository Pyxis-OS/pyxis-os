# System log write cost

Measured 2026-10-29 for the [system log writes](../../interfaces/kernel-log.md#system-log-writes):
what one 80-byte line costs through each path, on the change at its final head
and on the same tree for the existing paths.

## Method

- **Machine:** nested KVM on the Fedora development VM (host load average high
  from other builds), so nested-VM numbers, not native ones. QEMU patched 10.2.2,
  q35, 4 CPUs, no display.
- **Image:** `make image DISTRIBUTION=full`, builder `pyxis-llvm23.1.3-8bdf136`,
  with a native program as the Development space's init.
- **Program:** each variant writes 2000 lines of 80 bytes, timed with the
  monotonic clock; seven samples per variant, 3 s apart for the variants that spend
  the per-space burst. Not committed.
- **Variants:** `LOG_WRITE` and a log stream at a hidden level (debug) and at a shown
  level (info); a plain `write` to the framebuffer terminal; the existing
  `LOG_PUTCHAR`, 81 calls per line.

## Results (ns per line, seven samples)

| Path | Median | Range |
| --- | --- | --- |
| Terminal write (framebuffer console) | 1550 | 1484-1805 |
| `LOG_WRITE`, hidden level | 903 | 898-910 |
| Log stream, hidden level | 1012 | 999-1292 |
| `LOG_WRITE`, shown level (Caelum pane) | 2731 | 2665-2767 |
| Log stream, shown level (Caelum pane) | 2919 | 2799-4820 |
| `LOG_PUTCHAR`, shown (serial and pane) | 2.32 ms | 2.11-2.33 ms |

- A line at a hidden level is cheaper than a terminal write (0.6x). A shown line,
  which the Caelum TTY draws under the log lock, costs 1.8x (`LOG_WRITE`) to 1.9x
  (stream) a framebuffer terminal write: about 1.2-1.4 µs more per 80-byte line.
  A terminal session reached through the remote server cost about 83 µs per such
  line in an earlier measurement, so a service logging instead of printing to a
  Remote shell is far cheaper.
- A first version mirrored written lines to serial like kernel lines. That cost
  2.6 ms per shown line, the same as `LOG_PUTCHAR`, because serial output is slow
  and the log lock is held for it. Lines written by spaces are now kept off serial
  and the early console; they stay in the ring, on the Caelum pane and in UDP
  capture. `LOG_PUTCHAR` is unchanged and still pays serial.
- **Rate limit:** a flood of 9000 debug lines at full speed logged about 5000 (the
  burst) and then dropped 3997; after a 2 s pause the next line reported
  `log: dropped development 3997 log lines`.

## LOG_PUTCHAR after source stamping

Rerun 2026-10-29 on a quiet host (load average 1-3), same method, after
`LOG_PUTCHAR` was made source-stamped and line-assembled (81 calls per 80-byte line,
7 samples, ns per line):

| Path | Median | Range |
| --- | --- | --- |
| Terminal write (framebuffer console) | 1569 | 1492-2520 |
| `LOG_WRITE`, shown level | 2711 | 2688-2793 |
| `LOG_PUTCHAR`, 81 calls, shown level | 8095 | 7893-9623 |

`LOG_PUTCHAR` was 2.32 ms per line while it mirrored to serial. Now it costs about
0.1 µs per call, 286 times less per line, but still 5 times a terminal write for
the line because each character is a system call. A first run on a loaded host
(median 21 µs for `LOG_PUTCHAR`, 5.9 µs for the terminal write) is not used.

## Limits

Nested VM on a loaded host; one line size; the cost of the Caelum pane's drawing
depends on the framebuffer, not measured natively. Real services were not timed,
only the paths.

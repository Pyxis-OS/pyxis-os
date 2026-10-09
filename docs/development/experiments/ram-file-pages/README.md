# Page-backed RAM files

Measured on 2026-10-09 for the RAM-file debt paydown (#582). The owner accepted
three decisions on 2026-10-09:
- frames are allocated on the writing CPU;
- the RAM FILE profile ABI is retired;
- unwritten ranges are holes.

The behaviour is in the
[file contract](../../../interfaces/processes.md#implemented-file-calls).

**Configuration.**
- **QEMU:** 10.2.2 with nested KVM, q35, 4 CPUs, 8 GiB, VirtIO net with user
  networking.
- **Builds:** main `d673303` against this branch. Main later changed only
  docs.
- **Memory figure:** `fastfetch`'s allocator figure, which is allocated PMM
  memory.

**Workloads.**
- **Receive:** `tcp 10.0.2.2 5002 > tmp://rx.bin` from a host `socat`
  serving 64 MiB, then `sha256sum` and `rm`, three times.
- **`iobench`:**
  - `iobench read tmp://iob.bin` with 4088- and 65536-byte buffers, after
    `cat boot://share/iobench.bin > tmp://iob.bin`;
  - `iobench write` growing, with `--buffer 65536`, and with `--prepared`.

| Measure | Main | Page-backed |
| --- | --- | --- |
| Allocated at boot | 42.03 MiB | 42.03 MiB |
| Allocated after the `iobench` runs | 48.15 MiB | 42.03 MiB |
| Allocated holding the 64 MiB file | 315.42 MiB | 106.03 MiB |
| Allocated after `rm`, each of three cycles | 315.42 MiB | 42.03 MiB |
| 64 MiB receive, wall time | 13.49, 12.44, 11.64 s | 12.33, 12.10, 12.04 s |
| `iobench write`, growing 1 MiB, median (range) | 7.750 ms (5.054–14.449) | 0.523 ms (0.509–0.561) |
| `iobench write --buffer 65536`, median | 5.457 ms (4.293–6.484) | 0.514 ms (0.510–0.525) |
| `iobench write --prepared`, median | 0.155 ms (0.145–0.164) | 0.329 ms (0.328–0.346) |
| `iobench read`, 4088-byte payload, median | 0.138 ms (0.133–0.157) | 0.335 ms (0.317–0.367) |
| `iobench read`, 65536-byte payload, median | 0.129 ms (0.125–0.148) | 0.341 ms (0.319–0.365) |

All SHA-256 values matched. The receive is limited by the network path, not by
the file.

- **Growing writes** no longer copy the whole file or wait for the BSP.
- **Reads and prepared writes** now map each page through the CPU's scratch
  slot, about 2.4x and 2.1x slower than copying from one buffer. They still run
  at 2.4–3 GB/s.

**Behaviour checks,** run by a local program, not committed, built against
the SDK:
- **Sparse writes:** a write at 19000 after 5000 bytes leaves a gap that reads
  as zeros.
- **Truncation:** truncating to 4500 and regrowing by resize to 20000 kept
  4500 bytes, and the old bytes never reappeared. A write past the end inside
  the truncated page left a zero gap.
- **Large resize:** resizing to 1 GiB read zeros at the end. A 3-byte write at
  the end raised allocated memory by about 3 MiB, the 2 MiB index plus one
  frame.
- **Launch:** `echo` and `fastfetch` launched from copies in `tmp://`, and the
  copies' SHA-256 matched.
- **Screenshot:** `screenshot` produced a correct 1280x800 PNG through the
  snapshot path.

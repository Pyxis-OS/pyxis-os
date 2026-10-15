# Kernel log level cost

Measured 2026-10-15 for the [kernel log levels](../../interfaces/kernel-log.md#levels)
change: baseline main `cf8e5cdf` against the change at `aa074bad` on the same
base. The change was later rebased onto main `14f90078`; the rebased head was
rebuilt and booted once with the default build to confirm the same counts.

## Method

- **Machine:** nested KVM on the Fedora development VM, so these are nested-VM
  numbers, not native ones.
- **QEMU:** patched 10.2.2, q35, 4 CPUs, 1 GiB, OVMF, VirtIO SCSI CD-ROM,
  standard VGA, no display.
- **Builder:** `pyxis-llvm23.1.3-df87791`, `make -j8 image`.
- **Runs:** five boots per build, each sampled 25 s after QEMU starts.
- **Measured:**
  - host time from QEMU start to the serial `Caelum ready` line, which includes
    firmware and Limine;
  - serial bytes in the 25 s window, including about 1.4 KB of firmware output;
  - ring `next_line` and `used_bytes`, read through GDB at 25 s.

## Results

| Build | Ready (s) | Serial bytes | Ring lines | Ring bytes |
| --- | --- | --- | --- | --- |
| main, `LOG_LEVEL=info` (old default) | 1.515–1.811, median 1.518 | 4463 | 52 | 3066 |
| change, `LOG_LEVEL=info` | 1.470–1.749, median 1.478 | 4463 | 52 | 3118 |
| change, `LOG_LEVEL=debug` (new default) | 1.419–1.646, median 1.444 | 4463 | 115 | 6537 |
| main, `LOG_LEVEL=trace` | 1.596–2.817, median 1.665 | 78758–84134 | 1801–1932 | 82608–88503 |
| change, `LOG_LEVEL=trace` | 1.435–1.507, median 1.469 | 4463 | 2123–2188 | 99220–102210 |
| change, `trace` with `log.level=trace` | 1.491–1.864, median 1.563 | 78548–84722 | 1798–1945 | 84271–91033 |

- **Default build:** serial output and the drawn screen are byte-identical to
  main. Time to `Caelum ready` stays within run-to-run variation.
- **Ring:** the debug ceiling adds 63 one-off lines, about 3.4 KB of the
  256 KiB ring. Each line costs one more header byte than before, so the info
  build grows by exactly 52 bytes.
- **Trace build:** with the default info threshold, trace records stay out of
  serial and off the screen. Drawing them (`log.level=trace`) matches the old
  trace build.
- **Idle lines:** the trace rows were measured before #851 removed the per-idle
  line, which accounts for most of their lines.

## Serial cost

With `log.level=debug` or deeper, debug records reach serial and the screen as
they are produced. In one nested boot, uACPI namespace initialization took
33 363 µs when its debug table lines were drawn, compared with about 1 925 µs
when they were only retained. Serial output is synchronous, with interrupts
off, which is why serial follows the console threshold.

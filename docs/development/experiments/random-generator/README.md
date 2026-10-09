# Raw-source random_read baseline

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Measured 2026-10-08 on unchanged Pyxis `92762b107e813be5b6c5f4600e205049630dd3c6`, before the
[generator implementation](../../../devices/random-generator.md). Source selection and health
behavior remain [the current hardware contract](../../../devices/randomness.md).

## Inputs and method

| Input | Revision |
| --- | --- |
| userspace | `df78002764768b05b2843248d1e1f4cf6091d695` |
| ports | `2a5c30f402161a4545cdf4932b29d154cc43f734` |
| fs | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |
| builder | LLVM 23.1.3, `pyxis-builder:pyxis-llvm23.1.3-49e2c1a` |

The kernel and image were built with `make -j16 image PREBUILT="sdk userspace ports"` from
clean bundles at `6c7ced2` (the sleep-timer head merged into this main), verified by ordinary
image assembly; no toolchain rebuild. Default Kconfig includes xHCI and 120-tick HPET
maintenance. The single measurement consumer was compiled with the exported SDK's
`share/pyxis.mk` and ordinary `-O2 -g3` flags; it is not a packaged application, generator
implementation or benchmark framework. Its P1F was added as `random-baseline.pxe` to a copy of
`build/initrd-root` (sorted, null-separated GNU cpio/newc with `--reproducible --owner=0:0`,
ordinary xorriso EFI flags). The matched after runs keep this consumer and initrd and replace
only `boot/caelum.elf`. The consumer source is not kept in the tree.

Stock QEMU 10.2.2, nested KVM on the development VM (advertised Intel i9-12900K), q35, four
cores, 8 GiB, `-cpu max`, VGA 1280x800, a read-only VirtIO SCSI CD (avoiding the host AHCI
CD-ROM failure), VirtIO net and, for the VirtIO source, `virtio-rng-pci` backed by
`/dev/urandom`; the CPU case omitted that RNG device with a fresh firmware-variables copy.
Each source used one fresh boot and five individual invocations (not five boots), with no
debugger or other task-owned guest or build running. The VirtIO boot logged host-source
readiness; the CPU boot logged `RDSEED (RDRAND fallback)` and a passed boot self-test (which
instruction supplied each word was not instrumented). Both prepared TCP identity. There was no
RDRAND-only, failure-injection or native run.

A machine-mode remote client ran each `random-baseline` command, which borrowed explicit
`clock`/`random` grants and, for each extent 0/1/32/256, warmed 16 reads and measured 512,
each with a fresh five-second deadline and two `clock_now` boundaries. It reports durations
only, never random bytes, and summary output follows each extent's loop. All five command
completions returned status zero and session FINAL confirmed exit and complete drain.

## Observations

Each nonzero extent has 2560 measured successful reads per source. The zero-length control
bypasses hardware and the worker but still validates authority and deadline. The per-run
samples (total, min/max, loop time) are in Git history.

| Source | Bytes/read | Median mean µs | Range of run means µs | Median loop MiB/s |
| --- | ---: | ---: | ---: | ---: |
| VirtIO | 0 | 71.856 | 71.515–75.846 | — |
| VirtIO | 1 | 909.705 | 847.969–1168.111 | 0.000999 |
| VirtIO | 32 | 916.745 | 714.921–1368.946 | 0.031585 |
| VirtIO | 256 | 895.204 | 757.927–1109.010 | 0.259850 |
| CPU | 0 | 65.190 | 64.905–72.348 | — |
| CPU | 1 | 540.680 | 536.792–641.127 | 0.001663 |
| CPU | 32 | 688.805 | 668.529–844.732 | 0.042066 |
| CPU | 256 | 1804.430 | 1732.866–1834.458 | 0.131983 |

Latency includes the grant call, shared staging, worker scheduling, hardware service and clock
boundary overhead; throughput is `length * 512 / loop_elapsed` with no control subtraction.
These are sequential consumer measurements, not raw instruction, DMA or ChaCha throughput,
queue saturation, entropy quality, first-use latency or a native prediction (the kernel TCP
consumer already read at boot). Scheduling and host variation remain, and a global generator
still pays worker handoff. Repeats after implementation record the initial seed and
reseed/refill costs separately.

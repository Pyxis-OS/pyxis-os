# Raw-source random_read baseline

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Measured 2026-10-08 on unchanged Pyxis
`92762b107e813be5b6c5f4600e205049630dd3c6`, before the
[generator implementation](../../../devices/random-generator.md). The kernel bundle
records clean source and the ELF embeds `92762b107e81`. Source selection and
health behavior remain [the current hardware contract](../../../devices/randomness.md).

## Inputs and build

| Input | Revision |
| --- | --- |
| userspace | `df78002764768b05b2843248d1e1f4cf6091d695` |
| ports | `2a5c30f402161a4545cdf4932b29d154cc43f734` |
| fs | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |
| builder | LLVM 23.1.3, `git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a` |

The baseline kernel/image build used `make -j16 image PREBUILT="sdk userspace ports"`.
The existing independent bundle targets produced those clean bundles at
`6c7ced2`, the sleep-timer head merged into this main. Their pins, payloads and
kernel/SDK ABI identity were verified by ordinary image assembly. No toolchain
rebuild. Effective default Kconfig includes xHCI and 120-tick HPET maintenance.

The single measurement consumer was compiled with the exported
SDK's `share/pyxis.mk`, ordinary `-O2 -g3` flags and native library order. It is
not a normal packaged application, generator implementation or benchmark
framework. Add its P1F as `random-baseline.pxe` to a copy of `build/initrd-root`,
then assemble with the existing sorted, null-separated GNU cpio/newc flags
(`--reproducible --owner=0:0`) and ordinary xorriso EFI image flags. The image
otherwise contains the ordinary kernel, initrd inputs and boot configuration.
Local artifacts are under `<worktree>/build/`; keep this consumer/initrd for
matched after runs and replace only `boot/caelum.elf`.

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `a0d2b0e3594778819454543a059cb36982d1e36cfd7df2c431cbf32867435556` |
| Measurement ISO | `456e12ae8fdf984201a77627710ea8bcca6fbb4bff2eb340249a7015df06a5e2` |
| Measurement initrd | `3c5b5b7d026c547d9d0270715ac195af9178008668ebc78a72c5d8359da78e9a` |
| Consumer P1F | `cec74bc730dc027294abc2f5f95afd39673204e0a893282b79d300d3ae3f9813` |
| Consumer source | `4b6ecd5d9a5f509d2cddc7c194e287e6eb27220b7b3007e6dd92da7cba9df328` |
| Effective Kconfig | `ac12acc93c3fcbbdff1ace10d95b8f3cdf883e1c09d21ce413ba09df8324a98b` |

## Guest and method

Stock QEMU 10.2.2, nested KVM on the development VM (advertised host model
Intel i9-12900K), q35, four cores/one thread each, 8 GiB, `-cpu max`, standard
VGA 1280x800. All ordinary spaces/networking started. The read-only VirtIO SCSI
CD avoids the host AHCI CD-ROM failure. Each source used one fresh boot and five
individual invocations, not five independent boots. No debugger or another
task-owned guest/build ran during timing.

```sh
cp /usr/share/OVMF/OVMF_VARS.fd build/random-vars.fd
qemu-system-x86_64 -name 'Pyxis random baseline' \
  -machine q35 -accel kvm -cpu max -rtc base=utc \
  -smp cpus=4,sockets=1,cores=4,threads=1 -m 8G \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=build/random-vars.fd \
  -display none -serial mon:stdio \
  -drive if=none,id=random_cd,format=raw,readonly=on,media=cdrom,file=build/random-baseline.iso \
  -device virtio-scsi-pci,id=random_scsi,disable-legacy=on \
  -device scsi-cd,bus=random_scsi.0,drive=random_cd,bootindex=1 \
  -object rng-random,id=random_rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=random_rng,disable-legacy=on \
  -device virtio-net-pci,netdev=random_net,disable-legacy=on \
  -netdev user,id=random_net,hostfwd=tcp:127.0.0.1:2349-10.0.2.15:2323
```

For the CPU run, omit the two RNG object/device lines and use another fresh
copy of the same firmware variables template. All image/consumer bytes and
other configuration stay fixed. VirtIO boot logged host-source readiness.
The CPU boot logged `RDSEED (RDRAND fallback)` and a passed boot self-test;
which instruction supplied each word was not instrumented. Both boots prepared
TCP identity successfully. No RDRAND-only, failure-injection or native run.

Firmware code SHA-256:
`904bfa3e0d966372b43b804c4fe323ae63751566687c2bfdf52ca947f47eb13a`;
variables template:
`6ed987af3a3c155be71665f510eae3e007eda9b8b94afd59d45e91c4a11565cc`;
QEMU executable:
`27cd395848940fc6482256d85096fc64bc4fe3f3e909824d51c202f8314cd9e9`.

An existing machine-mode client connected to Remote with `--no-shell-echo`.
Each ordinary `random-baseline` command borrowed explicit `clock`/`random`
grants. For each extent 0/1/32/256, it warmed 16 reads and measured 512,
using a fresh five-second deadline and two `clock_now` boundaries per read.
It reports durations only, never random bytes. Summary output follows each
extent's completed loop; no network output occurs inside measured loops.
The JSON captures' five command completions each returned status zero, and
session FINAL confirmed shell exit and complete drain.

## Observations

The raw integer samples (each run's total, min/max and loop time) are in Git
history, not the tree. Each nonzero extent has
2560 measured reads per source, all successful. Zero-length control bypasses
hardware/worker but still validates authority/deadline.

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

Latency includes the actual grant call, shared staging, worker scheduling,
hardware service and clock boundary overhead. Throughput is
`length * 512 / loop_elapsed`, including loop/clock overhead; no control
subtraction. These are sequential consumer measurements, not raw instruction,
DMA or ChaCha throughput, queue saturation, entropy quality, first-use latency
or a native prediction. The kernel TCP consumer has already read at boot.
Scheduling/host variation remains; a global generator still pays worker
handoff costs. Repeat these same bytes/configurations after implementation,
recording initial seed and reseed/refill costs separately. All task-owned
QEMU/client/build processes stopped.

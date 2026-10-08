# System pointer qualification

Task 1 baseline was captured on 2026-10-08 before pointer code changes. Runtime
source is main `9a88813`; documentation-only commit `1c27d28` records round-three
acceptance and task 1 authorization. Pins: userland `df78002`, ports `8bdac14`,
fs `b427df2`, lwIP `a1aadb9`.

## Baseline configuration and method

Ordinary `make -j16 image` passed using
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a`. The second image
used `DISPLAY_SIZE=1280x800` for Bochs; its kernel and initrd are unchanged.
No compiler rebuild or code modification was needed.

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `9fc5a46eca75f76a5bbbdf4f07086a1a60309d86b4c9b43acb34d387f232522f` |
| Initrd | `3bd9c30014c92358e03f2ae1d723cf1b6fc2ea2031a49eb6e1ba38c4d85af102` |
| Effective kernel configuration | `ac12acc93c3fcbbdff1ace10d95b8f3cdf883e1c09d21ce413ba09df8324a98b` |
| Default ISO | `ca6810714e1986c56ef55381b4202b254c64945305c00a8e18cfbd7840c54bb3` |
| Bochs ISO (`DISPLAY_SIZE=1280x800`) | `7a5b290cd9eda48668319c0b30923be45603eb1957bfdbeef35058d65471d507` |

Interactive boots used packaged QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), q35, nested
KVM, `-cpu max`, four CPUs (one socket, four cores, one thread), 512 MiB,
`-rtc base=utc`, no NIC/storage/HOST export, and modern VirtIO RNG. Firmware was
the matching `/usr/share/OVMF/OVMF_CODE.fd` and `OVMF_VARS.fd` pair, with a fresh
variables copy per boot. Boot media used a modern VirtIO SCSI controller and
SCSI CD-ROM, avoiding the documented stock-emulator AHCI CD-ROM problem; no
kernel workaround or emulator replacement was made. Keep that device ordering
matched for the after samples.

The common arguments, entered manually for each boot, were:

```sh
cp /usr/share/OVMF/OVMF_VARS.fd build/OVMF_VARS.fd
qemu-system-x86_64 -machine q35 -accel kvm -cpu max \
  -smp cpus=4,sockets=1,cores=4,threads=1 -m 512M -rtc base=utc \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -drive if=none,id=pointer_cd,format=raw,readonly=on,media=cdrom,file=build/pointer-baseline/pyxis.iso \
  -device virtio-scsi-pci,id=pointer_scsi,disable-legacy=on \
  -device scsi-cd,bus=pointer_scsi.0,drive=pointer_cd,bootindex=1 \
  -display none -serial mon:stdio \
  -object rng-random,id=rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=rng,disable-legacy=on \
  -no-reboot -gdb tcp:127.0.0.1:1234
```

Default VGA used the boot framebuffer. Bochs used its ISO above and
`-vga none -device bochs-display`; VirtIO used the default ISO and
`-vga none -device virtio-gpu-pci,disable-legacy=on`. Debugger inspection confirmed
`DISPLAY_BOOT`, `DISPLAY_BOCHS` and `DISPLAY_VIRTIO_GPU`, rather than inferring
the backend from arguments. Every backend was 1280x800, pitch 5120, RGB shifts
16/8/0. All four spaces had started; static Caelum terminal output was selected,
with its text cursor visible and no pointer owner or cursor.

After boot completed, GDB used the matching baseline ELF, pagination off,
`set may-call-functions off`, and a hardware breakpoint at `space_present`.
Each sample was entered separately with the existing
[HPET method](../kernel/display.md#qualification-and-cost): read the
64-bit counter at `0xfffffe80402020f0`, `finish`, then read/subtract. Debugger
inspection confirmed a 10,000,000 fs period, so each tick is 10 ns. No sampling
loop, benchmark infrastructure or boot automation was added.

| Backend | Four baseline samples, ms | Median, ms | Range, ms |
| --- | --- | ---: | --- |
| Boot framebuffer | 2.450450, 1.726680, 1.298980, 1.467220 | 1.596950 | 1.298980–2.450450 |
| Bochs | 1.152940, 1.167430, 1.258670, 1.380620 | 1.213050 | 1.152940–1.380620 |
| VirtIO | 1.531600, 1.495750, 1.586260, 2.098920 | 1.558930 | 1.495750–2.098920 |

These are elapsed, debugger-profiled frame observations, including device waits,
scheduling, preemption and shared-host/nested-VM variation. They are not pure CPU
cost or native performance. Repeated samples show variation; no speedup claim or
performance threshold follows from these small sets. Mouse routing happens before
`space_present`, so this interval measures presentation, not input latency.
Baseline QEMU/debugger jobs were stopped before code work started.

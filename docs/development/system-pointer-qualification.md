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

## Task 1 results

The measured task 1 kernel contains changes through `d74cfa9`, with published
userland `6b45dd1` (userland PR #164). It uses the same main runtime base and
unchanged ports/fs/lwIP pins as the baseline. Later documentation and parent
gitlink commits do not change these binaries. Main subsequently gained deadline
timer changes; these samples do not measure that newer scheduler.

`make -j16 kernel sdk` and the complete userland build passed with the existing
builder. The default `make -j16 image` fails because Quake and SDL2 still consume
the removed `pointer_event.dx/dy`. Their migration belongs to separately
authorized task 2. Tasks 1 and 2 must integrate the ABI and all consumers together;
this draft is not a working default-image intermediate revision. No compiler
container rebuild, compatibility fields, placeholder lock API or stale consumer
binary was used to hide this dependency.

For the focused interactive image, temporary copies of the existing ports install
and development Lua manifests omitted the Quake executable and SDL2 development
library. The unchanged `scripts/stage-tree.lua` staged that subset; the existing
ports bundle recorder recorded its inputs, the userland Makefile installed all
userland against the fresh SDK and remaining development exports, and the ordinary
`assemble-initrd.sh` and `make-image.sh` assembled it. The Bochs image regenerated
`build/limine.conf` with `make build/limine.conf DISPLAY_SIZE=1280x800`. This is
manual qualification staging, not a new supported build profile or CI change.
The subset initrd is 38,881,792 bytes, versus the baseline's 39,330,304 bytes.

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `ea30f496fbb52cfd91654c78002ca4fba0031d025e2a6711f4325a4ec52c7604` |
| Initrd | `79291b05aafdb4478ced0bda9c8449c8a27de35beb3b6d9998b53f33a452f6de` |
| Default ISO | `313419131252529d44f93e4e905457c4ca7d17c0b43fdc0d4498502c6e2a4ee0` |
| Bochs ISO (`DISPLAY_SIZE=1280x800`) | `2d42a4a1f0367f8dc4511ce6cbfa9b144dfc9eec4f54f9da77109ac892a2d20e` |

### Presentation observations

The four after samples use the same headless QEMU arguments, HPET/GDB method,
selected static terminal, resolution and confirmed backend as the baseline.
The system pointer is now visible at screen center; the terminal text cursor
remains visible. No program pointer subscription is acquired.

| Backend | Four task 1 samples, ms | Median, ms | Range, ms |
| --- | --- | ---: | --- |
| Boot framebuffer | 0.863570, 1.104950, 0.697540, 1.045450 | 0.954510 | 0.697540–1.104950 |
| Bochs | 1.297140, 1.251430, 0.863760, 0.799590 | 1.057595 | 0.799590–1.297140 |
| VirtIO | 2.236020, 2.494740, 1.888450, 1.875630 | 2.062235 | 1.875630–2.494740 |

VirtIO's observed median rises by 0.503305 ms, approximately 32%; its ranges
overlap. Boot's lower range does not overlap its baseline. These small elapsed
sample sets, shared-host variation and changed initrd prevent attributing either
result solely to cursor composition or claiming a stable speedup/regression.
Keep the measurements for comparison with task 4's hardware cursor and task 5's
final matched qualification. The implementation uses at most a 256-byte pixel
scratch row and forwards existing composition spans through the capture tee; it
does not allocate or write an additional full frame.

### Interactive coverage

Boot-framebuffer and Bochs boots showed the kernel pointer and selected
Development by tab input. Bochs exercised `mousetest` custom image (16x20 BGRA8,
hotspot 1,1), surface-local coordinates, drawing, H hide/show, D default, C custom
and W bounded center warp. A custom/hidden monitor-image comparison changed only
114 pixels inside the cursor bounds. Debugger inspection confirmed physical
position versus the 32-pixel navigation offset, image ownership, retained image
while hidden, focus/button reset on Super+Down, restoration on Super+Up, and
image/subscription cleanup on Escape exit. Boot also exercised custom image and
hiding; it was confirmed as `DISPLAY_BOOT`, rather than assuming the VGA device
selected the boot backend.

VirtIO additionally ran with the existing GTK display on Xwayland and a modern
VirtIO NIC with only loopback host forwarding to the existing remote terminal.
This separate configuration was used for resize/capture, not the timing samples.
The GUI initially advertised 640x480. Resizing the QEMU window to 1000x700 yielded
a 1000x673 destination after GTK's controls; geometry generation and mapping
identity both changed from 1 to 2 as `mousetest` replaced its mapping. A second
resize to an 800x600 window yielded 800x573, generation/identity 3. During that
resize a held physical left button remained set while the subscription's accepted
buttons became zero. The program continued, kept its custom image, and warped to
the new surface center. No debugger geometry mutation was used.

The remote shell used existing `screenshot tmp://NAME.png` and `xfer send` with
explicit host confirmation into the task's build directory. Decoded RGB pixels
of both the visible-cursor and hidden-cursor 1000x673 PNGs matched independently
requested QEMU monitor dumps exactly. The first visible capture was requested
before the GUI resize debounce committed and correctly returned the previous
640x480 geometry; it was not compared as though it had the new size. After exit,
debugger inspection showed both display/pointer owners and the saved image null.
All task-owned QEMU, GDB and remote-client processes were stopped.

Same-size REPLACE identity changes, partial-alpha and maximum-size images,
malformed/denied requests, queue overflow and allocation failures were reviewed
in source, not separately exercised with synthetic probes or fault injection.
Physical PS/2 behavior remains deferred under the accepted
[native ThinkPad qualification debt](../technical-debt.md#native-system-pointer-qualification).
This task does not qualify lock/escape, terminal selection/controller queues,
Quake/SDL2 migration or the VirtIO hardware cursor.

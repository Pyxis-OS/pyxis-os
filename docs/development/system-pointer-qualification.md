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

## Task 2 and joint integration

Task 2 was explicitly authorized on 2026-10-08 and stacked on task 1. It adds
lock/state helpers, Super+Esc, durable activation gating and migration of Quake,
`mousetest` and the closed SDL2 backend. Final published dependencies are userland
`b83ff67` ([#164](https://git.internal/PyxisOS/pyxis-userland/pulls/164)), Quake
`be91da7` ([ports #65](https://git.internal/PyxisOS/pyxis-ports/pulls/65)) and SDL2
`a642f07` ([ports #66](https://git.internal/PyxisOS/pyxis-ports/pulls/66)), stacked
on Quake and including DevilutionX's native cursor option. Publish/merge order is
userland, Quake, SDL2, then [Pyxis #545](https://git.internal/PyxisOS/pyxis-os/pulls/545)
with the integrated pins. The SDL2 PR must target main after Quake merges.

Fresh ordinary `make -j16 kernel sdk` and `make -j16 image` passed with the
existing LLVM builder; the default image now includes working Quake and SDL2
instead of the task 1 qualification subset. `make -j16 image
DIABLO_DATA=/shared/diablo-shareware` also passed using the local shareware input.
No game data, cursor pixels or game screenshots were committed or uploaded.
The separately published consumer revisions preceded parent gitlink commits.
The compiler container and upstream source pins were unchanged.

The main merge in task 2 includes SDL2 closure and deadline timers through
`4332801`. The task 1 presentation samples above remain the matched observations
for their original runtime base; they do not isolate pointer cost on this newer
scheduler. Task 2 functional checks below are unprofiled interactive checks, not
new throughput or native-latency measurements. Final matched software/hardware
cost comparison remains tasks 4–5.

### Interactive configuration and evidence

Resize/modifier checks first used parent `f32ff13`; Quake used `304d1d7`;
activation-refusal checks used `62182af`; DevilutionX and locked capture used
`f017d45`. Later documentation changes do not change those paths.

The ordinary image initially ran with the same q35/four-CPU/nested-KVM configuration
as task 1, with GTK on Xwayland, modern VirtIO GPU and NIC, 512 MiB, and only a
loopback remote-terminal forward. Later Quake/DevilutionX checks used 2 GiB and
the local shareware ISO. They retained `-cpu max`, fresh matching OVMF variables,
modern VirtIO RNG, firmware VirtIO SCSI CD boot and no HOST export or disk.
QMP was exposed only by a local Unix socket for manual keyboard input.

GTK initially advertised 640x480. `mousetest` locked at physical (320,240);
relative motion left that parked hotspot unchanged. A real window resize yielded
800x573; mapping identity advanced to 2 while lock and a held left button stayed
set. Super+Esc with Shift/Control/Alt unlocked without terminating the program,
cleared accepted buttons and retained the image/preference. A fresh left click
relocked with accepted buttons zero and its press consumed. Super+Down/Up
revoked lock and restored graphics without automatic relock.

A second boot with the final activation rules exercised a simultaneous fresh
left/right press after escape. It granted activation but the unaccepted right
button refused lock; the attempt consumed permission. Releasing both and explicitly
requesting again stayed denied until another fresh left click. Exiting while
locked cleared the global lock, subscription and image; the space's activation
requirement survived. These are real monitor mouse/key inputs and read-only GDB
observations, without synthetic syscalls, debugger writes or fault injection.

Quake locked after first PRESENT and ran a `map start` level with `+mlook`.
Device motion changed the view while its hotspot stayed parked. Manual QMP input
pressed Super+Esc, released Super first, then repeated/released Escape: lock was
revoked, the pointer appeared, and no game menu opened. Ordinary motion moved
the pointer while unlocked. A fresh click relocked with the activation press
consumed and accepted buttons zero. Hidden-layer return did not relock. Keyboard
console commands and rendering remained available; exit cleared both owners.

DevilutionX's former Pyxis-only forced-software override was removed because it
prevented the required `SDL_CreateColorCursor` path. Its existing Hardware Cursor
option now selects a supplied native SDL image; composition remains software
until task 4. GDB observed a 33x28 BGRA image with hotspot (0,0), visible on the
menu. Its 3,696 copied bytes contained 88 alpha levels, 86 strictly between zero
and 255. Menu/town input used native positions; no second default cursor was
drawn over the supplied hand.

For the inventory warp, the window was resized to a 640x512 physical destination
and the game restarted, yielding 640x480 content beneath navigation. Restart
made the game's existing logical layout match the classic aspect ratio; its
wider logical layout does not need the inventory warp. A breakpoint on the actual
native request observed x=215, y=228, generation=2, mapping identity=3. It returned
`CALL_OK` and changed physical x from 375 to 215 while physical y stayed 260.
The test used the game's I key and normal `SetCursorPos`/SDL path.

A guest-only preference file with `Hardware Cursor=0` was written through native
Lua file I/O and the game restarted. Its native hidden state was true with no
supplied image; the software hand followed ordinary motion at physical (275,245),
and a click selected the hero menu. After a locked `mousetest` exit in that same
space, the next game's first left click was consumed as activation; permission
remained ready, and the second ordinary click still selected the menu. A pending
lock permission therefore does not swallow every click from an ordinary client.

While `mousetest` was locked, its saved image and show preference remained set.
A native `screenshot` PNG and an independent monitor dump matched all RGB pixels
at 640x512, with the effective cursor omitted. This complements task 1's visible
and hidden capture comparisons. Screenshot download used the existing explicit
confirmation path into the local build directory. All task-owned QEMU, debugger
and remote-client processes were stopped.

The interactive images used userland `b83ff67` and ports through `e2482df`; the
last ports follow-up `a642f07` only converts wheel counts to float before reversing
their sign, avoiding signed integer negation overflow. The final ordinary and
shareware builds include that follow-up. The final parent checks are tracked on
#545 for its exact submitted revision; dependency repositories report zero
action tasks, and their empty status response must not be called a CI pass.

### Remaining qualification limits

SDL relative-mode refusal/revocation and its Pyxis-only upstream ordering patch
were reviewed in source; DevilutionX uses ordinary input, so its game checks do
not exercise SDL relative mode. Kernel lock/refusal/revocation were exercised
through Quake and `mousetest`. Device/keyboard stream-loss paths, oversized cursor
fallback, same-size-only REPLACE and allocation failures remain source-reviewed
without fault injection. Native PS/2 behavior remains in the accepted ThinkPad
batch debt. Terminal controller/selection and actual VirtIO hardware cursor
implementation remain separately authorized later tasks.

The saved functional image with ports `e2482df` has these hashes:

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `503ee9602f383c7caec078fea1659a694eee4ac4c713d4b1294ffa277728013b` |
| Default initrd | `6f93c7756eb08928d3dc9f4fcd43cd65bbd1385906cbdd155d2982c8ea39f067` |
| Default ISO | `5fc140a575fb9e616fac16797c780b2e7784abec9ecf25c3e7898df95c46a42c` |
| Local shareware initrd | `f6da0dd8449681f8fc808dcc1f5edd2d7ef3ca06f4416da292e9af4c1ac8931c` |
| Local shareware ISO | `6e25a2ac5837f614eaef38c17f652df425ec6a8d4b0a6d7c73673ec240d38e65` |

### Input-source coordination follow-up

After coordination with proposed Bluetooth #548, `b77cd1d` separated the PS/2
adapter's continuity quarantine from the common physical snapshot and loss
handler. Normalized reports and remaining-mask loss reset are kernel-internal
and used by the real PS/2 path; no producer grant, epoch/sequence API, registration
system or second source was added. Availability remains PS/2-only at the adapter
boundary. Source review confirmed release/fresh-press and pre-report lock/warp
guard parity. The later proposal remains unaccepted. The final default image build passed.
A manual headless Q35/KVM, four-CPU, 512 MiB, VirtIO GPU/PS/2 smoke at
`b77cd1d` confirmed ordinary position changing from `(640, 400)` to `(675, 388)`,
locked motion leaving that position parked, Super+Esc revocation, fresh-left-click
relock and global lock cleanup on exit. Source-loss behavior remains reviewed
without fault injection. Earlier consumer-specific captures retain the revisions
above.

## Task 3 baseline

Captured on 2026-10-08 before task 3 code, at fresh main
`abbededa6aae7b112bc178a9788e775059bd79ae`, the tasks 1+2 merge. Pins:
userland `b83ff679e91911e9483e24901afca9b3b26d0071`, ports
`a642f07382e14bd233ac1be2b6a814e95c32d835`, fs `b427df2`, lwIP `a1aadb9`.
The full ordinary `make -j16 image` passed with the existing LLVM builder
`pyxis-llvm23.1.3-49e2c1a`; no compiler rebuild or code modification was needed.

Manual QEMU 10.2.2 boots used Q35, nested KVM, CPU max, four CPUs (one socket,
four cores, one thread), 512 MiB, UTC RTC, matching Fedora OVMF code/variables
with fresh variables each boot, modern VirtIO RNG, and VirtIO SCSI CD-ROM.
There was no NIC, HOST export or disk. The matching ELF was inspected through
read-only GDB. Default standard VGA without `display.size` confirmed
`DISPLAY_BOOT`; `bochs-display` with `DISPLAY_SIZE=1280x800` confirmed
`DISPLAY_BOCHS`; modern `virtio-gpu-pci` confirmed `DISPLAY_VIRTIO_GPU`.
All measured backends had 1280x800 scanout, pitch 5120, RGB shifts 16/8/0 and
160x48 TTY content cells below navigation.

The original default image booted Development's ordinary local shell. Manual
PS/2 motion and left drag moved the system pointer with no selection overlay,
as expected before task 3. For the matched mux samples, only the staged archive's
`config/live.lua` gained the existing `multiplexer = true` option for Development.
The existing cpio assembly command and `make-image.sh` rebuilt that archive/image;
tracked source files stayed unchanged. The Bochs configuration additionally
regenerated `build/limine.conf` with the existing DISPLAY_SIZE setting. The saved
baseline images are local build artifacts, not a new build profile or workflow.

A single-pane mux appeared with its shell and BSP footer. Manual pointer motion,
drag and wheel did not select text or browse history. Ctrl+B then `[` entered
its existing keyboard history mode. GDB observed a sleeping three-interest
readiness request for one pane; the existing source bounds its eight-pane case
at 17, within the native 32-interest limit. Graphics pointer sessions have no
wait readiness in this baseline. These observations do not qualify task 3's
future selection or source-loss behavior.

Four manually taken `space_present()` samples per backend used a hardware entry
breakpoint, a direct 64-bit HPET counter read, `finish`, and a second counter
read, as in task 1. QEMU's 10 ns HPET ticks were converted to milliseconds.
Development's idle single-pane mux, caret and terminal system pointer were shown;
all spaces had started. No trace option, guest write, synthetic syscall, test,
fault injection, benchmark harness or CI change was added.

| Backend | Samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Boot framebuffer | 0.772930, 0.889330, 0.817070, 1.129710 | 0.853200 | 0.772930–1.129710 |
| Bochs | 0.829880, 0.764840, 0.945830, 0.947870 | 0.887855 | 0.764840–0.947870 |
| VirtIO GPU | 2.191670, 1.642960, 2.132510, 1.562170 | 1.887735 | 1.562170–2.191670 |

These are shared-host nested-QEMU presentation samples, not native ThinkPad
performance or selection overhead. Repeat matched configuration/workload after
task 3. Baseline captures and raw GDB logs remain local. All task-owned QEMU and
GDB processes were stopped before the baseline was recorded.

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `250f82a57f22738d23859c923f8143ddf9f0e49c0521918e836ebf4d6f986e4f` |
| Default initrd | `2aea52fa7c7c6496e8a2c39de5f4d0001051ddac526975720ef24ee1caec48d8` |
| Default ISO | `bd647ef2f7f3fbd612ee0fecb2889301c4d981fe3473745ae094d7c5fdedbed0` |
| Mux opt-in initrd | `635d60070eeeb29fbc9a5f8eef9e1db8fa50f32c4fe4a005dea547a932227e95` |
| Mux opt-in ISO, boot framebuffer | `12eed52a9438779b77c7a6283a1a782d564530f04d9bab43cfd6c238c415674a` |
| Mux opt-in ISO, 1280x800 modeset | `b2a8784bb7c1a4d66539524f1cb111e1052159f7ab8ba0fed75650ac0e741622` |

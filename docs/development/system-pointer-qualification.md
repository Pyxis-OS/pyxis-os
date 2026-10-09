# System pointer qualification

Tasks 1–4 are merged. Task 5 closure and the default-cursor redraw are delivered
for review in [#562](https://git.internal/PyxisOS/pyxis-os/pulls/562); its
[closure record](#task-5-closure-and-cursor-redraw) reviews the matched QEMU
evidence and partial owner-reported native checks. Remaining native coverage
stays open in [technical debt](../technical-debt.md#native-system-pointer-qualification).

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
guard parity. At that validation revision the producer contract was proposed;
the later accepted conditional source-loss adjustment is recorded in
[pointer coordination](../interfaces/pointer.md#input-source-coordination). The final
default image build passed.
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

## Task 3 qualification

Task 3's three defaults were accepted on 2026-10-08 before implementation. The
pre-code baseline above is main `abbeded`. Core changes are `75f7afd` (graphics
readiness), `b013005` (local retained glyphs/selection) and `f0e52d7` (terminal
controller/routing/overlay). The parent consumes published userland
[PR #166](https://git.internal/PyxisOS/pyxis-userland/pulls/166), final pin
`91cc6c7bd6066800d7fd28d03b7cfbe1f872313c`; merge userland #166 before
[Pyxis #550](https://git.internal/PyxisOS/pyxis-os/pulls/550). Ports remain
`a642f07`; SDL/Quake event-loop changes and the hardware cursor are later work.

The complete ordinary `make -j16 image` passed with the existing LLVM builder,
including SDK, all userland and existing Quake/SDL ports. The later userland
follow-ups were rebuilt through the same ordinary image path. No compiler
rebuild, upstream import, tests, self-tests, fault injection or new CI/benchmark
infrastructure was added. Final submitted-head build/filesystem checks are
tracked on #550; userland has no existing action tasks, so no independent CI
success is inferred from its unparseable empty status response.

### Interactive configuration and local selection

The main functional run used QEMU 10.2.2, Q35, nested KVM, CPU max, four CPUs,
512 MiB, modern VirtIO GPU/RNG/SCSI CD-ROM, matching OVMF with fresh variables,
GTK/X11 and PS/2 input. A VirtIO NIC forwarded host 127.0.0.1:24568 to guest
2323 only for the existing remote screenshot/download path. Local key/mouse
reports were entered through HMP; GDB used the matching saved ELF and read-only
observations, never synthetic calls or guest-memory writes. The final matrix
runs removed the NIC and used headless 1280x800, as in the baseline.

Caelum and the ordinary Development TTY showed linear selection. Read-only GDB
observed a completed Caelum selection at indices 81–91 on its 80x28 grid.
Presented selected pixels used the selection background while the corresponding
TTY backing pixel retained its ordinary background. The glyph array is 7,680
bytes per TTY at the 1280x800 profile, not scrollback or another framebuffer.

An ordinary `echo ok` preserved selection on older untouched text. A native Lua
write moved to a selected cell, repainted the same glyph with a different SGR
colour, then returned the cursor to an unselected row: selection remained valid.
Replacing the selected glyph with Y invalidated it. Forty lines of ordinary Lua
output scrolled the TTY and cleared a newly completed selection. An actual host
window resize committed 800x573, TTY generation two and 100x33 cells, clearing
Caelum selection. A fresh press in the unused bottom pixel margin at y=568 did
not start selection. A later 1280x800 resize committed generation three.
Boot-framebuffer and Bochs runs also confirmed local Caelum overlay selection;
Bochs additionally exercised mux selection. No native ThinkPad claim is made.

### Native graphics readiness

`mousetest` ran with a sleeping three-interest native wait: POINTER READABLE,
KEYBOARD READABLE and DISPLAY RESIZED, whose caller was the acquired pointer
owner. A conditional read-only breakpoint observed `readiness_complete()` with
CALL_OK, pointer ready mask 1 and the same caller/owner after ordinary input.
Showing its terminal layer left the graphics subscription alive and unfocused;
Ctrl+C completed cleanup, leaving graphics and pointer owners null. Its existing
10 ms UI deadline remains; the new readiness does not claim an SDL blocking
wait fix or migrate Quake's loop.

### Mux routing, history and lifecycle

The optional mux image changed only the staged archive's existing
`multiplexer = true` configuration, as in the baseline. Trusted init/script
SHELL_SESSION/session/mux handoff succeeded: GDB observed terminal control owned
by mux with graphics pointer ownership null and a sleeping four-interest wait
for one pane. Eight panes in equal layout used 18 interests, including the
separate OBJECT_TERMINAL_POINTER queue, within 32.

Manual Lua output of 1,100 numbered lines filled history to 1,024 rows. A content
drag highlighted retained cells. One away detent showed `Scrollback 3/1024` and
moved the top visible line back by three; the opposite detent returned to live,
and a subsequent `echo ok` reached the shell. Wheel over the left pane browsed
it while the right pane retained the focused heading. Heading/content clicks
focused panes; an anchored drag crossed another pane and navigation without
switching spaces or selecting those regions. Controller-owned output never
activated kernel local-TTY selection.

BSP/equal layout changes and actual physical resizes reset the view identity.
An eight-pane layout shrank to 400x212, generation four, showing only its focused
pane. Selection clamped within that visible content; hidden pane rectangles,
headings/dividers/footer remained outside its selected range. Closing all eight
panes ended mux and left controller owner, queue and image null/zero.

Quake launched from a pane acquired graphics pointer ownership and initial lock
while mux kept its separate terminal owner, unfocused. Super+Down revoked lock
and focused the terminal controller while Quake's graphics/pointer owners
remained alive. Terminal motion/wheel and later graphics return worked. After
Ctrl+C, graphics ownership ended while the mux controller stayed acquired.

The detailed GTK consumer/capture checks used userland `27f8b58`. Final dependency
`da7aaa0` corrected cancellation of unfinished selection on output-driven view
changes; this was exercised in the final headless VirtIO image. A native Lua
program delayed then printed three lines while a mid-view drag remained held.
The selected rows would still fit after scrolling: highlighted content pixels
fell from 324 to zero, controller accepted buttons cleared to zero while the
physical left snapshot stayed one, and release was required before a new press.
Completed selections are preserved through unrelated unchanged visible output.

A separate native delayed-output run selected the oldest visible history row at
`1024/1024`, then printed another 1,100 lines. The old row was evicted; highlighting
vanished even though repeated digit text appeared in replacement rows. After the
final pane closed, GDB observed controller owner null and a new local kernel
selection valid at indices 2084–2086, confirming fallback handling.

The last `91cc6c7` correction refuses fresh selection in blank padding beyond a
retained history row's original width while keeping anchored-edge clamping.
The complete default image build passed. In a fresh GTK VirtIO boot, a pane
with old narrow history grew from the 640x480 to the 800x573 physical view.
A fresh click at x=360,y=50, inside the grown pane but beyond that historical
row's retained width, produced zero selection-colour pixels in the first content
row. The pane still focused; no retained edge cell was selected instead.
This correction does not change earlier full-width selection/capture paths.

### Capture and matched presentation samples

The existing Remote CAPTURE grant saved native PNGs through `screenshot`, then
`xfer send` and explicit host download confirmation. Both mux selection and local
Caelum overlay captures decoded to 1280x800 RGB and matched separate stable HMP
PPM dumps exactly (no differing pixels), including their visible cursors/caret.
The screenshot, game and raw image artifacts stay in the local build directory.

Four idle single-pane mux samples per backend repeated the baseline's no-NIC
1280x800 configuration and direct HPET entry/finish method. These final matrix
images use userland `da7aaa0`; the later padding-only hit-test follow-up does not
change idle presentation.

| Backend | Samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Boot framebuffer | 0.730090, 0.944420, 0.663710, 0.631010 | 0.696900 | 0.631010–0.944420 |
| Bochs | 0.862700, 1.541550, 0.986740, 0.719170 | 0.924720 | 0.719170–1.541550 |
| VirtIO GPU | 1.593140, 1.716030, 2.060900, 1.068220 | 1.654585 | 1.068220–2.060900 |

All ranges overlap their baselines. These small shared-host nested-QEMU samples
establish neither isolated selection overhead nor native performance. Hardware
cursor cost remains task 4, and milestone closure remains task 5.

Saved final-matrix artifacts with userland `da7aaa0`:

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `8bafcee0d26c6f4a1677a92924f772273d666dadaa88437452c5d80b215a5129` |
| Default initrd | `90c850fb923809701e51809c31f4cf032bc079e88cdcef267a5f5db49224e2ad` |
| Default ISO | `849d049a1262356d3d2c9a563886ccb8a02d21a0b18a0b643c3f23d6c3f7d0ba` |
| Mux opt-in initrd | `47f5e11c8b59fce4c986682215b76a71630ac8632617addbdeff5b27cce2e390` |
| Mux boot-framebuffer ISO | `5cf6ba1ae92427e29227ab1104fa851b076b8826104433392fab677e550954c0` |
| Mux 1280x800 modeset ISO | `cf19d6077d4b0219c9278694b5050377821bac44177e664ae04920967fe132ca` |

### Main integration follow-up

Main advanced to `77fc0ed` with merged Bluetooth contracts and the private audio
engine. Merge `f00bb4f` incorporated it without changing unrelated submodule pins.
The complete default image build passed again. A fresh headless Q35/KVM,
four-CPU, 512 MiB VirtIO/PS/2 smoke confirmed Caelum selection, mux controller
acquisition, simultaneous `mousetest` graphics ownership, its three-interest
pointer/keyboard/display wait and normal cleanup of both owners. No audio device
was added to this pointer qualification. The detailed checks and cost/artifact
tables above precede that integration; no integrated-head cost claim is made.

Main then advanced to `8c823b3` with the separately merged remote transfer
changes and provider direction. Userland #166 integrates its main `36d3059` at
`63d4324`; the parent pins that published merge, preserving both pointer and
transfer changes. Pointer code is unchanged from the preceding smoke; the
complete default image was rebuilt after this integration.

### Limits

Stale-view refusal, repeated/foreign acquisition, copied/closed grant lifetime,
controller fault cleanup, queue overflow, physical stream loss, panic/output-lock
refusal and resize allocation failure are source-reviewed without fault
injection. Terminal program image/hide calls share the qualified graphics image
lease/copy path but have no separate interactive image-client check. Remote mux
keeps its keyboard path; no remote pointer transport was built. Native PS/2
selection/wheel/input timing remains in the accepted ThinkPad debt. No clipboard
publication/paste, USB HID or second input source is implemented. Bluetooth's
accepted conditional reset/revoke predicates remain documented future integration.
All task-owned QEMU, GDB and remote-client processes are stopped after validation.


## Task 4 software baseline

Initial pre-code observations on 2026-10-08 use fresh main `b43a573`, after
pointer tasks 1–3 merged. Pins: userland `63d4324`, ports `a642f07`, fs `b427df2`,
lwIP `a1aadb9`. The worktree is `pyxis-pointer-task4`, branch
`pointer/virtio-cursor`; tracked source was clean during these observations.
The complete ordinary `make -j16 image` passed using
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a`.

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `984cca3b8565c5e4e37d8afea846b0ebb7f4cc89e0c47b564d3e65a1230def05` |
| Default initrd | `38473b0652cf93aa09931364bf23d5a61a0ec4c0560cbf24640c4e7d21ba7128` |
| Default ISO | `c1ec182d84350ccffdcec853396b50b8272f09654bc45b15d2eefa6d3bd068ca` |

QEMU 10.2.2 (`qemu-10.2.2-1.fc44`), Q35, nested KVM, `-cpu max`, four CPUs
(one socket/four cores/one thread), 512 MiB and UTC RTC used the matching raw
OVMF code/variables pair with fresh variables. No NIC, HOST or storage export;
modern VirtIO RNG and VirtIO SCSI CD boot avoid the known AHCI emulator issue.
Display arguments were `-vga none -device virtio-gpu-pci,disable-legacy=on
-display none`; PS/2 remained the only pointer source. Read-only GDB confirmed
`DISPLAY_VIRTIO_GPU`, 1280x800, pitch 5120 and native RGB shifts 16/8/0.

The initial four idle `space_present` entry/finish samples use the existing
hardware breakpoint/direct HPET method, 10 ns ticks. Caelum's terminal caret
and 9x20 software I-beam were visible at the initial physical hotspot (640,400).
After normal HMP relative motion, a completed selection at physical (144,80)
covered visible glyph indices 490–498; GDB confirmed the selection and physical
position before the second set of samples.

| Software workload | Samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Idle Caelum, visible pointer | 1.78791, 1.77650, 1.64776, 1.96830 | 1.782205 | 1.64776–1.96830 |
| Caelum, completed selection and visible pointer | 2.35495, 1.95409, 1.84484, 1.74706 | 1.899465 | 1.74706–2.35495 |

Quake `+timedemo demo1` and a second console `timedemo demo1` each completed
969 frames while the relative lock was held. Unprofiled reported rates were
1335.8 and 1559.4 fps (rounded reported elapsed 0.7/0.6 seconds). GDB observed
ownership/read terminal output after completion, without breaking during either
run. These are initial locked observations, not a completed repeated locked/
unlocked comparison. Later attempts to recall the console command did not
produce confirmed new timing output and are not samples.

Raw logs, the matched ELF/ISO, framebuffer dump and terminal output remain local
under `build/pointer-task4` and `/tmp/pyxis-pointer-task4-*`. All task-owned
QEMU/GDB jobs are stopped. These initial samples make no hardware or isolated
cursor-cost claim. The remaining software workloads were completed before code changes, as
recorded below; the hardware comparison follows them. The task's capture
completion and host frontend defaults were accepted 2026-10-08, as described in
[hardware ownership](../kernel/display.md#hardware-pointer) and
[frontend limits](qemu.md#hardware-pointer-frontend).


### Completed software baseline before task 4 code

The accepted frontend run used the same saved software ELF/ordinary ISO,
`GDK_BACKEND=x11`, relative PS/2 and `-display gtk,gl=off,zoom-to-fit=off`.
X11 inspection confirmed a 1280x827 window with its 27-pixel menubar and
1280x800 guest surface, unscaled 1:1. No build was running during the following
samples. An earlier overlapping-build pass and events sent while GDB had the
VM paused are excluded. For motion, a conditional entry breakpoint waited for
an actual hotspot change; HMP sent each report while the VM was running.
Positions alternated between (640,400) and (680,400), before measuring that
changed frame. These are four movement-frame samples, not a packet latency or
sustained-motion throughput measurement.

| GTK/X11 software workload | Samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Idle Caelum, visible I-beam | 3.72931, 3.89582, 4.79457, 4.39106 | 4.14344 | 3.72931–4.79457 |
| Confirmed interior motion frames | 4.38806, 4.31614, 3.65905, 4.07930 | 4.19772 | 3.65905–4.38806 |
| Completed Caelum selection, visible I-beam | 3.08912, 3.39852, 3.92462, 5.31993 | 3.66157 | 3.08912–5.31993 |
| Development TTY, capture frame | 8.16039, 4.85687, 4.42500, 4.29788 | 4.640935 | 4.29788–8.16039 |

Selection was confirmed valid over indices 506–514 before its samples. Fresh
press/motion/release were sent separately after focus settled; earlier combined
focus/press and short button pulses did not create a selection and are excluded.
Capture used the native `screenshot` command. Its timing spans presenter entry
through capture finish, including backing allocation and FILE publication; the
first is a cold allocation. All four reached `screen_capture_finish(true)`.
A read-only dump of the 1280x800 capture backing matched RGB pixels of the
same stopped frame's HMP framebuffer dump exactly, including the I-beam. The
following shell prompt is a later frame and is not used for that comparison.

Quake's warm console `timedemo demo1` runs completed 969 frames each, with no
breakpoint/stop during these measured runs. GDB read lock state and retained
TTY output after each completed result. A fresh left click allowed relock;
Super+Esc removed it without affecting keyboard input. Console commands were
typed explicitly while the console remained open; menu/history attempts without
new timing output are not samples. Initial launch runs are separate from these
warm samples.

| GTK/X11 software Quake state | Samples (fps) | Median (fps) | Range (fps) |
| --- | --- | --- | --- |
| Unlocked, visible system pointer | 1589.4, 1562.4, 1585.9, 1584.5 | 1585.2 | 1562.4–1589.4 |
| Locked, hidden system pointer | 1591.5, 1587.7, 1587.4, 1594.6 | 1589.6 | 1587.4–1594.6 |

The first three warm unlocked samples preceded the locked group; the final
unlocked sample followed Super+Esc. Each reported rounded elapsed time was
0.6 seconds. These short nested-VM runs establish a matched workload and
variation, not native or isolated cursor performance.

Resizing the same GTK window to 1400x927 produced a 1400x900, pitch-5600 guest
surface, generation two, and cleared the confirmed selection. The native
transaction copied four TTYs under the output lock in 3,114,460 ns (one observed
resize, not a repeated benchmark). Waiting for QEMU's coalesced geometry event
and committed guest dimensions avoids treating a transient scaled/centered
window as the qualified 1:1 configuration.

The local-only shareware image also built after explicitly mounting its local
data directory into the existing builder. Its ISO hash is
`06dc89033fd0bf7af6728c36a87761d0b6692b0b127ac3bc190af35b3bb4fb86`.
An initial assembly attempt omitted that mount and correctly refused missing
`spawn.mpq`; no fallback download or data publication occurred. All baseline
QEMU/GDB jobs are stopped. No task 4 pointer code changed before these checks;
raw logs/artifacts remain local for the hardware comparison.


## Task 4 hardware qualification

Both task-specific defaults were accepted on 2026-10-08 and recorded in
`67ef2be` before implementation. The completed software baseline was committed
as `87b311a`, also before code. Implementation commits `f5b288a` and `09cb32d`
add capture-only pointer composition and the bounded VirtIO cursor backend.
Merge `eafe6e9` incorporates documentation/CI main `0179163`, without changing
runtime inputs. No public ABI, SDK helper, dependency pin or compiler-container
change belongs to task 4. Userland remains `63d4324`, ports `a642f07`, fs
`b427df2` and lwIP `a1aadb9`.

The complete ordinary image and a separate local-only `DIABLO_DATA` image built
with the existing LLVM builder. No shareware data, game screenshots or raw
qualification artifacts are published. Saved artifacts identify which image
was used; later documentation/build provenance can change ELF bytes without
changing pointer source.

| Saved artifact | SHA-256 |
| --- | --- |
| Ordinary hardware kernel, `09cb32d` | `1db78f74368f8dfa9528badaefb9c086c8782f06db178fcab97ec3bf5280cb5a` |
| Ordinary hardware ISO, `09cb32d` | `4d184f67aa7ada5fadd4e34b44a2b3ec0421ea84d95d8063ee885d03380a8d13` |
| Local-only hardware shareware kernel, `eafe6e9` | `435fd3b7fa745a26dd36fb05c356200903900ec80ba5507843fd017764b427ef` |
| Local-only hardware shareware ISO, `eafe6e9` | `b7bfa5ac6b719165acb445d9425063d981f280a61498061bca13bfe0f9b7d88f` |
| Bochs software-regression kernel, `79873a5` | `ba85c599096e665b3232f4f40c1e71df7c193201cf752af8e17d36088f6fd51f` |
| Bochs software-regression ISO, `79873a5` | `24f4e757dba50fe32f2727a28e4fc5000165b84b2666dcabf08eb93cd786a87b` |

### Configuration and matched costs

The primary hardware run repeats the completed software baseline: packaged
QEMU 10.2.2, Q35, nested KVM, CPU max, four CPUs, 512 MiB, UTC RTC, fresh
matching OVMF variables, modern VirtIO GPU/RNG/SCSI CD-ROM, no NIC or exported
storage, GTK/X11, relative PS/2 and a committed unscaled 1280x800 guest surface
inside a 1280x827 window. No build overlapped cost measurements. Read-only GDB
used the matching saved ELF, hardware breakpoints and direct HPET reads with
10 ns ticks. Each sample was entered manually. Motion reports arrived while
the VM was running; a conditional breakpoint confirmed an actual position
change before measuring the changed frame. These measure presentation, not
packet-to-screen latency or sustained motion throughput.

| Hardware workload | Four samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Idle Caelum, visible I-beam | 3.55336, 4.68190, 4.00721, 3.33636 | 3.780285 | 3.33636–4.68190 |
| Initial confirmed interior motion | 17.14174, 6.76218, 6.04804, 4.77307 | 6.40511 | 4.77307–17.14174 |
| Repeated warm interior motion | 5.91814, 5.26302, 5.39955, 6.61419 | 5.658845 | 5.26302–6.61419 |
| Completed Caelum selection, visible I-beam | 4.38211, 3.36930, 4.31554, 4.37819 | 4.346865 | 3.36930–4.38211 |
| Development TTY, capture frame | 11.45258, 3.88325, 5.13161, 4.55891 | 4.84526 | 3.88325–11.45258 |

Selection was valid at the same indices 506–514 as the software comparison.
Capture timing spans presenter entry through capture finish, including backing
allocation and FILE publication; its first sample is cold. Unchanged cursor
state reused request ID 11 across these capture samples.

Warm motion was repeated to investigate the higher initial cost. Its median
is 1.461125 ms, approximately 35%, above the software median 4.19772 ms; those
warm ranges do not overlap. A direct MOVE request measured 1.68972 ms through
matching completion, returning success with no outstanding request. Source
review confirms one serial cursor wait after the existing frame transfer/flush,
without allocation, image transfer or another full-frame copy on an interior
warm move. The initial 17.14174 ms sample exceeds the nominal 16.67 ms frame
interval. These small debugger-profiled nested-VM samples include device waits,
scheduling and shared-host variation; they do not establish native performance
or an isolated CPU saving. Full-frame transfer/cadence is deliberately unchanged.
This task claims cursor ownership/composition behavior, not a performance gain.
Idle and capture ranges overlap their software baselines.

Warm Quake console runs repeated `timedemo demo1`, 969 frames each with rounded
reported elapsed time 0.6 seconds. There were no debugger stops during measured
runs. Initial launch warm-up is excluded; lock state and retained terminal
output were read after completion. The first three unlocked runs preceded a
fresh click and the locked group; Super+Esc preceded the final unlocked run.

| Hardware Quake state | Four samples (fps) | Median (fps) | Range (fps) |
| --- | --- | --- | --- |
| Unlocked, visible system pointer | 1600.7, 1590.8, 1571.9, 1596.4 | 1593.6 | 1571.9–1600.7 |
| Locked, transparent hardware shape | 1578.7, 1599.4, 1595.3, 1599.0 | 1597.15 | 1578.7–1599.4 |

Both ranges overlap the corresponding software runs. No native or stable
frame-rate improvement follows from these short runs.

### Cursor, capture and lifetime checks

Debugger inspection confirmed `DISPLAY_VIRTIO_GPU`, started/not stopped, and
both queues drained. The visible default I-beam is 9x20 with hotspot 4,10 inside
a padded 64x64 resource. Read-only XFixes inspection of GTK's live host cursor
matched uploaded RGBA pixels exactly. Idle frames reused the confirmed cursor
request; interior motion added one MOVE with a nonzero active resource ID.

A native screenshot reached `screen_capture_finish(true)` with both queues
drained. Its 1280x800 RGB backing equalled cursor-free HMP scanout plus exactly
one uploaded I-beam at physical hotspot (172,80). All differing pixels from
scanout were within x=168–176, y=70–89. This independently checks capture-only
composition without doubling the pointer. HMP screendump itself excludes the
host hardware cursor. Successful zero-byte cursor completion confirms buffer
consumption; QEMU supplies no separate acknowledgment of cursor application
or visible scanout timing, as accepted by the owner.

`mousetest` supplied its 16x20 cursor, hotspot 1,1. Upload and live host cursor
preserved opaque colours and alpha. W warped to the bounded surface center;
H installed a wholly transparent hardware resource. Lock also installed the
transparent shape while retaining the program's visible preference. Super+Esc
restored the custom cursor. Super+Down showed the terminal I-beam while graphics
ownership remained alive but unfocused; Super+Up restored the program image.
Exit cleared display/pointer/image ownership and restored the default cursor.

With the complete 1:1 viewport visible, a left-edge hotspot x=0 masked source
column zero while retaining hotspot 1,1. At physical (1279,799), only a 2x2
portion of the image remained, with all other resource pixels transparent.
Large HMP deltas that had not yet reached an edge were not treated as edge
checks; actual physical positions and resource masks were inspected afterward.
Resizing the primary GTK window to 1400x927 committed a 1400x900 guest surface,
generation two and framebuffer resource ID 4; fixed cursor IDs 2/3 stayed
independent. Cursor clipping returned to the complete image and both queues
drained. Four TTY copies took 3,160,440 ns in this single resize observation,
compared with the baseline's 3,114,460 ns; neither is a repeated benchmark.

### DevilutionX and software backends

The local-only shareware run used GTK/X11 fit mode for additional resize
checks. Pointer checks waited until committed guest dimensions matched the
viewport 1:1; transient scaled states are outside qualification. At 640x480,
ordinary motion exposed DevilutionX's 33x28 cursor with hotspot 0,0. Its BGRA
source matched the padded RGBA resource exactly. Live XFixes pixels preserved
all alpha values and opaque colours; 118 pixels had fractional alpha, with
host premultiplied colours within one value of rounded source colour times
alpha/255. Keyboard navigation hides the image by program choice.

Disabling the game's hardware-cursor option set explicit hidden state and
installed a transparent resource while the game's software pointer remained
visible in scanout. Re-enabling it and moving restored the program image.
A committed 800x600 resize used framebuffer ID 4 and a new 42x35 program image;
a later 480x360 resize used ID 5 and a narrower image clipped at the bottom.
Both retained cursor ownership/visibility and drained the queues. Single
four-TTY copy observations were 1,056,930 ns and 441,600 ns respectively.
Exit cleared graphics/pointer/image ownership. These are functional resize
checks, not matched cost samples or publication of game data.

Separate no-NIC headless 1280x800 boots confirmed `DISPLAY_BOOT` with the saved
ordinary hardware image and `DISPLAY_BOCHS` with `DISPLAY_SIZE=1280x800`.
Both retained the visible software I-beam and motion/local selection; GDB
confirmed completed selections at indices 509–518 and 500–509 respectively.
VirtIO startup was false and cursor backing remained unallocated on both.
Their software composition continues through the existing capture tee.
These smoke checks do not repeat the earlier tasks' complete consumer matrix.

### Limits and current-main integration

Malformed replies, prerequisite refusal, timeout, failure retention, panic,
maximum-size images and allocation unwind were inspected in source without
fault injection or synthetic clients. Frame/image leases last through normal
submission, matching changed-cursor completion and capture finish. Owner exit
releases source image ownership; the fixed driver cursor resources remain until
reboot. No second source, input policy, clipboard or USB HID work was added.

GTK/X11, relative PS/2 and stable 1:1 geometry are the qualified hardware path.
Wayland warp, scaled/centered placement and other frontend colour/position
callbacks are source-inspected limitations, not positive runtime qualification.
See [frontend limits](qemu.md#hardware-pointer-frontend) and their
[revisit point](../technical-debt.md#virtio-cursor-frontend-limits).
[Native PS/2 qualification](../technical-debt.md#native-system-pointer-qualification)
remains deferred to the owner's ThinkPad batch after Bluetooth investigation.
At this task 4 checkpoint, milestone closure still required separate owner
authorization; task 5 results are recorded below.

Main's separately merged Bluetooth runtime transport `114f2acb` was integrated
as `a84fb601`, preserving unchanged dependency pins and pointer rules. The
matched cost/consumer checks above precede that merge; no integrated-head cost
claim is made. Raw logs and images remain local under `build/pointer-task4`
and `/tmp/pyxis-pointer-task4-*`.

The complete ordinary image passed after this merge and contains no shareware
MPQ data. A fresh no-NIC, four-CPU, 512 MiB headless VirtIO/PS/2 boot confirmed
startup, default cursor upload, `mousetest` custom shape/hotspot, bounded warp,
lock hiding, Super+Esc restoration and owner/image/display cleanup. Both queues
drained and the driver remained available. This integration smoke checks guest
state, not host GUI placement; the earlier GTK qualification remains separate.
All task-owned QEMU and GDB processes are stopped.

| Current-main integration artifact, `a84fb601` | SHA-256 |
| --- | --- |
| Kernel ELF | `b1d001d1e9b7bdd7a5e3c32c8c7e39e0345e6f03ba8daf7e6db109c9d9a96419` |
| Default initrd | `6f403d68cdf92ccceb9b69c5ab2b5d3506b0b740c47f9b1360af5b4ad2e343b6` |
| Default ISO | `4628b38c4e87f9c887dfb94ab44cb569d7a766dd36d57eff27e0f9a49cd0994c` |


### Review follow-up: deferred ordinary cursor completion

Claude's #560 review requested removing the serial cursor-completion wait from
ordinary frames before merge. This is a bounded task 4 fix: no authority,
public interface, resource budget, fallback or input policy change. Ordinary
MOVE and UPDATE commands now remain posted in the driver's single cursor slot.
The next frame polls completion without sleeping. Control/frame copies can
overlap that cursor work because their storage is disjoint. Slot/backing reuse
and resize drain the earlier command before mutation. Capture drains matching
completion even when it reuses a state posted by an ordinary frame. Fenced
image preparation and uncertain-ownership retention remain unchanged.

The cache describes last posted state, distinguished from confirmed state by
an outstanding descriptor. Async command and resource bytes belong to the
driver; no source-image pointer or lease survives the frame. Status, cookie,
zero-length completion and original one-second deadline checks apply when
polling or draining. Independent read-only review found no blocker in buffer
reuse, shared interrupt wakeups, capture, panic/failure or resize ownership.

The ordinary `make -j16 image` passed with the existing builder. The first
compile attempt exposed a missing include for `screen_capture_active`; adding
its existing internal header resolved it. Runtime artifacts use the fix on
`eae72387` with unchanged pins and include no game data:

| Async review artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `36329148b12f707b7cf9f47bf6421e612574ccac3f634c749dcc50dafa119667` |
| Ordinary ISO | `ba0829bb391efcca2cfc8e8addec586d955dfaef5b6513d8e24efcae276bf835` |

GTK/X11, relative PS/2, unscaled 1280x800, 1280x827 host window, QEMU/firmware,
CPU/devices and HPET/GDB method repeat the earlier motion workload. No task build
was running. Two initial hardware motion observations (4.98025 and 6.85309 ms)
preceded the following four warm measurements. The position alternated between
(640,400) and (680,400); every measured frame returned with one cursor descriptor
still posted rather than waiting for its completion. A direct MOVE posting
observation was 0.96015 ms and returned success with one outstanding descriptor;
this includes notify/MMIO and debugger/host variation, not a completion wait.

The warm hardware median remained above the earlier 4.19772 ms software median.
To check that difference, the unchanged saved pre-code software ELF/ISO was
booted again under the same current host conditions, after stopping the hardware
VM, with two initial moves before its four measured warm frames.

| Motion workload | Four warm samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Deferred hardware MOVE | 7.11648, 4.00517, 4.99356, 5.62520 | 5.30938 | 4.00517–7.11648 |
| Saved software baseline, current-host repeat | 5.91137, 5.49673, 5.00733, 4.10500 | 5.25203 | 4.10500–5.91137 |

The contemporaneous medians differ by 0.05735 ms, about 1.1%, with overlapping
ranges. This does not meet or establish a stable improvement over the historical
4.20 ms target, nor identify a native performance effect. The descriptor still
being posted at frame return proves that QEMU completion no longer serializes
that frame's tail; the remaining difference is not evidence of a forced cursor
completion wait. Full-frame transfer/cadence is unchanged. Keep both the
historical and repeated software observations rather than replacing the baseline
or attributing shared-host/nested-VM variation to the cursor alone.

A native screenshot reached `screen_capture_finish(true)` with both queues
drained. Its pixels matched scanout plus exactly one uploaded I-beam, with
all differences in x=636–644, y=390–409. `mousetest` installed its custom image,
lock hid it and Super+Esc restored visibility. GTK growth committed 1400x900,
framebuffer ID 4, queues drained and driver available. Exit cleared graphics,
pointer and image owners. Queued slot reuse/status/deadline failures remain
source-reviewed without fault injection. These checks do not repeat the earlier
DevilutionX data/image or boot/Bochs matrix because those paths are unchanged.
Raw logs are `/tmp/pyxis-pointer-task4-async-gtk.log` and
`/tmp/pyxis-pointer-task4-async-software-control.log`; artifacts remain local.
All task-owned QEMU/GDB processes were stopped at this checkpoint. Task 4 was
parked in #560 for morning review; it subsequently merged as `52451d3a`.
The owner authorized task 5 on 2026-10-09; its closure record follows.


## Task 5 closure and cursor redraw

The owner authorized closure with the default-cursor redraw on 2026-10-09,
after task 4 merged. #560 merged as `52451d3a`; `pointer/milestone-close` was
rebased onto that fresh main before opening its PR. No dependency update belongs
to task 5: userland stays at main's `362574b1`, ports `a642f073`, fs `b427df29`
and lwIP `a1aadb91`.

- [x] Task 1: ordinary surface input and bounded software cursor, #545.
- [x] Task 2: relative lock, Super+Esc and consumer migration, #545,
  userland #164 and ports #65/#66.
- [x] Task 3: local/mux selection, wheel history and terminal/graphics native
  readiness, #550 and userland #166.
- [x] Task 4: VirtIO hardware cursor and capture-only software blend, #560.
- [x] Task 5: cursor redraw, reviewed qualification and permanent references,
  delivered for review. This checkbox records task delivery, not its merge.

The implemented [pointer contract](../interfaces/pointer.md),
[PS/2 device reference](../devices/mouse.md),
[display/capture ownership](../kernel/display.md#hardware-pointer),
[mousetest usage](../userland/mousetest.md) and
[mux behavior](../userland/multiplexer.md#local-pointer-input) replace the completed
WIP. Git retains the accepted decisions and worklists; this report retains
measurement/configuration boundaries rather than duplicating the proposal.

### Reviewed closure evidence

The earlier task records supply matched QEMU checks on boot, Bochs and VirtIO,
which the owner accepted as sufficient for milestone closure. Task 1 covers
ordinary motion, program images/hotspots/hiding, warp, geometry and capture;
task 2 covers lock/escape/fresh-click activation, Quake and SDL/DevilutionX;
task 3 covers local/mux selection, wheel/history, layout/clipping/lifecycle and
native wait readiness. Task 4 qualifies the hardware queue, host/upload images,
clipping, capture, focus, lock hiding, teardown, resize and local-only
DevilutionX hardware-cursor option.

The historical hardware warm-motion median 5.659 ms included a serial cursor
wait. The approved review fix removes that wait from ordinary-frame completion:
frames returned with a descriptor still posted, while capture and resource reuse
drain it. Its median was 5.309 ms versus the contemporaneous saved software
image's 5.252 ms, with overlapping ranges. Both exceed the historical software
median 4.198 ms. This is a measured nested-VM limit, not a stable native gain or
proof of hardware-cursor bandwidth savings. Full-frame submission/cadence is
unchanged. The full sample tables, cold observations and raw method remain above;
task 5 does not replace them with another timing series for an init-only redraw.
Source-only refusal/overflow/fault/panic/allocation cases remain distinguished
from interactive checks, without tests or fault injection.

### Default images and captures

The owner's native report described the old arrow as a sliver with a vertical
tail. Character-row tables in `kernel/pointer.c` now draw a 12x19 classic arrow:
vertical left edge, 45-degree head edge, notched tail angled down to the right,
black outline and white fill. It stays inside the existing transparent 16x24
image at hotspot `(0,0)`. The 9x20 I-beam uses the same table style, serifs and
unchanged hotspot `(4,10)`. Bounds and alphabet checks run at initialization;
height guards are compile-time checks. Opaque black/white pixels use the existing
straight-alpha BGRA storage; no ABI, program-image or input change is made.

The before image uses task 4 integration `60ed2ed8`; the after captures were
made with the redraw before its compile-time height guards were added. Those
guards add no runtime instructions. Both ordinary full-image builds passed with
`pyxis-llvm23.1.3-49e2c1a`; the guarded kernel also built. After rebase, ordinary
and `DISPLAY_SIZE=1280x800` Bochs image builds passed again. No compiler rebuild,
new tests or boot/benchmark automation was added.

Manual QEMU 10.2.2 Q35/nested-KVM boots used CPU max, four CPUs, 512 MiB,
UTC RTC, fresh matching OVMF code/variables, PS/2, modern VirtIO RNG/SCSI CD,
and no NIC/storage export. Before/after software captures use the confirmed
1280x800 boot framebuffer. The Development terminal remains shown: the arrow
hotspot is `(200,8)` over navigation; the I-beam is `(800,400)` over empty content.
PNG conversion preserves original pixels. The linked details are 8x
nearest-neighbour extracts for review, not native scale or new native evidence.

| Cursor | Before | After |
| --- | --- | --- |
| Arrow | [full capture](../images/pointer-task5/arrow-before.png), [8x detail](../images/pointer-task5/arrow-before-detail.png) | [full capture](../images/pointer-task5/arrow-after.png), [8x detail](../images/pointer-task5/arrow-after-detail.png) |
| I-beam | [full capture](../images/pointer-task5/ibeam-before.png), [8x detail](../images/pointer-task5/ibeam-before-detail.png) | [full capture](../images/pointer-task5/ibeam-after.png), [8x detail](../images/pointer-task5/ibeam-after-detail.png) |

Bochs smoke confirmed `DISPLAY_BOCHS`, 1280x800/pitch 5120 and no started
VirtIO GPU. Both drawn shapes matched the boot-framebuffer capture pixels at
the same hotspots. A native `screenshot` reached `screen_capture_finish(true)`;
its RGB backing exactly matched the stopped frame's scanout, including I-beam.

VirtIO used GTK/X11, relative PS/2, unscaled 1280x800 inside its 1280x827 window.
The uploaded 64x64 padded arrow and live XFixes host cursor matched all bytes,
including hotspot `(0,0)`. The I-beam retained `(4,10)`. Native capture completed
with both queues drained and its pixels equalled cursor-free scanout plus
exactly one uploaded I-beam; differences were confined to x=796–804, y=390–409.
Those I-beam pixels also matched the boot/Bochs captures. Earlier incorrectly
typed HMP commands did not run capture and are excluded. Typing with breakpoints
disabled and explicit short key holds avoided debugger/key-release interference;
no guest call injection or memory mutation was used.

| Artifact | SHA-256 |
| --- | --- |
| Before kernel | `7d7122e3643b8d92e4da6c224af2169ba03ec6e5db3ff9c7d35793a76d87f4a0` |
| Before ordinary ISO | `226789d8045260e47c6c8a5fc34acf985375336786258246d3d117278f25786e` |
| After capture kernel | `dbf69533a9f156774c52ffee12f7be128230e05706cc61d8752ea8dc4a82634d` |
| After capture ordinary ISO | `229dc8cdb55da0456225ab2931a5e554ddf66b1f964b32232567e22c6a525e42` |
| Rebased guarded kernel, Bochs/VirtIO smoke | `653a28afc594d28f2726e6e5fdb32581b39aac4e70548b6d181dcceaf8e0525b` |
| Rebased Bochs ISO | `728dd5ced6cdae5ec9aab3cebdacb3bf8ebd58d68df5c6ef1068ff99eba82f3f` |
| Rebased ordinary VirtIO ISO | `1e07b035feede5f359d8a1255c31f8ef240c8ec4f7173409142237b50ceb7b85` |

### Native evidence and remaining work

The owner reported partial ThinkPad PXE qualification on 2026-10-09 at
1920x1080 using PS/2 and the boot framebuffer. Boot 1 (`114f2ac`) checked
ordinary motion/buttons, tabs and unspecified text selection; a remote-terminal
screenshot contained the local I-beam. Boot 2 (`183f793`) checked Super+Esc in
Quake, switching spaces with the cursor shown and local-terminal selection.
Mux selection/wheel were not checked: no space opted into `multiplexer = true`,
and manual `mux` launch produced the expected missing-authority diagnostic.

Click-to-relock, cursor-visible layer changes, program image/hotspot/show/hide
and warp, native cost samples and judgment of the new native arrow remain open
in the exact [native qualification split](../technical-debt.md#native-system-pointer-qualification).
These are owner reports, not agent-run native validation; boot 1's selection
report is not upgraded to mux coverage. Native checks remain open even with
QEMU milestone closure.

Clipboard export/publication/paste and encoding remain in the
[clipboard proposal](../wip/clipboard.md). Multi-source Bluetooth integration,
USB HID, absolute-mode scrolling, broader host frontends, damage tracking,
recovery and advanced selection remain separately scoped work; the references
and [technical debt](../technical-debt.md#system-pointer-selection-and-input-limits)
retain their consequences and revisit points. Local-only shareware data and game
images were not republished for task 5. All task-owned QEMU/GDB processes are
stopped. Final exact-head CI is reported on the task 5 PR; the owner merges it.

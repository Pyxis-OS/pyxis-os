# Screenshot qualification

The screenshot milestone closed on 2026-10-08 with QEMU qualification and the
owner's explicit acceptance of deferred native ThinkPad checking. Native capture,
download and performance remain [technical debt](../technical-debt.md#native-screenshot-qualification).
The implemented contracts live in [screen capture](../interfaces/screen-capture.md),
the [command reference](../userland/screenshot.md) and the
[library profiles](ports.md#zlib-development-library).

## Images and environment

Manual checks used q35, four CPUs, 2 GiB, `-accel kvm -cpu max`, raw OVMF and
modern VirtIO network/RNG devices. This is nested KVM, not owner hardware.
The published builder was
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a`, manifest digest
`sha256:50bfb587f2e73d6478ae1c91e86984ccb8223baa4367fe27fa227808e87bc038`.
Ordinary `make -j16 image` supplied kernel, SDK, runtime, ports and packaged command;
no compiler rebuild, probe program, test or workflow was added.

Driver/layer checks used Pyxis `e75ef1b427d535414bb9f7136f0f45f70441ddb2`
and the header-only userland follow-up `6cae1098e11b14cfa3a33ebb479df1a336c63a28`.
Other pins were fs `b427df29f865bc361b8da92bcd74e114581e9a32`, ports
`e85d307336af66d6d5171c5f943ffe312fd086d5` and lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

| Artifact | SHA-256 |
| --- | --- |
| Driver/layer kernel ELF | `e201f2c57ccb4bcd409e4ec1cc140ee4d1fa258c6976f765cd15652cdbef0181` |
| VirtIO ISO | `9a8b26d0231b41b596255af9f7c08b642175c0bb284cee96cce483a1b1157ae5` |
| Bochs ISO, explicit 800x600 boot setting | `3ea49dee3f604017616d3013b568f936427baa28f43337d633e24afa0a441c94` |
| Packaged screenshot P1F | `a44667d707f67806a1fb0d754b334ec2a15cdd5504cf01ee98b4415a116e3c59` |

The closure branch subsequently incorporated main `cdc096e` and preserved its
SDL2 libc additions in the userland dependency `56bc258fb562e21ad5e096592c2864d447271eea`.
The ordinary image build was rerun for that integration. Measurements below
describe the identified qualification images, not performance of every later
main revision.

## Displayed pixels and snapshot lifetime

Each PNG was saved from the Remote shell while a static local space remained
shown, then downloaded using `xfer send`, host confirmation and SHA-256
verification. QEMU monitor `screendump` produced a PPM of that same shown screen.
Host Pillow decoding and RGB comparison found zero differing pixels, with no
mask or excluded region, in every row below.

| Backend and shown content | Dimensions | PNG bytes |
| --- | --- | ---: |
| Boot framebuffer, Caelum TTY | 1280x800 | 64,224 |
| Bochs, Caelum TTY | 800x600 | 40,039 |
| VirtIO, initial Caelum TTY | 640x480 | 28,737 |
| VirtIO, resized Caelum TTY | 1024x741 | 38,226 |
| VirtIO, Development Mandelbrot graphics | 1024x741 | 139,881 |
| VirtIO, Development terminal with graphics hidden | 1024x741 | 15,067 |
| VirtIO, shrunk Development terminal with graphics hidden | 640x453 | 7,731 |

Debugger inspection verified `DISPLAY_BOCHS` and `DISPLAY_VIRTIO_GPU`, rather
than inferring drivers from the requested devices. Bochs used `-vga none
-device bochs-display` and `display.size=800x600`. VirtIO used `-vga none
-device virtio-gpu-pci,disable-legacy=on -display gtk,gl=off,zoom-to-fit=on`.
The GTK window was physically enlarged and shrunk; no kernel geometry was
substituted. Guest dimensions exclude GTK chrome. The first real resize advanced
generation one to two, with pitch 4096 and extent 3,035,136 bytes. Shrinking
produced pitch 2560 and extent 1,159,680 bytes. The selected terminal layer
remained visible while the graphics surface remained published but hidden.
Comparisons cover navigation, cursor, content and the presenter's clipping and
background pixels across a space switch and layer changes.

One initial 640x480 snapshot FILE was retained with a diagnostic reference to
the real native result while its normal command handle closed. Its original
extent stayed 1,228,800 bytes. Raw backing dumps before resize, after resize and
after switching space and layers all had SHA-256
`10f84f7f80979f2298d3d96c390ec23c84a3ea819acf9896805fbe2f72fcee70`.
The diagnostic reference was then released through the real object operation;
the final release reached `destroy_file`. This is debugger-assisted lifetime
evidence, not an additional packaged consumer or injected allocation-failure test.

The review of [userland #159](https://git.internal/PyxisOS/pyxis-userland/pulls/159)
independently reported a boot-framebuffer comparison at Pyxis `1a1514b`,
1280x800 standard VGA, four CPUs and KVM. Its 24,407-byte PNG matched the monitor
except x0–79, y256–271, where the shell prompt/cursor appeared after the command.
That is reviewer evidence, distinct from the unmasked comparisons here. The
review's private-header naming nit is resolved by
[userland #162](https://git.internal/PyxisOS/pyxis-userland/pulls/162): `encode.h`
now distinguishes the command's declaration from libpng's `<png.h>`.

## Earlier authority and failure checks

Native implementation qualification in
[Pyxis #513](https://git.internal/PyxisOS/pyxis-os/pulls/513) observed the 64-byte
reply, RGB shifts 16/8/0, compact FILE extent, READ and EOF, WRITE/RESIZE refusal,
copied-handle retention and final destruction. With a real VirtIO caller pending,
a second real CALL returned BUSY with zero reply bytes and its table loan cleared;
the admitted call completed and pending/active/private state cleared. Those early
CALL checks used a diagnostic grant at an unsubmitted-process checkpoint.

Subsequent ordinary policy-chain checks observed CAPTURE through Development
and Remote init/session/shell handoffs, foreground, pipeline and background
children. Read-only kept DRAW without CAPTURE and still ran Doom. Installed and
rescue policy, missing-seed refusal and strict boolean parsing were source-inspected,
without an installed boot in this milestone.

The packaged command checks in
[Pyxis #521](https://git.internal/PyxisOS/pyxis-os/pulls/521) established quiet
success, host PNG decode, guest replacement and host overwrite refusal preserving
the old hash. Read-only's missing-CAPTURE refusal preserved an existing guest
destination; boot-archive parent authority failed before staging. Publication
over an existing directory failed, preserved it and reported both possible names
while retaining the temporary. A later capture skipped that temporary without
changing its hash. Allocation/backend/stop failures, partial I/O, libpng recovery
and uncertain-write cleanup remain source-reviewed without fault injection.

## Matched presenter cost

An isolated local control was built from the same `e75ef1b` source, builder,
configuration, boot settings and initrd as the measured current kernel. The only
control change bypassed the capture begin/copy/finish presenter hooks, used direct
`display_copy` and retained ordinary `display_end_frame`. The native ABI, seed,
service and space creation remained linked. No capture request ran during idle
sampling. This isolates presenter hooks; it is not a whole-system pre-feature
comparison. The [measurement-only patch](experiments/screenshot-qualification/idle-control.patch)
is retained for reproduction; it is not applied to the implementation.

| Artifact | SHA-256 |
| --- | --- |
| Current measured ELF | `fcb0f4f46eeec7b9e5d1765674f081180497592f494327fc47e74f8c8bfa2e2c` |
| Control ELF | `e9995a02cde267b2f970e3e8095c93402cc00455b25a3d5348554d0c577d1820` |
| Recorded control patch | `d5316989269cb6c8639d0eb96eade503a4a44d23409666e4ba5177ee366409e2` |
| Common initrd, userland `28f8c16` | `b348e7f4d75725d0c1b87c2184295354523668945b88addc346212587dbf2410` |
| Effective kernel configuration | `ac12acc93c3fcbbdff1ace10d95b8f3cdf883e1c09d21ce413ba09df8324a98b` |

All three idle runs used the same QEMU 10.2.2 AHCI-fix executable, default standard
VGA boot framebuffer 1280x800, pitch 5120, modern VirtIO NIC/RNG device order and
fresh raw OVMF variables. All four spaces and networking were ready; Caelum was
selected with its TTY cursor visible. HPET period was 10 ns. Manual hardware
breakpoint/`finish` pairs measured from the first C statement of `space_present`
to its return. Each sample was entered separately; no sampling loop was added.

The timing sequence was a hardware breakpoint at `space_present`, a read of the
64-bit HPET counter at `0xfffffe80402020f0`, `finish`, then a second read. The
debugger used the matching ELF and `set may-call-functions off`. Reproduction
must keep source, configuration, initrd, firmware and device order matched;
the earlier task-3 presenter timings use a different compiler/layer implementation
and are unsuitable for attributing capture overhead.

| Sample | Control before, ticks | Current idle, ticks | Control after, ticks |
| --- | ---: | ---: | ---: |
| 1 | 86411 | 155128 | 123827 |
| 2 | 97715 | 98945 | 111810 |
| 3 | 80257 | 123330 | 94653 |
| 4 | 111419 | 118815 | 85812 |
| 5 | 80931 | 93278 | 86323 |
| 6 | 80475 | 81353 | 83709 |
| 7 | 91851 | 76259 | 107792 |
| 8 | 101296 | 69558 | 137582 |

| Run | Median ms | Range ms |
| --- | ---: | --- |
| Control before | 0.891310 | 0.802570–1.114190 |
| Current idle | 0.961115 | 0.695580–1.551280 |
| Control after | 1.012225 | 0.837090–1.375820 |

The current median lies between the controls, with overlapping ranges and host
drift. These samples do not demonstrate a stable idle regression. Timings include
device waits, scheduling/preemption, debugger and host/nested-VM variation;
they are elapsed observations, not pure CPU cost or native performance.

## Active capture and encoding

The same measured current kernel/ISO/initrd and device configuration were used
for five real `screenshot tmp://cost.png` commands from the Remote shell, with
static Caelum shown at 1280x800. A hardware breakpoint at `space_present`,
conditioned on a pending request, selected the admitted capture frame. Measuring
through its return includes backing allocation, composition, display submission
and FILE publication. No delay between request arrival and frame admission is
included. The first capture is retained in the results.

Encoding was timed from `screenshot_encode` entry to return, including row reads,
conversion, libpng/zlib and native output writes. Every call returned true. The
matching debugger ELF's entry and loadable segment bytes were verified against
the packaged P1F; the private-header rename changes no executable behavior.
The relevant breakpoint was disabled while `finish` ran, then re-enabled before
the next separately entered command. Both intervals used the same 10 ns HPET
counter as the idle measurements. Command staging/rename, remote download and
whole-command latency are outside the encoding interval.

| Sample | Active presenter, ticks | Encoder, ticks |
| --- | ---: | ---: |
| 1 | 769010 | 2769166 |
| 2 | 99652 | 2305748 |
| 3 | 388996 | 2356184 |
| 4 | 136697 | 3108953 |
| 5 | 218349 | 3649859 |

| Interval | Median ms | Range ms |
| --- | ---: | --- |
| Active presenter | 2.183490 | 0.996520–7.690100 |
| PNG encoder | 27.691660 | 23.057480–36.498590 |

The final 64,224-byte RGB PNG passed the explicit download and unmasked
boot-framebuffer comparison above. Its raw snapshot was 4,096,000 bytes. These
small, debugger-profiled samples establish successful execution and observed
cost for this static TTY scene. They do not predict graphics/noisy-image encoding
time, transfer time or native hardware performance. All task-owned QEMU,
debugger, remote-client and build jobs were stopped after qualification.

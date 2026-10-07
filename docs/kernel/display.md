# Display drivers and resizing

Caelum presents one software-rendered screen through a boot framebuffer,
VirtIO GPU 2D or Bochs driver. The navigation bar, every local TTY and selected
application graphics share its native 32-bit pixel layout. Pixel dimensions are
distinct from terminal cell dimensions. Remote terminal sessions keep their
creation dimensions and generation one.

The physical interface is in [display.h](../../include/kernel/display.h), with
drivers in [kernel/display](../../kernel/display/) and hardware access under
arch. [Mapped graphics](../interfaces/graphics.md) describes the independent
per-space DRAW capability; it grants no hardware or mode-setting authority.

## Selection and early output

Before AP startup, the BSP selects the first supported PCI display in discovery
order. A modern VirtIO GPU takes precedence only when it is that first device;
otherwise Bochs is attempted. With no supported device, the Limine framebuffer
remains the target. There is one screen and no multi-device failover.

The Limine adapter accepts an absent framebuffer as an all-zero descriptor.
Early output always reaches serial and mirrors a directly writable framebuffer
when supplied. A VirtIO-only OVMF boot can expose PixelBltOnly GOP, which Limine
does not return as a framebuffer. A bounded pre-AP GET_DISPLAY_INFO command
obtains initial geometry using temporary queue storage with IF=0 and no scheduler
waits. This is the sole boot-only queue ownership exception.
Without a usable firmware target or successfully prepared driver, boot panics
on serial.

Normal presentation begins after early-console retirement. Boot and Bochs
panics can reclaim a permanently mapped direct target using the
[early-console panic protocol](early-console.md#panic-ownership). VirtIO panics
remain serial-only: no emergency queue, command, reset or GPU allocation.
The owner confirmed normal ThinkPad boot-framebuffer output in
[PR #471](https://git.internal/PyxisOS/pyxis-os/pulls/471); native panic remains
unqualified. The ThinkPad has no native AMD GPU driver.

## VirtIO GPU

The sole BSP presenter owns runtime queue use with IF=1. It creates a 2D
resource, attaches page-list backing, selects one enabled scanout, then
transfers and flushes full frames through fenced commands. Interrupt entry
records activity and wakes the presenter; it performs no queue or allocation
work. Command and reset waits are bounded. There is no hardware cursor, vblank,
3D acceleration or recovery after terminal driver failure.

DISPLAY events are acknowledged before querying geometry and rechecked after
the transaction. Changes coalesce to the latest enabled dimensions of the
selected output. Unsupported sizes and allocation refusal retain the last
valid geometry. Failed preparation retries on a fresh host event; a space
registry change during preparation instead defers a fresh attempt.

Only confirmed fenced detach and resource unreference permit GPU backing
reclamation. Unknown device ownership retains backing, control queues and PCI
resources until reboot and stops presentation. Even successful reset does not
establish the required teardown contract. Failure never tries a separate
firmware VGA target. Graphics acquisition, presentation, replacement and size
queries become unavailable; release still works.

## Bochs

`display.size=WIDTHxHEIGHT` is a boot-only request, supplied by
`make run QEMU_VIDEO=bochs DISPLAY_SIZE=800x600` or standard VGA's same
`DISPLAY_SIZE` option. A missing option retains firmware geometry without an
ordinary log line. Invalid, unsupported or unavailable modes report refusal
and keep the firmware target. There is no live Bochs resize signal or runtime
mode-setting API.

The driver validates the DISPI identity, BAR0 aperture, BAR2 registers, VRAM,
alignment and common space geometry requirements. An enabled firmware DISPI
mode is required. BAR0 stays WC, BAR2 UC; boot pixels must begin at BAR0.
The permanent aperture borrows existing boot leaves and extends them before
AP startup. It remains retained even after refusal.

Mode programming preserves the saved firmware state and verifies all relevant
registers, including virtual dimensions, offsets and byte order. Failure
restores and verifies firmware state. Unverified restoration stops boot with
a serial panic instead of publishing an uncertain direct target.

## Transactional local resizing

Between frame leases, the BSP stages a GPU surface plus every local TTY,
navigation and cursor buffer. Allocation and VM mutation use IF=0, outside
the output lock. It checks the space registry before scanout switching and
after the device wait. A newly created space causes rollback and preparation
against the new registry. Device switching leaves the old logical geometry
published until confirmed success.

The output lock protects copying the latest raster and committing all local
pixel pointers, dimensions and geometry generations together. Stable TTY and
console objects keep their identity. No allocation or device wait occurs under
this lock. Every CPU's console and kernel-log writes wait during the multi-space
copy; its cost grows with screen size and number of spaces.

TTY resizing preserves whole rasterized cells around the cursor, without
reflow or scrollback. Height shrink drops enough top rows to retain the cursor;
growth preserves surviving positions. Cropped columns and rows are lost.
New cells and margins use the TTY background. Colors, tab width, parser state,
space identity, focus and capture survive; cursor position is translated and
clamped, and pending wrap is cleared. Columns are `width / font_width`; rows
are `(height - bar_height) / font_height`.

Old AP-visible pixel mappings remain intact until writers and the presenter
have relinquished them. The BSP then requires
[acknowledged TLB retirement](smp.md#memory-and-output-boundaries) on every
online CPU before unmapping or reusing pages or virtual addresses. The handler
takes no locks and performs no logging, allocation or device work. A bounded
missing acknowledgement retains one old mapped batch until reboot and disables
further resizing while leaving committed presentation working. A late
acknowledgement alone does not reclaim it.

## Observation and applications

Console SIZE returns columns, rows and generation atomically in 24 bytes under
READ or WRITE. Display SIZE returns content width, height, pitch, generation
and channel shifts in 48 bytes under DRAW, without acquiring graphics.
Generations start at one and advance on committed local resizes.

Each [wait interest](../../include/abi/wait.h) contains handle, event mask and
observed generation. RESIZED is a coalesced level condition: the current
generation differs from the supplied observation. It reserves no state. The
readiness worker remembers notifications across scanning and parking, closing
the query-to-sleep race. READABLE on console/terminal input preserves existing
FIFO read arbitration; acquired keyboard sessions expose physical-event
readability. Resize never appears as an input byte, keyboard focus event or
SIGWINCH.

The blocked libterm line editor re-queries and redraws without a key. It retains
the entire input and logical cursor, showing a bounded cursor window when the
prompt/input exceed screen capacity. Input capacity comes from the caller's
buffer. A resize redraw returns toward the old rendered origin, clamping at
the top when that origin was cropped. It can scroll without clearing surviving
preceding rows. Adaptive event decoding uses an explicitly delegated
clock READ handle; absent clock authority preserves legacy input behavior.
Kilo redraws its editor and search prompt while retaining its existing minimum
of two columns and three rows.

Acquired graphics mappings keep their address, extent, pitch and dimensions
until owner-only REPLACE or RELEASE. Presentation clips the top-left
intersection and fills uncovered destination margins. REPLACE takes an expected
generation, prepares a zeroed candidate at a disjoint user address through the
parked-process BSP loan, and returns BUSY on mismatch. Allocation or mapping
failure leaves the old mapping and ownership intact. Success retires the old
user alias within the call while presenter leases retain old backing. There
is no RELEASE/ACQUIRE gap; a visible session selects the new blank buffer until
redraw. The old pointer is invalid after successful return.

Mandelbrot replaces at render checkpoints and recomputes aspect while retaining
centre and zoom. Doom checks at frame boundaries and recomputes integer scale
and letterboxing of its fixed 320x200 frame. Below scale one, Doom continues
with the old clipped mapping until growth. Replacement failure preserves
rendering into old backing and waits for a fresh generation before automatic
retry. Query-only programs see changed geometry on their next SIZE call;
other graphics ports retain fixed mappings until explicitly adapted.

## Qualification and cost

Earlier tasks qualified boot and post-handoff direct panic output, serial-only
VirtIO panic, standard VGA and Bochs boot modes, transactional GTK growth/shrink,
all-CPU TLB acknowledgements and real allocation refusal. See merged
[boot #468](https://git.internal/PyxisOS/pyxis-os/pulls/468),
[VirtIO #473](https://git.internal/PyxisOS/pyxis-os/pulls/473),
[Bochs #485](https://git.internal/PyxisOS/pyxis-os/pulls/485) and
[geometry #487](https://git.internal/PyxisOS/pyxis-os/pulls/487) for their exact
revisions and validation limits. Geometry copy/swap measurements across four
TTYs ranged from 0.45 to 4.39 ms in nested KVM.

Task 5b's baseline used main `3b84214`, userland `c4bcf16`, ports `eb648d5`, an
ordinary GCC image, q35, GTK VirtIO 640x480, four CPUs, 256 MiB, nested KVM and
raw OVMF. Build with `make -j16 image`; the interactive command was equivalent to:

```sh
make debug CPUS=4 MEMORY=256M ACCEL=kvm QEMU_VIDEO=virtio QEMU_DISPLAY=gtk \
  OVMF_CODE=/usr/share/OVMF/OVMF_CODE.fd OVMF_VARS=/usr/share/OVMF/OVMF_VARS.fd
```

In GDB with the matching `build/caelum.elf`, connect to `localhost:1234`, let
boot finish, then stop at `space_present`. The observed HPET period was
10,000,000 fs (10 ns/tick). Repeat these commands manually at each entry:

```gdb
set $started = *(unsigned long long*)0xfffffe80402020f0
finish
p (*(unsigned long long*)0xfffffe80402020f0 - $started) * 10
continue
```

Five idle `space_present` HPET samples through manual GDB were
2.877760, 2.410710, 3.385060, 2.764070 and 2.609910 ms (median 2.764070 ms).
They include GPU waits, preemption and host/debugger variation, not just CPU
composition. Baseline ELF SHA-256:
`476df5a370243cce153e4f4bface52299ea20647cf30857f035ce0d40989a5f1`.
The pre-integration kernel at `674a893` (plus the console header documentation),
userland `f40468a` and ports `be081b8` passed an ordinary GCC SDK, applications,
ports and ISO build. Kernel/runtime changes compiled cleanly; existing vendor
warnings remain. The matched idle run measured 2.346790, 4.174720, 2.623370,
2.624480 and 3.323830 ms (median 2.624480 ms), with overlapping baseline/changed
ranges. No regression or speedup is established. Changed ELF SHA-256:
`12cb5f4bbd1fdf9001ba43fe4a3f1ecccc23659ba58fb7a1fb591eb23b1ce170`.

Interactive GTK growth/shrink woke a blocked shell and Kilo, including Kilo's
active search prompt. Kilo saved `tmp://r`, retained its text/search and returned
to the shell. The shell kept an input larger than a 32x9-cell destination,
showed a cursor window, accepted Home, and restored the full wrapped text on
growth to 1024x741 without a key. GDB observed its next wait carrying the new
generation. Mandelbrot redrew with the new aspect; a real REPLACE returned
CALL_OK with a disjoint address and retained visible selection. Doom also
replaced at 1024x709 content geometry. At a 256x185 physical destination it
continued with its old 1024x709 mapping and clipped output.

At 192 MiB with the same four CPUs and a VirtIO NIC, a normal remote Lua
session retained a 20,000,000-byte string (including its allocator's peak
storage). The physical transaction committed 1650x1290, generation two, but
Mandelbrot's REPLACE returned NO_MEMORY. GDB confirmed the original frame,
user address and visible selection remained unchanged; controls still worked.
After Lua exited, a fresh 800x573 resize produced a successful 800x541
replacement at generation three. No injected failure or test consumer was used.
That remote terminal remained 80x24, with its wait observing generation one,
and accepted commands before and after local resizing.

No tests, self-tests, hooks, fault injection, boot/output automation or CI were
added. Partial decoder races, arbitrary reply overlap, stale generation,
presenter-preempted retirement and backend failure were source-reviewed;
the real allocation-refusal and successful replacement paths were exercised.
An ordinary standard-VGA boot with four CPUs and 256 MiB retained DISPLAY_BOOT,
1280x800 and generation one; interactive shell input and `echo ok` passed.
That headless run used QEMU 10.2.2 with the existing AHCI fix described in
[QEMU troubleshooting](../development/qemu.md#ahci-cd-rom-crash-before-kernel-entry).
GTK runs used the installed QEMU 10.2.2. QEMU, GDB and the remote client were
closed after validation.

After merging main `625fd2a`, including kernel and userland TLSF fixes and native
cp, the combined `1dadc76` with userland `9bb5523` and ports `be081b8` passed
the full ordinary GCC image build. Its four-CPU, 192 MiB GTK/NIC boot repeated
the real 1650x1290 replacement refusal with old address/frame/visibility intact,
then successfully replaced at 800x541 after pressure withdrawal and a fresh
resize. The performance samples above precede this main integration; no
integrated-head performance claim is made. Combined ELF SHA-256:
`9514567b265dfa87debaca5531c8177e08be07fccb7124106664ef4ae32f0a90`.
Kilo also redrew while idle at 640x453 on the combined revision, then exited
normally. All validation processes were closed.

The next ordinary owner-run ThinkPad boot should check terminal sizing and
Doom or Quake startup after the ABI changes. No such native check is claimed
here. Full-frame idle presentation, peak old/new storage, unsupported devices
and failure retention remain in
[technical debt](../technical-debt.md#virtio-gpu-resize-limits-and-runtime-retention).

Future physical GPU drivers, 3D, multi-monitor selection, damage tracking,
compositing, scrollback and remote session resizing need their own bounded
contracts. [Desktop and graphics](../wip/desktop-graphics.md) retains the
physical-hardware direction. Completing this display work does not authorize
those directions.

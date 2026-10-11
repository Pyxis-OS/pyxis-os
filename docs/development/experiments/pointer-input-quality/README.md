# Pointer input quality task 1 qualification

2026-10-11, USB report mice and shared scroll integration. Implementation is
in Pyxis #716, userland #214 and ports #99; dependencies merge first. Native
ASUS/dongle/Keychron qualification is pending through the owner and luna.

## Revisions and configuration

Before-code baseline: Pyxis `56bd4da8`, userland `df210f1a`, ports `df3210b0`.
After the native-thread integration, repeat controls use A `a7b16fe2`, userland
`76d0508`, ports `df3210b0`; B `0e5e94cd`, userland `de944e7`, ports `28b5a3bc`.
Each pair has matching source-built SDK/apps for its ABI (80 versus 88 bytes),
so these runs cannot share an initrd. Both retain fs `b427df29` and lwIP `a1aadb91`.
The final functional/default build is Pyxis `bdbe892f`, userland `303d913e`, ports
`28b5a3bc`: rebased onto the later mux CWD/numbered-focus work. Cost samples are
from the stated A/B pair, not a performance measurement of that later rebase.

Existing LLVM 23.1.3 builder; QEMU 10.2.2 Q35, nested KVM, host CPU, four CPUs,
512 MiB, fresh OVMF variables, UTC RTC, 1280x800 boot software framebuffer,
virtio RNG. PS/2 plus USB keyboard/mouse through two boot-present USB 2 hubs.
`POINTER_SYNTHETIC=1 DISPLAY_CURSOR_PROBE=1 LOG_LEVEL=info`, with boot options
`display.cursor.probe=1 pointer.synthetic=schedule`. No HID dumps during costs.
The existing synthetic schedule is settle 10 seconds, idle/motion/idle/motion
30 seconds each, then stop; no buttons, wheel, focus or space changes. Caelum
is shown. Probe output itself redraws occasionally while otherwise idle.

Sealed kernel SHA256: A `fa368ed31d343562e15bf391197fb85913424184ae721615a10c0ad5b703516a`,
B `642e5285b6f24b3e77dd6af1c8be1b51fface042892c9455db502ee9e56ddbc2`.
Initrd SHA256: A `670f9cb5699498d29478d3db3be5973d5bda05de35a9c8a9c6fc9a63090402c4`,
B `479486937183b1ca54f75311eb463fdef5cd8ca7f40a1b630f76ab23e7d224a6`.
Raw logs/images stay in local `build/pointer-baseline/`, `build/scroll-cost-*`
and the separate control worktree; no raw captures are committed.

## Presenter and BSP costs

Before code, the two idle windows each presented 16 frames (compose 5.52/5.14 ms,
copy 5.34/5.24 ms); motion presented 1789/1798 frames (compose 828.27/826.05 ms,
copy 681.55/665.42 ms). The refreshed interleaved A1/B1/A2/B2 results below are
30-second boundary deltas. Frames, compositions and scanout copies agree.

| Run | Window | Frames | Compose total, ms | Copy total, ms |
| --- | --- | ---: | ---: | ---: |
| A1 | Idle 1 | 15 | 8.90 | 4.84 |
| A1 | Motion 1 | 1792 | 901.24 | 740.63 |
| A1 | Idle 2 | 16 | 7.02 | 5.39 |
| A1 | Motion 2 | 1789 | 797.58 | 669.25 |
| B1 | Idle 1 | 16 | 5.98 | 5.86 |
| B1 | Motion 1 | 1792 | 941.50 | 777.22 |
| B1 | Idle 2 | 15 | 31.30 | 29.68 |
| B1 | Motion 2 | 1622 | 2039.19 | 1819.70 |
| A2 | Idle 1 | 16 | 6.01 | 7.31 |
| A2 | Motion 1 | 1793 | 717.68 | 619.59 |
| A2 | Idle 2 | 16 | 4.92 | 5.64 |
| A2 | Motion 2 | 1760 | 984.29 | 911.93 |
| B2 | Idle 1 | 15 | 4.93 | 4.57 |
| B2 | Motion 1 | 1800 | 637.82 | 561.90 |
| B2 | Idle 2 | 16 | 4.65 | 4.93 |
| B2 | Motion 2 | 1789 | 672.07 | 571.73 |

The existing host `/proc` sampler took five complete five-second windows after
the synthetic schedule: idle, then USB F1 and relative motion at 10 Hz. BSP vCPU
host-thread percentages are fractions of one host CPU, not all four CPUs.

| Run | Idle median (range), % | USB-active median (range), % |
| --- | ---: | ---: |
| A1 | 11.6 (11.6–13.2) | 13.2 (11.8–17.0) |
| B1 | 12.0 (11.4–15.4) | 18.8 (16.0–22.6) |
| A2 | 9.0 (8.2–9.0) | 10.4 (10.2–10.8) |
| B2 | 9.6 (9.0–9.8) | 11.6 (11.0–11.6) |

B1 active sampling overlapped a local build and is confounded. Boundary-crossing
samples and a B2 sample overlapping another QEMU boot are excluded. Other host
build activity was observed. Presenter elapsed totals include preemption; BSP
samples include scheduling, other guest work and KVM. They do not isolate the USB
worker. Idle skipping survives and most motion windows stay near 60 frames/s;
B1's slow window and B2's faster results show substantial variation, preventing a
causal USB-cost conclusion. These are nested-VM observations, not native costs.

## Build and interactive checks

Ordinary SDK/runtime, full ports/apps, probe and default image builds passed.
Pyxis exact-head CI at `bdbe892f` passed all four jobs; dependency repositories
have no available CI status. The PR records the final submitted-head CI result.

QEMU/GDB found the 88-byte record, installed natural policy, live USB/PS2 sources
and an empty pointer queue. QEMU's report mouse admits a four-byte Input layout,
Button 1–5, signed X/Y and Wheel, no multiplier. Mousetest shows buttons 4/5.
Two sources holding left aggregate by OR; USB release retains the PS/2 hold;
held-source unplug quarantines the surviving hold until release; replug recovers.
These source checks used B before the userland rebase.

On the final functional build, with an archive-authored Development mux space:
a natural detent moves history three rows, the opposite detent returns to live,
and wheel on an unfocused pane preserves the other pane's keyboard focus.
Quake locks on a fresh click, routes relative motion, unlocks with Super+Esc and
relocks after another click. Chocolate Quake receives SDL's natural `MWHEELUP`
event; its existing independent relative-mode issue is outside this task. Local
TTY drag selection works. A USB-only boot (`i8042=off`) starts normally, accepts
keyboard input and retains one keyboard/one pointer with an additional unsupported
absolute tablet attached; the tablet does not become an input source. A separate
archive-authored policy boot installs mouse traditional (1) and touchpad natural
(0), confirmed in GDB. Live startup has no installed machine keys; invalid
installed-key fallback remains code-inspected rather than natively qualified.

Inspection, not QEMU coverage: multi-ID button snapshots, multi-packet report
termination, scoped multiplier Feature RMW/readback, horizontal/high-resolution
reports, all 32 physical buttons and unsupported actual-boot-mouse fallback.
Stock QEMU supplies no suitable device for those cases. No fault fixtures or
self-tests were added. The decoder/profile bounds are in [USB input](../../../devices/usb-hid.md).

## One owner/luna native boot

Luna builds the submitted Pyxis head and published dependency pins with
`make -j16 image USB_HID_DESCRIBE=1 LOG_LEVEL=info` using the existing builder.
For a mux check, author `multiplexer = true` in the Development archive space
before image assembly; keep a second ordinary local-terminal space. No compiler
rebuild. The existing PXE entry uses `init=boot://boot-init.pxe usb.hid.describe=1
log.udp=1`; retain already-qualified display settings, with no synthetic motion.
The option is diagnostic-only; normal builds/boots add no HID dump lines.

Attach the ASUS mouse, dongle and Keychron pointer interface on the intended
USB 2 topology. Retain bounded descriptor, chosen protocol/layout and Feature
readback output from the UDP log, excluding MACs/serials. No device strings are
read by the HID dump. Try the wheel and numbered buttons in mousetest, then hover
mux history: wheel pull moves content down/viewport up, three rows per detent,
without changing pane focus; push returns toward live. Try horizontal or small
high-resolution motion only if the actual descriptor offers it. Verify keyboard
chords/repeat, physical drag selection, two-source holds, release, held-source
unplug and fresh released-state replug. Quake: click to lock, relative mouse-look,
Super+Esc unlock, fresh click to relock. Keep native results distinct from QEMU.

Missing/invalid installed direction keys use the authored archive value; valid
`natural`/`traditional` keys apply next boot. A later paired direction boot may
qualify installed overrides; this one boot does not claim it. Current merged
PS/2 firmware-relative motion remains mouse-class; task 2 introduces distinct
Synaptics touch/TrackPoint sources and rejection. No palm/tap-off claim here.

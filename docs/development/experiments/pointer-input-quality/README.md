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
An additional QEMU boot with `intel-hda`, `hda-output` and a null audio sink
confirms natural wheel pull on the master icon raises 50% to 55%.

Inspection, not QEMU coverage: multi-ID button snapshots, multi-packet report
termination, scoped multiplier Feature RMW/readback, horizontal/high-resolution
reports, all 32 physical buttons and unsupported actual-boot-mouse fallback.
Stock QEMU supplies no suitable device for those cases. No fault fixtures or
self-tests were added. The decoder/profile bounds are in [USB input](../../../devices/usb-hid.md).

## Native report refusal and parser correction

Owner, ThinkPad, 2026-10-11 about 17:00, Pyxis `0df08ffe`,
`USB_HID_DESCRIBE=1`, `usb.hid.describe=1`: Logitech G502 X Plus through
PowerPlay Lightspeed receiver `046d:c53a` and ASUS dongle `1ea7:0066` both moved
normally but only left/right click worked; wheel/extra buttons did not. G502
motion had a high polling rate. The masked log is local at
`/shared/present/batch2/pointer716/native-describe.log`. Both chose boot fallback
with wheel disabled; this is a failed report-mode qualification, not a wheel pass.

Captured mouse-interface descriptors are fixtures, with no strings or serials:
[Logitech interface 1](descriptors/logitech-046d-c53a-interface-1.hex) (123 bytes)
and [ASUS interface 1](descriptors/asus-1ea7-0066-interface-1.hex) (185 bytes).
SHA256 of decoded bytes respectively:
`0302dd523939104d370e95239da756d088c83008c9f0f124d4e6926e866b6cf0`,
`3efbbb9ad50a93761e82db9f0a567ef3cbde94f87016718ac993ab8a721c240c`.

The unmodified parser reproduces both refusals after retaining their Mouse
fields: local Usage Maximum expands every range and rejects more than 64 usages.
Logitech's ignored Consumer range is 1..0x2ff; ASUS Consumer is 1..0x3ff, plus
vendor page **0xffbc** range 0..0xff. ASUS's preceding vendor application on
0xffb5 has single usages, not that range. The correction skips expansion in
ignored collections, retaining only the first local usage needed to identify a
possible nested Collection. Range pairing, order, same-page checks, report-length
accounting and Mouse/root usage budgets remain unchanged.

A temporary native-host caller invokes the actual kernel parser on the byte
fixtures; no new framework, CI or kernel self-test is added. Both now parse:

| Fixture | Input IDs and wire bytes | Output/Feature | Mouse fields | Receive |
| --- | --- | --- | ---: | ---: |
| Logitech | 2:9, 3:5, 4:2 | None | 20 | 9 |
| ASUS | b5:8, 2:7, 1:3, 3:2, 4:2 | b5 Output:8; no Features | 12 | 8 |

Logitech ID2 declares Button 1–16 and signed 16-bit X/Y −32767..32767.
ASUS ID2 declares Button 1–8 and signed 12-bit X/Y −2047..2047; X/Y start at
payload bits 8/20. Both have signed 8-bit Wheel and Consumer AC Pan (0x238),
without Features/multipliers: each axis is enabled at 120/1. ASUS's longest
Input is the ignored vendor IDb5, so receive stays eight bytes. Logitech's
ignored 2-bit System Control with flags 0x60 and ASUS's vendor-before-Mouse
ordering are accepted. No additional rejection was found in either fixture.

Manual report vectors exercise both extrema: Logitech produces X/Y
−32767/+32767, raw physical wheel/pan −127/−127, mask 0xffff; ASUS produces
−2047/+2047, wheel/pan +127/+127, mask 0xff. Subsequent ignored Consumer/vendor
reports produce zero motion/scroll and preserve those button snapshots. These
are parser/decode checks, not native interrupt reports or control-transfer proof.
The owner's G502 has 16 buttons, one mapped on-board to a keyboard chord through
the separate boot-keyboard interface; descriptor Button 1–16 does not imply
that this remapped control will also send a mouse bit.

Default and `USB_HID_DESCRIBE=1` image builds passed after the correction.
QEMU 10.2.2 Q35/nested KVM, four CPUs/512 MiB and the same two-hub USB keyboard/
mouse topology still selects report protocol, receive 4, wheel enabled. Mousetest
shows buttons 4/5 and natural SY −120; GDB finds two keyboards/two pointers
(PS/2 plus USB), empty queue and installed policy. An attached absolute tablet
remains excluded. The task-owned QEMU/debugger are stopped.

Native retest must show `protocol report`, receive 9 (Logitech) / 8 (ASUS),
`pan 120/1 enabled 1 wheel 120/1 enabled 1`, and the Mouse ID2 lengths above.
Then repeat wheel/buttons/mux and the on-board keyboard chord. Captured AC Pan
establishes a supported layout, not that each physical mouse offers an accessible
horizontal control. Native report-mode wheel/button behavior remains pending.

## Native burst freeze, investigation still open

Owner-reported retest, ThinkPad, 2026-10-11 about 17:10, `7491e419` with
`USB_HID_DESCRIBE=1 usb.hid.describe=1`: both receivers now select report
protocol, Logitech Button 1–16 / ASUS Button 1–8, Wheel/Pan enabled at 120/1.
After testing the G502 (suspected free-spin wheel), both USB mice stop permanently
and replug does not recover. PS/2 keyboard, touchpad/TrackPoint and RTL8111 remote
shell continue. This establishes native report admission; burst stability and
functional device closure fail. The owner identifies the retest log on horse at
`~/shared/batch-2026-10-09/caelum-4.log`, after its last Caelum-ready marker.
That retest copy was not yet present locally during this inspection; these
native facts are the owner's report, not a decoded interrupt capture.

The xHCI change in task 1 was the pre-start HID receive-length setter; transfer
and event rings remain one page/256 TRBs, with two posted receives per stream.
The original G502 boot fallback requested **32 bytes**, ASUS eight; report mode
requests nine/eight. A nine-byte TD on a 32-byte endpoint is valid. Existing
worker passes check USBSTS HCH/HSE/HCE/CNR, advance ERDP and clear EHB after a
bounded drain, then rearm and collect. Input arithmetic is signed 64-bit before
int32 saturation; mux history changes are bounded/clamped. The retained controller
quarantine is intentionally permanent until reboot if ownership/fencing fails.
No particular error or quarantine chain is established yet; recovery policy is
unchanged. [xHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)
§§4.9.4 and 4.10 cover event/TD semantics.

QEMU `7491e419`, same four-CPU Q35/KVM boot framebuffer and two USB2 hubs,
USB keyboard plus two mice, archive-authored mux with 51 history rows: an
interactive QMP flood sent 20,505 wheel-down commands/15 s, 20,976 wheel-up/15 s,
and 39,808 alternating HMP ±127 wheel commands/15 s. Mux reached/clamped its
history endpoints, remained responsive, and both USB mice, keyboard and replug
continued. GDB found running=true, failed=false, failure=NULL, 5,947 events and
5,945 IRQs, no pending command, three live pointers and an empty pointer queue.
This **did not reproduce** native failure. Stock QEMU's mouse endpoint cadence
is 8/10 ms, so command rate is not a 1 kHz transfer-rate claim. Its descriptor
has no Pan; no QEMU hardware Pan coverage is claimed. Raw evidence remains local
in `build/usb-burst-7491/`; QEMU/debugger are stopped.

Default/diagnostic image builds and host fixture checks passed. The default
QEMU boot emitted no diagnostic lines; the opt-in boot emitted correctly formatted
five-second observations and survived another 14,392 wheel commands/12 s with
1,593 events, running=true, failed=false. The host fixture also confirms that
Wheel −128 is outside both descriptors' declared −127..127 range: it remains
rejected (reason 3, Logitech field 18 / ASUS field 10). Whether native hardware
actually emits that value is unknown. No fault was injected into the kernel.

### One diagnostic retest through luna

A temporary `usb.xhci.diagnose=1` option adds info-level observations only, with
no default-boot output, packet dump or USB recovery-policy change. The presenter's
BSP/IF=0 service samples retained controller mappings once per five seconds,
even if the USB worker is parked: USBSTS, USBCMD, IMAN, ERDP, software dequeue/
cycle, live head status/control, IRQ/event counts, running/failed, last worker
stage/age, command state and wait/notification flags.
These hardware reads are diagnostic samples, not coherent ownership proof.

On controller quarantine, registers are captured before containment; the
existing interrupt/Run/bus-master quarantine happens before printing. Error
metadata includes first failure before a fence helper can overwrite it, last
and ring-head event type/CC/pointer, and retained endpoint/dequeue/TD ownership.
Endpoint detail includes configuration/Tx Info and retained receive identities.
Endpoint/software detail is post-stop, explicitly distinct from the saved
pre-quarantine registers. A binding loss reports transport status or decode
length/ID/failed Mouse field/range, with the failed stream completion preserved
independently of later events. Class input is released before logging. No keyboard
payload, key identities, device
strings or serials are logged. Decode failures remain failures. One source loss
record per lost binding, one full quarantine dump per controller, five-second
summaries; no unbounded per-successful-report logging.

Luna builds this diagnostic head with the same existing builder and
`make -j16 image USB_HID_DESCRIBE=1 LOG_LEVEL=info`. Keep the owner's prior PXE
options, adding **`usb.xhci.diagnose=1`** alongside `usb.hid.describe=1 log.udp=1`.
No trace log level or new build flag is needed. This option requires the new
kernel; `7491e419` does not recognize it. Keep the same receiver topology and
ordinary terminal/mux workload; no modes, device firmware or polling settings
are changed.

1. Wait ten seconds for two baseline summaries, then try ordinary input from
   both receivers and the G502's free-spin wheel.
2. On freeze, leave devices untouched for ten seconds so the independent
   summaries show whether the worker still advances. Verify PS/2 input and run
   `echo alive` and `lsusb` from the remote shell.
3. Replug one receiver once, wait another ten seconds, and retain the full UDP
   log from the final Caelum-ready marker. Mask MACs; keep the raw log outside
   the repo. Filter `usb xhci diag`, `usb HID diag` and `xHCI` for review.

Logging can perturb burst timing; this is diagnosis, not cost qualification.
After the cause/fix is established, remove these temporary observations or move
error detail to ktrace and repeat native qualification with the option off.

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

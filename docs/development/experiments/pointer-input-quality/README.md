# Pointer input quality qualification

2026-10-11: USB report/shared-scroll task 1 is merged (#716, userland #214,
ports #99), with the owner's ASUS/G502 pass below. Task 2 starts from fresh main;
its before-code baseline and accepted decisions are recorded below. Keychron and
unexercised axes remain native coverage debt.

## Revisions and configuration

Before-code baseline: Pyxis `56bd4da8`, userland `df210f1a`, ports `df3210b0`.
After the native-thread integration, repeat controls use A `a7b16fe2`, userland
`76d0508`, ports `df3210b0`; B `0e5e94cd`, userland `de944e7`, ports `28b5a3bc`.
Each pair has matching source-built SDK/apps for its ABI (80 versus 88 bytes),
so these runs cannot share an initrd. Both retain fs `b427df29` and lwIP `a1aadb91`.
The pre-descriptor-correction functional/default build is Pyxis `bdbe892f`, userland `303d913e`, ports
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

The diagnostic follow-up retains parent main's userland pin `a84259c0`
(which contains userland main `2228716` plus its published math follow-up):
userland #214 head `2628293d02e6734ea14f4164b15f0862a4ee5719`, ports unchanged
at `28b5a3bc`. This prevents rolling back the merged parent's libc inputs.
The earlier flood results retain their stated revisions and are not re-labelled
as measurements of the refreshed initrd.

### Native diagnostic at 59947277: receiver retirement evidence

Owner-run ThinkPad boot, 2026-10-11 evening, `USB_HID_DESCRIBE=1`,
`usb.hid.describe=1 usb.xhci.diagnose=1`. Masked evidence is outside the repo at
`/shared/present/batch2/pointer716/native-freeze-diag-59947277.log` (117 lines).
The corrected trigger is the G502 button mapped on-board to Super+R on its
keyboard interface; the earlier free-spin attribution is superseded.

Measured: ASUS immediately loses its composite binding on a four-byte Input
whose first byte is zero, rejected as an unknown Report ID. That resembles a
boot-format packet but is not a protocol readback or proof of its origin.
The admitted report descriptor has no ID 0. G502 input later stops; controller
7:0.3 advances from 3,412 events to 3,616, then remains static. Its worker age
continues at 5–12 ms, pending-command=0, running=1, failed=0, USBSTS=0, USBCMD=d,
IMAN=2; ERDP matches software index 32/cycle 1, with an old cycle-0 mouse Transfer
Event at the head. The last command changes from type 16 to type 10 (Disable
Slot). This establishes software device retirement on that controller; it does
not establish a halted controller, parked worker or full event ring. Samples
are five seconds apart and do not independently date the owner's replug.

Inspected: a HID STALL/transaction error/babble marks the whole device retiring.
`usb_hid_collect` then takes the early not-present path before its diagnostic
loss branches, so this capture can omit the failed stream completion even though
it was retained. The device subsequently reaches Disable Slot. Root/hub change
retirement also exists, so the exact G502 trigger remains unproven. With ASUS
already retired, loss of G502 can account for both mice stopping without a
controller failure. Task 1 also changed keyboard receive length from endpoint
packet capacity to exactly eight bytes; overlength keyboard input is a candidate,
not a measured completion or identified packet format.

The accepted runtime policy explicitly retires the whole composite binding on
malformed input/transport loss; dropping reports or switching protocol at runtime
would change that policy. No such change is implemented. SET_PROTOCOL currently
has completion confirmation but no GET_PROTOCOL readback. HID 1.11 §§7.2.5–6
and Appendix G define mode readback for boot interfaces; readback cannot by itself
prove that old packets are absent from a device FIFO.
[USB-IF HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf).

That analysis preceded the owner's correction below. No hardware trigger was
claimed proven from the incomplete completion diagnostic; if another capture is
needed, it must include the not-present loss path's retained stream completion,
controller/slot/port identities and root-port state. The userland #214 refresh below retains only its scroll commit above current
main, dropping the stacked math commits.

### Owner correction accepted 2026-10-11

Daybreak's independent inspection agrees with software receiver retirement,
with exact-eight-byte keyboard TDs a likely babble trigger. The owner's four
accepted corrections are implemented: endpoint-packet-sized receives (also large
enough for admitted layouts); retained completion diagnostics on the early
unavailable path; observational GET_PROTOCOL after mouse SET_PROTOCOL(report);
and malformed-report dropping/counting, with whole-binding retirement at 16
consecutive malformed reports on one interface or transport failure. Valid
input resets only its own interface's streak. Drop diagnostics are rate-limited
to five seconds plus retirement; held state survives a dropped report. Initial
malformed GET_REPORT snapshots are skipped without altering other snapshots.
No new default klog output or automatic fallback from protocol mismatch/ID 0.

Userland #214 is published at `c075ae0d55487456736a95dbece80c5f854e39ec`,
rebased directly onto main `ee9ccae`; its only ahead commit is the scroll
integration. The earlier correction checks below used `f18f07b` on `dca986b`. The stacked rint/FP_ILOGB commits are dropped; ports #99 remains
`28b5a3bc`. Dependencies merge before the parent. No compiler rebuild.

Qualification of the correction at parent `7049e9a2` / userland `f18f07b`:
probe and default `make -j16 image` builds pass with the existing builder.
The first QEMU ISO attempt exits 139 during Limine loading, before kernel output,
matching the documented host AHCI CD-ROM problem. Qualification instead boots a
read-only 192 MiB FAT disk on VirtIO-blk (same source-built kernel/initrd), QEMU
10.2.2 Q35/nested KVM/host CPU, four CPUs/512 MiB, 1280x800 boot framebuffer.
The archive's Development space is authored with `multiplexer = true`.

With `usb.hid.describe=1` alone, two USB2 hubs, USB keyboard and two mice:
GET_PROTOCOL returns 1 for each report mouse. USB Super+R chords, typing, second
mouse motion and low/full-speed leaf replug pass. A 15-second interactive QMP
wheel flood sends 12,044 down, 11,993 up and 11,674 alternating ±127 commands.
GDB after replug finds running=true, failed=false, no pending command, 2,415
events and empty interrupt queues; keyboard receives eight bytes, QEMU mice
four (matching their packet capacities). The default-options boot shows both
diagnostic flags false, no diagnostic lines, and healthy USB. Mux wheel browsing
enters scrollback and returns to live at the newest endpoint; USB typing then
prints `defaultalive`. Local captures/GDB/serial remain in
`build/receiver-fix-qemu/`; all task-owned QEMU/debugger jobs are stopped.

The existing manual host caller against the actual parser accepts both captured
native descriptor layouts, rejects ID-0 four-byte and out-of-range packets
without altering button snapshots, and accepts a subsequent valid report. No
new framework or kernel fault injection. Per-interface streak reset, the
sixteenth-malformed retirement and initial-snapshot paths were source-reviewed;
no injected malformed-stream runtime result is claimed. Stock QEMU has no
composite Logitech/ASUS model, native 1 kHz mouse cadence or Pan coverage.
These checks do not establish that native babble was the trigger or that either
receiver's intermittent protocol behavior is fixed.

After main's address-parking merge, parent `130336f5` / userland `c075ae0`
retain the identical USB correction. Both default/probe image rebuilds pass;
a short fresh VirtIO-disk boot with both diagnostic options confirms USB Super+R,
`echo currentalive`, mouse readback=1, and low/full-speed root attachment/replug
(`usb-mouse,usb_version=1`). GDB finds healthy controller, no command pending,
empty keyboard/mouse queues, 169 events. The attempted default QEMU USB2
high-speed root replug is refused under the existing post-boot low/full-speed
scope; PORTSC and generation advance, so it is not a controller event stall.
All owned processes are stopped. Native checks must record actual link speed:
high-speed reattachment remains outside the accepted post-boot profile.

Native qualification remains pending. Luna should rebuild the submitted parent
head with the same `USB_HID_DESCRIBE=1 LOG_LEVEL=info` and boot options
`usb.hid.describe=1 usb.xhci.diagnose=1 log.udp=1`. Confirm ASUS works from its
first input, capture its GET_PROTOCOL result and any drop count, and try the
G502's Super+R mapped button, ordinary motion, wheel/free-spin and other buttons.
Then verify both devices still respond and replug a receiver. Retain failed
completion/endpoint/requested/residual metadata if a device retires. Do not infer
controller failure from loss of both receivers. PS/2 and remote-shell checks
remain the control. No raw key payload or new device-firmware/polling settings.

### Native fa67a519 retest and length-tolerance follow-up

Owner-run ThinkPad, 2026-10-11 about 20:45, same diagnostic options and
`USB_HID_DESCRIBE=1` build. The full masked 257-line log remains outside the repo:
`/shared/present/batch2/pointer716/native-retest-fa67a519.log`.

**G502/Lightspeed 046d:c53a passes:** owner confirms motion, buttons, wheel/free-spin,
Cmd+R mapped button and replug. Pan is not qualified: tilt is mapped on-board to
back/forward. The log establishes twelve-byte keyboard packets received in boot
protocol and dropped/counted without fatal transport/binding loss. It does not
prove every mapped chord used those packets (the owner reports the chord works).
Mouse TD length is 32, report layout 9, and protocol readback is 1.

**ASUS 1ea7:0066 still fails:** protocol readback is 1; four-byte ID-0 packets are
unknown, and four-byte ID-1 packets differ from the descriptor's three-byte
Consumer Input layout. The log records retirement at sixteen consecutive
malformed packets. It does not contain raw Input bytes to distinguish boot-format
mouse data from another encoding. Native report motion/wheel once worked on
`7491e419`; boot-to-boot variation remains unresolved. The owner additionally
reports that plain main's boot-protocol path works for ASUS motion/buttons
without wheel. This supports investigating boot-format packets after report-mode
readback; it does not establish their encoding without the raw capture.

The owner authorizes known mouse Input length tolerance: ignore trailers,
zero-fill a short payload, preserve declared ID/field-range checks and exact
Feature lengths. Missing/unknown IDs remain failures. Padding an ID-1 Consumer
report does not make it pointer data. Boot keyboard decoding stays eight bytes.
Under either diagnostic option, the first length mismatch per layout is recorded;
the first four interrupt packets per admission on a report-protocol Mouse interface
log ID/length and
at most sixteen actual bytes, with no keyboard payload. Declared Keyboard/Keypad
collections or keyboard Input usages on a mixed Mouse interface suppress raw
bytes there as well. No boot-format fallback is implemented or inferred.

Correction checks (decoder `9afb06bc`, sampling restriction `ff884d31`, unchanged
dependency pins): default/probe image builds pass. The existing manual host caller
against the actual parser decodes extended G502/ASUS packets identically to the
complete admitted prefix, zero-fills two-byte and partial-field ID-2 inputs,
and accepts ASUS ID-1 length four as a Consumer layout. Unknown ID 0 and
out-of-range values still fail without changing button snapshots. Both captured
Mouse descriptors have keyboard_data=0, allowing the requested capture. These
are constructed host inputs against measured descriptors, not captured ASUS
payloads. Feature exactness and mixed-keyboard raw suppression were reviewed.
No new framework or kernel fault injection.

QEMU 10.2.2 Q35/nested KVM/host CPU, four CPUs/512 MiB, read-only VirtIO FAT
boot disk, USB keyboard plus full-speed mouse (`usb_version=1`), same current
kernel/initrd: default options have no diagnostic lines, USB `echo lengthalive`
works, wheel/motion and replug pass; GDB finds healthy controller, no pending
command, empty queues and 136 events. The final diagnostic boot with both options
prints `probealive` and readback=1, handles motion/wheel, and remains healthy at
99 events. Actual malformed wire logging and ASUS motion require the native
retest; QEMU was not made to produce bad packets. Evidence remains local in
`build/length-tolerance-qemu/`; all owned QEMU/debugger jobs are stopped.

Luna should rebuild the next submitted head with the same flags/options. Preserve
the G502 pass as a control; try ASUS ordinary motion, left/right buttons and
wheel. Retain `usb HID mouse sample` and `Input ID` lines alongside readback and
malformed counts. The samples must establish the wire encoding before any
separate boot-format fallback decision. Raw packet logs stay outside the repo;
no device settings, firmware or polling changes are requested. The bounded
samples include accepted packets, so padded ID-1 inputs remain visible alongside
unknown ID-0 inputs. They are the first four interrupt packets, excluding initial
control snapshots; move immediately after plugging ASUS in, then press/release a
button. Replug while moving if an idle-only sample consumes the budget.

### Native ASUS wire evidence and reserved-ID switch

Owner ThinkPad capture at `52e7f310`, 2026-10-11: masked 126-line log retained at
`/shared/present/batch2/pointer716/native-asus-52e7f310.log`. GET_PROTOCOL reads 1,
but four interrupt samples have reserved ID 0 and four-byte boot-mouse motion;
the owner reports button presses change the first byte to 1/2, explaining the
brief apparent motion when those bytes were interpreted as report IDs. Plain
main's boot path also works for ASUS motion/buttons without wheel. G502 continues
using nine-byte ID-2 report packets. Raw packet bytes stay outside the repo.

The owner accepts a generic reserved-ID switch, implemented and qualified by
the native result below: boot-subclass Mouse plus ID-bearing descriptor plus leading 0 selects
boot decoding for this attachment, including optional byte-3 wheel. No switch on
nonzero IDs, nonboot mice or no-ID descriptors. Malformed short boot packets keep
the existing retirement policy. Do not issue SET_PROTOCOL again: preserve the
observed wire format and the command-free input path; another control write could
change live reports. No quirk table, endpoint resizing or sibling retirement.

Retest ASUS motion, wheel and left/right clicks, plus G502 motion/free-spin,
buttons/chord and replug as the regression control. Use the same build and boot
options below; retain the single `action=reserved-ID-boot` diagnostic and protocol
readback. The compliant QEMU report mouse must remain in report mode.

Checks at decoder `d395ad0d`: diagnostic image build passes. QEMU 10.2.2,
Q35/nested KVM, host CPU, four CPUs/512 MiB, read-only VirtIO FAT boot disk,
USB keyboard/full-speed mouse: motion, wheel and `echo reportalive` work.
GDB confirms mouse.ids=false, report_protocol=true, reserved_id_boot=false,
malformed_reports=0; the controller is running, not failed, no pending command.
Zero-leading no-ID reports never switch. This QEMU descriptor has no report
IDs; positive reserved-ID switching is covered by the native result below.
All owned jobs stopped; no kernel fault injection or new test framework.

### Native task 1 closure

Owner ThinkPad retest at `e7a18849`, 2026-10-11 about 21:45, same diagnostic
build/options: ASUS 1ea7:0066 motion, clicks and wheel pass after the reserved-ID
switch; G502/Lightspeed motion/buttons/wheel/free-spin/Cmd+R/replug still pass.
Masked 141-line log stays outside the repo at
`/shared/present/batch2/pointer716/native-asus-switch-e7a18849.log`; it records one
ASUS `action=reserved-ID-boot`. ASUS back/forward side buttons do not work; the
owner accepts that limit and closes further ASUS work. No pan qualification:
G502 tilt is mapped on-board to back/forward. Keychron and high-resolution/
multiplier hardware remain unqualified in technical debt. Task 1 closure is
accepted; Synaptics task 2 starts only after #716 merges.

The retained diagnostic boot options are opt-in. Inspection of all added klog
calls confirms descriptor/layout output requires `usb.hid.describe=1`, controller
samples/quarantine detail require `usb.xhci.diagnose=1`, and HID readback/loss/
malformed/switch output requires either. Both options default false. Ordinary
attachment detail stays ktrace; no new default-boot klog output is introduced.

### One diagnostic retest through luna

The opt-in `usb.xhci.diagnose=1` option adds info-level observations only, with
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

## Task 2 before-code baseline

Pyxis `df7ec8ab4cecbeb5b466b9c0e91b4324e8476c60`, userland `95888f0b`, ports
`28b5a3bc`, fs `b427df29`, lwIP `a1aadb91`; branch `pointer/synaptics-absolute`.
No task 2 implementation changes. Existing LLVM 23.1.3 builder:
`make -j16 image PREBUILT="sdk userspace ports" POINTER_SYNTHETIC=1 DISPLAY_CURSOR_PROBE=1 USB_HID_DESCRIBE=0 LOG_LEVEL=info`.
SDK/apps/ports are verified task-1 bundles with these exact dependency pins;
SDK metadata records `5d8a1a60` plus then-uncommitted documentation. There is no
code/header/dependency difference to this baseline; matching ABI/content checks
pass. The kernel is freshly source-built at `df7ec8ab`.

Kernel SHA256 `a321c504ddaf793de6035e47b74134283bc904d69cb06b174a3fe6ea57b6ca96`;
initrd `4fd03498dc30815cd9550e004edfe29f67927af8e54422adf3fddcf4bc2f42f3`.
QEMU 10.2.2 Q35/nested KVM, host CPU, four CPUs/512 MiB, UTC RTC, virtio RNG,
1280x800 boot software framebuffer; read-only 192 MiB FAT VirtIO boot disk,
PS/2 plus USB keyboard/full-speed mouse through two boot-present USB2 hubs.
Both boots use identical kernel/initrd/config and fresh OVMF variables.
`display.cursor.probe=1 pointer.synthetic=schedule`, no HID/xHCI dump options.
Existing schedule: settle 10 s, idle/motion/idle/motion 30 s each; Caelum shown,
no physical input during costs. These are interval deltas from named boundaries:

| Run | Window | Frames | Compose total, ms | Copy total, ms |
| --- | --- | ---: | ---: | ---: |
| 1 | Idle 1 | 16 | 6.96 | 6.30 |
| 1 | Motion 1 | 1777 | 751.50 | 637.39 |
| 1 | Idle 2 | 16 | 5.32 | 12.53 |
| 1 | Motion 2 | 1800 | 751.08 | 670.27 |
| 2 | Idle 1 | 15 | 7.95 | 5.01 |
| 2 | Motion 1 | 1789 | 839.48 | 613.62 |
| 2 | Idle 2 | 16 | 4.80 | 4.97 |
| 2 | Motion 2 | 1800 | 670.53 | 576.30 |

The existing host `/proc` CPU method uses five-second BSP vCPU thread samples,
as fractions of one host CPU. Run 1 complete interior motion samples: 22.2–22.8%
(three); idle: 17.8–18.4% (two). Run 2 retained complete samples: motion 22.0%,
idle 17.8% (one each); boundary-crossing samples are excluded. Small sample counts
and nested-host scheduling limit interpretation; this is aggregate BSP/KVM work,
not isolated driver/filter time. Presenter elapsed totals include preemption.

After the schedule, GDB confirms PS/2 four-byte wheel packets, empty raw queue,
no input-loss/overflow/resynchronization counts, mouse-class source;
typing timestamp initially zero. Run 2 explicitly selects QEMU's PS/2 input
handler for relative motion/wheel. Both probe options are true, both USB dump
options false. Raw evidence stays local in `build/synaptics-baseline/`; owned
QEMU/debugger/build processes are stopped. No new measurement infrastructure or
kernel fault injection. Repeat matched, interleaved A/B after implementation.

This measures the shared source/presenter path. Synthetic motion bypasses
Synaptics decoding and contact policy; QEMU supplies no Synaptics pressure,
width, contact or TrackPoint/clickpad evidence. Native identity/capabilities,
coordinate geometry and contact distributions remain unknown. Owner/luna
qualification must measure those and repeat the native baseline with matched
images/configuration; this QEMU record is not palm-rejection qualification.

## Task 2 matched QEMU implementation checks

Final code B is `3709cc39d007fe26ef70fb5f18b1ff6ab6b1c03c`, kernel SHA256
`d773727c21594e80fc01d7039197d8a013931b2e923095a2a2ea4f979814d2d7`.
Control A uses the sealed baseline above. Both use exactly the same initrd
`4fd03498dc30815cd9550e004edfe29f67927af8e54422adf3fddcf4bc2f42f3`,
dependency pins, probe/synthetic flags, QEMU devices/CPU/accelerator and fresh
OVMF variables. No physical input or local build overlaps retained cost windows.
Both Synaptics options are off during costs, so these measure relative/shared
pipeline coexistence, not absolute packet decoding or palm filtering. The final
code avoids contact-diagnostic timing reads entirely on the relative path.

Retained interleaved A3/B3/A4/B4 samples below follow the two before-code boots.
Earlier exploratory candidate boots are superseded by the diagnostic-clock
gating correction; their raw records remain local. Named boundary deltas:

| Run | Window | Frames | Compose total, ms | Copy total, ms |
| --- | --- | ---: | ---: | ---: |
| A3 | Idle 1 | 16 | 5.62 | 5.31 |
| A3 | Motion 1 | 1799 | 749.56 | 628.63 |
| A3 | Idle 2 | 16 | 6.10 | 5.53 |
| A3 | Motion 2 | 1774 | 1164.46 | 1059.65 |
| B3 | Idle 1 | 16 | 7.24 | 5.62 |
| B3 | Motion 1 | 1798 | 926.04 | 755.81 |
| B3 | Idle 2 | 16 | 10.78 | 9.21 |
| B3 | Motion 2 | 1685 | 1023.51 | 891.52 |
| A4 | Idle 1 | 15 | 5.69 | 5.29 |
| A4 | Motion 1 | 1775 | 1080.79 | 990.16 |
| A4 | Idle 2 | 17 | 5.38 | 5.82 |
| A4 | Motion 2 | 1800 | 659.45 | 587.33 |
| B4 | Idle 1 | 15 | 4.75 | 5.15 |
| B4 | Motion 1 | 1779 | 606.10 | 524.86 |
| B4 | Idle 2 | 16 | 4.49 | 5.00 |
| B4 | Motion 2 | 1800 | 624.19 | 539.51 |

Existing `/proc` five-second BSP vCPU host-thread samples, as fractions of one
host CPU; each cell has ten complete interior samples, crossing boundaries and
post-schedule samples excluded:

| Run | Idle median (range), % | Motion median (range), % |
| --- | ---: | ---: |
| A3 | 18.3 (17.8–19.2) | 24.3 (22.0–43.6) |
| B3 | 25.3 (17.8–32.4) | 25.6 (22.4–32.4) |
| A4 | 16.7 (14.6–17.4) | 20.5 (19.2–33.0) |
| B4 | 14.5 (13.8–15.4) | 18.9 (18.0–19.8) |

Idle skipping remains 15–17 frames per 30 seconds; motion is usually near full
rate. B3's second motion window is slower (1685 frames), while B4 is faster than
both controls in elapsed compose/copy cost. Controls also vary substantially.
No isolated driver/filter cost or causal regression/improvement is established:
host scheduling/load is not controlled, elapsed totals include preemption and
BSP CPU includes KVM and all guest BSP work. The reason for individual slow
windows is unproven. This is nested-VM evidence, not a native performance claim.
GDB after each retained run reports the ordinary four-byte PS/2 path, empty raw
queue and zero loss/resynchronization/overflow counts.

The probe and ordinary flag-off images build using verified unchanged
SDK/apps/ports bundles; the public ABI and dependencies do not change. On an
explicit opt-in boot, QEMU query 00 returns `00 00 64`, lacks the Synaptics
signature, then reset restores device ID 3/four-byte wheel input. GDB confirms
both options true, relative mouse-class source, no phantom touchpad/guest source,
two physical pointer sources and zero queue loss. A hardware breakpoint catches
raw vertical wheel `+1`, natural policy, scale 120/1 at the coordinator. Physical
PS/2 motion changes position by exactly the injected right/down delta; left
holds/release work. USB motion works alongside it, and releasing USB left leaves
an independently held PS/2 left active. USB keyboard runs `echo ok`; after USB
keyboard removal the shell reports its existing input-loss diagnostic, then
PS/2 typing completes `echo ps2` with a fresh Enter. Typing timestamps advance;
keyboard and mouse setup remain healthy.

The ordinary image uses kernel SHA256
`3358493e1e9e0af3c5bdf7845f4d3ff2e45bfc234b7ca87ef458fb601ed37a27`
and the same initrd. GDB confirms both Synaptics options/probe disabled, PS/2
available with four-byte packets; a held key resides in the live USB source while
the PS/2 key remains up, proving USB delivery. Its boot has no
`synaptics:`/`synaptics-contact:` output. No default is enabled by this PR.

An attempted targeted-keyboard QMP command aborted host QEMU after all B4 cost
windows had finished (`qemu-fixed-text-console.device` property missing).
The opt-in functional boot above repeats the checks through the monitor, with
the keyboard removal proving PS/2 delivery; no pass is claimed for that targeted
command. QEMU cannot exercise recognized Synaptics geometry-failure recovery,
absolute-W count/pressure/width, palms, physical clickpad or guest routing.
Those branches are inspected and await the native pass below. Raw records stay
in local `build/synaptics-baseline/`, `build/synaptics-after/` and
`build/synaptics-functional/`; no raw captures or new test framework are committed.

## Task 2 opt-in implementation and native run sheet

Owner decisions 1/2 accepted and 3 changed/accepted 2026-10-11 on #739. The
geometry exception preserves relative touch motion; it does not qualify palms
or two-finger scrolling. Code uses queried bounds/resolution or the accepted
narrow owner-measured T14 profile. The first native boot recovered to relative
mode; the contact pass remains pending. See the
[accepted calibration](../../../wip/pointer-input-quality.md#t14-geometry-correction--accepted-2026-10-13).
QEMU cannot provide positive absolute-W/contact/TrackPoint evidence.

The independent arch implementation follows the
[manufacturer Rev. B guide](https://studylib.net/doc/18112502/synaptics-ps-2-touchpad-interfacing-guide)
§§3.2/4.2–4.4/5.1.5. Mode bit 2 means DisGest in relative W=0 and EW in W=1;
absolute mode `0xc1` does not enable EW or four-byte guest packets. Reporting
stays disabled for mode readback and the §4.1 `EB` master-packet proof. The
firmware extension/query/button facts are checked against
[Linux v6.17 synaptics.c](https://github.com/torvalds/linux/blob/v6.17/drivers/input/mouse/synaptics.c)
and [its capability definitions](https://github.com/torvalds/linux/blob/v6.17/drivers/input/mouse/synaptics.h),
without incorporating GPL implementation. Advertised firmware 8.1 minimum
query and 8.1/8.2 omitted extended-button snapshots are narrow protocol facts,
not a guessed ThinkPad identity. Unhandled button layouts recover to relative
instead of silently losing switches.

Luna stages the owner-run image; do not stage or boot the laptop from this task.
Build the published `pointer/t14-calibrated-geometry` correction head with the
existing compiler container; #739 alone lacks this calibration:

```sh
make -j8 image LOG_LEVEL=info LOG_UDP=1 DISPLAY_FLIP=1 \
  DISPLAY_FLIP_METRICS=1 DISPLAY_TIMING=off DISPLAY_CURSOR_PROBE=1 \
  POINTER_SYNTHETIC=0 USB_HID_DESCRIBE=0
```

Duplicate the known flip-on PXE entry as **Synaptics absolute qualification**;
keep its existing boot/initrd paths and options, then append
`ps2.synaptics=1 ps2.synaptics.diagnose=1`. There is no new Make flag for these
boot-only options. Keep a relative control entry with `ps2.synaptics=0` and no
Synaptics diagnosis. Do not run `pointer.synthetic=schedule` during contact
testing. On AC, built-in panel and normal Development terminal, the owner boots
the opt-in entry and follows the named windows from the first
`synaptics-contact: window=settle` line. Expect raw resolution `0,54`, effective `45,54`,
`source=owner-measured-T14-102x70`, geometry 1 and qualified basic W. Pass-through
should be 1 and extended stick buttons 0. If admission fails,
record the query/fallback result; relative recovery is not a palm/scroll pass.

| Elapsed, s | Window | Owner actions |
| --- | --- | --- |
| 0–10 | settle | Lift contacts and leave controls idle. |
| 10–30 | normal-finger | First slide one finger edge-to-edge on X/Y (left/right, top/bottom), then centrally/diagonally; lift/reapply; confirm taps do not click. |
| 30–50 | edge-entry | Start at each edge, move parallel, then deliberately inward; check exclusion and entry without a jump. |
| 50–70 | resting-palms | Rest/move a palm, then reduce width/pressure without lifting; check rejection persists until full lift. |
| 70–90 | typing | Type/repeat with an existing contact; add a contact while typing; check quiet-interval rebaseline versus required lift. |
| 90–110 | finger-transitions-physical-drag | Add/remove second/third fingers, then hold/release a physical press during a drag; check fresh baselines and primary click. |
| 110–130 | TrackPoint-with-palm | Rest a palm while moving the stick and using each physical L/M/R button; check touch rejection does not affect them. |
| 130–150 | two-finger-scroll | Stable vertical/horizontal/diagonal strokes, small strokes, reversals and lifts; check natural direction and no residual scroll. |

After `done`, check modifier-only input, physical clicks during palm/typing
rejection, independent held switches, local selection and mux scrolling, and
fresh-click lock/unlock/relock in Quake. Repeat mouse/touchpad direction checks
under separately authored natural/traditional machine settings (next boot),
then repeat core finger/palm/typing/drag/stick/scroll checks with diagnosis off.
The owner's full native pass precedes the accepted default-on change in this
same task. Preserve a relative-mode control boot if geometry is unavailable.

Initial authored tuning is documented in [mouse input](../../../devices/mouse.md#synaptics-opt-in-absolute-input).
The first edge-to-edge slide checks observed X/Y against queried 1266..5678 /
1162..4694; retain the normal-finger summaries, including contacts rejected by
edge policy. Observed contact coordinates are collected before filtering. The
98.04x65.41 mm sensing area and its border allowance remain estimates until this
pass; do not widen bounds from isolated outliers. Five-second summaries contain
valid Z/W distributions, observed X/Y,
finger/rejection counts and packet gaps, with seven named twenty-second windows
after ten seconds settle. `transport-resets` includes coordinator queue overflow;
baseline-only samples count neither accepted nor rejected. Gaps are delivery
spacing, **not filter CPU time**. Existing presenter/probe counters measure
aggregate elapsed compose/copy cost including preemption. No typed text,
key identities or serial-number queries/logs are used; raw contact packets
are not dumped. The diagnostic schedule stops at 150 seconds. New `synaptics-transport:` lines
contain cumulative touch/guest packet, overflow/framing/unsupported/reset counts.
Guest count should increase during actual stick use; capability alone does not
prove delivery. No guest protocol/initialization changes are made in this fix.

Keep the masked UDP log and short owner observations at
`/shared/present/batch2/pointer-task2-native/`. Useful extraction:

```sh
grep -E 'synaptics:|synaptics-contact:|synaptics-transport:|mouse:|display-cursor-probe:|display-flip-metrics:' masked.log
```

Record image hashes, head/pins, command line, actual window actions and any
fallback. For a later hands-free pipeline A/B, use the baseline `df7ec8ab` and
#739 kernels with the same matching initrd, `POINTER_SYNTHETIC=1
DISPLAY_CURSOR_PROBE=1` and `pointer.synthetic=schedule`; repeat interleaved
boots. This bypasses contact filtering and cannot replace the hands-on pass.

## T14 calibrated admission correction — 2026-10-13

Owner native baseline: main `d3bc9b5a`, T14 Gen 1 AMD, explicit
`ps2.synaptics=1 ps2.synaptics.diagnose=1`. The session stopped at step 1: valid
resolution query `00 80 36` supplied X0/Y54, making geometry unavailable, and
relative recovery with tap-off succeeded. This is a failed absolute admission,
not a palm/scroll result. Source log: horse's
`~/shared/batch-2026-10-09/caelum-4.log`, starting at line 19128; raw logs are not
committed. The owner measured visible width/height three times: 102x70 mm.
The [accepted profile](../../../wip/pointer-input-quality.md#t14-geometry-correction--accepted-2026-10-13)
records the complete non-serial fingerprint, X45/Y54 scaling and inferred border
allowance. Profile matching, calibration ordering and unchanged guest decoding
were source-reviewed; positive native admission/contact evidence is pending.

Local comparison: current main `0e95af3a82afc2ea8f009c046fa96cb6c08ba1bc`
versus implementation `542bae7197cd213ae68a53f92c6ee3e8b182fe0e` (compiled from
its identical implementation diff before commit). Both use successful main CI
run 2189's SDK, userland and ports bundles; the verifier admits their unchanged
inputs. Dependency pins: userland `c472de7d`, ports `f93becb9`, fs `414be0ac`,
lwIP `a1aadb91`. Builder: `pyxis-llvm23.1.3-4c23a20`.

```sh
make -j8 image PREBUILT="sdk userspace ports" LOG_LEVEL=info
```

The ordinary source image build passed in the existing container (SELinux
labeling disabled for the workspace mount; pure-Python host kconfiglib/genconfig
made available to its Python). One pre-existing lwIP shadow warning remains;
changed sources introduce no warning. Main artifact ELF SHA256:
`699b022141c18292f862148a8673bd5eccc4349b332810b4f72155a9b59bc07b`.
Compiled candidate ELF SHA256:
`6e544c3bef573afdb2fb50ff88b2c3cd115fc7bb7d9ce5d0ce82e3ab9aa674a7`.
Both retained boots use the same main initrd SHA256:
`d50e05d5c89c96d246ff4a8002c5baa0f5c215cfa50b80709eeb43674739ce81`.
These are local qualification artifacts, not hashes of luna's later build.

Interactive QEMU 10.2.2, Q35/nested KVM, host CPU, four vCPUs/512 MiB, fresh OVMF
variables, standard VGA/software cursor at 1280x800, modern VirtIO block boot
volume and RNG; no NIC/USB. Both opt-in boots use the same cmdline, firmware,
initrd and boot disk contents except the kernel. Neither recognizes the emulated
PS/2 device: query 00 is `00 00 64`, and reset recovers ID3/four-byte wheel mode.
Read-only GDB inspection confirms both options true, available relative input,
no absolute/guest profile and zero input-loss/overflow counts; candidate
calibration and diagnostic scheduler are inactive. Monitor motion `50,-10`
moves the pointer from `640,400` to `690,390`; left press/release changes held
buttons 1/0. A wheel report was injected on the recovered four-byte path. PS/2
keyboard runs `echo ok` in both shells. No performance measurement is claimed.
An additional candidate boot omitting both options reaches the shell; GDB
confirms both false, relative four-byte mode and no diagnostic scheduling, and
serial contains no Synaptics lines. All QEMU/GDB jobs are stopped.

QEMU cannot reproduce this fingerprint, validate the physical border allowance,
exercise actual basic-W palms/scroll or prove guest delivery. The owner/luna
re-run must show raw/effective geometry, edge-to-edge extents, actual TrackPoint
packet increments and switches, then the contact phases above. The shared short
sheet is `/shared/present/batch2/pointer-task2-native/RUNSHEET.md`. No default
activation, guest commands, guest decode changes or dependency repoints are made.

### Owner calibrated re-run, 2026-10-13

Owner on the ThinkPad, correction `b4b4f594`: absolute admission; edge-to-edge
observed X 1266..5678 and Y 1162..4686; palm rejection "solid"; scroll works;
TrackPoint guest packets increase 2..384 during its window, no overflows or
malformed packets, zero transport resets. Owner also booted Synaptics-only and
the standard entry. Log stays on horse at
`~/shared/batch-2026-10-09/caelum-4.log`, from line 19278. No raw log committed.

Qualification remains open: the owner reports overlong typing suppression and
basic-W's one-position limitations for separated-finger scroll and clickpad
trampoline drag. The milestone records accepted interaction requirements, the
confirmed until-lift typing latch, image-sensor AGM evidence and three pending
defaults. No follow-up interaction implementation or native pass is claimed.

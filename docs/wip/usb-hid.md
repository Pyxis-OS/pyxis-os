# USB HID boot keyboards and mice

Status: **QEMU and native input qualified; ready to merge #654**.
Implemented behavior and limits live in [USB boot input](../devices/usb-hid.md).
No public input ABI or task-owned dependency pin changes are needed.

## Accepted contract

Owner accepted 2026-10-10: one shared keyboard path with per-device holds,
USB repeat at 500 ms then 33 ms and unchanged PS/2 typematic; the existing
[pointer source rule](../interfaces/pointer.md#input-source-coordination), boot
keyboard/mouse composites and only established wheel layouts; boot paths through
five USB 2 hubs, runtime low/full-speed leaves on roots or boot-present chains,
32 retained attachment records per controller and possible subtree quarantine.
[Surviving a USB switch flip](../technical-debt.md#usb-switch-subtree-removal)
without quarantine is a much-later follow-up.

## Delivery

1. [x] Capture unchanged-source input/CPU baseline before code.
2. [x] Extend private endpoint handles, bounded leaf attachment/retirement and
   boot-present USB 2 hub notifications, preserving storage/HCI progress.
3. [x] Bind boot HID and integrate shared keyboard/pointer state, repeat and loss.
4. [x] Qualify QEMU, repeat matched measurements and prepare native batch below.
5. [x] Qualify native keyboard, shared keyboard holds, keypad and composite mouse
   input/hotplug on the ThinkPad.

## Native qualification — 2026-10-10

Owner ThinkPad results (2026-10-10, luna build `2d678543`, default entry): direct
USB-C Keychron Q6 Pro `3434:0660` on `07:00.3` passed letters, modifiers,
extended keys, repeat, cross-keyboard holds and hotplug. Num Lock/keypad text
exposed the pre-existing shared-layout gap; `a38f6689` adds digits/operators and
existing navigation aliases. Full SDK/applications/ports/image rebuild passed;
QEMU USB input produced `1234567890.+-*/` with Num Lock and Home/Right/Delete/End
editing without it. GDB confirmed the Home bytes `1b 5b 48`.

ASUS `1ea7:0066` on `07:00.4`, full speed, advertises keyboard `03/01/01` and
mouse `03/01/02`, one endpoint each, but supplied no mouse input. The owner's
`3e684a0d` probe confirmed both interfaces configured successfully, then keyboard
GET_REPORT stalled on boot and replug, rejecting the whole composite. Owner
accepted the correction: either interface's initial GET_REPORT STALL starts
empty after EP0 recovery and does not reject its sibling. Other errors remain
fatal; mouse SET_IDLE retains its optional-STALL rule. The throwaway info probe
is not part of this PR and its branch was deleted after qualification.

Owner recheck on luna's ThinkPad build `79e9d11d`: ASUS mouse motion, buttons
and replug passed; Quake was played with the USB mouse. Keychron Num Lock,
numpad digits, arrows with Num Lock off and the home cluster passed. Together
with the earlier keyboard/shared-hold/hotplug checks, this completes the native
input task. These are owner-reported native results, not agent measurements.
The recorded devices were full-speed root attachments; native dock/switch hub
chains, low-speed devices and Bluetooth/storage coexistence were not reported
in this batch. LED output and the ASUS wheel remain outside qualification.

The correction's ordinary kernel/image build passed with current SDK/app/ports
bundles. A matched four-CPU QEMU root keyboard/mouse boot still bound both;
typing ran `hostname`, and `mousetest` acquired relative lock and a left-button
press. GDB confirmed live sources, idle EP0 and a healthy controller. This QEMU
fixture supplies successful GET_REPORT; the owner's dongle boot/replug recheck
above qualifies the fallback. After merging main, the full kernel/SDK/apps/ports/
image rebuild and the same QEMU checks passed at `79e9d11d`, with userland
`89c520b6` and ports `9397093c`. Exact-head CI workflow #1667 passed all four jobs.

## QEMU qualification — 2026-10-10

Ordinary LLVM 23 builder `49e2c1a` kernel/image builds passed, reusing verified
SDK/application/ports bundles: `make -j16 image PREBUILT="sdk userspace ports"`.
The before-code image `b0e2d1c6` changes only docs from main `dc91a4c6` and uses
userland `8cbd9f87`. Main advanced during work, so the final matched pair is main
`df0885f2` and implementation `0a891e2a`, both using userland `5aede1c2`, ports
`19fb10b0`, fs `b427df29` and lwIP `a1aadb91`, with identical SDK contents and ABI.
No compiler rebuild or new test/boot automation was added. Raw measurements stay
local; the tables retain samples, revisions and configuration.

QEMU 10.2.2, Q35, nested KVM, `-cpu host`, four CPUs (one socket/four cores/one
thread), 2 GiB, fresh raw OVMF variables, standard VGA 1280x800, UTC RTC, ISO boot,
modern VirtIO RNG/network and user networking. Common device arguments:

```sh
-device qemu-xhci,id=xhci \
-device usb-hub,id=hub1,bus=xhci.0,port=1 \
-device usb-hub,id=hub2,bus=xhci.0,port=1.1 \
-device usb-kbd,id=hidkbd,bus=xhci.0,port=1.1.1 \
-device usb-mouse,id=hidmouse,bus=xhci.0,port=1.1.2
```

Manual monitor input and matching-ELF GDB inspection established:

- Typing, modifiers, shared-space navigation, USB `KEY_REPEAT`, motion, buttons
  and the descriptor-confirmed wheel. `mousetest` acquired input and relative lock.
- Ordinary hub-leaf removal/reattachment and post-boot root full-speed attachment
  (`device_add usb-kbd,...,usb_version=1`); endpoint/slot retirement left the
  controller running. Runtime high-speed root attachment was refused.
- A held USB mouse's removal revoked lock and cleared buttons. Removing an
  attached buttonless mouse preserved the PS/2 survivor's same lock object.
- Pre-bind RightCtrl+Shift+V became quarantined initial holds, with zero accepted
  keys/events; release then fresh chord reached clipboard as `KEY_V` PRESS with
  Control+Shift. Seven-key rollover reset input, and recovery supplied no new press.
- Five chained full-speed hubs at `3.1.1.1.1.1` delivered real typing. A high-speed
  root keyboard delivered input with endpoint interval exponent 6 from `bInterval=7`.
  The same controller completed a 128 KiB BOT media/GPT read probe on read-only
  USB storage. Removing a hub subtree quarantined it; PS/2 remained available.

The last checks also ran at `0a891e2a`; detailed initial-hold/rollover/lock checks
ran at `5db7669d`, before the unrelated main merge. At `b9e59d8a`, a USB-only
`-machine q35,i8042=off` boot ran `hostname` and `mousetest`, with both PS/2
availability flags false and exactly one live USB keyboard and mouse. This fixes
the stock shell starting before USB input enumeration; the normal PS/2 path and
steady-state paths measured above are unchanged. The owner's ASUS result
qualifies composite binding. Low-speed scheduling, real high-speed hub/TT routing
and HCI coexistence remain source-reviewed, not native or QEMU-device qualification.
QEMU supplies no composite or low-speed fixture here. Its host input dispatcher
cannot establish independent physical holds across multiple keyboards; the owner
qualified shared keyboard holds natively.

Five warmed five-second CPU samples per state, no debugger during CPU sampling.
Idle shows Caelum with hubs and HID devices present (unbound in controls). Active
selects Development and sends F1 (40 ms) plus one-count mouse motion at 10 Hz.
PS/2 active removes the USB leaves while retaining the hubs.
Values are median (range), percent of one host CPU:

| Workload / host thread | Before code | Matched main | Implementation |
| --- | --- | --- | --- |
| Idle BSP vCPU | 9.8 (9.6–10.0) | 11.0 (9.8–14.2) | 11.0 (10.2–11.6) |
| Idle QEMU main | 0.0 (0.0–0.0) | 0.0 (0.0–0.2) | 0.0 (0.0–0.0) |
| PS/2 active BSP vCPU | 9.0 (8.6–10.2) | 10.2 (9.6–11.6) | 10.6 (10.4–11.0) |
| PS/2 active QEMU main | 0.6 (0.4–0.6) | 0.6 (0.4–0.8) | 0.6 (0.4–0.8) |
| USB active BSP vCPU | unavailable | unavailable | 11.6 (11.4–12.8) |
| USB active QEMU main | unavailable | unavailable | 0.6 (0.6–1.0) |

Eight host-monotonic samples per device: timestamp before monitor `sendkey a 40`
or `mouse_move 1 0`, then GDB hardware breakpoint at `keyboard_route_event` or
`pointer_handle_input`. Milliseconds, median (range):

| Input | Before code PS/2 | Matched main PS/2 | Implementation PS/2 | Implementation USB |
| --- | --- | --- | --- | --- |
| Keyboard | 11.390 (4.242–19.275) | 12.087 (5.129–17.414) | 10.270 (3.233–15.938) | 9.483 (2.631–15.680) |
| Pointer | 10.903 (3.380–16.820) | 10.364 (3.780–17.734) | 7.900 (2.883–16.484) | 11.790 (1.765–17.731) |

Ranges overlap; these samples establish no latency improvement. vCPU thread time
includes guest work, KVM exits and some emulation, not pure guest BSP accounting;
QEMU main is reported separately. Timings include monitor/socket/debugger and
host scheduling costs, not uninstrumented or native latency. VMs/debuggers stopped.

## Further native coverage

Luna stages through the owner. Record image/ELF revision, controller, actual
keyboard/dongle descriptors, link speeds and dock/switch hub path; `lsusb -n`
shows the boot snapshot, not post-boot devices.

1. Boot the ThinkPad with the USB keyboard and ASUS 2.4 GHz mouse on the actual
   USB 2 dock/switch chain. Check letters/digits, both modifier sides, locks,
   extended keys, repeat, focus changes and fresh clipboard/navigation chords.
2. With USB and PS/2 together, hold the same key/button on two devices and release
   one; the survivor must stay held. Use `mousetest` for motion, three buttons,
   drawing and lock; unknown wheel layouts remain undecoded until established.
3. Unplug/replug individual low/full-speed leaves after boot, without removing
   their hubs. Check held-key removal, held-button lock revocation, buttonless
   survivor lock preservation. Successful initial snapshots quarantine observed
   attachment holds until release; GET_REPORT-stalling devices start empty.
4. Check input during existing USB storage/Bluetooth work, capturing ktrace for
   admission or loss. A switch flip that removes a hub subtree can still require
   reboot; do not count that accepted limit as ordinary leaf survival.

Steam Deck lizard mode requires actual boot interfaces; neither it nor later
Bluetooth HID is qualified by these USB checks. Record native behavior and costs
when extending this coverage. These checks do not block the qualified input task.

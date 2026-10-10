# USB HID boot keyboards and mice

Status: **baseline captured; implementation in progress**. Owner decisions accepted
2026-10-10. No public raw-USB interface or new input grant is added.

## Accepted contract

- USB and PS/2 keyboards share focus, grants, shortcuts, locks and the existing
  US text mapping. Per-device held state prevents one device releasing another's
  key. USB host repeat starts after 500 ms, then repeats every 33 ms; PS/2
  typematic stays unchanged. Repeat and unchanged reports create no activation.
  If attachment observes held keys/buttons, release them all before that source
  supplies new presses; queued pre-bind state cannot create activation. Keyboard
  rollover resets accepted input and quarantines recovered holds until release.
- Boot-subclass keyboard and mouse interfaces include composite dongles, with
  one keyboard and one mouse interface per device. Select boot protocol; decode
  the eight-byte keyboard and three-byte mouse reports. Wheel needs an established
  extension layout; unknown trailing bytes have no invented meaning. General
  report-protocol HID, media keys and keyboard LED updates are outside this task.
- The [pointer source rule](../interfaces/pointer.md#input-source-coordination)
  aggregates buttons and continuity suppression. Losing a button-holding source
  cancels drag, resets accepted input and revokes lock. Losing a buttonless source
  leaves a live survivor's input and lock unchanged; losing the last source revokes
  lock. No acceleration or public pointer ABI change.
- Boot admission covers low/full/high-speed leaves on roots or through up to
  five external USB 2 hubs, with existing transaction-translator routing. Runtime
  low/full-speed leaf attachment works on roots and boot-present supported hub
  chains. Runtime new hubs and USB 3 HID are outside this profile; a dock's USB 2
  companion path is eligible.
- Each controller has 32 HID attachment records, including initial, failed and
  retired admissions. Prepared endpoint/DMA resources stay separate from storage
  and Bluetooth and remain retained until reboot. Exhaustion refuses new devices.
  Ordinary HID-leaf removal needs a device-local stop/disable fence; uncertain
  ownership or hub-subtree removal may quarantine the controller until reboot.
  [USB switch survival](../technical-debt.md#usb-switch-subtree-removal) is later work.

The BSP controller worker owns USB commands, streams and DMA. A bounded progress
pass services HID while existing command/control/storage waits drain events.
Input delivery performs no allocation, USB command or sleep under input locks.
Attach/detach diagnostics use ktrace, with no new klog lines. The boot inventory
remains a snapshot; runtime attachment does not silently rewrite it.

## Delivery

1. [x] Capture unchanged-source input/CPU baseline; record matched configurations,
   revisions, repeated samples and debugger perturbation.
2. [ ] Extend private interrupt endpoints, bounded leaf attachment/retirement and
   boot-present hub monitoring without breaking Bluetooth or storage progress.
3. [ ] Bind boot HID and integrate shared keyboard/pointer state, repeat and loss.
4. [ ] Qualify QEMU roots, hubs/chains, coexistence and hotplug; repeat measurements
   and prepare the owner's native batch. Native results remain separate.

## Qualification

QEMU uses `qemu-xhci`, `usb-kbd`, `usb-mouse` and `usb-hub`, with PS/2 still present.
Inspect configuration/endpoint contexts, completion identities, report lengths and
source state with the matching ELF in GDB. Exercise typing, modifiers, repeat,
focus/clipboard freshness, pointer buttons/motion/wheel, drags/lock, leaf detach
and reattach, chained hubs and concurrent storage. Separate BSP work from host
QEMU cost; debugger-observed latency is not uninstrumented native latency.

The owner checks the ASUS dongle and a USB keyboard on the ThinkPad through the
actual dock/switch chain, including boot and leaf attachment after boot. Check
held-key/button removal, PS/2 coexistence, typing/repeat/shortcuts and pointer
motion/buttons/wheel. Removing the whole hub subtree can still disable USB until
reboot. Luna stages through the owner. Record actual descriptors before claiming
dongle wheel support. Steam Deck lizard mode is eligible only if its actual
interfaces expose these boot protocols; neither Deck nor Bluetooth HID is qualified
by QEMU USB checks.

Reference: [USB HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf),
especially §§7.2.4–7.2.6 and appendices B/C; QEMU 10.2.2's
[mouse report implementation](https://raw.githubusercontent.com/qemu/qemu/v10.2.2/hw/input/hid.c)
establishes its fourth-byte wheel extension. No upstream code is copied.

## Baseline — 2026-10-10

Source `b0e2d1c6` changes only documentation from main `dc91a4c6`; LLVM 23 builder
`49e2c1a`, kernel `make -j16 image PREBUILT="sdk userspace ports"`, unchanged
userland `8cbd9f87`, ports `19fb10b0`, fs `b427df29`, lwIP `a1aadb91`. Verified
local SDK/application/ports bundles have these pins and the matching public ABI.
QEMU 10.2.2: Q35, nested KVM, four host CPUs, 2 GiB, raw OVMF, standard VGA
1280×800, VirtIO RNG/network, `qemu-xhci`, two chained full-speed `usb-hub`s,
`usb-kbd` at `1.1.1` and `usb-mouse` at `1.1.2`. Enumeration completed; HID was
unbound, as expected. Five five-second idle samples on Caelum, no debugger:

| Host-thread CPU (% of one CPU) | Median | Range |
| --- | ---: | ---: |
| BSP vCPU | 9.8 | 9.6–10.0 |
| QEMU main | 0.0 | 0.0–0.0 |

These `/proc` thread times include KVM execution/exits; they are not pure guest
accounting, and emulation can also run in a vCPU thread. After removing the two
unbound HID leaves and selecting Development, five five-second samples of PS/2
F1 (40 ms) plus relative mouse motion at 10 Hz gave BSP 9.0% (8.6–10.2%) and
QEMU main 0.6% (0.4–0.6%). The hubs remained attached.

Eight debugger-observed host injection-to-route samples per source used HMP
`sendkey a 40` / `mouse_move 1 0`, with conditional hardware breakpoints at
`keyboard_route_event` / `pointer_handle_input`: keyboard median 11.390 ms,
range 4.242–19.275 ms; pointer median 10.903 ms, range 3.380–16.820 ms. Socket,
debugger and host scheduling costs are included; this is not native or
uninstrumented latency. Raw samples stay local. Baseline VM/debugger stopped.

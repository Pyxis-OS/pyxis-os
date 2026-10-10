# USB HID boot keyboards and mice

Status: **implementation assigned; baseline first**. Owner decisions accepted
2026-10-10. No public raw-USB interface or new input grant is added.

## Accepted contract

- USB and PS/2 keyboards share focus, grants, shortcuts, locks and the existing
  US text mapping. Per-device held state prevents one device releasing another's
  key. USB host repeat starts after 500 ms, then repeats every 33 ms; PS/2
  typematic stays unchanged. Repeat and unchanged reports create no activation.
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

1. [ ] Capture unchanged-source input/CPU baseline; record matched configurations,
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

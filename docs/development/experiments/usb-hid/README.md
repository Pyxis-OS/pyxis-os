# USB HID boot input qualification

QEMU and native qualification of the [USB boot keyboard and mouse input](../../../devices/usb-hid.md)
task (#654), 2026-10-10. Behavior and limits are in that reference; the owner-reported
native results below are not agent measurements.

## QEMU

Builds used the ordinary LLVM 23 builder `49e2c1a` with verified SDK, application and
ports bundles (`make -j16 image PREBUILT="sdk userspace ports"`); no compiler rebuild
and no new test or boot automation. The before-code image `b0e2d1c6` differs from main
`dc91a4c6` only in docs. The final matched pair is main `df0885f2` and implementation
`0a891e2a`, both with userland `5aede1c2`, ports `19fb10b0`, fs `b427df29` and lwIP
`a1aadb91`, and identical SDK contents and ABI. Raw measurements stayed local.

QEMU 10.2.2, Q35, nested KVM, `-cpu host`, four CPUs, 2 GiB, fresh OVMF variables,
standard VGA 1280x800, UTC RTC, ISO boot, modern VirtIO RNG and network with user
networking. Common device arguments:

```sh
-device qemu-xhci,id=xhci \
-device usb-hub,id=hub1,bus=xhci.0,port=1 \
-device usb-hub,id=hub2,bus=xhci.0,port=1.1 \
-device usb-kbd,id=hidkbd,bus=xhci.0,port=1.1.1 \
-device usb-mouse,id=hidmouse,bus=xhci.0,port=1.1.2
```

Monitor input and matching-ELF GDB inspection established:

- Typing, modifiers, shared-space navigation, USB `KEY_REPEAT`, motion, buttons and
  the descriptor-confirmed wheel; `mousetest` acquired input and relative lock.
- Hub-leaf removal and reattachment, and a post-boot root full-speed attachment, left
  the controller running; a runtime high-speed root attachment was refused.
- Removing a held USB mouse revoked lock and cleared buttons; removing a buttonless
  one preserved the PS/2 survivor's lock.
- A pre-bind RightCtrl+Shift+V became quarantined initial holds with zero accepted
  events; release then a fresh chord reached the clipboard. Seven-key rollover reset
  input and recovery supplied no new press.
- Five chained full-speed hubs delivered real typing, a high-speed root keyboard
  delivered input with interval exponent 6 from `bInterval=7`, the same controller
  completed a 128 KiB BOT read probe, and removing a hub subtree quarantined it with
  PS/2 still available.
- A USB-only `-machine q35,i8042=off` boot ran `hostname` and `mousetest` with exactly
  one live USB keyboard and mouse.

Samples are five warmed five-second runs per state with no debugger attached (median
and range, percent of one host CPU), and eight host-monotonic latency samples per device
(timestamp before monitor `sendkey a 40` or `mouse_move 1 0`, then a GDB hardware
breakpoint at `keyboard_route_event` or `pointer_handle_input`):

| Workload / host thread | Before code | Matched main | Implementation |
| --- | --- | --- | --- |
| Idle BSP vCPU | 9.8 (9.6–10.0) | 11.0 (9.8–14.2) | 11.0 (10.2–11.6) |
| PS/2 active BSP vCPU | 9.0 (8.6–10.2) | 10.2 (9.6–11.6) | 10.6 (10.4–11.0) |
| PS/2 active QEMU main | 0.6 (0.4–0.6) | 0.6 (0.4–0.8) | 0.6 (0.4–0.8) |
| USB active BSP vCPU | unavailable | unavailable | 11.6 (11.4–12.8) |
| USB active QEMU main | unavailable | unavailable | 0.6 (0.6–1.0) |

| Input, ms | Before code PS/2 | Matched main PS/2 | Implementation PS/2 | Implementation USB |
| --- | --- | --- | --- | --- |
| Keyboard | 11.390 (4.242–19.275) | 12.087 (5.129–17.414) | 10.270 (3.233–15.938) | 9.483 (2.631–15.680) |
| Pointer | 10.903 (3.380–16.820) | 10.364 (3.780–17.734) | 7.900 (2.883–16.484) | 11.790 (1.765–17.731) |

Ranges overlap, so the samples show no latency change. vCPU thread time includes guest
work, KVM exits and some emulation, and the latency figures include monitor, socket,
debugger and host scheduling cost, not native latency.

## Native (owner, ThinkPad)

- **Keychron Q6 Pro** `3434:0660`, direct USB-C: letters, modifiers, extended keys,
  repeat, cross-keyboard holds and hotplug passed. Num Lock and keypad text exposed a
  missing shared-layout mapping (digits, operators and navigation aliases), added in
  `a38f6689` and rechecked at `79e9d11d`; QEMU produced `1234567890.+-*/` with Num Lock
  and Home/Right/Delete/End editing without it, and GDB confirmed Home as `1b 5b 48`.
- **ASUS 2.4 GHz dongle** `1ea7:0066`, full speed, keyboard `03/01/01` and mouse
  `03/01/02`, one endpoint each. At first no mouse input arrived: both interfaces
  configured, then keyboard GET_REPORT stalled on boot and replug and the whole
  composite was rejected. The owner accepted the correction that either interface's
  initial GET_REPORT STALL starts empty after EP0 recovery without rejecting its
  sibling, other errors staying fatal. At `79e9d11d` mouse motion, buttons and replug
  passed and Quake played with the USB mouse.
- Exact-head CI #1667 passed all four jobs; the full kernel, SDK, apps, ports and image
  rebuild passed at `79e9d11d` (userland `89c520b6`, ports `9397093c`).

## Limits

The native devices were full-speed root attachments. Native dock or switch hub chains,
low-speed devices, Bluetooth and storage coexistence, LED output and the ASUS wheel are
unqualified; low-speed scheduling and real high-speed hub and transaction-translator
routing are source-reviewed only. QEMU supplies no composite or low-speed fixture, and
its input dispatcher cannot establish independent physical holds across keyboards. See
[technical debt](../../../technical-debt.md#usb-hid-native-coverage).

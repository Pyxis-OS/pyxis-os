# Copilot space picker qualification

Implemented against main `a3efad31` (2026-10-18), with dependency pins unchanged.
The owner accepted the keyboard list, exact Shift+Super+F23 chord and always-on
binding on that date. The [keyboard reference](../devices/keyboard.md#space-picker)
is the contract. Vela's actual physical chord is still unmeasured.

An ordinary clean source image built with the existing
`pyxis-llvm23.1.3-abc00d0` compiler container, `make -j8 image PREBUILT=`, and
empty private-game-data overrides. No compiler-container change was needed.
QEMU 10.2.2 used Q35, `-cpu max`, 2 GiB, matching raw edk2 OVMF CODE/VARS, an
ISO and no disks or network. Four-CPU nested KVM qualified the default five-space
configuration. One-CPU TCG qualified a private 30-space configuration and a
320x128 Bochs display (`display.size=320x128`); these were functional checks,
not timing measurements. Two additional KVM boots stalled before a usable
kernel/debugger connection; the long-list/display checks used TCG instead.

QEMU's host F23 mapping cannot generate the needed guest codes. PS/2 checks
injected set-2 bytes through the 8042 output buffer (`D2` at port `64`, then the
byte at port `60`), allowing the guest IRQ and decoder to consume each byte.
USB checks used an xHCI keyboard and real modifier/F12 reports, with GDB
substituting usage `72` for `45` immediately before `keyboard_snapshot`.
Thus transport and guest decoding were exercised, but this is not physical
keyboard qualification. No such substitution or scan probe is in production.

Observed checks:

- Both modifier sides open the list; Control/Alt and bare F23 do not. Caps Lock
  does not suppress it. Trigger repeats and modifiers-first release do not
  toggle or commit. A second fresh chord and Escape cancel.
- Up/Down, Home/End, Enter and cancellation preserve the active space until
  commit. A 30-space list scrolls to its last row; a 320x128 display clips the
  panel without drawing outside it. GDB confirmed selected/active identities.
- A breakpoint on `keyboard_route_event` saw no confirmation Enter repeat or
  release; the next fresh Enter press/release routed normally.
- Mousetest held a real keyboard session and relative pointer lock. Opening
  the picker cleared the lock; dismissal left activation required and not
  ready. USB keyboard removal and a malformed PS/2 Pause sequence cancelled
  the picker through input reset.
- Ctrl+Alt+Delete replaced the picker with the power overlay, retaining modal
  keyboard focus. Source review also checked volume-popup takeover; neither
  handoff clears overlay focus between modal owners.

The owner-present stick packet contains production and separate diagnostic
kernels. Its filtered modifier/non-text scan capture will establish vela's
actual press/release order before native support is claimed.

# Mouse input

The PS/2 mouse on the 8042's auxiliary port delivers relative motion, wheel
counts and button state through
[`mouse_read_event()`](../../include/kernel/mouse.h). One BSP kernel task
consumes them; the call is nonblocking and preserves interrupt state. This is
QEMU's default mouse. On the ThinkPad, the Synaptics touchpad in its firmware
relative mode reports as a standard PS/2 mouse, and TrackPoint motion is
expected to arrive through the same stream. Synaptics absolute mode and USB HID
mice are not supported.

Events carry raw device counts without acceleration. Signs follow the display:
+dx is right, +dy is down and +wheel scrolls toward the user. The device's own
+Y is up, so the decoder negates it. `buttons` holds the left, right and middle
buttons after the packet. QEMU and some devices also send a packet with all
deltas zero, for example when a wheel step ends.

The presentation task drains these events and routes them to the active space's
[pointer session](#userspace-pointer-sessions).

## Controller and setup

The [keyboard](keyboard.md) and mouse share one controller driver,
[`ps2.c`](../../arch/x86_64/ps2.c). Each received byte goes to the keyboard or
mouse queue according to the controller status register's auxiliary bit,
whichever of IRQ 1 or IRQ 12 announced it. Setup runs once on the BSP with
interrupts disabled. **A mouse failure leaves the mouse unavailable and never
fails keyboard setup or boot.** If keyboard setup fails, the mouse is not
attempted.

1. ACPI provides the IRQ 12 route alongside the keyboard's. An invalid IRQ 12
   override, or a route on another I/O APIC, beyond its inputs or sharing the
   keyboard's GSI, leaves the mouse unavailable. Without a route no auxiliary
   command is sent.
2. While the keyboard port is disabled, the controller must show a working
   second port: enabling and disabling it toggles the configuration bit, and
   the port test returns 0. A single-port controller would pass mouse commands
   to the keyboard, so this check precedes them.
3. After the keyboard selects scan set 2, and before it starts scanning, the
   auxiliary port is enabled and the mouse is reset, set to defaults, and
   probed for an IntelliMouse wheel (sample rates 200, 100, 80, then device
   ID 3). The sample rate returns to 100. Reset waits up to 4 s for its ACK and
   again for its self-test result, and other replies up to 500 ms. These are
   Linux libps2's bounds: Synaptics devices such as the ThinkPad's touchpad
   finish the reset before ACKing it. A responsive device costs nothing extra;
   a port with no device can add about 4 s to boot before the mouse is marked
   unavailable.
4. After keyboard scanning starts, the mouse enables reporting. Keyboard bytes
   that arrive while waiting for its ACK enter the keyboard queue. IRQ 12 is
   unmasked last.

A failure at any step logs the step, controller status and last reply, then
disables the auxiliary port and its interrupt.

## Packets

Packets are 3 bytes, or 4 with the wheel. The IRQ only queues bytes; the
consumer decodes them. A first byte without bit 3 set is skipped, which
realigns a shifted stream. A packet with an X or Y overflow bit keeps its
buttons and wheel but reports no motion. A full queue or a controller error
discards queued bytes and any partial packet, then the next read returns a
reset event; the consumer must release any buttons it considers held.

`unsynchronized_bytes`, `overflowed_packets` and `lost_inputs` in
[`mouse.c`](../../arch/x86_64/mouse.c) count these cases for debugger
inspection.

Resynchronization relies only on bit 3. A device that drops a byte mid-packet
can produce misaligned packets until a first-byte check fails; there is no
inter-byte timeout. During setup, a packet byte from firmware-enabled reporting
that equals an ACK or error reply can be mistaken for one; the result is at
worst an unavailable mouse for that boot.

## Manual inspection

From QEMU's monitor (Ctrl-a c on the serial terminal), `mouse_move 10 -5`
moves right and up, `mouse_move 0 0 1` is one wheel step away from the user,
and `mouse_button 1`, `2` and `4` press left, right and middle (`0` releases).
Under KVM, a hardware breakpoint on the instruction after `mouse_read_event()`
returns in `handle_pointer_input()`, conditioned on a true result, can print
each event. A hardware breakpoint set before boot can stop inside setup.

## Userspace pointer sessions

Each space owns a pointer object. Boot gives each workload init a named `pointer`
grant, and the shell and session launcher forward it wherever they forward
`keyboard`: to a pipeline's first stage when its stdin is a console. Its `INPUT`
right authorizes [the pointer protocol](../../include/abi/pointer.h), restricted
to processes in the object's own space. Libpyxis provides `pointer_acquire`,
`pointer_read` and `pointer_release` in `<pointer.h>`.

ACQUIRE, READ and RELEASE follow the [keyboard session](keyboard.md#userspace-keyboard-sessions)
rules: exclusive acquisition by one process (`CALL_BUSY` otherwise, including
for the owner), `CALL_UNAVAILABLE` without a working mouse, process ownership
independent of handles, release on RELEASE or process exit, and blocking or
`POINTER_READ_POLL` reads of one 24-byte event. Pointer, keyboard and display
sessions are independent; a game acquires each it needs. There is no
cross-session wait, so an application reading both input sessions polls or
blocks on one of them.

`POINTER_INPUT` events carry the device's relative counts with the signs above,
plus the buttons held after the event. There is no absolute position,
acceleration or on-screen cursor. A packet that changes nothing for the session,
such as QEMU's zero wheel-release packet, produces no event.

### Focus, held buttons and loss

Only the active space receives input; the log space on multicore boots receives
none. Switching spaces discards the old session's queued events and publishes
`POINTER_FOCUS_LOST`; the new session receives `POINTER_FOCUS_GAINED`. Each
event's `POINTER_EVENT_FOCUSED` flag records focus when it was queued.
Control events carry no motion or buttons. Applications release all held
buttons on any focus or `POINTER_STATE_RESET` event.

A button counts as held for a session only after a press it observed. A button
held across acquisition, a focus change or a reset is withheld until it is
released and pressed again, so a drag begun in another space cannot arrive as a
held button. Presses are judged against the device's previous packet, not the
session's.

The queue holds 64 events. When it is full, a new event with the same buttons
as the newest `POINTER_INPUT` event adds its motion and wheel into that event,
saturating at the 32-bit limits. Any other event discards the queue, clears held
buttons and queues `POINTER_STATE_RESET`. Device loss resets every application
space's session the same way. These notifications wake a blocked reader even
while its space is inactive.

Routing, ownership and the queue share a per-object lock. All callers hold
IF=0; lock order is pointer, then scheduler queues. No allocation, user copy or
context switch occurs under the pointer lock. Space switching and event
delivery run on the BSP; session calls can run on APs.

## Mouse test program

`mousetest` shows pointer input and needs named `display`, `keyboard`,
`pointer` and `clock` grants. The left 30% of the screen shows the left, middle
and right buttons, coloured while held; an up or down arrow for half a second
after a wheel step away from or toward the user; and the position. The position
starts at the screen centre, moves one pixel per device count and is clamped to
the screen. The right 70% is a white drawing pad: each input event with the left
button held sets one black pixel at the position. A red marker shows the
position without drawing into the pad. Escape releases the sessions and returns
to the shell.

It polls both input sessions every 10 ms while focused and blocks on the
keyboard session while unfocused. Its text uses a built-in 5x7 font with only
the digits and letters it displays.

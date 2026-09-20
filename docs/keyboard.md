# Keyboard input

The PS/2 keyboard delivers physical key events through
[`keyboard_read_event()`](../include/kernel/keyboard.h). One BSP kernel task
consumes them; the call is nonblocking and preserves interrupt state. The
presentation task drains pending events before drawing and sleeping.

Each event identifies a key, press/release/repeat action, and modifier state
after that action. Key names describe PC key positions, not text: `KEY_A` does
not choose a character or keyboard layout. Lock modifiers toggle on the first
press; their LEDs are not updated. Pause produces a press/release pair because
its ordinary scan sequence has no separate release.

The IRQ handler only collects bytes into a bounded queue. The consuming task
decodes them. If bytes are lost, the next read reports `KEY_STATE_RESET` with
`KEY_NONE` and clears held keys and modifiers. Discard any held-key state in the
consumer too. A false return leaves the event unchanged.

Initialization uses ACPI to find the keyboard's I/O APIC route and directs it to
the BSP. The controller uses untranslated scan set 2, with the auxiliary port
disabled. `keyboard_available()` is false if firmware reports no usable route
or controller, or keyboard initialization fails. Serial input is separate.

Space navigation, character mapping and userspace delivery belong to callers;
the driver installs no bindings or consumer task.

The presentation task binds Alt+Left and Alt+Right to the previous and next
space in CPU order, wrapping at either end. The selected tab's name is underlined.
Each arrow press switches once; releases and repeats do not switch. Either Alt
key works, lock modifiers do not affect the shortcut, and adding Shift, Control
or Super suppresses it. Other key events are consumed without an action until
application input is defined.

For manual inspection, QEMU's monitor accepts `sendkey left`, `sendkey shift-a`
and similar commands while the VM runs. Enter the monitor with Ctrl-a c from
the serial terminal. Under TCG, use the precautions in [the GDB guide](gdb.md)
to stop in the BSP scheduler and call `keyboard_read_event()` with allocated
event storage. Under KVM, inspect memory or use breakpoints instead of injected
function calls: GDB's temporary return breakpoint can land on a non-executable
kernel stack.

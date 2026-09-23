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
or Super suppresses it. Remaining input goes to the selected space's console.
Caelum discards application input on multicore boots; the single-CPU development
fallback shares its console with userspace.

The session maps US ASCII key positions, with Shift, Caps Lock and typematic
repeat. Enter yields newline, Backspace `\b`, Tab `\t`, and Escape `0x1b`.
Ctrl+letters yield control bytes without signal or EOF meaning. Alt/Super chords,
modified navigation, function keys and the numeric keypad (except Enter) have no
text binding yet. Physical key events remain separate from this text mapping.

Unmodified navigation keys produce terminal sequences:

| Key | Bytes |
| --- | --- |
| Up / Down | `ESC [ A` / `ESC [ B` |
| Right / Left | `ESC [ C` / `ESC [ D` |
| Home / End | `ESC [ H` / `ESC [ F` |
| Delete | `ESC [ 3 ~` |
| Page Up / Page Down | `ESC [ 5 ~` / `ESC [ 6 ~` |

Each console retains a 4 KiB byte queue, including while its space is inactive.
A sequence is enqueued whole or rejected whole. Reads can split sequences; a
userspace terminal decoder must retain partial sequences. Standalone Escape is
ambiguous with a sequence prefix. Wrapped cursor editing is provided by [libterm](terminal.md); history remains
userspace work for the shell.

Overflow clears the queue and latches `CALL_INPUT_LOST`. Further input is
discarded until a nonempty read acknowledges the loss by returning that status.
A device `KEY_STATE_RESET` applies the same policy to every application console:
lost scan bytes may include a space switch, so the intended destination is
unknown. No kernel echo or line editing is performed.

For manual inspection, QEMU's monitor accepts `sendkey left`, `sendkey shift-a`
and similar commands while the VM runs. Enter the monitor with Ctrl-a c from
the serial terminal. Under TCG, use the precautions in [the GDB guide](gdb.md)
to stop in the BSP scheduler and call `keyboard_read_event()` with allocated
event storage. Under KVM, inspect memory or use breakpoints instead of injected
function calls: GDB's temporary return breakpoint can land on a non-executable
kernel stack.

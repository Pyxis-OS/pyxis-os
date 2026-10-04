# Keyboard input

The PS/2 keyboard delivers physical key events through
[`keyboard_read_event()`](../../include/kernel/keyboard.h). One BSP kernel task
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
the BSP. The controller uses untranslated scan set 2. The auxiliary port carries
the [mouse](mouse.md); controller bytes are routed by the status register's
auxiliary bit, and a mouse failure never affects keyboard setup.
`keyboard_available()` is false if firmware reports no usable route
or controller, or keyboard initialization fails. Serial input is separate.

An ACKed `F0 02` selects scan set 2. Setup then queries the set while scanning
is disabled. The query commands still require ACKs, but the ID byte is optional:
the current wait is 20 ms, measured by the initialized monotonic clock and
checked every 1,024 status polls. A received ID other than `02`, or a controller
parity/timeout error, fails setup. Absence permits the ACKed selection with an
info message. Controller output is drained immediately before scanning is
enabled; a late ID observed there must also be `02`.

This supports firmware that ACKs the query without returning its ID. It does
not select translated scan set 1. The drain removes pending bytes while scanning
is stopped; replies delayed until after scanning starts cannot be independently
identified. The expected late `02` has no set-2 key mapping and produces no event.
Native validation must confirm letters, digits, modifiers and extended keys.

Space navigation, character mapping and userspace delivery belong to callers;
the driver installs no bindings or consumer task.

The presentation task binds Super+Left and Super+Right to the previous and next
space in CPU order, wrapping at either end. The selected tab's name is underlined.
Each arrow press switches once; releases and repeats do not switch. Either Super
key works, lock modifiers do not affect the shortcut, and adding Shift, Control
or Alt suppresses it. A shortcut's arrow repeats/releases remain consumed even
if Super is released first. Remaining input is routed to the selected space's
keyboard session, or its console when there is no session.
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
ambiguous with a sequence prefix. Wrapped cursor editing is provided by [libterm](../userland/terminal.md); history remains
userspace work for the shell.

Overflow clears the queue and latches `CALL_INPUT_LOST`. Further input is
discarded until a nonempty read acknowledges the loss by returning that status.
A device `KEY_STATE_RESET` applies this policy to every application console
without a captured keyboard session; captured sessions receive a physical-input
reset instead. Lost scan bytes may include a space switch, so the intended
destination is unknown. No kernel echo or line editing is performed.

For manual inspection, QEMU's monitor accepts `sendkey left`, `sendkey shift-a`
and `sendkey meta_l-right` (Super+Right) while the VM runs. Enter the monitor with Ctrl-a c from
the serial terminal. Under TCG, use the precautions in [the GDB guide](../development/gdb.md)
to stop in the BSP scheduler and call `keyboard_read_event()` with allocated
event storage. Under KVM, inspect memory or use breakpoints instead of injected
function calls: GDB's temporary return breakpoint can land on a non-executable
kernel stack.

## Userspace keyboard sessions

Each space owns a keyboard object. Boot gives init a named `keyboard` grant;
the shell forwards it to children and session successors when present. Its
`INPUT` right authorizes [the keyboard protocol](../../include/abi/keyboard.h),
restricted to processes in the object's own space. Libpyxis provides
`keyboard_acquire`, `keyboard_read` and `keyboard_release` in `<keyboard.h>`.

ACQUIRE and RELEASE are header-only requests with no reply. ACQUIRE returns
`CALL_BUSY` if any process already owns the session, including the caller, or
`CALL_UNAVAILABLE` when the keyboard device is unavailable. READ and RELEASE
require the acquiring process. Acquisition queues an initial focus notification.
READ returns one fixed-size event; zero flags block, while `KEYBOARD_READ_POLL`
returns `CALL_TIMED_OUT` if the queue is empty. Validate reply storage before
consuming an event or blocking. Display acquisition is independent.

Ownership belongs to the process, not an individual handle. Copying a grant
does not transfer an acquired session; closing its last handle does not release
it. Another handle to the same object can release the session, and process
exit/fault releases it even if all handles have been closed. There is one task
per process, so the owner cannot release or exit concurrently with its blocked
READ. Future external termination will need to detach that waiter explicitly.

While acquired, physical input does not also enter the console queue. Acquisition
and release discard queued console bytes; a terminal reader already waiting
continues to wait for future text. Session release restores text routing without
replaying captured keys. Held-key repeats/releases from before the routing
change are ignored until a fresh press.

### Focus and loss

Only the active space receives key presses, releases and repeats. Switching
spaces discards the old session's queued events and publishes `KEY_FOCUS_LOST`;
the new session receives `KEY_FOCUS_GAINED`. Both reset held-key state. The
kernel's accepted-press state is also cleared, so a key held across a switch
must be released and pressed again. Session modifier bits reflect accepted
modifier presses; lock-toggle bits reflect the keyboard's current lock state.
The terminal text mapper retains its physical modifier behavior.

Each event's `KEYBOARD_EVENT_FOCUSED` flag records focus when it was queued.
Control events use `KEY_NONE` and zero modifiers. Applications must clear all
held keys on **any** focus or state-reset event and take focus from the flag.
Rapid transitions can replace an unread focus notification; the latest event
still establishes a fresh state. An already-dequeued event can precede the new
notification in the reader's execution.

The session queue holds 64 events. Overflow drops the queued events and the
event that overflowed it, clears accepted presses, and queues `KEY_STATE_RESET`.
Subsequent fresh presses may follow that reset; orphan repeats/releases are
ignored. Device scan loss similarly resets all application destinations. These
notifications wake a blocked reader even while its space is inactive.

Routing, ownership and queues share a per-object lock. All callers hold IF=0;
lock order is keyboard, then console input, then scheduler queues. No allocation,
user copy or context switch occurs under the keyboard lock. Publication detaches
the task-owned wait record before waking it, preserving wake-before-park handling.
Space switching and event delivery run on the BSP; session calls can run on APs
without a new BSP allocation request queue.

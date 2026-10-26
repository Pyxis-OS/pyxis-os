# Native gamepad input

`gamepad` is a space-local INPUT object. The kernel grants it beside keyboard
to each space's first process; session, shell and multiplexer forwarding follow
keyboard's ordinary local-input policy. Bundles still declare `gamepad/input`.
There is no trusted-space opt-in flag or raw HID/output authority.

The [public ABI](../../include/abi/gamepad.h) defines the operations and layouts.
The SDK exports it and userland supplies `<gamepad.h>` wrappers.

## Ownership, focus and recovery

ACQUIRE is exclusive and process-owned, independent of display ownership. An
empty controller catalog is valid. Repeated or competing acquisition is BUSY.
Copies and handle closure neither transfer nor release ownership; RELEASE or
owner exit ends it. Every operation requires INPUT in the object's own space.
A simultaneous READ makes RELEASE BUSY until the reader completes.

Focus follows keyboard: selected space, no hidden existing graphics layer and
no focused system overlay. Console viewers need no graphics surface. Acquisition
does not select a space/layer. A pad never becomes system navigation, keyboard
input or pointer motion.

Focus changes discard queued state and invalidate accepted input. Buttons and
hat directions held through acquisition, reset or focus return require release
or neutral and then a fresh action. Sticks and triggers resume physical position
on the next complete focused report. No kernel deadzone or analog rearming
threshold is applied. Unfocused STATE queries expose no physical input.

READ returns a full snapshot or a lifecycle/reset event. VALID controls whether
the snapshot has usable axes/buttons/hat; invalidation is not a physical all-zero
report. Clear application-held input on focus loss, RESET or device removal.
After RESET, refresh CATALOG and query STATE for each attachment. Old READ events
with earlier sequence/catalog generation must not replace newer recovery state.

One level-triggered READABLE interest covers input, focus and topology. Readiness
does not acquire/reserve input. Lost ownership reports ERROR. READ with POLL
returns TIMED_OUT when empty. Without POLL it blocks; user copying occurs outside
the object lock. Failed event delivery invalidates continuity and queues RESET.

The implementation keeps 64 events. On a full queue, the newest continuous state
may replace the tail only for the same attachment/focus and unchanged buttons/
hat. Other overflow discards unreliable pending input, rearms held actions and
queues RESET. It does not silently lose a button edge or promise every USB report.

## Identity and the Edge profile

CATALOG returns up to eight live attachment IDs and a generation. INFO and STATE
select one ID; removal returns NOT_FOUND. Recheck the generation when enumeration
races hotplug. These are finite implementation bounds, not generic HID support.
Each attachment has a fresh boot-local ID; a replacement cannot inherit an old
handle's identity. INFO contains actual USB VID/PID/bcdDevice, a layout profile
and a trusted bounded name. It supplies no serial or physical-uniqueness claim.

The initial profile admits only DualSense Edge USB `054c:0df2` with the captured
Game Pad input layout and matching interface/endpoint. It checks descriptor
semantics before accepting ID `01` reports of exactly 64 bytes. No short-payload
extension, trailer ignoring, feature/output request or vendor initialization is
used. Unused sensor, touch and audio fields remain unexposed. Transport loss
invalidates the source before existing USB retirement/loss acknowledgement.

The six axes have native USB-report precision of eight bits each, unsigned
0–255 (Pyxis preserves the report bytes): left X/Y, right X/Y, left trigger,
right trigger. Hat directions are 0–7 clockwise from up; 8 means neutral.
Nineteen named bits cover the face buttons, shoulders, digital trigger buttons,
Create/Options, stick clicks, PS, touchpad click, mic, Fn and paddles. Analog
triggers and digital trigger bits remain independent; the kernel does not copy
SDL's analog fallback. Physical correspondence is checked in the task 1 viewer.

Existing USB record retention and possible controller quarantine still bound
reconnects. Exhausted attachment/catalog/sequence identities refuse rather than
wrap. Mouse and I2C touchpad behavior is unchanged. Generic HID, Ultimate 2,
Bluetooth and output effects remain later work in the
[controller milestone](../wip/controller-support.md).

## Visualizer

Install the `gamepadtest` program package and its `fonts` dependency, then run
`gamepadtest` from a local graphical shell. The bundle draws both sticks,
independent triggers, d-pad and all nineteen buttons live. Fn and paddles are
shown below the front controls. The empty state waits for attachment; invalid
input hides axis values. Escape exits; Tab or Left/Right chooses another pad.
Raster updates are capped at roughly 60 frames per second, with event-driven
input/readiness and display-resize handling.

Task 1 qualification is chronium's brief Edge plug-and-control check on vela.
No exhaustive host capture or every-trigger-stop matrix is required. A no-pad
QEMU check verifies presentation, empty acquisition and launch/exit, not physical
control correspondence or native USB behavior. SDL and LUS/SoH consumers follow
in separately assigned tasks.

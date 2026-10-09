# Mousetest

Run `mousetest` from a local shell with named `display`, `keyboard`, `pointer`
and `clock` grants. It acquires graphics and independent input sessions, then
uses the [system pointer](../interfaces/pointer.md) with a supplied 16x20 image
and hotspot `(1, 1)`. The left 30% of its view shows surface-local position,
held buttons, wheel direction and lock state. The right 70% is a white drawing
pad: each ordinary input event with left held sets one black pixel, so fast
strokes are dotted. Locked motion shows relative counts and leaves the drawing
pad unchanged.

| Key | Behavior |
| --- | --- |
| H | Toggle saved cursor visibility |
| D / C | Restore the kernel default / supplied image |
| W | Warp to the visible mapping/destination intersection's center |
| L | Toggle desired relative lock |
| Escape | Release input/graphics sessions and return to the shell |

Lock can be refused without exiting. Super+Esc revokes it before keyboard
capture and consumes Escape. While lock is still desired, a fresh surface click
reports `POINTER_ACTIVATED` and makes one new lock request; it does not repeatedly
poll LOCK. Press L to withdraw that desire. Cursor hiding during lock preserves
its saved image and show preference. WARP is refused while locked.

The loop drains input, then uses three `wait_many` interests: pointer READABLE,
keyboard READABLE and display RESIZED. A 10 ms UI deadline keeps transient wheel
feedback current; input readiness wakes it sooner. State changes clear held
controls, except same-session locked geometry changes retain buttons and relative
input. Focus loss removes input without pausing execution.

A committed destination generation change prepares a new drawing pad and
replaces the graphics mapping. Surviving ink is cropped/copied without scaling.
Allocation or replacement refusal preserves the old mapping and ink until a
fresh generation permits retry. Geometry and mapping identity are re-queried
before using ordinary coordinates or warp.

The [qualification report](../development/system-pointer-qualification.md)
records configurations, measurements, native checks and remaining runtime
coverage. This program performs no clipboard publication or paste.

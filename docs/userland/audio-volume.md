# Volume controls

The bar controls [software master/per-space gain](../interfaces/audio.md#user-volume-controls).
Boot master is unmuted at **50%** (about −30.3 dB); spaces start unmuted at 100%.
Settings survive session release but reset on reboot. Neither level boosts PCM.

The 24 px speaker beside the battery controls master; each tab has its own space
speaker. Click toggles mute, hover opens the slider, wheel changes 5%, and clicking
or dragging the vertical track selects a percentage. The popup shows confirmed
level/mute; a space popup also reports master mute. Inactive-tab controls do not
switch focus. Muted/zero uses the crossed speaker; low/medium/high cover
1–33/34–66/67–100%. Caelum and unavailable audio show disabled controls.
At narrow sizes whole controls/popups are omitted rather than scaled or overlapped.
The battery remains unchanged. [Owner artwork](../../assets/ui/volume/README.md)
is blitted unscaled as a mask in the theme's text colour.

**Super+G** focuses master, **Super+Shift+G** the active space popup, and
**Super+M** toggles master mute. Extra Control/Alt prevents those chords.
Within a focused popup, arrows change 1%, Page Up/Down 5%, Home/End select
0/100%, Space toggles mute, and Escape closes without reverting changes.
Hover alone leaves content keyboard input active. Explicit focus suspends it,
resets held keys and sends application focus loss/gain without releasing capture.
Consumed keys stay suppressed until release. Existing navigation/escape keeps
priority; Super+Shift+V remains shared-clipboard Paste.

The popup stays open across the icon's whole bar slot and slider, closes when
leaving unless focused/dragging, and consumes an outside click. Resize, space
switch or input loss cancels it. A drag keeps its original target. Hit testing
uses the last presented layout. Screenshot capture includes the popup before the
cursor. Relative pointer lock bypasses hover/popups; use **Super+Escape** first.
Master mute still works under capture/lock. Laptop media keys are not decoded yet.

Mute keeps the selected level; a positive slider change unmutes that control.
0% remains silent when unmuted. Other changes/unmute ramp over 5 ms of newly
mixed frames; already-published audio can remain in the nominal 80 ms ring.
Muting does not pause, release or stop queue consumption. Native listening and
performance evidence live in the [milestone](../wip/audio-volume.md).

# Pointer input quality

USB report-protocol mice and calibrated PS/2 Synaptics touchpads feed the
[system pointer](../interfaces/pointer.md#input-source-coordination) through the
same source coordinator. Device reports never bypass focus, geometry, lock,
held-button quarantine or user-activation rules. Touchpad and pass-through
TrackPoint are separate logical sources even though they share one transport.

Each touchpad adapter owns a retained filter context, associated with its input
source before attachment. Geometry, contacts, rejection latches and scroll
fractions are source-local. Transport loss resets that context; coordinator
queue overflow resets all associated contexts. Registration/delivery allocates
nothing and remains BSP-owned with interrupts excluded.

## Scroll and settings

Signed `scroll_x`/`scroll_y` use 120 units per conventional detent, positive
viewport right/down. Button usages 1–32 occupy the existing mask. Source holds
are ORed; loss clears only that source under the shared reset/lock rules.
Scroll creates no activation and changes no keyboard focus. Conversion retains
fractions per source/axis; queue coalescing saturates each axis independently.
Discrete consumers retain a remainder per destination/view and clear it on
reset, focus, geometry or view changes.

Natural scrolling is the shipped default for mouse and touchpad independently.
The kernel applies direction once to both axes: wheel pull/finger down gives
negative `scroll_y`, moving displayed content down; fingers right give negative
`scroll_x`. Traditional reverses these. SDL performs its coordinate conversion
without applying direction again. Mux uses vertical scroll for history; it
never reinterprets horizontal events as vertical.

Stock boot init validates the plain machine settings
`system://config/machine/input/mouse/scroll-direction` and
`system://config/machine/input/touchpad/scroll-direction`, each `natural` or
`traditional`, then installs policy once through its private input-policy right.
Missing/invalid installed values use the archive's authored value; both shipped
archives author natural. Settings apply next boot. Ordinary programs cannot
change policy; the [machine settings](../userland/machine-settings.md) writer
remains separate work.

## USB mice

The [USB HID reference](usb-hid.md) defines bounded descriptor admission,
relative X/Y, vertical Wheel, horizontal AC Pan, buttons and Resolution
Multiplier handling, plus the boot fallback and attachment lifetime. The USB
worker owns controls/streams; only admitted reports reach input normalization.
Malformed reports are dropped/countable, with retirement after sixteen
consecutive malformed reports or unrecoverable transport errors. Receive TDs
cover the admitted endpoint maximum packet size, independently of decoded size.

On a boot-subclass mouse whose descriptor declares Report IDs, reserved ID zero
proves boot-format input: decoding switches for that attachment, keeping an
optional fourth wheel byte and diagnostic-only reporting. Nonzero first bytes
never trigger the switch. The ASUS dongle's motion/clicks/wheel are qualified;
its unsupported side buttons are an owner-accepted limit. The G502/Lightspeed
motion/buttons/free-spin/chord/replug regression is qualified. Remaining
Keychron, horizontal and high-resolution hardware coverage lives in
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1291).

## Calibrated Synaptics activation

Owner decision accepted 2026-10-13: absolute mode is **default-on only for a
matching calibrated profile**, currently the T14 Gen 1 AMD fingerprint below.
All other Synaptics pads retain relative input, including pads with plausible
queried geometry. `ps2.synaptics=0` forces relative; `1` enables qualified
admission and cannot bypass the fingerprint. There is no runtime switch.

`ps2.synaptics.diagnose=1` enables bounded info-level query/contact summaries;
it observes the selected mode rather than independently forcing absolute mode.
Use it with `ps2.synaptics=0` to diagnose relative input. There is no new default
output, raw contact API, serial-number query or keystroke logging.

The [arch decoder](../../arch/x86_64/ps2/synaptics.c) owns 8042 commands and
identity/query/format validation; the [kernel filter](../../kernel/input/touchpad.c)
owns palm/edge/typing admission and motion/scroll conversion. Mode readback and
a forced master packet establish the format before publishing input. Failed
negotiation restores a proven relative format; uncertain bytes are never sent
to the relative decoder. Missing usable geometry preserves relative motion
without claiming palm/scroll qualification. Firmware taps may remain on the
accepted geometry-only legacy fallback or on an unqualified legacy pad after
pre-qualification query failure. Failure to prove a restored format still
refuses auxiliary input. No tapping, tap drag or finger-count
button synthesis is implemented.

Queried image sensors use c5 (absolute/high-rate/W/EW), then the separate AGM
request, sliced model selector 03 followed by F3 C8. With W enabled, bit 2 is
EW and admits the secondary packet format: this corrects the earlier EW-off
premise, accepted 2026-10-13. Sliced arguments have the E6 delimiter and four
E8 pairs. W2 position/count packets update private state, not switch/lift events.
The [mouse reference](mouse.md#synaptics-absolute-input) documents packet,
filter, fallback and guest ownership in detail. No four-byte guest negotiation,
SMBus/RMI4 driver or new transport is included.

### Calibrated T14 geometry

The owner measured the visible pad three times as 102x70 mm. Queried X resolution
is zero; calibrated X45 units/mm makes the 4412-unit sensing span 98.04 mm,
with inferred 1.98 mm borders. Firmware Y54 gives 65.41 mm within 70 mm,
with inferred 2.30 mm borders. These borders are authored allowances, not
measured sensor edges. Native edge slides observed X1266..5678/Y1162..4686
against queried Y maximum 4694; the eight-unit difference does not change bounds.
No square-unit inference or generic resolution default is used.

The complete non-serial fingerprint must match. Query 01's last byte is live
mode state and is intentionally excluded; other fields are exact:

| Query | Response |
| --- | --- |
| 00 identity | `20 47 1a` |
| 01 board/more-query flags | `37 8f` (mode byte ignored) |
| 02 capabilities | `f0 14 a3` |
| 03 model | `01 e2 a1` |
| 08 raw resolution | `00 80 36` |
| 09 extended capabilities | `94 03 00` |
| 0a firmware | `3c 9c fd` |
| 0c continued capabilities | `12 e8 00` |
| 0d maximum | `b1 b7 92` |
| 0f minimum | `27 59 24` |
| 10 more capabilities | `50 00 00` |

## Contact behavior and limits

Strong palms remain rejected until lift. Edge-origin contacts need deliberate
inward movement and a fresh baseline. Typing gates only new contacts begun
within 75 ms of a nonmodifier press/repeat; admitted fingers remain usable.
Gated contacts retry after the quiet interval without compulsory lift. Release
never rearms typing. Physical clicks and TrackPoint bypass contact rejection.
Transport/queue loss keeps contact quarantine until a confirmed lift.

Fresh paired observations within 50 ms are matched by minimum physical
displacement. Scroll uses the centroid, entering at <=25 mm finger separation
and leaving at >=30 mm. Physical click always prevents scroll; the larger-moving
finger drives the pointer, retaining the prior choice on ties. Missing/invalid/
stale secondary data immediately falls back to basic-W count/primary-position
scroll while unpressed and reported-primary drag while pressed. Pair/fallback
changes clear motion fractions/baselines while preserving known admission.
Fallback cannot measure separation, centroid motion or an unreported moving
finger; ambiguous identity produces no invented movement. AGM supplies no
secondary width. Tunables remain explicitly revisitable in Kconfig:
TOUCHPAD_TYPING_MS, TOUCHPAD_PAIR_FRESH_MS, TOUCHPAD_SCROLL_ENTER_MM and
TOUCHPAD_SCROLL_LEAVE_MM. Their pressure/distance scales are device tuning,
not universal contact or force units.

## Qualification and deferred work

Task 1 merged in #716, userland #214 and ports #99; task 2 in #739/#773.
Owner T14 runs on 2026-10-13 established calibrated admission/extents, solid
palms, working TrackPoint, good typing response and EW/AGM pair scroll/drag.
At #773 fef995a7 the owner reports "trackpad feels great now": arriving AGM
positions, usable/separated pairs, active trampoline and unused basic-W fallback.
The latest diagnosis-off repeat is optional and unclaimed. Local QEMU exercises
ordinary non-matching relative recovery, not actual Synaptics contact behavior.
The [qualification record](../development/experiments/pointer-input-quality/README.md)
keeps revisions, measurements, failures/corrections and native reproduction steps.

[Terminal mouse reporting](https://git.internal/PyxisOS/pyxis-os/issues/1345) is separate: device support
does not qualify Links/Neovim application scrolling. Tapping, inertia/gestures,
public contacts, SMBus/RMI4, HID-over-I2C hardware beyond vela's ELAN touchpad,
Bluetooth and unsupported hardware qualification remain in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1344),
not unfinished implementation placeholders in this milestone.

## Precision HID-over-I2C integration

The source-owned filter accepts complete native Tip/Confidence/Contact-ID frames
as well as the existing Synaptics hints. Native coordinates are descriptor-derived
micrometres; absent Z/W values stay absent. Tip-active/Confidence-clear quarantines
the whole gesture until complete all-up; physical buttons remain independent.
Stable IDs preserve contact identity across reordered slots. The first admitted
report establishes a baseline, the second can produce motion; no moving average
or extra native scroll-entry report is added. Typing, edge admission, scroll
separation/dead zone and source policy remain shared.

The [vela milestone](../wip/vela-i2c-touchpad.md#task-4-integration-and-qualification-boundary)
records qualified bounded client recovery and native input behavior.

Identified HID single-switch clickpads retain per-contact touchdown positions,
receipt times and button roles. Unpressed button-strip contacts promote to
ordinary pointer contacts after `TOUCHPAD_BUTTON_MOVE_MM`, rebaselining before
motion; a physical press prevents distance promotion for that contact lifetime.
Primary-zone two-finger secondary selection requires observed unpressed,
confident stationary contacts within `TOUCHPAD_CLICK_PAIR_MS` /
`TOUCHPAD_CLICK_PRESS_MS` and `TOUCHPAD_CLICK_STILL_MM`. Missing or untrusted
history falls back to primary; selection is immediate and held until release.
Once the primary-zone pair qualifies as secondary, later presses retain it
while both contacts remain down within `TOUCHPAD_CLICK_HOLD_MM` (default 3 mm)
of their positions at the original qualification. Later presses do not move
those anchors. Lift, contact replacement, an extra contact, movement beyond
that bound or untrusted history clears the qualification; a held press keeps
its selected button until release. The initial qualification still uses the
separate 1 mm touchdown bound.
Unknown report IDs do not update history; lift, source loss and reinitialization
clear it. Existing bottom-right selection, other physical buttons and the
legacy Synaptics path stay separate. The
[owner-accepted defaults](../wip/vela-i2c-touchpad.md#native-qualification-owner-2026-10-16)
and [qualification record](../development/experiments/clickpad-tuning/README.md)
keep authored policy distinct from measured native behavior.

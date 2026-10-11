# ThinkPad pointer input quality

Plan accepted 2026-10-10. Task 1 is merged (#716, userland #214, ports #99).
Task 2 is assigned to Codex alpha (2026-10-11); baseline captured from fresh
main `df7ec8ab`, decisions below accepted; opt-in implementation is assigned. Scope:
USB relative mice and the ThinkPad's PS/2 Synaptics touchpad/TrackPoint, feeding
one [system pointer](../interfaces/pointer.md#input-source-coordination).

## Recorded owner direction

Accepted 2026-10-10: **natural scrolling is the default for both sources**, with
independent mouse and touchpad direction settings. Natural means finger motion
or pulling the wheel toward the user moves displayed content toward the user;
traditional reverses that relationship. Apply direction to both axes once in kernel source normalization, not again
in each application. With the accepted viewport-positive axes, a natural wheel
pull/downward finger stroke gives negative `scroll_y` (content down, viewport
up); fingers right give negative `scroll_x` (content right, viewport left).
Traditional reverses these. API coordinate conversion, such as SDL positive-up,
is separate from direction policy. Tap-to-click is off by default;
this milestone implements no tap or tap-and-drag synthesis. Physical buttons
and clickpad presses supply clicks. Any later tapping option must be an explicit
setting, defaulting off.

At main `131a9266`: USB uses boot protocol, except an exact QEMU wheel
layout; the ASUS `1ea7:0066` wheel remains unqualified. PS/2 firmware-relative
mode merges touchpad and TrackPoint and can generate taps. The Fedora
[inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt) names
`SynPS/2 Synaptics TouchPad` and `TPPS/2 Elan TrackPoint`; it establishes no
absolute-mode, pressure, width, multi-contact or clickpad capabilities.
The public pointer record has one integer vertical `wheel` and three button
bits. Mux consumes it for history; SDL consumes it for application wheel events.
Links' Pyxis profile does not request terminal mouse reports, and Neovim's TUI
disables them. Their application scrolling needs the separate terminal-mouse
follow-up: xterm modes 1000/1002 and SGR 1006 in local TTY, mux and remote.
Device decoding alone cannot claim those consumers work.

## Accepted contract (owner, 2026-10-10)

1. **Shared scroll representation and buttons.** Replace the single wheel field
   with signed `scroll_x`/`scroll_y`, 120 units per conventional detent; positive
   means scrolling the viewport right/down. Preserve fractional conversion
   residue per source and axis, and expose Button usages 1–32 in the existing
   mask, without global back/forward shortcuts. Update SDK and consumers together.
2. **Touchpad rejection and recovery.** Reject strong palms until lift; suppress
   touch motion/scroll while typing and for an initial 500 ms after the last
   non-modifier physical press/repeat, from either keyboard path. Contacts begun
   while suppressed require lift. Physical clicks and TrackPoint remain
   independent. Relative fallback on a Synaptics device is permitted only with
   established tap-off; otherwise report the touchpad unavailable, retaining a
   verified guest path if possible.
3. **Settings authority and application integration.** Boot-only validated
   machine keys, read by stock boot init and installed once through a
   boot-init-only input-policy right; ordinary programs cannot change policy.
   Keep natural defaults with independent mouse/touchpad direction settings;
   changes apply next boot. No live tool, new writer or service. Deliver the two
   device tasks and shared-event consumers; Links/Neovim wheel qualification
   depends on the separately assigned terminal-mouse work.

The 500 ms interval and parser/filter budgets below are starting tuning, not
hardware truths or permanent architectural limits. Native evidence may change
them; changes to the accepted behavior/authority boundary need an owner decision.

## Shared integration

Keep BSP source ownership, ORed button holds, suppression/quarantine, loss,
focus, geometry, hover-wheel routing and fresh-click activation rules. Scroll
never grants activation or changes focus. Do not release another source's hold.
Touchpad and pass-through TrackPoint become separate logical sources, despite
sharing one transport. Absolute contacts produce relative deltas, not screen
warps; normal pointer positioning/lock still owns coordinates. No raw USB,
contact stream or input-bypass grant is added.

Shared header/source-coordinator and settings plumbing belongs to task 1 and
is published as the common base before integration. Task 2 can develop its
PS/2 decoder/filter independently against that agreed adapter contract; avoid
duplicate edits to the coordinator. Both tracks retain fractions when converting
raw reports. Queue coalescing saturates each axis independently. Discrete
consumers keep a remainder per destination/view and clear it on reset, focus
or geometry/view change; high-resolution input must not be truncated per report.
SDL receives both precise axes; mousetest shows them and extra buttons. Mux
converts vertical movement to existing history rows and does not reinterpret
horizontal movement as vertical. Volume controls keep their discrete steps.
Unsupported horizontal actions remain explicit consumer limits.

Planned machine keys: `system://config/machine/input/mouse/scroll-direction` and
`input/touchpad/scroll-direction`, enums `natural` / `traditional`. Accepted
2026-10-11: missing or invalid installed values use the archive's authored value,
matching the hostname convention, with an opt-in diagnostic. Both shipped
archives author natural.
Use the [plain store convention](../userland/machine-settings.md), shared schema
validation and archive defaults; installed values survive updates, live images
use archive configuration. Changes apply next boot. The validating writer is
still the settings milestone's separate task. Filter thresholds belong to one
authored device profile, selected from queried identity/resolution; calibration
may produce validated profile overrides in this subtree, not competing runtime
state or guessed universal pressure units.

## Tasks

1. [x] **USB report-protocol mice and shared scroll integration.** The existing
   BSP USB worker owns descriptor admission, control requests and interrupt
   streams. Admit one standard relative Mouse application collection on a
   supported interface, including boot/non-boot mice and composite keyboard/mouse
   devices; keep boot keyboards and their repeat/activation behavior unchanged.
   Parse bit offsets, signed ranges, Report IDs, local/global state and collection
   scope before selecting report protocol. Decode X/Y, Wheel, Consumer AC Pan
   and numbered buttons. Padding/ignored fields still contribute to report size;
   button-bearing IDs retain independent snapshots, so wheel-only reports do
   not release buttons. No tablet, vendor protocol, gamepad or output-report driver.

   Use prepared bounded layouts: at most the existing 4096-byte control buffer
   and 1024-byte receive capacity, with initial budgets of 16 report layouts,
   64 relevant fields and eight nested collections/global pushes. Bound usage
   expansion and offset arithmetic. Unsupported descriptors fall back only on
   an actual boot mouse after confirmed SET_PROTOCOL(boot), using its defined
   prefix and the existing qualified QEMU extension. No trailing-byte guesses.
   Unsupported mouse admission preserves a supported keyboard sibling. Runtime
   malformed reports are dropped without changing held state; per-interface
   counts and consecutive streaks are retained, and a valid report resets that
   interface's streak. Sixteen consecutive malformed reports or transport failure
   retire/reset the whole binding (including a composite sibling). Only the
   reserved-ID evidence rule below switches decoding; no independent interface
   lifetime is added.

   **Owner correction accepted 2026-10-11, implemented and natively qualified:**
   receive TDs cover at least endpoint MaxPacketSize and the admitted report
   size within HID capacity; report IDs and field ranges remain strict. Protocol admission reads
   GET_PROTOCOL after SET_PROTOCOL(report), as diagnostic evidence without a new
   boot fallback. Both diagnostic options cover the early unavailable/retiring
   path's failed completion metadata before retirement. Malformed input uses
   the drop/count/16-consecutive policy above, replacing one-report retirement.

   **Later owner follow-up accepted 2026-10-11, implemented and qualified:**
   for known mouse Input IDs, ignore trailing bytes and zero-extend missing
   payload; never fabricate an absent ID. Keep other unknown-ID rejection, field
   ranges, boot keyboard lengths and exact Feature images. Under either
   diagnostic option, record the first length mismatch per layout and the
   first four report-protocol mouse interrupt packets (ID/length plus up to 16 bytes).
   No keyboard raw data, including declared keyboard fields on a mixed Mouse
   interface.

   **Reserved-ID switch accepted 2026-10-11, implemented and natively qualified:**
   on a boot-subclass Mouse interface with an admitted ID-bearing descriptor,
   a nonempty Input beginning with reserved ID 0 selects boot decoding permanently
   for the attachment. Never switch on a nonzero byte, a nonboot interface or a
   descriptor without IDs. Decode the three-byte boot prefix and byte-3 wheel
   when length is at least four; wheel scale becomes 120/1, pan is absent and
   scroll fractions reset. Short packets keep the malformed-retirement policy.
   No device quirk table or new endpoint lifetime. Do not send SET_PROTOCOL again:
   preserve the observed boot stream rather than changing a live device format.
   Log the switch once under either diagnostic option. Reattachment starts fresh.

   Enable high-resolution wheel Features only for understood wheel-scoped
   Resolution Multipliers: read the complete Feature, preserve unrelated fields,
   write the selected scalar and confirm readback. Start with positive integral
   effective multipliers; other scopes/encodings need evidence before admission.
   An uncertain multiplier must not publish guessed scroll: recover a confirmed
   scale/boot fallback or disable the affected wheel with a diagnostic. Reuse
   EP0 STALL recovery and the optional initial GET_REPORT rule.

   Topology/capacity stays as [USB boot input](../devices/usb-hid.md): boot
   low/full/high-speed leaves through up to five supported USB 2 hubs;
   post-boot low/full-speed leaves on roots or boot-present chains; 32 retained
   admissions per controller. USB 3 periodic endpoints, new runtime hubs and
   USB-switch subtree recovery are not added.

   Capture/review the ASUS report descriptor before promising its wheel; a
   layout outside this profile needs an explicit scope decision, not guessing.

   **After this task, the owner can:** use the ASUS wheel once its descriptor
   fits and native qualification passes, and supported mouse
   buttons in mousetest/SDL, and natural vertical scrolling in mux history;
   qualify horizontal/high-resolution controls on a device that actually offers
   them. The same normalized events are ready for terminal application adapters.

2. [ ] **Synaptics absolute mode, palms and two-finger scrolling.** Keep 8042
   commands in arch and gesture policy beside kernel input. Probe identity,
   model, firmware, capabilities/extensions and coordinate resolution/bounds,
   disable reporting while changing mode, then validate the negotiated packet
   format before publishing input. Decode six-byte absolute X/Y, pressure Z,
   capability-qualified width W and finger-count hints. Basic W is not full
   contact tracking; AGM/semi-MT or image-sensor extensions require their own
   advertised layout and native evidence. Never guess unsupported fields.

   Demultiplex TrackPoint packets and board-specific stick buttons before touch
   filtering. Decode physical switches/clickpad press separately. Mode bit 2
   changes meaning between gesture-disable and extended-W modes: no blanket
   write to enforce tap-off. On failed negotiation, reset/re-establish a proven
   format; never send absolute bytes to the relative decoder. Preserve ordinary
   PS/2 mice. A known/expected Synaptics fallback that cannot distinguish or
   disable firmware taps cannot meet this milestone's tap-off contract.

   Use pressure hysteresis for contact admission, qualified width/pressure for
   strong palms, and edge-origin exclusion with deliberate inward movement
   allowed only after a fresh baseline. Calibrate edge bands from usable bounds
   and resolution; pressure has no universal force scale. Apply typing rejection
   before movement/scroll publication, without blocking physical clicks or stick
   motion. Reset baselines on lift, contact-count changes, rejection, packet loss
   and reconfiguration. One finger moves; two stable valid fingers scroll on
   both axes with a small dead zone, no inertia or tap emulation. If only aggregate
   coordinates are available, use them conservatively; ambiguous contact changes
   cancel scrolling rather than invent independent contacts. More contacts do
   not create gestures. Sample rate and gain are native starting tuning.

   **After this task, the owner can:** type with palms resting without pointer
   jumps, scroll with two fingers, drag using physical buttons/clickpad press,
   and use the TrackPoint while a palm rests on the touchpad. Native qualification
   must establish these together; a successful mode command is insufficient.

## Task 1 delivery (2026-10-11)

Implementation is in Pyxis #716, userland #214 and ports #99. Merge userland and
ports first, then the parent pin PR. The archive fallback was accepted 2026-10-11.

- [x] Report decoder, source/scroll policy, SDK and consumer integration.
- [x] Before-code baseline, interleaved QEMU cost record and interactive checks.
- [x] Owner native ASUS and G502 qualification; task 1 closure accepted 2026-10-11.
      Remaining device/axis coverage stays in [technical debt](../technical-debt.md#usb-hid-native-coverage).

The [qualification record](../development/experiments/pointer-input-quality/README.md)
records revisions, measured variation, coverage limits and exact native steps.
Task 1 is complete: ASUS motion/clicks/wheel and G502 motion/buttons/free-spin/
Cmd+R/replug pass at `e7a18849`. ASUS back/forward buttons are unsupported and
accepted without further work. Keychron, horizontal and multiplier coverage are
unqualified, not claimed by closure.

**Task 2 assigned 2026-10-11:** start only after #716 merges, on fresh main;
capture the baseline first and bring at most three decisions with defaults to
the owner before code. Current firmware-relative PS/2 input remains mouse-class;
this delivery supplies the touchpad policy/typing/reset base, not palm filtering.

## Task 2 accepted decisions (2026-10-11)

Before-code [baseline](../development/experiments/pointer-input-quality/README.md#task-2-before-code-baseline)
is captured before implementation. Natural directions, tap-off, palm
quarantine until lift and the initial 500 ms typing rule remain accepted.
Pressure/width hysteresis, edge band, motion gain, scroll dead zone and sample
rate are authored tuning, to be adjusted from native evidence rather than made
new architectural constants.

The checked-in ThinkPad inventory names Synaptics and Elan devices but does not
establish firmware, query capabilities, clickpad/stick-button routing, bounds or
resolution. [Rev. B §4.3](https://studylib.net/doc/18112502/synaptics-ps-2-touchpad-interfacing-guide)
says mode bit 2 is DisGest with Wmode=0 and EWmode with Wmode=1. Basic W replaces
gesture fields; physical switches are decoded independently. [Linux v6.17](https://github.com/torvalds/linux/blob/v6.17/drivers/input/mouse/synaptics.c)
is protocol evidence, not code to vendor into the MPL driver. No licence reuse
is proposed.

1. **Qualification boundary. Accepted:** first implementation/owner boot is
   opt-in, using basic six-byte absolute W, qualified finger-count hints and
   separate pass-through/physical-button paths. Query identity/capabilities and
   validate the format before publishing touch input. Extended-W/AGM stays off
   unless the actual device needs it and its advertised layout/native evidence
   is established; report that prerequisite rather than guessing contacts.
   Ordinary boots retain today's path during qualification. After the owner
   confirms the complete native palm/scroll/button/TrackPoint checks, enable the
   qualified Synaptics profile by default in this same task, before closure.
2. **Single-switch clickpad. Accepted:** each physical clickpad press is
   primary click, with no click zones or finger-count right-click synthesis.
   Preserve separately decoded physical TrackPoint left/middle/right buttons.
   This follows Linux's capability-qualified primary-switch interpretation;
   it does not treat firmware tap bits as physical switches. If the device is
   not a clickpad, retain its advertised physical button mapping.
3. **Missing usable geometry. Changed and accepted:** use validated queries or
   an explicitly calibrated identity profile when available. Otherwise fall back
   to today's relative PS/2 mode so touch motion keeps working. This geometry
   fallback is an explicit exception to withholding the touchpad; it does not
   qualify absolute palm/edge filtering, two-finger scrolling or independent
   touch/stick separation. Establish tap-off when the relative firmware supports
   it; if unavailable, the accepted legacy fallback can retain firmware taps.
   Other mode/transport failures keep the existing proven-format recovery rule.
   Do not substitute panel dimensions or claim calibrated edge rejection.


Keep PS/2 commands/query/packet decoding in arch; keep contact
admission, palm/edge/typing rejection and one-/two-finger conversion beside
kernel input. Reuse the existing per-source scroll/hold/loss coordinator, with
separate touchpad and guest sources; no public contact or raw-input interface.
The first native record must establish identity/query bytes, physical switch
routing and usable geometry, then qualify finger, palm, typing, lift/count
transitions, physical drag and guest behavior. Luna stages owner-run builds;
no native hardware was touched in the baseline. All three decisions were accepted by the owner on 2026-10-11, with decision 3
changed as recorded above. Native qualification is still pending.

## Qualification and tuning

Before either implementation, capture revisions/configuration and matched
idle/motion input and BSP/presenter costs, with repeated interleaved runs on the
same CPU/device/accelerator settings. `POINTER_SYNTHETIC=1` and
`DISPLAY_CURSOR_PROBE=1` provide timestamped idle/motion windows; they measure
pipeline cost (distinguish compose/copy totals, guest BSP/USB worker cost and QEMU
emulation), not pressure, contacts, wheel or palms.

QEMU covers ordinary PS/2 and USB mice/keyboards, boot fallback, two devices
holding buttons, report-mode wheel/extra-button decoding and chained supported
hubs, with descriptors and source/queue state inspected in the debugger. It does
not cover Synaptics absolute, ASUS, horizontal or multiplier hardware; report
unexercised layouts explicitly.

Owner/luna native batch: retain identity/query/mode facts, ASUS descriptors,
selected protocol, hub path and multiplier readback. Use an opt-in bounded contact
diagnostic with automatically timed/named windows for normal fingers, edge entry,
resting palms, typing, finger transitions, physical drags and TrackPoint use with
a palm down. Summaries include pressure/width distributions, accepted/rejected
samples, resets, queue loss and software timing, omit typed text/key identities,
and keep raw records local or in the PR. Tune from these records, then rerun with
diagnostics off. Attach/detach and normal diagnostics stay in ktrace; no
unconditional packet dump or new boot klog lines.

Qualify natural and traditional directions separately for each source, held
buttons on loss/replug, small opposite wheel increments, diagonal two-finger
scroll, and unchanged keyboard/TrackPoint operation. Device-task closure requires
native ASUS wheel and touchpad/palm/stick evidence. Links/Neovim direct scrolling
is qualified after the terminal reporting follow-up, in tabs and mux; remote input
needs that follow-up's independent validation.

## Deferred limits and sources

[Technical debt](../technical-debt.md#pointer-gestures-and-alternative-transports)
keeps pinch, swipe/rotation, momentum, tapping, advanced contact tracking and
I2C-HID/RMI4 outside this milestone. Keep
[USB switch subtree recovery](../technical-debt.md#usb-switch-subtree-removal)
separate. Descriptor/collection groundwork is reusable for later gamepads;
Steam Deck lizard mode works only if its actual interfaces fit the admitted
keyboard/mouse profile. No vendor mode switching, gamepad API or Deck claim.

Implement from the [USB HID 1.11 specification](https://www.usb.org/sites/default/files/hid1_11.pdf)
and [HID Usage Tables 1.6](https://usb.org/sites/default/files/hut1_6.pdf).
The [Synaptics guide](https://ccdw.org/~cjj/l/docs/ACF126.pdf),
[Rev. B extended-mode guide](https://studylib.net/doc/18112502/synaptics-ps-2-touchpad-interfacing-guide)
and pinned [Linux v6.17 Synaptics source](https://github.com/torvalds/linux/blob/v6.17/drivers/input/mouse/synaptics.c)
identify protocol/quirk evidence; [libinput palm handling](https://wayland.freedesktop.org/libinput/doc/latest/palm-detection.html)
informs the policy, not a claim to match its full implementation. Summarize
manufacturer documents; do not vendor them. Linux is GPL-2.0-only: independently
implement documented protocol behavior, and bring any needed code reuse/fixup
licence decision to the owner rather than copying it into MPL driver code.

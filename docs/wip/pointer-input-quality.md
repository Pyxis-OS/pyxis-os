# ThinkPad pointer input quality

Plan accepted 2026-10-10; **implementation tasks require a separate owner go**.
Two device tracks can proceed in parallel when assigned. Scope:
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

Inspected main `131a9266`: USB uses boot protocol, except an exact QEMU wheel
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
`input/touchpad/scroll-direction`, enums `natural` / `traditional`. Missing or
invalid values use the accepted natural defaults, with an opt-in diagnostic.
Use the [plain store convention](../userland/machine-settings.md), shared schema
validation and archive defaults; installed values survive updates, live images
use archive configuration. Changes apply next boot. The validating writer is
still the settings milestone's separate task. Filter thresholds belong to one
authored device profile, selected from queried identity/resolution; calibration
may produce validated profile overrides in this subtree, not competing runtime
state or guessed universal pressure units.

## Tasks

1. [ ] **USB report-protocol mice and shared scroll integration.** The existing
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
   malformed reports or transport failure retain existing whole-binding
   retirement/reset (including a composite sibling), not a silent protocol
   switch; independent interface lifetime is not added.

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

## Qualification and tuning

Before either implementation, capture revisions/configuration and matched
idle/motion input and BSP/presenter costs. Repeat interleaved runs with the same
CPU/device/accelerator settings. Existing `POINTER_SYNTHETIC=1` and
`DISPLAY_CURSOR_PROBE=1` provide timestamped idle/motion windows without an
owner stopwatch. Distinguish their compose/copy totals from total guest BSP/USB
worker cost, and guest cost from QEMU emulation. They generate motion, not hardware pressure, contact count,
wheel or palms; they measure pipeline cost, not touchpad correctness.

QEMU: ordinary PS/2 and USB mice/keyboards, boot fallback, two devices holding
buttons, report-mode wheel/extra-button decoding and chained supported hubs;
inspect descriptors/layouts and source/queue state in the debugger. Do not
claim QEMU has Synaptics absolute, ASUS, horizontal or multiplier hardware
coverage. Report unexercised layouts explicitly.

Owner/luna native batch: retain identity/query/mode facts, ASUS descriptors,
selected protocol, hub path and multiplier readback. Use an opt-in bounded
contact diagnostic with automatically timed/named windows for normal fingers,
edge entry, resting palms, typing, finger transitions, physical drags and
TrackPoint use with a palm down. Summaries include pressure/width distributions,
accepted/rejected samples, resets, queue loss and software timing; omit typed
text/key identities. Raw records stay local/in the PR. Tune from these records,
then rerun the same actions with diagnostics off. The owner follows prompts and
reports behavior, not hand-timed durations. Attach/detach and normal diagnostics
stay in ktrace; avoid an unconditional packet dump or new boot klog lines.

Qualify natural and traditional directions separately for each source, held
buttons on loss/replug, small opposite wheel increments, diagonal two-finger
scroll, and unchanged keyboard/TrackPoint operation. Require native ASUS wheel
and touchpad/palm/stick evidence for device-task closure. Links/Neovim direct
scrolling is qualified after the terminal reporting follow-up, in tabs and mux;
remote input needs that follow-up's independent validation.

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

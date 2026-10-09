# Volume control proposal

Assigned by the owner **2026-10-09**, proposal only; the three defaults below
are pending. [Native full-scale tones were painfully loud](../technical-debt.md#hd-audio-volume-control).
Volume gates the later MIDI/SPC/keyboard-piano players; none is included here.

## Three owner decisions

1. **Gain/defaults — recommended: software master plus per-space attenuation,
   0–100% in 1% steps, with separate mute.** Space gain precedes summing;
   saturate the mixed result to S16, then apply master gain. Thus settled master 10%
   caps PCM magnitude at 10% of full scale even with eight sources; quieting the
   master does not recover clipping already caused by the mix. Boot master
   **muted, remembered level 10%**; each new space starts unmuted at 100%.
   Settings survive session release/reacquisition within that space, but remain
   volatile across reboot. Alternative: persistent levels, requiring trusted
   userspace restore/storage authority and an explicit startup-mute policy.
2. **Authority — recommended: trusted bar controls both user levels; existing
   playback grants cannot set either.** A producer can read its levels in STATUS
   and scale its own PCM, but cannot undo the user's attenuation/mute. No master
   grant or control command is added initially. Alternative: a separately
   delegated own-space control right and command; its holder could raise that
   user's space level, so it must not accompany ordinary playback delegation.
3. **UI/assets — recommended: the recorded widgets, hover sliders plus explicit
   keyboard control, four compiled masks, owner artwork under MPL-2.0.** Keep
   pointer-lock escape and the battery unchanged. Dedicated laptop media-key
   decoding waits for native input evidence. Alternative: include observed
   volume-key mappings in this milestone; ACPI/firmware buttons remain separate.
   Artwork's MPL-2.0 designation is proposed here, not presumed already granted;
   the owner can specify different asset terms before import.

## Mixer and state

Use bounded integer/fixed-point scaling, no floating point or boost. At unity
retain today's exact mix/clipping behavior. Converter/pin gain stays at the
existing checked 0 dB/mute/start-stop settings on both QEMU and ALC257: command
rings are stopped during playback, so live codec volume would introduce another
transport/lifetime policy. See the [engine](../devices/hda.md) and
[session interface](../interfaces/audio.md).

Gain changes take effect on newly mixed frames. Already-published PCM can remain
in the nominal **80 ms DMA ring**; software mute is not instantaneous audible
silence. Keep all refill guards, capacity, queues and ownership unchanged.
Propose a 5 ms sample-count ramp for ordinary level changes/unmute; mute emits
zeros immediately in subsequent mixing. Muted/zero-level sessions still consume
PCM, update writable readiness and finish normally; mute is not pause or release.
Zero remains zero when unmuted; moving a slider above zero explicitly unmutes
that control. Mute otherwise preserves the selected level.

The BSP audio worker owns master state and retained per-space audio-object state.
The presenter sends bounded, coalesced control intents without waiting for DMA
or codec operations; the worker applies them and publishes a locked snapshot
with a generation. Widgets show confirmed selected target/mute, not instantaneous
ramp/output amplitude; the worker remains the authority. Coalescing must preserve
relative wheel/key actions and toggle order, not lose them to stale snapshots. Per-space settings follow the space, not focus or whichever
process next acquires it. Producer STATUS keeps its existing owner checks.
No kernel home:// writes, config override or persistent restore is proposed.

## Widgets and input

The existing bar is 32 px high. Center the unscaled **24×24** master icon in a
separate slot immediately left of the battery; when no battery is present the
master retains its slot beside the right chevron. Reserve 24 px inside each tab
for its own-space icon while retaining title/layer-marker clipping and chevrons.
Recompute visible-tab widths for whole controls; at widths too narrow for a
complete widget, omit its drawing/hit region rather than scale or overlap it.
Caelum's log tab has no producer and shows a disabled space control. Absent or
failed audio disables the controls rather than suggesting playback works.
The battery's text, gradient and visibility stay unchanged.

One classic vertical slider opens on icon hover, below the bar, clamped to the
screen, with percentage and mute state. Moving between icon and popup keeps it
open; its hover region includes the full bar slot and popup connection, so it
cannot disappear while crossing their boundary. Leaving closes it unless dragging
or explicitly keyboard-focused.
Left-click the icon toggles mute; track click/drag selects a level, and wheel
changes 5%. Space icons reflect their own setting; their popup also reports
master mute. High/medium/low icons cover 67–100/34–66/1–33%; mute or zero uses
slot 3. Controls on an inactive tab adjust that space without switching focus.

Use the last presented layout for hit testing; widget/popup input is consumed
before tab selection or program input. Drag targets stay fixed until release;
resize, space switch or input loss closes/cancels the popup without retargeting.
An outside click closes and is consumed. Overlay composition follows content
and precedes the cursor, including screenshot capture, so the slider is visible
without changing any program's surface ownership.

Hover alone leaves keyboard delivery unchanged. Explicit popup focus suspends
content delivery, resets held-key state and publishes focus loss/gain while
retaining an application's existing capture. Slider click focuses that popup;
**Super+V** focuses the master popup, **Super+Shift+V** the active space popup. Arrows change
1%, Page Up/Down 5%, Home/End 0/100%, Space toggles mute, Escape closes without
reverting applied changes. **Super+M** toggles master mute as a trusted shortcut,
including under application capture. Other popup shortcuts/hover do not override
relative pointer lock: use existing Super+Escape first. Existing escape/navigation
chords retain priority; consumed keys need release before returning to a program.
The keyboard ABI/Set 2 decoder currently has no volume/mute codes. Native media
keys may be keyboard or firmware events; do not claim support without observation.

## Owner icon source

Owner-drawn `/shared/pyxis-assets/pyxis-icons.{aseprite,png}`, supplied
**2026-10-09**: 192×24, eight 24×24 slots. Inspected PNG has exactly opaque black
and magenta pixels. **Already accepted:** magenta is transparent; blit black ink
as an unscaled mask in the theme's text colour. Slots 0–3 are high/medium/low/muted;
ignore 4–7 and introduce no shared pool or battery artwork changes.

At implementation, retain the original editable Aseprite and PNG under
`assets/ui/volume/`, with an attribution/licence note crediting the owner and
recording the shared-folder source. Decision 3 proposes **MPL-2.0** for these
original assets; update [licensing](../../LICENSING.md) to cover them explicitly
once accepted. A bounded host conversion generates only four 24×24 bit masks
for kernel drawing; no runtime PNG/Aseprite parser, Aseprite build dependency,
network fetch or new image library. Source hashes:

- PNG: `9f63995b47ef08342e4fdd377ee22b791417c78696a465adf316f6bc14909b47`.
- Aseprite: `6c6d103732c0d36351e70921ac830ddfc859f3b9c2e4f7cdb95a0319ba76b0d7`.

## Delivery and qualification after acceptance

1. [ ] Kernel worker/mixer state, user authority and read-only STATUS information;
   gain/mute continuity across session lifetime. Keep numeric call layouts with
   the implementation and update SDK/libpyxis consumers together.
2. [ ] Import licensed owner assets, four-mask conversion and bar controls;
   route only these UI events, preserving content focus/lock and battery behavior.
3. [ ] Capture baseline before performance changes, then matched QEMU WAV at
   0/1/10/25/50/100%, combined master/space steps, ramps and exact mute after the
   ring drains. Check unity match, clipping/peak bound, two-space isolation,
   hidden playback, reacquisition, eight sources, readiness and repeated UI use.
   Measure presenter/idle and mixer/refill cost at identical CPU/device settings;
   nested QEMU is not native performance. No new test/CI or boot automation.
4. [ ] Owner native speaker/headphone listening: boot muted, unmute at 1%, then
   small comfortable increases with brief tones; verify both controls, mute delay,
   changes without pops/dropouts, hidden-space isolation and warm/cold defaults.
   Do not repeat painfully loud full-scale speaker tests. Observe media-key input
   only when the ThinkPad is available. Fold completed behavior into references;
   record any residual latency, persistence or input limits as short debt.

Docs only here: no code, binary asset import, dependency pin or measurement changes.
Implementation requires acceptance and a separate owner assignment.

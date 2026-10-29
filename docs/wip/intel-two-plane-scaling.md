# Intel game plane and native-resolution UI — proposal

Status: proposal only, based on `474e2d7043b5` (0.0.10). No implementation is
started or authorized by this document. Chronium assigned the proposal on
2026-10-29; main remains frozen until the release uploads finish.

## Goal and already-decided behaviour

A game chooses its render resolution. On qualified vela hardware, a lower
hardware plane scales that image while a higher, native-resolution alpha plane
carries Pyxis navigation, pointer and overlays. Both changed planes latch in the
same vblank; the inherited mode, clocks and link remain unchanged. This is display
composition, not GPU rendering acceleration or the later 120 Hz modesetting work.

**Decided (chronium, 2026-10-29):** the default fills the destination uniformly,
with filtering for non-integer scaling. Never independently fit X and Y or
stretch the aspect ratio. Integer nearest-neighbour scaling with centred borders
is a per-program bundle option, not a space toggle. Put destination-aspect modes
first. Keep the tab bar. The destination choice below is still proposed.

## Current evidence and the missing work

At the release pins, [`DISPLAY`](../../include/abi/display.h) allocates three
source slots at the space's content size. `kernel/space.c` defines navigation as
32 pixels: vela's 2880x1800 scanout therefore has a **2880x1768** content area at
`(0,32)`. This is a current value to query, not a new permanent 32-pixel contract.
SDL2 advertises only that one mode. Its GL path accepts a smaller canvas but
CPU-scales it into native-size slots; the software path forces native dimensions.

The matched display30 profile has two scalers. Fedora exposes one primary,
four overlays and a cursor per pipe; linear XRGB/ARGB8888 and nearest filtering
are exposed. These observations do not prove spare firmware DDB allocations.
Linux's applicable scaled RGB extent limits are the intersection of plane and
scaler constraints, not a hard-coded list of game sizes. Its ratio limit below
3 is a **downscale** limit, not a three-times upscale ceiling. Nearest filtering
requires coefficient programming, not just a flag.

The existing backend qualifies only primary-plane flips and initial GOP return.
It does not yet qualify second-plane enable, DDB/DBUF allocation, watermarks,
inherited CDCLK/MBUS/memory-power conditions, scaler state, paired completion or
general restoration. DC0 and PSR-off alone do not establish those conditions.

## Round 1: three decisions, with recommended defaults

### 1. Destination: below navigation, with no covered game pixels

**Default:** use the content rectangle below the always-visible tab bar. Tell
native clients and SDL the destination dimensions and generation. At today's
vela geometry, exact-aspect modes include **2880x1768, 1440x884, 720x442 and
360x221**. Advertise these first, then useful conventional game resolutions;
the list is advisory, not the only permitted source dimensions. A game still
selects its mode through its normal video menu/API.

Default fitting uses one factor `s = min(destination_w/source_w,
destination_h/source_h)`, centred, with no crop. A different-aspect source gets
borders rather than distortion. The integer option uses `floor(s)` and nearest
sampling, with borders wherever needed; if not even 1x fits, refuse that source
request and preserve the old session instead of silently changing policy.
Pixel/subpixel rounding and the hardware's representable phase/rectangle rules
must be settled before scaler/API code; this does not authorize separate X/Y
fit factors or a hidden crop. Exact content-aspect modes avoid that ambiguity.

Alternative: scale against the full 2880x1800 panel and put the bar over it.
That hides part of the game and requires an occlusion/safe-area contract. Do not
auto-hide the bar or quietly call the panel size the usable game area.

### 2. One source-resolution contract, including the software fallback

**Default:** extend native DISPLAY acquisition/replacement to request exact source
width/height and a scaling preference. `(0,0)` retains the ordinary native-size
client case. Returned slots describe source pixels; SIZE describes the physical
content destination. Return/query the accepted source-to-destination transform.
Retain destination generations, transactional replacement, whole-frame SUBMIT
and current layer-selection rules. REPLACE prepares the new source/policy but
keeps the old frame and its transform visible until a new SUBMIT is presented.
Fitting, pointer geometry and capture follow the shown frame, not the latest
mapping metadata; transform/identity publication must be settled before code.
No physical modesetting right is added: space-local DRAW remains the authority.

Use one native/libpyxis mode derivation and transform for SDL software and GL.
SDL enumerates logical source modes and implements mode selection; presentation
copies source-sized rows without its old native-size scaling pass. Destination
changes refresh the list/transform without rewriting an explicitly chosen source.
Non-Intel or unqualified Intel hardware uses the ordinary single-plane compositor
with the same fitting policy; mode selection must not falsely imply acceleration.
At acquisition/replacement, explicit source storage must fit the page-rounded
native content byte capacity of that destination generation. This preserves the
existing per-slot memory envelope while allowing other dimensions/aspects. LIMIT
reports an oversized request and NO_MEMORY an allocation failure, preserving the
old session. A later destination resize does not invalidate existing source slots.
Ordinary native-size acquisition/replacement still follows the current size.

Driver scanout reservation is a separate, fixed boot capacity (decision 3).
Hardware promotion also requires that capacity and the hardware checks to fit;
otherwise compose in software, including after a supported destination resize.
Do not shrink or enlarge the reservation implicitly, conflate the two capacities,
or silently substitute another source. The mode list does not promise acceleration.

Proposed bundle preferences, separate from grants:

```json
"display": {
  "scaling": "integer-nearest",
  "initial_resolution": {"width": 720, "height": 442}
}
```

Omission means uniform fitted fullscreen and the game's normal initial choice.
The optional initial resolution is a preference for native/SDL initialization;
a later explicit game choice wins. It is not a universal force override for an
engine with its own renderer/configuration. No game-specific argument rewriting.
Trusted launchers carry validated preferences as private TEXT startup values
using existing launch entries, replacing inherited companions for each bundle.
These grant no authority and are not implicitly forwarded to other programs.

Alternative: keep source negotiation/CPU scaling separately in SDL. That leaves
native clients and other libraries with different modes, geometry and policies.
The recommendation is the shared native contract, not another SDL-only path.

There is no arbitrary native/2 cap: the byte capacity, fitting policy and
applicable format/extent checks govern admission. Exact request layouts,
preference names and raster rounding are task-local interface details to settle
before code. The ABI changes require rebuilding the SDK, affected native clients
and SDL consumers for the next development ABI; no compiler-container rebuild
follows.

### 3. Kernel-owned two-plane presentation, with bounded inherited ownership

**Default:** automatically promote only the selected space's visible graphics
session when the matched Intel configuration qualifies; no new application plane
grant, space toggle or wrapper. Use primary plane1 for opaque XRGB game pixels,
plane2 above it for native premultiplied ARGB UI. Keep kernel-owned game scanout
buffers and copy only source-resolution pixels into them. Keep native UI buffers
separate. Do not directly scan writable user slots in this slice.

Provision two game scanout buffers, each with one native content buffer's byte
capacity, before AP startup; reuse the existing full-panel owned backs for UI.
Retain the new buffers/mappings for the boot, just as the current flip backing.
This costs roughly two content buffers (about39 MiB on today's vela), even while
no game runs. Their capacity is fixed by boot geometry, not later SIZE replies. A smaller source changes layout/stride within that capacity and
copies only source pixels; it does not incur native-size per-frame CPU scaling.
The current GGTT/PAT writer is explicitly bootstrap-only: do not call it after
AP startup or silently introduce runtime GGTT mutation for a mode change.
Dynamic backing and larger sources can follow with explicit runtime ownership.

A frame reference prevents freeing but does not prevent SUBMIT from returning
that slot writable. Direct user-slot scanout would need a different unavailable-
until-retired state and is unnecessary for this initial goal. Driver-owned copies
preserve today's application slot lifetime while removing native-size CPU scaling.

Start with read-only inventory of the missing DDB allocations, slice/fuse state,
latencies, CDCLK/MBUS/memory-power inputs, plane/scaler controls and completion
sources. Then use the pinned Linux calculations and update ordering for this
profile's linear8888 pair. Every source/layout change needs a new validated
fetch/scaler/watermark plan before arming. Program only qualified state, retaining
the inherited clocks, mode, link, DC0 and PSR-off contract.
If safe operation requires a new clock, memory-power/firmware policy or wider
ownership, report that decision before writes; do not assume the existing holds
prove it. Unknown/insufficient resources keep software composition.

The alternatives are direct user-slot scanout or taking full display/clock
ownership first. Both expand the present lifetime or hardware scope. The default
is owned low-resolution copies and bounded inherited-mode admission.

## Presentation, input and recovery requirements

- The kernel presenter owns planes. First SUBMIT makes graphics eligible;
  subsequent submissions do not steal focus or undo Super+Down. Hidden games
  continue running as today but have no displayed game plane. Switching spaces,
  showing TTY, RELEASE and exit remove the old image from presentation.
- Native UI needs explicit alpha authoring: transparent over the game, opaque
  navigation/borders/TTY/modal backgrounds, correctly premultiplied UI/pointer
  pixels. Existing RGBX words have unused bits zero; merely selecting ARGB would
  make the whole UI transparent. No public game-alpha format is required.
- Pair game/UI updates under one transaction and a reference-driver safe update
  window; unchanged planes retain their confirmed frame references. Individual plane writes are not automatically atomic. Configure scaler
  before enabling its plane; follow CTL/SURF arm ordering and DDB transition rules.
  Retain each old buffer until its changed plane's correlated completion/live
  evidence and the common frame association confirm retirement. Plane1's existing
  flip-counter selection cannot stand in for plane2 completion.
- Navigation, pointer, volume UI and graphics/TTY switching stay native resolution.
  Full-screen power/picker overlays and idle blanking must cover or disable the
  game as part of the same confirmed update, including transparent areas.
- Proposed input path: hit testing, source-coordinate events and warp use the
  kernel's shown-frame transform once. Borders/navigation do not begin ordinary
  game input; anchored drags still receive out-of-bounds motion/releases as today.
  Preserve raw relative counts and focus/reset rules. Settle transform identities
  and event publication with the API task, then remove SDL's duplicate GL mapping.
- CAPTURE must still show the game and native UI from a coherent frame. Today's
  software-copy tee would capture only UI after this change. Before capture code,
  decide its exact fidelity path: a recommended candidate is one confirmed opaque
  software-composed UI frame during capture, then return to the pair. Never claim
  a software approximation is bit-identical to an unobserved hardware-filtered
  frame, or return an incomplete UI-only image as success.
- Before programming, retain the original GOP/plane/scaler/DDB/watermark state.
  Admission failures before effects keep the current single-plane backend.
  A normal return must stop/retire pending updates, restore the GOP on plane1,
  disable plane2/detach the scaler with Linux's ordered allocation transition,
  and confirm restoration before retiring old frame leases. Boot-owned backing
  stays reserved for reuse until reboot; this adds no GGTT reclamation. Restore
  only owned changes. A post-write uncertain outcome fences register writes and
  retains possible references until reboot; blind GOP-copy fallback is unsafe.
  Panic must retain an independently safe route rather than depend on a game.

The exact allocation transition, whether a temporary black frame is necessary,
capture fidelity, exact raster rounding and integer-policy behaviour on a later
physical shrink are task-local decisions,
not silently accepted defaults. Raise them when the inventory/API work establishes
what is required, before implementing the affected step.

## Small task sequence and qualification

1. Read-only resource/completion inventory on vela; report whether inherited
   clocks/memory-power state can support the pair. No new plane/scaler writes.
2. Calculate and validate DDB/watermarks/scaler/format state, then qualify static
   two-plane composition and reversible GOP restoration. Settle transition policy
   from that evidence before enable code; no blanket Linux modesetting import.
3. Add common source selection/fitting and manifest preferences, then native/SDL
   adapters. Settle rounding/input details first. Keep ordinary clients,
   software rendering and GL on the same source/destination contract.
4. Integrate paired flips, UI/space selection, overlays, blanking, capture and
   teardown. Qualify source replacement, exit during presentation, both planes'
   retirement and honest refusal. A native underrun/ownership error stops further
   attempts for analysis; no automatic retry hides it.

QEMU checks the ABI, geometry/policy, SDL modes, software fallback, lifetime and
space/input/capture paths. It cannot qualify Intel fetch timing or DDB hardware.
Native vela packets are hands-free through the orchestrator: record mode and
source/destination, filter, backend, DDB/watermarks, underruns and correlated
completion; compare the existing terminal and1803-frame OpenArena workloads at
matching render sizes. Keep the inherited approximately60Hz mode. No timing or
FPS gain is promised before the matched native measurements.

## References and evidence

Pinned Linux: [`05f7e89ab973`](https://github.com/torvalds/linux/tree/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display).
Relevant sources: `intel_display_device.c` (display30 profile/fuses),
[`skl_scaler.c`](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/skl_scaler.c)
(limits/filters), `skl_universal_plane.c` (alpha/arm),
[`skl_watermark.c`](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/skl_watermark.c)
and `intel_dbuf_bw.c` (allocation/latency/CDCLK), `intel_display.c` and
`intel_crtc.c` (grouped update window).

Existing [inherited scanout](intel-inherited-scanout.md) remains the qualified
single-plane contract. Local native evidence: `/shared/present/vela-tearing/`:
`results/vela-planes.txt`, `alias-native-20261028/results-vela/`, and
`ggtt-deadlines-native-20261029/results-vela-deadlines/`. Source pins inspected:
Pyxis474e2d704, userland6b1cac9e, ports90ed9e60. No new native run was performed.

## You should know

This proposal changes how games request a source size and how captures/input
relate to scaled content, even when hardware promotion falls back. The recommended
boot-owned game buffers cost about39 MiB until reboot and bound hardware
promotion. Explicit source requests exceeding the current destination's native
byte budget would return LIMIT without changing the session; a fixed hardware
reservation mismatch instead selects software composition. It does not
add application GPU/plane authority, direct user-buffer scanout or 120Hz ownership.
DDB/watermark qualification and confirmed restoration are gates, not established
facts from the existing tear-free primary-plane result. Only this proposal is
being delivered; implementation waits for the decisions.

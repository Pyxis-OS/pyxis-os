# Renoir hardware cursor

**Design accepted 2026-10-10. Task 1 complete; native software baseline recorded.**
The owner merged #665 and assigned the read-only inventory/software baseline.
Hardware cursor support remains unimplemented; tasks 2–3 need separate go-aheads.
Scaled game planes remain a separate later decision. This proposal builds on the [Renoir flip backend](../kernel/renoir-flip.md),
the [pointer contract](../interfaces/pointer.md) and the existing
[VirtIO cursor path](../kernel/display.md#hardware-pointer).

## Scope and current behavior

The first cursor backend would be opt-in and require the qualified, opt-in
Renoir flip backend, its unique mono GOP route, unchanged inherited unity scaler,
and accepted UMA exclusion proof. Boot, Bochs, VirtIO and refused Renoir setups
keep their current paths. No program gets register or surface-address authority;
input focus, lock, Super+Esc, custom cursor ownership and geometry stay unchanged.

Today Renoir frames include the software pointer through cursor row composition
and `pointer_present_copy`. The BSP presenter already recomposes periodically;
a mouse report does not separately submit a frame. Hardware support should
remove pointer pixels from ordinary base-frame composition and service changed
cursor state independently, including between pending-flip input drains. It
must add no base compose, copy or flip for cursor-only movement. This does not
promise to eliminate the existing periodic full-frame cadence; content-driven
frame scheduling would be a separate task. The task-1 native record measures the existing software baseline; no hardware
cursor improvement is claimed.

## Linux sequence and accepted write set

Reference: AMD DC in Linux v6.19.10, pinned commit
`271f8eab9590b57a2ff0c8c9eee357723c4a85cb`, the same source as the flip backend.
The [DCN2.1 HUBP function table](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c)
selects `hubp2_cursor_set_attributes` and **`hubp1_cursor_set_position`**.
The [attribute helper](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn20/dcn20_hubp.c)
writes image address high then low, actual width/height, pitch/mode/chunk control
and request scheduling. The [position helper](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c)
writes enable, position, hotspot and destination fetch offset.

The [DPP helpers](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/dpp/dcn20/dcn20_dpp.c)
set cursor mode, expansion and degamma; the inherited
[DPP position helper](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/dpp/dcn10/dcn10_dpp.c)
sets its cursor enable. The [stream update](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/core/dc_stream.c)
brackets HUBP/DPP programming with a per-OPP cursor lock. The
[hardware sequencer](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hwss/dcn10/dcn10_hwseq.c)
contains a VUPDATE keepout workaround; the
[MPC helper](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/mpc/dcn10/dcn10_mpc.c)
uses `CUR_VUPDATE_LOCK_SET`, not a surface flip lock or an OTG blank operation.
The proposed implementation retains these requirements rather than assuming
that address/position writes alone are sufficient.

Accepted allowlist additions for **only the verified route**:

| Block/register | Writable fields/purpose | Instance 0 DWORD offset |
| --- | --- | --- |
| HUBP `CURSOR0_n_CURSOR_CONTROL` | enable, color mode, pitch, lines/chunk; magnification off | `0x0678` |
| HUBP `CURSOR_SURFACE_ADDRESS_HIGH` / `ADDRESS` | owned image address, high before low | `0x067a` / `0x0679` |
| HUBP `CURSOR_SIZE` | actual width and height | `0x067b` |
| HUBP `CURSOR_POSITION` / `HOT_SPOT` | position and integer hotspot | `0x067c` / `0x067d` |
| HUBP `CURSOR_DST_OFFSET` | clock-derived fetch offset | `0x067f` |
| HUBPREQ `CURSOR_SETTINGS` | DST_Y_OFFSET=0, CHUNK_HDL_ADJUST=3 | `0x065e` |
| DPP `CNVC_CURn_CURSOR0_CONTROL` | enable, matching mode, expansion=0, degamma ROM off | `0x0ce0` |
| MPC `CUR_VUPDATE_LOCK_SETn` | active OPP cursor-update lock set/release | `0x1361` |

Offsets come from the [DCN2.1 definitions](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h);
BASE_IDX 2 starts at DWORD `0x34c0`, so BAR bytes are `(0x34c0 + offset) * 4`.
Instance 0 values illustrate the table; select HUBP, DPP and OPP from routing,
using their own instance tables, never a shared assumed stride or pipe number.
Use named field masks and preserve unrelated fields, including addressing and
security configuration. No cursor memory-power, clock, stereo, color matrix,
OTG blank, surface mode, VM, PCI, firmware or interrupt writes are proposed.
If the cursor block needs powering up or its address domain is unsupported,
refuse hardware support and return the additional authority question to the owner.

Each changed cursor transaction needs full device, route, inherited layout,
owned scanout set and expected cursor configuration validation immediately
before any MMIO write. The existing six-read pending-flip observation is no
cursor-write authority. The sole BSP display owner and its direct-writer/panic
claim bracket the transaction; no independent GPU claimant or AP writer.
Validation covers immutable cursor fields separately from fields the driver
owns and deliberately updates; it does not mask unexplained live changes.

## Image storage, format and coordinates

Accepted storage: one additional **64 KiB-aligned, 64 KiB UMA reservation**
under the same [Linux-derived exclusion standard](../kernel/renoir-flip.md#surfaces-and-memory),
with two 16 KiB image slots and remaining padding reserved. For the captured
layout the first candidate is offset `0x10f0000`, immediately after the rounded
spare scanout extent. Recompute it from owned extents, alignment and every boot
reservation; this is a candidate, not an unconditional allocation. Pin it outside
the PMM and map it once WC, with no WB/WC alias. CPU and GPU translations use the
already verified direct UMA path. Exhaustion/refusal retains software rendering.

The public image remains straight-alpha BGRA8, dimensions 1–64 and an in-image
hotspot. Internally convert to little-endian ARGB8888 (BGRA bytes), premultiplied
`channel = (channel * alpha + 127) / 255`, retaining alpha and zeroing transparent
RGB/padding. Use fixed 64-pixel pitch (256 bytes/row), actual image dimensions,
no magnification or degamma. Linux [Renoir capabilities](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/resource/dcn21/dcn21_resource.c)
allow larger cursors, but this proposal does not enlarge Pyxis's 64x64 contract.
Defaults and every accepted program-supplied image use the same path. Only image
changes upload pixels; movement changes coordinates without another image copy.

Copy from an immutable pointer image lease into a free driver slot, then fence
WC stores before address programming. Never overwrite or release an image that
may still be fetched. Two slots allow one current and one prepared/pending image;
coalesce newer requests while both are busy. Register readback alone does not
prove old image retirement. Task 1 must establish a source-backed latch/retirement
predicate from the MPC cursor pending/taken/ack state and native reads; if the
available evidence cannot prove it, report the blocker before enabling writes.
No guessed delay becomes an accepted retirement rule.

Use the pointer snapshot's **physical screen pixel coordinates**, with one
screen-space translation from graphics/terminal ownership. The current route's
viewport, RECOUT and MPC rectangles match, its H/V ratios are unity, and rotation
and mirroring are excluded. Thus cursor pixels and hotspots stay 1:1 at the panel,
including navigation/margins and all four edge clips. Preserve the scaler state
rather than programming it or borrowing future scaled-plane coordinates.

Linux computes CURSOR_DST_X_OFFSET using the DCHUB reference clock, pixel clock
and H scale ratio. Its lock workaround uses timing/VUPDATE fields. Task 1 must
identify read-only, firmware-backed clock/timing evidence and validate it against
the current pipe. The earlier Linux 138.700 MHz panel measurement is a reference,
not permission to hard-code a clock. Missing or ambiguous inputs refuse the
cursor; do not restore the failed counter-derived period/beam-racing authority.

## Presenter, capture and failure

Mirror VirtIO's separation of image preparation, position/visibility updates and
capture-only software overlay, while keeping hardware-specific logic in the AMD
arch driver. The BSP presenter services the latest pointer after input draining,
including during an ordinary pending flip, without entering base-frame composition
or retiring its fronts. Unchanged cursor state does no transaction. Coalesce
motion to the latest position; service cadence is bounded by the presenter/input
loop and hardware latching, not a promise of one visible update per PS/2 packet.
Full cursor validation cost is included in qualification.

Software blending stays active on unsupported paths. Once the hardware cursor is
active, ordinary scanout surfaces stay cursor-free. Transition from software to
hardware only at a confirmed cursor-free base frame, avoiding a baked old cursor.
Freeze the frame's hardware/software choice throughout composition. Pointer lock
and saved hidden state both disable the hardware cursor; routing, unlock and
relock rules remain the current pointer contract.

For CAPTURE retain one leased pointer snapshot with the selected base frame.
Blend that snapshot into capture storage only, using the existing capture tee;
it never returns to scanout. Defer newer visual cursor transactions through that
capture's base confirmation and matching cursor latch proof, while input keeps
routing and the latest visual state is coalesced. Publish only after both succeed;
otherwise publish no FILE. Shape retirement and capture latch proof are distinct
requirements. This is a presented software snapshot, not atomic photon-time
capture of two independently updated planes.

| Condition | Accepted behavior |
| --- | --- |
| Unsupported/invalid preparation before activation | Existing software composition; no cursor writes or allocation from unproven memory. |
| Cursor-only failure with route/owned set still fully provable | Bounded guarded disable, prove it latched, then fresh software composition. Abort any unmatched capture and retain backing until fetch retirement is proven. |
| Mode/layout/device loss or uncertain cursor disable | Stop cursor MMIO and buffer mutation; pin possibly fetched images, mark display unavailable and reject capture. A potentially live hardware cursor cannot safely be covered by a second software cursor. No repair/reprobe. |
| Flip timeout/FALLBACK | Never resume flips. Attempt only the separately authorized, fully validated cursor-disable transition. Reject the affected pending presentation/capture, retain both surfaces, then compose a fresh software-cursor frame before validated dual-copy fallback. No continued hardware motion in FALLBACK. |
| FAILED | No further cursor or flip writes, no speculative disable, pinned backing, existing unavailable behavior. |
| Panic | Preserve no GPU commands; paint possible base fronts under existing panic ownership. Pin cursor backing; a still-enabled hardware cursor may overlay panic pixels. |

The owner accepted the single guarded disable-only timeout exception on
2026-10-10: today's FALLBACK otherwise stops GPU writes. An unsuccessful disable
or missing latch proof means unavailable,
not a silently doubled or stale software fallback. In particular, today's
`finish_flip(FALLBACK)` immediately copies retained staging; hardware-mode staging
is cursor-free. It must not count as successful software fallback after disable
or publish its capture. Fresh software composition is required. Resize/mode change never
reuses old geometry/addresses; first slice supports only the inherited fixed
mode, and boot/refusal keeps current software behavior.

## Accepted owner decisions — 2026-10-10

1. **Register authority and gate. Accepted:** opt-in cursor support
   only with the qualified mono flip backend; extend its write allowlist by the
   exact HUBP/HUBPREQ, DPP and MPC cursor fields above. Full validation and the
   writer/panic claim guard every transaction. No power/clock/firmware changes.
2. **Image contract and memory. Accepted:** retain 1–64 straight BGRA8
   publicly; convert to premultiplied ARGB8888 internally at fixed 64-pixel pitch.
   Reserve one revalidated 64 KiB UMA region with two immutable-in-use slots;
   hardware support waits for proven image retirement and clock/latch evidence.
3. **Independence, capture and failure. Accepted:** cursor updates may
   run during write-free pending flips with their own full validation; capture
   freezes its visual snapshot until base and cursor confirmation. Permit one
   guarded disable-only exception on timeout before software fallback; FAILED or
   lost ownership performs no GPU writes and pins backing. Default boot stays
   unchanged until native qualification and a separate default-enablement decision.

All three defaults are accepted. Following #665 merge, the owner assigned task 1
on 2026-10-10. Tasks 2–3 still require separate assignments.

## Tasks and owner outcomes

- [x] **1. Read-only cursor prerequisites and baseline.** Authorized and completed
  2026-10-10. `DISPLAY_CURSOR_PROBE=1` implements the bounded register/clock
  inventory and software counters, without cursor allocation or new GPU writes.
  The [native record](../development/experiments/renoir-cursor-inventory/README.md#native-result--2026-10-10)
  contains the owner-run ThinkPad evidence at `d81c731a`: stable disabled cursors,
  checked UMA candidate, and two interior motion/idle pairs at 59.52 frames/s.
  Idle still composes/copies every frame at 1.188–1.228/0.940–0.953 ms per frame.
  Linux's setters and idle native bits do not prove transaction-correlated cursor
  latch/disable/old-fetch retirement; that gap blocks task 2's writes.
  **What the owner can do after this task:** compare future work against the
  measured software control and review the remaining cursor prerequisites.
  The owner queued whole-frame skipping before cursor task 2; it is a separate
  presenter task, with decisions required before implementation.
- [ ] **2. Bounded cursor backend and presenter integration.** Separately assigned:
  implement the accepted allowlist, owned images, independent updates,
  lock/hidden behavior, capture and proven fallback transitions; vendor only the
  needed pinned AMD DC subset with complete MIT notices and LICENSING entry.
  QEMU checks unavailable/refusal and existing boot/Bochs/VirtIO paths. **Owner
  outcome:** opt-in native hardware pointer motion and custom images without
  cursor baking or extra base-frame work, subject to native qualification.
- [ ] **3. Matched qualification and reference.** Separately assigned: owner-run
  interleaved software/hardware boots on AC with identical consumers/options,
  using Luna staging and masked UDP logs. Compare the cursor-only baseline,
  full-validation/update costs, capture and default/custom translucency, hotspots,
  clipping, hide/show, local TTY/mux/space/layer selection, Quake lock/unlock,
  native Quake and 72 Hz Chocolate Quake responsiveness and tearing. Keep base
  flip/poll costs distinct; an idle frame count alone cannot attribute cursor
  work. No forced timeout/panic exercise without owner assignment. **Owner
  outcome:** a measured cursor-cost result and qualified scope, with remaining
  limits in debt and implemented behavior in the display/flip references.

The native software baseline is recorded; hardware-cursor writes and
qualification have not been performed for this track. No SDL2/pointer ABI, input-device track or scaled-plane work
is included.

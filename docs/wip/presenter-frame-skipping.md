# Presenter whole-frame skipping

Owner-authorized 2026-10-10 after #670. Implemented on the task branch;
QEMU qualification is complete; native interleaved qualification awaits
Luna staging. Renoir hardware cursor task 2 remains separately unassigned.

## Accepted defaults — 2026-10-10

- Boot framebuffer, Bochs, VirtIO and qualified Renoir share one skip rule.
  Every visible change forces a complete frame; no dirty rectangles.
- Preserve existing visuals: steady TTY caret, no new bar clock or blink timer.
  Existing caret, title, layer marker, battery and volume changes invalidate.
- Retain approximately 16.7 ms input/device service and pacing while skipping
  composition, copy and submission. Capture forces a full frame. Preserve the
  independent power overlay, panic ownership and three-slot application handoff.

## Implemented contract

TTY raster/caret/selection and application submissions have visual generations
under their existing output-lock/BSP owners. Graphics margin background has a
separate generation. Navigation tracks registry/title/layer/focus and viewport
changes plus the battery's drawn presence/percentage. Volume tracks confirmed
audio and popup changes; power tracks its independent state/selection/status.
Pointer snapshots track effective visible position, immutable shape and hotspot;
hidden movement does not invalidate pixels. One comparison image lease prevents
freed-address reuse from making a new image look unchanged.

The presenter compares the visible input generations before allocating a frame
lease or drawing navigation. Geometry change, first frame, failed previous frame
or pending capture forces full composition. Hidden graphics submissions and
hidden TTY output do not dirty the selected layer; layer markers still update.
Only captured generations are acknowledged after successful completion. Changes
arriving during composition or a pending flip remain pending for a later frame.
TTY output remains row-wise, preserving its existing concurrency contract.

Skipped iterations continue draining input and checking resize. VirtIO polls
cursor completion/deadline and transport health without submitting a new frame.
Idle Renoir ticks perform full device/route/layout/front ownership validation;
changed frames retain their existing pre-copy/submit and completion proofs,
without an additional full read. The previous flip-completion pacing marker is
reset every service tick. Pending flips retain 1 ms sleeps, their existing
50 ms/poll bound, input drains, pinned failure surfaces and capture requirements.

Power overlay update, generation sampling and rendering bypass TTY/output locks
and normal-content dirty state. No skipped frame consumes application pending
slots, writes fronts, issues flips or publishes captures. Panic retains the
existing direct-writer arbitration and paints its possible fronts independently.

Opt-in `display.cursor.probe=1` now reports every 120 service ticks, including
idle skips. Frame/composition/copy totals retain their meanings; service/skip
counts distinguish presenter activity from actual pixel work. Default logging
adds no lines. There is no new ABI, write allowlist or input authority.

## Qualification and owner outcomes

- [x] Capture current-main baseline before code (`4b6550a6`), source image and
  QEMU idle/motion/idle/motion counters, preserving the artifacts.
- [x] Implement generations, whole-frame skip and independent device service.
- [x] Matched QEMU A–B–A–B plus boot/Bochs/VirtIO transition/capture checks
  and ordinary default build; [record](../development/experiments/presenter-frame-skipping/README.md).
- [ ] Exact-head CI and implementation review.
- [ ] Owner-run native interleaved control/change boots via Luna, using the
  existing flip-on metrics entry; review responsiveness, tearing and counters.

After implementation review the owner can stage the candidate and compare its
idle work against the control. Native performance is not inferred from QEMU.
[Dirty regions](../technical-debt.md#presenter-dirty-regions) remain later work,
after measuring the work left by whole-frame skipping.

# Presenter whole-frame skipping

Boot framebuffer, Bochs, VirtIO and qualified Renoir share one whole-frame skip
rule. Every visible change forces a complete frame; unchanged inputs skip
composition, scanout copy and submission. Input/device service remains at
approximately 60 Hz. Capture always forces a frame. This preserves existing
visuals: no new bar clock or caret blink timer is added.

## Change tracking

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

## Qualification and limits

[The qualification record](../development/experiments/presenter-frame-skipping/README.md)
contains baseline revisions, matched QEMU runs and the owner-run ThinkPad native
A1–B1–A2–B2. Native idle pixel work falls about 99%; continuous software-pointer
motion still requires full frames. The short native functional pass covers
idle capture, typing, pointer, selection, tabs, power-overlay cancel, volume,
Quake and Chocolate Quake. The latter's mouse capture/unlock issue is routed
separately and is not fixed here.

These compose/copy elapsed measurements are not total BSP CPU profiling.
Runtime host resize and forced timeout/panic/wedged-writer paths are not newly
qualified. [Dirty regions](../technical-debt.md#presenter-dirty-regions) and
Renoir hardware cursor writes remain separate work; no new ABI or GPU writer
allowlist is introduced.

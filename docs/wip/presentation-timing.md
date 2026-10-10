# Presentation timing and pacing

Status: **steps 1 and 2 are implemented; step 3, program timing and pacing, is
unassigned.** The original investigation (2026-10-09, Pyxis `f4c5cdbc`) followed
the owner's report of a tear line with ordinary Quake at about 72 fps on the
ThinkPad's 60 Hz panel. Its three decisions were accepted on 2026-10-09: software
staging first, a read-only Renoir probe next with page flips deferred to a separate
contract, and optional low-latency pacing with truthful timing capability and no
unconditional 60 Hz cap or pretend SDL vsync.

## What is done

- **RAM staging for boot and Bochs:** merged in #610; one WB staging frame is
  composed, then copied whole to the front surface. VirtIO already composes into
  its attached backing. See the
  [measurements](../development/experiments/presentation-staging/README.md).
- **Completed-frame handoff:** merged in #618: three slots per session, newest
  pending frame wins, SUBMIT never waits, continuous sampling retired. The contract
  is in [graphics](../interfaces/graphics.md#slots-and-frame-handoff), the
  [measurements](../development/experiments/frame-handoff/README.md) beside it.
- **Read-only Renoir timing:** the observer is documented in the
  [display reference](../kernel/display.md#read-only-renoir-firmware-timing) with
  its [qualification record](../development/experiments/renoir-presentation/README.md)
  and the [Fedora reference](../development/experiments/renoir-linux-timing/README.md).
  It failed native qualification on 2026-10-09, with period estimates from 13.887
  to 20.785 ms and worse tearing and input delay in blank-copy mode, so timed
  copies stay off and the counter-derived period is not an authority. See the
  [technical debt](../technical-debt.md#native-renoir-presentation-qualification).
- **Page flips:** the opt-in [Renoir flip backend](../kernel/renoir-flip.md) is
  natively qualified for normal play and replaces beam-racing on that hardware.

## What each backend can promise

- **VirtIO/QEMU:** the specification offers transfer, flush and resource selection,
  not vblank or a vsync-latched flip. Completion is command completion, never a
  monitor timestamp, and host tearing depends on the frontend and compositor.
- **Bochs:** DISPI offset panning can select VRAM pages, but it is not a
  retrace-latched flip, and VGA status polling establishes no vblank.
- **ThinkPad GOP:** the framebuffer descriptor carries memory, format and geometry,
  not vertical timing. Only the Renoir flip backend adds a synchronized flip.

Neither RAM double buffering nor a 60 Hz sleep alone promises tear-free output.
Software cadence, command completion and verified hardware timing must stay
distinct in anything reported to programs.

## Step 3: program timing and pacing

Not started and not authorized; this is the contract the accepted direction implies.

- **Observation contract.** Timing should reach programs through a native display
  observation and wait: a geometry generation, a frame sequence, a monotonic
  timestamp and period, and whether the source is software cadence, command
  completion or verified hardware timing. Buffer retirement and display timing are
  different events. No SDL vsync claim without supported timing, and no timer cap
  presented as vsync.
- **Pacing.** Quake could pace to display timing instead of its unrelated 72 Hz
  sleep while keeping timedemo uncapped. Pending frames stay bounded, and nothing
  queues without limit, waits under output locks or with IF=0, or changes
  hidden-space, focus or input semantics.
- **Cost reference.** One 1920x1080x4 frame is 8,294,400 bytes (7.91 MiB), about
  498 MB/s of destination writes at 60 Hz before source reads. Damage-only copies
  would write a fraction but need damage tracking for navigation, cursor old and
  new positions, caret and every source; moving Quake changes most pixels. That
  tracking is later work, after correctness and measurement.
- **Before and after.** Same ThinkPad mode, AC power, workload and revision;
  record composition and copy P50/P95/max, missed intervals, application cadence
  and input latency, idle TTY and cursor apart from moving Quake and from
  timedemo. Camera clips or owner observation decide visible tearing; FPS and
  screenshots alone cannot.

Open items outside step 3: the cheaper [flip polling](renoir-flip-polling.md)
follow-up and the [flip backend's remaining native qualification](../technical-debt.md#renoir-flip-backend-qualification).

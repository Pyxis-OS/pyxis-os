# HID-over-I2C touchpad

Vela's ELAN `04f3:327e` precision touchpad supplies movement, physical
click/drag and two-finger scrolling through the shared
[pointer filter](pointer-input-quality.md#precision-hid-over-i2c-integration).

## Attachment and ownership

The [ACPI worker](../kernel/acpi.md#hid-i2c-endpoint-snapshot) captures the live
endpoint, companion and electrical resources. PCI claim/mapping authority and
native GPIO windows are prepared before AP startup; one BSP worker owns
LPSS/DesignWare transfers. Only the exact supported companion binds. A fenced
unassigned BAR uses the general [PCI allocator](pci.md#assigning-an-unassigned-memory-bar),
not a fixed hardware address. No address scan, public raw-I2C authority or
borrowed AML mapping is supplied. Controller, descriptor and source backing
remain owned until reboot.

The Intel GPIO attachment reads the qualified asserted level without changing
pad configuration, ownership or interrupt routing. There is no blind input-read
fallback. The reusable DesignWare master uses bounded polled FIFO transfers
with repeated START/final STOP and verified disable/drain. Uncertain controller
cleanup remains latched. The BSP worker performs I2C with interrupts enabled;
short input-state changes exclude interrupts and never allocate or wait.

## Transport and recovery

The client validates descriptors, selects descriptor-derived Input Mode 3 while
preserving unrelated feature fields, and verifies it. Input uses asserted-line
polling at a default 4 ms, with at most four reads per turn and sleeping between
turns. Transfers have a 100 ms bound and RESET completion a 5 s bound.

Length-valid unknown input IDs are counted and ignored without changing contact,
button, quarantine or source state. Failed transfers and malformed known reports
contain source loss. Recovery refreshes identity, descriptors and features within
prepared buffers, restores mode 3 and preserves held-contact quarantine. Three
fast retries wait 250 ms, 1 s and 4 s; their budget is retained across successes
and later failures. Subsequent attempts wait 30 s. Unproven ownership or bus
cleanup prevents retry; recovery never clears the uncertain-controller latch or
resets/reassigns LPSS/GPIO ownership.

## Input and qualification

The adapter admits one descriptor-derived parallel Precision Touchpad report,
with up to five retained contact slots. Complete Tip/Confidence/Contact-ID
frames use physical micrometre coordinates without fabricated pressure or width.
Compatibility/vendor reports do not duplicate pointer input. Motion supports
one/two fingers; valid physical buttons remain independent of motion rejection.

A ready client attaches its pointer source before the first report, including
after recovery. A new admitted contact establishes a zero-delta baseline; motion
can begin on its next report. Uncertain or previously held input requires actual
all-up, and first-snapshot held buttons remain suppressed until release. Silence
and elapsed time never imply lift. RESET alone preserves the last validated
release knowledge; failed transfers or malformed known reports make it uncertain.
See the shared filter for palm, typing, scrolling, acceleration and clickpad
policy. Boot-only `input.touchpad_speed` scales the native HID motion curve;
there is no live setter.

Owner runs on 2026-10-15/16 qualified
[live resources](../development/experiments/vela-hid-i2c-snapshot/README.md#native-vela-2026-10-15),
[descriptor reads](../development/experiments/vela-lpss-descriptor/README.md#native-final-head-recheck-2026-10-16),
polling and movement rate, RESET/mode restoration, held-contact quarantine and
final movement/drag, secondary click, both scroll axes, palm/typing and
lift/re-touch. [Input qualification](../development/experiments/vela-hid-i2c-input/README.md#final-native-rerun-2-owner-2026-10-16)
and [polling measurements](../development/experiments/vela-hid-i2c-polling/README.md#native-sweep-2026-10-16)
keep native observations separate from QEMU controls and service-time estimates.

## Limits

GPIO interrupt delivery and non-spinning DesignWare completion remain
[deferred](https://git.internal/PyxisOS/pyxis-os/issues/1492), requiring a separate
implementation go. Polling is the implemented route. Service-time estimates do
not establish scheduler utilization, energy use or loss-free delivery; the
device reports no sequence counter for a no-loss proof.

[Clickpad tuning](https://git.internal/PyxisOS/pyxis-os/issues/1493) remains
revisitable with GUI use; acceptance did not require another native run of the
3 mm retained-pair bound. Suspend/resume, firmware updates, tap gestures,
additional hardware and Steam Deck work are outside this implementation.

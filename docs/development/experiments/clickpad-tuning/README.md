# Clickpad tuning

Owner accepted 2026-10-18: >3 mm pre-press strip promotion; stationary primary-zone
pair selection with <=100 ms touchdown separation, <=300 ms after later touchdown,
and <=1 mm initial displacement. A qualified pair allows <=3 mm per contact
from its original qualification positions for later presses. These thresholds
are Kconfig tuning. Primary fallback, immediate selection and latch-until-release;
no public ABI change. The [native contract](../../../wip/vela-i2c-touchpad.md#native-qualification-owner-2026-10-16)
is separate from actual device qualification below.

## Baseline before implementation

Clean main `82f9387dc1d8900141ca50f5848adca51c8db8e9`; userland `3677edd2`,
ports `33661036`, fs `25337999`, lwIP `a1aadb91`. Exact CI run 2989/API 3089
bundles verified, ordinary assembly passed with builder
`pyxis-llvm23.1.3-abc00d0`. No compiler/container rebuild.

Nested KVM Q35/max, 4 CPUs, 2 GiB, standard VGA1280x800, fresh OVMF,
PS/2 pointer, xHCI hub/keyboard and hot-added USB mouse, VirtIO net/rng.
Existing Remote ipcbench CALL/SEND64/4096: 256 messages, five rounds;
ten sequential Lua echo launches after one warmup. No GDB/build overlap.

| Workload | Baseline ms median [min..max] |
| --- | --- |
| CALL64 completion | 101.222 [72.545..103.470] |
| CALL4096 completion | 98.800 [81.013..104.091] |
| SEND64 completion | 15.440 [12.153..16.291] |
| SEND64 admission | 1.230 [1.201..1.283] |
| SEND4096 completion | 15.549 [12.058..20.343] |
| SEND4096 admission | 1.336 [1.272..1.680] |
| Echo launch | 8.682 [3.633..10.427] |

All samples verified, exit0/draincomplete. Existing local mousetest PS/2/USB
motion, held-button release and wheel expiry passed. These are noisy nested
controls, not native performance or clickpad latency measurements. QEMU has no
actual ELAN/HID-over-I2C clickpad. Raw commands, hashes, screenshots and results:
`/shared/present/clickpad-tuning/baseline/`.

## Implementation and remaining qualification

The HID helper receives the frame's existing monotonic receipt time. Roles retain
origin, observed touchdown and movement/press history per ID; lost/uncertain
history cannot qualify secondary. Distance compares physical displacement from
touchdown, not accumulated jitter path. A new pointer role uses the shared
filter's ordinary first-frame baseline; no stored movement is replayed.

Code `339f158f`: ordinary kernel/image build passed with the matched SDK,
userland and ports bundles; generated config confirms 3/100/300/1. Source review
found no issue in boundary comparisons, retained movement/press history,
first-seen-pressed fallback, late untrusted-contact invalidation, other physical
buttons or loss cleanup. This is source inspection, not injected frame coverage.

The matched 4-CPU QEMU repeated ordinary PS/2/USB movement, held-button release,
wheel expiry and Remote successfully. Read-only GDB confirmed live relative
PS/2 and absent LPSS; no inferior calls or mutations. Existing controls repeated
with the same five-round/256-message and ten-launch commands:

| Workload | After repeat ms median [min..max] |
| --- | --- |
| CALL64 completion | 106.629 [94.338..111.361] |
| CALL4096 completion | 97.160 [83.124..108.413] |
| SEND64 completion | 15.441 [12.649..16.278] |
| SEND64 admission | 1.210 [1.195..1.626] |
| SEND4096 completion | 16.815 [12.084..40.114] |
| SEND4096 admission | 1.390 [1.263..3.104] |
| Echo launch | 8.517 [3.858..10.295] |

The initial after pass was slower: CALL64 111.778 [73.414..270.964], CALL4096
243.565 [210.205..304.053], SEND64 35.655 [23.990..51.175], SEND4096 32.087
[14.200..55.265] ms completion. One repeat returned near baseline ranges; both
sets are retained. Small samples and uncontrolled outer-host variation do not
support causal attribution or native performance claims. The HID clickpad path
is inactive in these QEMU controls. Raw evidence:
`/shared/present/clickpad-tuning/after/`. All qualifier jobs stopped.

Owner native vela check on 2026-10-18, personal daily packet based on main
`30327a46` plus #900 and this PR at `bb94fafc`: two-finger secondary selection,
strip-start motion, scrolling and ordinary motion worked reliably. Repeated
presses with the same still primary-zone pair reverted to primary after the
first secondary press. The first follow-up retained qualified pair identity
while both contacts remained down within the initial 1 mm stillness bound.
The later combined vela-972-945 native packet confirmed that repeated presses
stayed secondary, while the retained 1 mm bound was too strict for ordinary drift.

That follow-up at `0ff17a92` passed an ordinary kernel build. QEMU10.2.2/Q35/KVM, 4 CPUs,
2 GiB, VGA1280x800, xHCI hub/keyboard/mouse and VirtIO net/rng booted the
updated kernel. An explicitly requested, qualification-only userspace replay
linked the actual helper source against the matching SDK. The old helper
reproduced five failing repeat/held-selection checks (exit1); the corrected
helper passed all11 cases (exit0, complete Remote drain). Cases cover late
repeated presses, contact-slot reorder, exactly1 mm vs greater displacement,
held-button latch, lift with ID reuse, replacement, long-resting thumb and
right-zone side-at-press. This is algorithm replay in QEMU, not ELAN hardware
qualification. Read-only GDB confirmed the updated state layout and running
controller; no inferior calls. Source, binaries, build commands, guest results
and hashes: `/shared/present/clickpad-tuning/repeated-pair/`.

The retained-pair grace is now independently tuned by `TOUCHPAD_CLICK_HOLD_MM`,
default 3 mm from each contact's position at original qualification. Repeated
presses never move those anchors; initial qualification remains within 1 mm
of touchdown. Movement beyond the hold bound or lift clears the retained pair,
preserving a held button until release. The ordinary kernel build passed with
`pyxis-llvm23.1.3-3d69e83`; generated defaults remain 1 mm initial and 3 mm hold.

Grace source `2aa9e93b` passed the extended replay's 20 cases in the same four-CPU
QEMU configuration: 0.5/1/2/2.9/exactly 3 mm retention, either contact exceeding
3 mm, held-button latch, slot reorder, lift/replacement, fixed original anchors
without repeat-induced creep, qualification-position rather than touchdown
anchors, and the unchanged initial 1 mm boundary, thumb and right-zone rules.
The executable linked the actual helper and current private header/config with
the existing SDK `82f9387d` and its matching `abc00d0` compiler. The guest used
the current `3d69e83` kernel and prior ordinary public archive/menu, adding only
the replay executable. Command/client/session exit 0 and complete Remote drain;
QEMU stopped by PID. This is algorithm replay, not native contact qualification.
Sources, configuration, commands, output and hashes:
`/shared/present/clickpad-tuning/retained-pair-grace/`.

Accepted as-is on 2026-10-18 with this evidence. No new native 3 mm grace run is
claimed, and a separate native grace rerun is no longer a merge gate. Further
touchpad refinements wait for practical use with an actual GUI.

No ThinkPad change or PXE staging: selected-mode diagnosis waits until epsilon
releases the ThinkPad. No shipped probe, test target or new harness infrastructure.

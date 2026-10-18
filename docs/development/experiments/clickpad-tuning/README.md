# Clickpad tuning

Owner accepted 2026-10-18: >3 mm pre-press strip promotion; stationary primary-zone
pair selection with <=100 ms touchdown separation, <=300 ms after later touchdown,
and <=1 mm maximum displacement. All four thresholds are Kconfig tuning. Primary
fallback, immediate selection and latch-until-release; no public ABI or runtime
policy change. The [native contract](../../../wip/vela-i2c-touchpad.md#native-qualification-owner-2026-10-16)
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

Ordinary build and matched after controls pending. Native vela mousetest check
pending. No ThinkPad change or PXE staging: selected-mode diagnosis waits until
epsilon releases the ThinkPad. No new tests, probes or harness infrastructure.

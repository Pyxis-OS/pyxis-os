# Vela HID-over-I2C polling: task 3

## Pre-code baseline, 2026-10-16

Main `a6510a156efabc47e5986b5a603ec536f55cea2b`, existing df87791 builder,
matching CI2849 artifacts (Forgejo API run ID2949). Kernel SHA-256
`210be166047f8313a9a1eea1059a9e369891d5ba37d8b0e4830925c5ab979787`,
ISO `298e9ad6d08869ec674338eaf497410c833693dec80d9467322a5fec361067a8`.
Artifact metadata was checked against source/dependency revisions before use.
An earlier wrong API-ID download was rejected, not used as baseline evidence.

Nested KVM Q35/max, 4 CPUs, 2 GiB, OVMF, standard VGA, VirtIO-net/rng,
xHCI hub/keyboard; stock live spaces. Interactive read-only GDB: LPSS ABSENT,
claim NULL, master base zero. Remote uses the matching locally built client.
Four existing IPC commands (CALL/SEND, sizes64/4096, 256 messages, 5 rounds)
verify all samples and exit0/drain complete, followed by ten native echo
launches through Lua. No build overlapped timing; shared-host load uncontrolled.
Raw records: task worktree `build/task3-baseline/`.

| Nested baseline | Median ms (range) |
| --- | --- |
| CALL64 completion | 104.841 (98.992–122.802) |
| CALL4096 completion | 77.110 (73.720–96.666) |
| SEND64 completion | 13.188 (12.204–14.501) |
| SEND64 admission | 1.211 (1.199–1.236) |
| SEND4096 completion | 15.874 (12.290–23.842) |
| SEND4096 admission | 1.431 (1.261–2.078) |

These are nested controls, not native polling/CPU costs. Native no-polling
observations and the moving-finger interval sweep remain due. The owner chose
service-time min/median/max, wake/poll counts and `mean service × poll rate`
with scheduler/cache limitations; no scheduler probe is authorized for task3.

## Implementation and QEMU controls

Ordinary df87791 `make -j16 image PREBUILT='sdk userspace ports'` passes
at clean `61b20de4`; the finite measurement configuration also links at
`218a7f63` with `KCONFIG_CONFIG=build/task3-measure.config`. No compiler,
container, SDK, userland or ports change. The component bundles match main
`a6510a15` (userland `cff500e6`, fs `25337999`, ports `f75c3a1c`, lwIP `a1aadb91`).

Four-CPU control (same Q35/devices as baseline): GDB confirms LPSS ABSENT,
transport NULL, three GPIO virtual slots reserved but no physical page filled
and no attach attempt. USB Enter reaches `input_keyboard_snapshot` from
`usb_hid_collect`; no injected inferior calls. All IPC samples verify and
Remote reports exit0/complete drain. One-CPU measurement-enabled boot, with
legacy PS/2 and no USB keyboard: GDB confirms absence/no transport/no GPIO
fill; Enter arrives as scan code `0x5a` through `ps2_interrupt` and
`keyboard_receive`. Remote echo/exit succeeds. Neither machine emulates
LPSS/ELAN/GPIO: positive reset, layout/feature traffic, refusal/cleanup and
measurement phase execution remain source-inspected, not runtime-qualified.

| Nested control | A main median ms (range) | B `61b20de4` median ms (range) |
| --- | --- | --- |
| CALL64 completion | 104.841 (98.992–122.802) | 102.514 (95.837–106.516) |
| CALL4096 completion | 77.110 (73.720–96.666) | 147.480 (97.438–258.433) |
| SEND64 completion | 13.188 (12.204–14.501) | 14.966 (13.660–19.042) |
| SEND64 admission | 1.211 (1.199–1.236) | 1.314 (1.238–1.617) |
| SEND4096 completion | 15.874 (12.290–23.842) | 14.733 (14.130–22.963) |
| SEND4096 admission | 1.431 (1.261–2.078) | 1.352 (1.307–1.735) |

Five IPC samples each; shared host load is uncontrolled and variation is large.
The first B CALL64 overlapped a read-only GDB stop (median290.849 ms); it is
preserved as `call64.jsonl` but excluded from timing comparison. The replacement
`call64-rerun.jsonl` above has no debugger/build overlap. Other timed commands
have no local debugger/build overlap. There is no measured native IPC cost
or attribution: no controller/transport worker runs in these QEMU controls,
and IPC instructions are unchanged. Do not infer no regression from this table.
Raw B controls: task worktree `build/task3-after4/`; 1CPU `build/task3-after1/`.

Ten echo launches through exit, A: median7.833 ms, range3.224–10.009 ms.

Ten echo launches through exit, B: median9.081 ms, range7.608–11.929 ms.

## Native packet and remaining gate

`/shared/present/vela-hid-i2c-polling/` holds the exact clean qualification
source/configuration and SHA256SUMS. `CONFIG_HID_I2C_MEASURE=y` is explicit;
ordinary default is off. This is accepted finite transport instrumentation,
not a scheduler probe or a normal installed build. Normal existing stick entry;
no limine.conf edits, blanking option for attended use, or manual sync step.

The owner attaches the Valve dock at boot. After the arming delay and idle
windows the owner moves a finger during the four 10-second windows, then
lifts. Remote collects the log and all counters. GPIO input/ownership/REVID,
RESET zero completion, descriptor-derived feature verification and valid reports
must pass first; an early refusal needs only its log, not finger movement.

Expected live table-derived GPIO layout (source-inspected only): GPI1 CID
INTC105F, Community1, GPP_F26 followed by GPP_E25, stride16, PADOWN base0x130.
Local pin17 derives PADCFG0 offset0x910 and PADOWN0x138 shift4; native
REVID/PADBAR/config/resource values still must agree, no fixed physical address.

The report descriptor and host frame counts/spacing can expose burst/backlog
behavior but cannot prove losslessness without a device sequence counter.
Service brackets use the architecture clock; native TSC provenance must be
reported as `clock=TSC`. Polls/elapsed gives actual rate. Service sum/elapsed
expressed in ppm equals mean service × poll rate (microsecond denominator
rounding below1 µs); `cost-valid=0` invalidates that estimate. It includes
interrupt time inside the bracket and excludes sleeping, scheduler, cache
consequences and phase-summary/sorting overhead. Bounded sample counts are
printed; a capped distribution is only a prefix sample. Sampled assertion
spans cannot establish the line stayed continuously asserted between polls.

Native results and the resulting default interval are pending. Task4 must add
bounded automatic client recovery before real pointer input. No scheduler-level
accounting or native CPU-utilization claim is made in task3.

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

Nested 4-CPU order: A1(main), B1(`61b20de4`), A2(main), B2(`9893f178`).
Five verified IPC samples and ten echo launches per fresh boot. Medians in ms:

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 104.841 | 102.514 | 69.248 | 89.609 |
| CALL4096 completion | 77.110 | 147.480 | 72.980 | 89.281 |
| SEND64 completion | 13.188 | 14.966 | 11.917 | 17.866 |
| SEND64 admission | 1.211 | 1.314 | 1.190 | 1.201 |
| SEND4096 completion | 15.874 | 14.733 | 13.299 | 13.861 |
| SEND4096 admission | 1.431 | 1.352 | 1.264 | 1.269 |

Ranges and launch summaries are retained below; shared host load is
uncontrolled. B CALL4096 and SEND64 completion medians are higher; these
nested controls establish neither native cost nor a cause. Source inspection
and GDB confirm no LPSS/transport worker or GPIO attachment runs in QEMU and
IPC instructions are unchanged; link layout differs. Do not infer no regression.
The first B1 CALL64 overlapped a read-only GDB stop (median290.849 ms): preserved
as call64.jsonl but excluded from timing comparison; call64-rerun.jsonl supplies
the table. Other timed commands have no local debugger/build overlap. All
sessions exit0/drain complete. Records are build/task3-baseline{,2}/,
build/task3-after4/ and build/task3-final4/; final 4CPU GDB again confirms
LPSS ABSENT, transport NULL, zero GPIO fills at 9893f178.

A1 ranges(ms): call64/completion 98.992..122.802; call4096/completion 73.720..96.666; send64/completion 12.204..14.501; send64/admission 1.199..1.236; send4096/completion 12.290..23.842; send4096/admission 1.261..2.078. Echo median7.833, range3.224–10.009.

B1 ranges(ms): call64/completion 95.837..106.516; call4096/completion 97.438..258.433; send64/completion 13.660..19.042; send64/admission 1.238..1.617; send4096/completion 14.130..22.963; send4096/admission 1.307..1.735. Echo median9.081, range7.608–11.929.

A2 ranges(ms): call64/completion 68.139..80.317; call4096/completion 71.178..79.648; send64/completion 11.829..14.766; send64/admission 1.181..1.223; send4096/completion 12.255..15.070; send4096/admission 1.259..1.372. Echo median7.727, range2.864–8.549.

B2 ranges(ms): call64/completion 85.676..100.817; call4096/completion 81.813..96.477; send64/completion 13.040..18.146; send64/admission 1.180..1.211; send4096/completion 13.101..18.473; send4096/admission 1.253..1.309. Echo median7.103, range4.225–9.902.

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
Local pin17 derives PADCFG0 offset0x910 and PADOWN0x138 shift 4; native
REVID/PADBAR/config/resource values still must agree, no fixed physical address.

The report descriptor and host frame counts/spacing can expose burst/backlog
behavior but cannot prove losslessness without a device sequence counter.
Service brackets use the architecture clock; native TSC provenance must be
reported as `clock=TSC`. Polls/elapsed gives actual rate. Service sum/elapsed
expressed in ppm approximates mean service × poll rate (microsecond denominator
rounding below1 µs); `cost-valid=0` invalidates that estimate. It includes
measurement bookkeeping and interrupt time inside the bracket and excludes sleeping, scheduler, cache
consequences and phase-summary/sorting overhead. Bounded sample counts are
printed; a capped distribution is only a prefix sample. Sampled assertion
spans cannot establish the line stayed continuously asserted between polls.

The final default interval is pending owner decision. Task4 must add
bounded automatic client recovery before real pointer input. No scheduler-level
accounting or native CPU-utilization claim is made in task3.

### First native attempt, 2026-10-16

Owner-attended vela, `0d16b807`, normal stick entry with Valve dock; raw log
`/shared/present/vela-hid-i2c-polling/native-run1-gpio-refused.txt`. PCI assignment,
DesignWare and the ELAN HID descriptor pass. GPIO capture refuses `unsupported
GPIO resource layout`, before any client command. No RESET, mode change or
rate measurement occurred.

Source inspection of the supplied DSDT identifies the mismatch: GPI1 uses
`_HID` INTC10BB / `_CID` INTC105F and `_CRS` calls `GCRS(0x5a0000)`. That helper
returns an ExtendedIRQ plus **QWordMemory**, not FixedMemory32. The memory
resource is a fixed, positive-decode, noncacheable consumer range of 64 KiB
at live `SBRG + 0x5a0000`, with zero granularity/translation and no source tail.
The physical base must be evaluated, never assumed from this expression.

The existing `_DSD` path already matches the supplied Community1/GPPF/GPPE
packages and pinned Linux `db6a186505c8` generic platform driver's cumulative
pin numbering. The correction admits the decoded QWord range only after
checking those resource semantics and its exact extent; all existing mapping,
pad ownership and input-only checks remain. Missing/malformed `_DSD` properties
now name their key. Native GPIO and protocol qualification remain pending.

### Native sweep, 2026-10-16

Owner-attended vela, corrected clean head `8d2aeaf3`, same packet configuration
and dock Remote collection. Files under `/shared/present/vela-hid-i2c-polling/`:
`hid-i2c-task3.txt`, `native-run2-machine.jsonl`, `summary-run2.txt`.
The existing summary helper ran successfully on the native trace.

Native GPIO capture/read-only input passes: resource `0x40005a0000+0x10000`,
REVID `0x1100000`, GPP_F pin 17, PADCFG `0x910`, PADOWN `0x138` shift 4.
SET_POWER, RESET zero completion, 675-byte report descriptor and Input Mode
feature ID 3, bit 0, width 16 GET/SET/readback (0 to 3) pass. TSC selected on 16 CPUs at
3686.400 MHz; all cost-valid flags are 1, all sample arrays remain below capacity.
No pointer input is published.

| Poll ms | Frames | Delivered/s | Read gap median ms | Poll gap median ms | Whole-poll service min/median/max µs | Service estimate % |
| --- | --- | --- | --- | --- | --- | --- |
| 8 | 1029 | 123.8 | 8.009 | 8.009 | 18.2 / 953.8 / 1042.3 | 9.912 |
| 4 | 1464 | 146.3 | 8.015 | 4.009 | 18.4 / 950.3 / 1066.8 | 14.367 |
| 2 | 1463 | 146.2 | 6.033 | 2.008 | 17.4 / 34.8 / 4620.2 | 15.148 |
| 1 | 1463 | 146.3 | 7.057 | 1.008 | 18.2 / 34.8 / 1050.5 | 16.993 |

These are one 10 s window per interval, not repeated stationary-load trials.
The 8 ms moving span is 8.306 s of the 10 s phase; reaction timing was not
separately recorded. Delivered/s uses first-to-last report span, not the whole
phase; service fraction uses the whole phase. All valid reports are ID 84
(`0x54`), no malformed reports in the moving windows. Actual poll counts are
1249/2495/4982/9896. Read-spacing ranges are 7.942–16.032 /3.906–8.123 /
4.069–9.727 /5.996–7.152ms. The isolated 4.620 ms service maximum at 2 ms includes
interrupt/preemption time; its cause is unmeasured.

No-poll baseline wakes 1249 times with 0 polls/frames. Initial lifted idle at 8 ms
has 1249 polls/0 frames, service 18.1/34.8/108.4 µs (mean 35.203µs), estimate 0.4395%.
Final lifted phase has 147 valid tail frames across 1.169 s and 5 malformed reads;
no transport disable. Only the first malformed header was retained:
length 31, ID 0, expected length 0 (undefined ID), max 31. The other four packets,
their exact ordering and malformed streak lengths were not captured. A firmware
empty/stale-level read is a possibility, not a demonstrated cause. This does
not match [pinned Linux's ELAN `BOGUS_IRQ` exception](https://github.com/torvalds/linux/blob/db6a186505c8156e55681a3b93ada431863b85c1/drivers/hid/i2c-hid/i2c-hid-core.c#L491)
(length 0xffff); no ID 0 quirk
or substitute valid report is justified. Strict rejection/assembly discard stays.

`sampled-assertion-max-ns=0` and `asserted-after-drain=0` throughout mean the
final level check after bounded draining was inactive. The measurement receives
that final level only; it does not measure entry assertion/pulse width. Every
read required an asserted entry level. Zero therefore says nothing about the
low pulse duration or whether reports were replaced before the read.

4/2/1 ms reach the same observed 146.2–146.3 reports/s plateau (about 6.84 ms mean
period). 8 ms underdelivers relative to it, consistent with replacement/coalescing;
neither the sensor's internal rate nor loss is proved. The descriptor exposes
Scan Time, but payload values were not retained to compare device timestamps.

The DesignWare FIFO/enable/STOP/cleanup loops poll MMIO without sleeping;
interrupts remain enabled. Whole-poll timing includes GPIO reads, transfer,
cleanup and timing bookkeeping. Subtracting the idle mean per poll from moving
total service gives an **inferred** 0.916–0.924 ms incremental service per valid
report, not a measured read-only distribution. At nominal 400 kHz,31 data bytes
plus one 7-bit read address require 288 SCL clocks, about 720µs before START/STOP,
device stretching and software overhead; actual SCL/stretching were not measured.

GPIO interrupts alone remove empty polls/detection latency, while unchanged I2C
spinning retains most active cost. A sleeping/interrupt-completed DesignWare
transfer could free the CPU during bus time; it would not shorten wire time and
needs its own FIFO/completion/deadline ownership work. No such change is made.
Service estimates remain wall-time proxies, excluding scheduler/cache effects.
The milestone records the pending interval/cost and malformed-tail decisions.

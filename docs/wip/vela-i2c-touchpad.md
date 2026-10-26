# Vela I2C touchpad

Status: **all three defaults accepted, 2026-10-15; task 1 qualified on vela;
task 2 qualified on vela; task 3 merged in #904 with final 4 ms polling;
task 4 merged in #910 and qualified on vela, 2026-10-16.**
Later tasks need their own implementation go. The
[Steam Deck milestone](steam-deck.md#touch-track) stays on hold. Its DesignWare
and HID-over-I2C layers are reused as the direction, not resumed as Deck work.

Deliverable: vela's ELAN precision touchpad moves the existing system pointer,
clicks/drags and scrolls with two fingers, through the existing palm/typing/edge
filter and `CONFIG_TOUCHPAD_*` tuning. GPIO interrupt delivery follows working
polling. No new pointer protocol, public raw-I2C authority, firmware update,
suspend/resume or unrelated I2C device support is included.

## Inspected evidence and gaps

Inspected main: `9d5fd738`. The redacted
[Linux inventory](../targets/ideapad-slim5-16iph11/ideapad-inventory.txt) observes
ELAN `04f3:327e`, `i2c-hid-acpi`/`hid-multitouch`, behind PCI `00:15.0`
`8086:e378` (`intel-lpss`, DesignWare I2C #0). Its BAR0 is reported as 4 KiB;
the Linux snapshot shows D3, so usable power/clock state cannot be assumed.
BAR addresses remain observations, not fixed mappings.

The supplied `/shared/present/vela-acpi/` tables were disassembled locally with
the existing iasl; no AML was executed and no hardware was accessed:

| Table evidence | Consequence |
| --- | --- |
| SSDT31: `\_SB.PC00.I2C0.TPD0`, `_CID=PNP0C50`; `_INI` selects `ELAN06FA` for the ELAN vendor selector | Resolve the live device after namespace initialization, not from raw string search |
| DSDT: I2C0 `_ADR=0x00150000`; SSDT31 I2cSerialBus points to I2C0, seven-bit, exclusive, 400 kHz | Resolve the ACPI companion to the retained PCI inventory; do not hard-code a BDF |
| SSDT1 HQNV fields `TPSA`, `TPDP`, `TPIT`, `TPTP` supply address, descriptor register, interrupt selector and vendor | Their numeric values are firmware memory, absent from the table files; evaluate live `_CRS`/`_DSM`, never use template zeros |
| `_CRS` selects GPIO or an extended interrupt using `TPDM`; GPIO form is level/active-low, exclusive-and-wake | Resolve the live route. Linux's `intel-gpio` hwirq 17 is not a globally meaningful GPIO number |
| `_INI` uses GPIO ownership helpers; optional power methods are conditional | Preserve the ACPI owner's initialization ordering; do not borrow reset sequences from another device |

Native descriptor, GPIO, reset, feature selection and the transport sweep are
qualified below. Contact semantics and pointer integration remain task4; the
table files alone do not settle those questions.

**Native task 1, owner, 2026-10-15:** `7a305bef` captured
`\_SB_.PC00.I2C0.TPD0`, controller `\_SB_.PC00.I2C0`, address `0x15`,
seven-bit, 400 kHz, HID descriptor register `0x0001`. Its interrupt is GPIO
`\_SB_.GPI1` pin 17, level/active-low, exclusive and wake-capable. Discovery
took 297 µs. These are live AML results, not fixed driver constants; the
[record](../development/experiments/vela-hid-i2c-snapshot/README.md#native-vela-2026-10-15)
keeps native evidence separate from descriptor/input work still due.

uACPI supplies decoded connection resources and the owned ACPI-to-PCI companion
handoff. LPSS/DesignWare and the read-only GPIO attachment are qualified on vela.
[PCI preparation](../../include/kernel/pci.h) reserves claim/mapping authority
before AP startup; the accepted fresh-address fill follows the
[ACPI worker](../kernel/acpi.md#ownership)'s owned resource capture. The native
driver cannot borrow the ACPI mapping window.

The existing [touchpad filter](../devices/pointer-input-quality.md#contact-behavior-and-limits)
now retains filter state per source; its Synaptics path keeps pressure/W
thresholds, inferred contact identity and orientation assumptions. Explicit
HID Tip, Confidence, Contact IDs and absent pressure use the explicit native
frame path, qualified on vela in task 4. The reusable path is
`touchpad_report` → an `INPUT_POINTER_TOUCHPAD` source → `input_pointer_report`,
including shared typing policy, button ownership, focus, scroll direction and
loss handling. The USB report-mouse parser is not a precision-touchpad parser.

Task 2 consumes the general [PCI BAR assignment](../devices/pci.md#assigning-an-unassigned-memory-bar)
capability, qualified natively with vela's unassigned BAR0. It is not a fixed
address exception; live root apertures and conservative collision proof govern
the placement.

## Delivery tasks

Each task needs its own implementation go after the decisions are settled.
Tasks 1–4 and their native owner checks are complete; task 4 merged in #910.
Task 5 needs a separate implementation go. See the
[task 1 record](../development/experiments/vela-hid-i2c-snapshot/README.md).

1. [x] **Live firmware endpoint snapshot.** On the ACPI worker, locate ELAN06FA/PNP0C50,
   evaluate `_STA`, `_CRS` and HID-I2C `_DSM`, and copy owned resource results:
   I2C source/address/speed, descriptor register, GPIO/IRQ source and electrical
   attributes. Reject missing, malformed or ambiguous resources; retain no
   borrowed uACPI buffers.
   **After this task the owner can:** read the actual vela endpoint and interrupt
   route in a bounded diagnostic, including values unavailable in the dump.

   - [x] Owner check: actual endpoint/resource diagnostic captured on vela;
     QEMU separately qualifies absence and continued boot/input.
2. [x] **LPSS preparation and one HID descriptor read.** Reserve the compatible
   PCI controller/MMIO authority before AP startup; after the snapshot, bind its
   exact companion. Audit D0, LPSS reset/clock and DesignWare capability/timing
   setup. Reserve claim authority/mapping space before AP startup; the accepted
   PCI allocator may place the fenced BAR after live ACPI capture. Use bounded FIFO
   transfers with repeated START/STOP, clock-stretch
   tolerance and explicit arbitration-loss, address/data-NACK, busy and timeout
   results. No DMA or I2C address scan. Validate the descriptor's length/version,
   register fields and declared limits. Do not reset other clients to hide a
   failure. Separate Intel LPSS attachment from the reusable DesignWare master;
   later AMD AMDI0010 attachment must not depend on Intel registers or PCI.
   **After this task the owner can:** see a validated HID v1.00 descriptor and
   device identity read from vela's touchpad, without input publication.

   - [x] Owner check: native validated descriptor read on vela at `60847f3e`
     (2026-10-15), ELAN `04f3:327e`, HID v1.00, report 675/input 31 bytes.
     See the [task 2 record](../development/experiments/vela-lpss-descriptor/README.md).
3. [x] **HID-over-I2C polling transport.** SET_POWER, bounded RESET/zero-length
   completion, report-descriptor fetch and descriptor-bounded input/feature
   transfers. Follow the [published enumeration sequence](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/plug-and-play-support-and-power-management).
   Poll first, with explicit idle/no-report behavior and bounded draining. A
   narrow GPIO input-level reader may be needed here to poll the asserted line;
   enabling/routing GPIO interrupts remains task 5. Never assume unconditional
   input-register reads are safe. Loss or malformed/incomplete reports reset
   assembly and source state; no old-contact replay or permanent busy loop.
   Measure native moving-finger reports/second, inter-report spacing and whether
   reads coalesce or lose reports. Choose the interval from that rate, below
   8 ms when the device exceeds 125 Hz and GPIO status makes idle polls cheap.
   The target is no missed report and no extra delay beyond roughly one native
   report period; absence of a sequence counter limits any no-loss claim.
   **After this task the owner can:** touch the pad and observe bounded, validated
   input reports and honest reset/transport errors, before pointer integration.

   - [x] Bounded transport, feature/layout admission and read-only GPIO attachment.
   - [x] Ordinary/measurement builds; 1/4-CPU QEMU absence and existing input.
   - [x] Native GPIO, reset, features and moving-finger rate/cost sweep (`8d2aeaf3`).
   - [x] Final 4 ms interval and contained lift-tail limitation accepted, 2026-10-16.
4. [x] **Precision-touchpad input through the shared filter.** Decode the actual
   descriptor's digitizer collections, complete contact frames, Contact IDs,
   Tip/Confidence, geometry/units and physical buttons. Select descriptor-derived
   [Input Mode](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-configuration-collection)
   3 through a feature report, preserving unrelated feature fields; do not also
   route compatibility-mouse reports. Reapply after reset and rebaseline only
   from fresh, complete frames. Give the existing filter source-owned state and
   explicit contact/palm hints; preserve PS/2 behavior and configuration policy.
   Keep physical switches independent of contact rejection. More-than-two-finger
   gestures and tap-to-click are outside this slice.
   **Required before real input:** bounded automatic client reinitialization
   with explicit retry/backoff, fresh-state rebaseline and transport-local
   containment. Task 3's temporary disable-until-reboot behavior cannot become
   the real touchpad recovery policy (owner, 2026-10-16).
   Classify task 3's five contained malformed lift-tail reads during the
   lift/rebaseline checks before pointer input goes live (owner, 2026-10-16).
   Audit the whole filter's added latency at the measured rate, including
   `TOUCHPAD_STABLE_SAMPLES=2` admission and rebaseline gates; preserve rejection
   semantics without adding an unnecessary smoothing or reporting delay.
   **After this task the owner can:** move, click/drag and two-finger-scroll with
   vela's own touchpad, including palm/typing/lift and loss checks.

   - [x] Source-owned filter, explicit native contacts and bounded recovery implementation.
   - [x] Ordinary build and 1/4-CPU QEMU/GDB absence/existing-input controls.
   - [x] Session (a): native lift-tail classification and announced RESET accepted, 2026-10-16.
   - [x] Session (b): native first-touch/lift admission, gestures and release checks;
     final rerun `38c8c990` / userland `15a82ece`, owner, 2026-10-16.
5. **Intel GPIO interrupt, polling fallback.** From the live resource and proven
   Panther Lake pin-controller layout, own the specific line, acknowledge its
   level correctly and wake the BSP worker; no I2C/AML in interrupt entry.
   Preserve neighboring pins and firmware ownership. Compare delivery/idle cost
   with polling; retain the working polling route on failed interrupt delivery.
   Keep the GPIO attachment separate from later AMD GPIO/FTS3528 work.
   Evaluate taking this task immediately after task 4: native-rate interrupt
   delivery avoids polling delay. It still needs its own owner go.
   **After this task the owner can:** use the same touchpad behavior with measured
   interrupt-driven idle cost and an honest polling fallback.

## Task 2 attachment and limits

Only one unique `8086:e378` from complete PCI inventory is reserved before APs.
Other LPSS functions are untouched. Checked D0/memory access, native function/APB
and internal-DMA reset precede completed claim/BAR sizing. The retained bootstrap
window must fit the sized BAR; internal DMA stays in reset and interrupts stay
masked. Firmware D3hot with `NoSoftReset` uses an explicit PCI-owned preparation:
clear BME/mask INTx while still in D3hot, verify, request D0, wait 10 ms, verify
PMCSR and unchanged BARs, then enable memory decode. Ordinary probe callers keep
their BME guard. Reset-causing wake, failed reset/readback or unsupported identity refuses, with
owned resources retained after irreversible effects. The accepted PCI assignment boundary permits a prepared one-use native window
to be filled later; no later unmap or ECAM permission mutation. Only an exact live direct-root, segment-zero companion and
I2C source index zero can bind. No bus scan or client reset.

The reusable [DesignWare core](../../include/kernel/i2c/designware.h) uses bounded
polled FIFO transfers on one BSP worker: repeated START, final STOP, RX credits,
separate NACK/arbitration/busy/timeout errors and verified disable/drain. No
partial descriptor succeeds; cleanup errors survive and unproven cleanup latches
failure. Native-endian 32-bit register access and encoded FIFO depths are required.
100/400 kHz are supported. Valid firmware counts/hold take precedence; otherwise
the PCI profile supplies a 100 MHz clock and conservative rounded timing. A zero
SDA RX hold is raised to one cycle to avoid spurious arbitration loss. No UART/SPI
clock-divider writes. Descriptor v1.00 length/fields are validated; no report
descriptor, SET_POWER, HID RESET, input/GPIO reads or pointer publication yet.

Register/clock facts were checked against Linux
[`db6a186505c8`](https://github.com/torvalds/linux/tree/db6a186505c8156e55681a3b93ada431863b85c1/drivers):
`mfd/intel-lpss{,-pci}.c` and `i2c/busses/i2c-designware-{core.h,common.c}`.
They are GPL reference sources; no upstream implementation was imported. The
new native code is independently implemented under MPL-2.0. This adds no build
dependency, mirror requirement or compiler-container change.
Descriptor validation follows the
[Microsoft HID-over-I2C 1.00 specification](https://download.microsoft.com/download/7/d/d/7dd44bb7-2a7a-4505-ac1c-7227d3d96d5b/hid-over-i2c-protocol-spec-v1-0.docx),
including distinct register IDs. Active report/input/command/data registers
must be nonzero; output may be zero only when no output reports are declared.

Vela leaves the controller in D3hot/NoSoftReset with BAR0 unassigned. Its
conditional controller `_PS0` is empty, with no controller/ancestor `_PR0`;
the optional child power-resource block is disabled in the captured table.
No AML power method is needed for the qualified native D0 path. `_PS3` can
clear BAR0 and is not a rollback. Pinned Linux orders platform D0, PCI PM
D0/delay/readback and only then LPSS enable/reset (`drivers/pci/pci.c`,
`pci-driver.c`, `mfd/intel-lpss-pci.c`). The native handoff preserves PME
policy and refuses reset-causing wake.

### Unassigned BAR integration

The owner accepted the general [PCI BAR assignment](../devices/pci.md#assigning-an-unassigned-memory-bar)
capability on 2026-10-15; #897 precedes this PR. The LPSS unassigned path keeps
IO/MEM/BME off across verified D3hot-to-D0 preparation and reserves its own
4 KiB virtual window before AP startup. Only after the exact live companion
matches does the BSP worker request the generic allocator's checked placement.
It validates the actual sized BAR extent, completes the D3hot ownership fence,
enables/reads back memory decoding and runs the same LPSS reset/identity setup.
No unowned BAR is sized and no address is taken from the Linux snapshot.
Failure retains the native window/claim; no descriptor or pointer success is
published. Native assignment and the descriptor read are qualified below.

## Task 3 implementation and limits, 2026-10-16

Owner implementation go and all task 3 decisions are accepted. The native
positive transport sweep passes with the accepted lift-tail limitation;
QEMU has no LPSS/ELAN device. The
[record](../development/experiments/vela-hid-i2c-polling/README.md) keeps baseline,
source inspection, QEMU and native evidence separate.

One BSP worker performs SET_POWER(on), a 60 ms compatibility settling delay
(pinned Linux), one RESET and asserted-line zero-length completion, then the
complete device-sized report descriptor. Ordinary input is a direct I2C read,
without an input-register prefix. Length/report-ID checks require a whole
admitted report; no partial data or pointer events are published. Unexpected
reset invalidates mode; eight consecutive malformed frames are treated as
unrecoverable. Each turn drains at most four inputs, checks the level again
when the budget is exhausted, and sleeps rather than catching up in a spin.

Prepared descriptor/report capacity is 4 KiB, a tunable implementation bound;
feature lengths come from the report descriptor, independent of zero output
capacity. The allocation-free layout walker tracks per-type/per-ID lengths,
global/local/collection state and a unique qualified Input Mode field; unsupported
or ambiguous layouts refuse. Qualification performs complete feature GET,
bit-preserving SET and GET/readback for mode 3. Ordinary task 3 builds leave
mode selection unchanged; persistent selection/rebaseline belongs to task 4.

The read-only Intel GPIO attachment captures the live INTC105F `_CRS`/`_DSD`
community/group properties on the ACPI worker. A pre-AP native reservation is
filled once with three possible UC register pages; no borrowed AML mappings,
pad mux/direction/pull/ownership changes or IRQ writes. REVID/PADBAR, host
ownership and enabled GPIO input configuration are checked. Invalid level or
layout refuses; there is no blind input-read fallback. Intel-specific offsets
remain in arch; later AMD attachment can supply the same electrical-level reader.

Vela's first task3 native attempt (`0d16b807`, 2026-10-16) reached the ELAN
descriptor but refused GPIO before any RESET or rate capture: its `_CRS` uses
QWordMemory, whereas the initial decoder admitted only FixedMemory32. The
corrected capture supports that fixed, untranslated, noncacheable consumer
range and retains the live `_DSD` group layout. Native run2 at `8d2aeaf3`
qualifies this attachment, RESET, mode3 readback and the finite input sweep;
the [experiment record](../development/experiments/vela-hid-i2c-polling/README.md)
holds the firmware/resource details and the refused run.

Normal interval defaults to 4 ms after native qualification. Optional
`CONFIG_HID_I2C_MEASURE` is off by default: it starts a finite sweep two minutes
after readiness, with 10-second no-polling and polling-idle windows, four
10-second moving windows (8/4/2/1 ms), then a final idle window. Fixed arrays
retain at most 16,384 samples per service/turn-spacing/read-spacing distribution;
min/median/max, totals and sample counts are emitted only at phase boundaries.
No scheduler or IRQ instrumentation. Read-spacing/burst and sampled GPIO
assertion spans are observations; without a sequence counter they cannot prove
no loss, continuous assertion or the device's internal sampling rate. The normal
build has no measurement allocation or per-report timestamp call.

Register/protocol references are the HID-over-I2C specification above and pinned
Linux `db6a186505c8` `i2c-hid-core.c`, `pinctrl-intel-platform.c` and
`pinctrl-intel.c`. New code is independent MPL-2.0; no GPL implementation, build
source, mirror, SDK or compiler-container change is introduced.

### Accepted task 3 decisions, 2026-10-16

1. **Failure containment — accepted for task 3:** one startup RESET, up to 5 seconds for
   its completion, with each I2C operation bounded by the existing 100 ms
   timeout and the remaining overall deadline. Malformed data clears report
   assembly; an unresynchronizable protocol error or failed bus cleanup disables
   this transport until reboot, preserving other input. No periodic reset loop
   or client power cycling. **Owner requirement:** bounded automatic client
   reinitialization with retry/backoff is required before task 4 publishes real
   input; see the recovery prerequisite above and technical debt.
2. **Measure the actual touchpad mode — accepted:** if the descriptor exposes
   the standard Input Mode feature and compatibility mode prevents precision
   reports, permit a narrowly descriptor-derived mode-3 selection during task 3
   qualification. Read/preserve unrelated fields and verify the result; no guessed
   report ID/offset or pointer integration. This moves that small configuration
   prerequisite forward from task 4; task 4 still owns contact decoding and
   persistent mode/rebaseline policy. No guessed feature fields or pointer
   integration are authorized.

### Accepted task 3 measurement, 2026-10-16

**Owner decision: no scheduler probe for task 3.** Measure each poll's service
with the architecture TSC timing facility, reporting min/median/max alongside
wake/poll counts and actual poll rate. Report `mean service time × poll rate`
as an estimated CPU fraction, with explicit limits: scheduler and cache effects
are excluded; interrupt/preemption time inside the bracket can inflate service
time. This is not scheduler-level utilization or energy measurement. No native
runtime query or scheduler instrumentation is added. Baseline and enabled
observations keep display/audio/network state matched.

Scheduler-level accounting is deferred to task 5's interrupt-versus-polling
comparison and requires its own owner decision then. The declined scheduler
probe proposal is superseded by this measurement contract.

### Accepted task 3 completion decisions, owner, 2026-10-16

1. **Final default: 4 ms polling.** The native device delivers about 146 reports/s
   (about 6.8 ms mean period); 4 ms reaches that plateau. The roughly 1 ms average
   detection difference from 2 ms is not worthwhile for pointer use at twice the
   idle polling cost. At 250 polls/wakes per second, lifted idle service is
   estimated at 0.9%, versus 1.7% at 500/s (extrapolated from the measured 35.203 µs
   idle mean, not separately measured idle phases). The 4 ms moving phase's
   service estimate is 14.367%, with the existing scheduler/cache limitations.
   The [native record](../development/experiments/vela-hid-i2c-polling/README.md#native-sweep-2026-10-16)
   holds the table and interpretation; no scheduler probe is added.
2. **Contained lift-tail rejects accepted as a task 3 limitation.** Task 3
   retained strict discard pending classification. Task 4 session (a) supplies
   that evidence; the accepted unknown-ID rule below replaces its source-loss
   consequence, without substituting ID 84 or synthesizing a lift.

Deferred: GPIO interrupt input and non-spinning DesignWare I2C completion are
outside tasks 3/4 and need a separate implementation go.

### Accepted task 4 decisions, owner, 2026-10-16

Task 4 uses two PRs: source-owned shared filter state, then descriptor-driven
ELAN precision input with bounded client recovery. Preserve the Synaptics
profile and behavior, per-source input coordination and the existing tunables;
no public ABI or compiler change. Four-ms polling stays. GPIO interrupts and
non-spinning DesignWare completion remain outside tasks 3/4.

1. **Palm policy:** a Tip-active/Confidence-clear contact quarantines the whole
   touch gesture until a complete all-up frame. Valid physical buttons stay
   independent. Per-contact quarantine is deferred.
2. **Client recovery:** three reinitialization attempts after 250 ms / 1 s /
   4 s, then at most one retry per 30 s, retaining the budget across flaps.
   Retry only with proven bus cleanup and retained ownership. Restore/verify
   mode 3 and require a fresh lift/rebaseline; never erase an uncertain-controller
   failure latch or replay old contacts. Existing 100 ms operation and 5 s RESET
   completion bounds apply to each attempt.
3. **Qualification:** one announced real HID RESET in a never-merged build while
   a finger/button is held; no shipped trigger. Combine it with bounded
   lift-tail classification capture, with pointer publication disabled, in
   session (a). Its RESET evidence is accepted; no repeat is required (owner,
   2026-10-16). Session (b) checks the same fresh-lift gate on every real
   lift/re-touch with publication enabled, plus gestures and release checks.
   Another run is scheduled only if (b) fails.

Both packets follow `/shared/present/RUNSHEETS.md`: numbered owner steps,
`stick-deploy --initrd`, existing normal entry, deployed `boot://` tools and
persistent `home://` output/Remote collection. No limine.conf edits or blanking
option for these attended runs. The owner supplies gestures; Remote collects
bounded diagnostics. The [task 4 record](../development/experiments/vela-hid-i2c-input/README.md)
keeps baseline, implementation and native outcomes distinct.

## Task 4 integration and qualification boundary

The adapter admits one descriptor-derived parallel Precision Touchpad report,
up to five retained contact slots, after reading Contact Count Maximum and the
optional Button Type feature. Contact Count includes reported UP records;
Tip selects active contacts, Contact ID supplies identity, and Confidence
supplies palm evidence. Compatibility/vendor reports do not duplicate input.
Length-valid unknown input IDs are counted and discarded without changing
contact, quarantine, button or source state. Incomplete framing and malformed
known-ID reports retain loss/recovery containment; no undefined-ID lift
exception is inferred. Motion supports one/two fingers;
valid physical buttons remain independent, including when motion is rejected.

Descriptor logical ranges, physical units and exponent scale both axes into
micrometres (rounded physical span, quantized coordinates), with Y adapted
to the existing filter convention. No pressure or width is fabricated. Stable
IDs replace displacement matching only on this native path. An ordinary new
contact establishes a baseline on its first report and can move on the second:
`TOUCHPAD_STABLE_SAMPLES=2` adds one report period, about 6.8 ms at the measured
rate, rather than two extra reports. Scroll entry uses an already-valid centroid
delta; deliberate typing/edge/palm gates and the scroll dead zone remain.

All buffers, profiles, filter and source records remain adapter-owned until
reboot. The BSP worker performs I2C with interrupts enabled; short input changes
exclude interrupts and never allocate or wait. Source loss clears that source's
pending buttons/fractions and quarantines contacts. A successfully initialized
client attaches its pointer source before the first report, including after
recovery, so applications can acquire an idle pad. Availability does not imply
a lift or fabricate button state: the first real snapshot suppresses already-held
physical switches until release. Client reinitialization refreshes
identity, descriptors and features within prepared buffers and verifies mode 3.
Contact admission follows the accepted first-contact rule below. The three fast retries are
boot-retained; later failures cannot replenish the budget. Each later attempt
waits 30 s. A read-only check of actual disabled/empty/inactive hardware,
masked DMA/interrupts and the existing cleanup latch admits a retry; no retry
clears an uncertain-controller latch or resets/reassigns LPSS/GPIO ownership.
Recovery-interrupted measurement phases have invalid CPU estimates; sleeping
retry time is excluded from service samples.

### Session (a) and accepted follow-up, owner, 2026-10-16

The never-merged build `35698201` captured 16 identical 31-byte reads: length
`0x001f`, undefined Report ID 0 and 28 further zero bytes. GPIO was asserted
before each read and deasserted afterwards. Three complete five-read bursts
and the start of a fourth followed valid Tip-off/buttons-released frames and
preceded the next touch. Total 95 rejects and 19 all-up frames (`95 = 19 × 5`)
support five empty reads per lift; the remaining 79 payloads were not captured.
Report ID 0 here is neither valid Contact ID 0 nor zero-length RESET completion.

**Unknown input IDs — accepted:** after validating the length against the
completed native read (nonempty HID image, within the input buffer), an ID
absent from the parsed input-report table is quietly counted and ignored.
This is the general Linux-style rule, not an ELAN quirk. It does not call
`hid_pointer_lost`, synthesize a lift, interpret RESET, release buttons or
change any source/filter state. Length violations, failed transfers and
malformed reports with a known input ID retain their existing containment.
The distinction fixes session (a)'s admission failure: blanket source loss
on each empty tail had reasserted quarantine immediately after valid all-up.

**RESET evidence and next check — accepted:** zero completion, mode 3 restored
and verified, 651 held finger/button frames quarantined and zero quarantine
motion stand as session (a)'s recovery evidence. No repeat of (a). Its
`lifts-after-reset=19` counts all-up frames at the moment quarantine clears,
not durable admission; `private-admitted=0` exposed the tail-handling defect.
The consecutive cue lines came from an untimestamped saved-log replay, not
measured phase collapse. Session (b) uses the ordinary real-input build and
checks fresh lift/re-touch admission on every lift, gestures and the 0.0.6
HTTP/HTTPS/tone items. The RESET trigger and captures remain unmerged.

### Session (b) follow-up decisions, accepted by owner, 2026-10-16

Native `7922b3c4` passed movement/drag, typing, both scroll axes (horizontal
confirmed by SX), HTTP, verified HTTPS and audio. That build lacked secondary
click and used fixed 6 pixels/mm; vela needed about 4.25 pad widths / 4.5 pad
heights for traversal. The ready-client attachment correction is implemented
subsequently. The [experiment record](../development/experiments/vela-hid-i2c-input/README.md)
keeps those observations separate from follow-up qualification.

1. **Clickpad mapping — accepted:** identified HID single-switch clickpads use
   bottom 25% / right-half secondary click, plus two confident main-area fingers
   at physical press. A bottom-zone thumb selects the button without counting
   as a second main-area finger or moving the pointer. Its current side at press
   selects the button, latched until physical release. A main-area contact never
   gains a button role merely by entering the zone; leaving the bottom area ends
   that role until lift. Without trustworthy classification, the physical press
   stays primary. Held-at-admission suppression, independent switches and loss
   cleanup remain. No tap gestures. Synaptics button behavior stays unchanged;
   current Synaptics code has no software zones to reuse.
   Amendments accepted 2026-10-17/18: an unpressed zone contact can become a
   pointer contact after physical movement, and a stationary recent pair can
   select secondary in the primary zone. See the accepted tuning below.
2. **Adaptive motion — accepted:** the HID path derives velocity in physical
   mm/s without position smoothing or extra report delay. Equal gain on both
   axes projects fractions of the active output's width into pixels. Retuned
   after the owner's native feedback: 1/500 screen-width/mm at at most 10 mm/s,
   approximately 1/120 at 100–250 mm/s, rising to 1/80 at 400 mm/s. The default
   reference is now 120 mm per screen width at 100 mm/s (previously 50 mm);
   slow precision is unchanged and high-speed gain starts above 250 mm/s.
   Boundary changes clear
   fractions/history. Scrolling, mice and legacy Synaptics 6 pixels/mm remain
   unchanged; only the native HID path uses this projection.
3. **Runtime tuning — accepted:** finite `input.touchpad_speed` in [-1, 1],
   default 0, is read at boot, including installed boot.lua, through existing
   boot-only input-policy authority. Archive speed is the fallback. The speed
   scales the default curve from 0.5x at -1 through 1x at 0 to 2x at +1.
   No compiler rebuild or live setter. Later scrolling policy can use this
   input table; no momentum or unused parameters are implemented here.

References: [Microsoft's zone defaults](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-tuning-guidelines#right-click-zone),
[libinput button roles](https://wayland.freedesktop.org/libinput/doc/latest/clickpad-softbuttons.html)
and [adaptive acceleration](https://wayland.freedesktop.org/libinput/doc/latest/pointer-acceleration.html).
The combination and curve are Pyxis policy, not identical Windows/libinput behavior.

Horizontal scroll required no HID fix: the owner observed SX change. Userland's
`mousetest` shows four directional arrows, received pointer/scroll-event counters
and last-scroll age. Native checks use the dock **or USB Ethernet adapter**
attached before boot; existing NCM serves both.

### Native qualification, owner, 2026-10-16

Final rerun `38c8c990` with userland `15a82ece` passed first-touch motion without
a preparatory tap, lower acceleration, bottom-right secondary click, primary
thumb-plus-finger drag, release-latched button selection, both scroll axes with
natural direction, 500 ms scroll-feedback expiry, palm rejection, typing and
lift/re-touch. Earlier session (b) also passed HTTP, verified HTTPS and tone.
Session (a)'s RESET/mode restoration and held-contact quarantine remain accepted;
its qualification overlay stays unmerged. See the
[experiment record](../development/experiments/vela-hid-i2c-input/README.md#final-native-rerun-2-owner-2026-10-16).

**Clickpad tuning — accepted as-is, 2026-10-18:** a two-finger press involving
the primary button zone previously selected left. The implemented tuning below
is accepted with its measured evidence and limits recorded in
[clickpad tuning](https://git.internal/PyxisOS/pyxis-os/issues/1493).

- An unpressed zone contact moving more than 3 mm from touchdown becomes an
  ordinary pointer contact until lift, with a fresh motion baseline. A contact
  seen during any physical press cannot promote by distance until lift.
  Existing leave-strip behavior and side-at-press selection stay unchanged.
- A primary-zone press selects secondary for exactly two confident contacts
  observed before the press, with touchdown receipts at most 100 ms apart,
  press at most 300 ms after the later touchdown, and neither contact ever more
  than 1 mm from its touchdown position. Insufficient/untrusted history,
  moving fingers and a long-resting thumb keep primary. Decide at press with no
  added waiting; keep the selected button until release. Existing bottom-right
  and two-main-finger selection stays unchanged. Once that primary-zone pair
  qualifies as secondary, retain its qualification for later presses while the
  same two contacts stay down, each within 3 mm of its position at the original
  qualification. Later presses do not move those anchors. Lift, replacement, an
  extra contact, movement beyond that bound or untrusted history clears it; a
  held physical press keeps its selected button until release. This retained
  grace does not relax the initial 1 mm touchdown bound.
- `TOUCHPAD_BUTTON_MOVE_MM`, `TOUCHPAD_CLICK_PAIR_MS`, `TOUCHPAD_CLICK_PRESS_MS`,
  `TOUCHPAD_CLICK_STILL_MM` and `TOUCHPAD_CLICK_HOLD_MM` are authored Kconfig
  tuning, not ABI. The owner tunes them natively. Coordinates stay physical,
  with per-source retained history; no pressure/width inference, taps, new
  smoothing or Synaptics policy.

Native vela checks covered the original gestures and repeated presses with
the earlier 1 mm retained bound. The 3 mm grace passed the 20-case QEMU algorithm
replay; no additional native 3 mm run is claimed or required for this acceptance.
Further touchpad refinement waits for practical use with an actual GUI.

**First-contact rule — accepted by owner, 2026-10-16:** at startup, or
after an unambiguously released recovery state, the first complete frame of a
new touch establishes a zero-delta baseline; motion starts on the next frame.
Previously held contacts/buttons or uncertain state still require actual
all-up. First-snapshot button suppression remains. Silence and elapsed time
never imply lift. The adapter retains this knowledge separately from reset
filter baselines: a failed input transfer or malformed known report makes it
uncertain; a RESET notification alone preserves the last validated state.
Unknown IDs cannot clear or change it. Held-through-RESET contacts therefore
remain quarantined, preserving session (a)'s requirement.

## Accepted owner decisions, 2026-10-15

1. **Split preparation and ownership — accepted:** reserve checked PCI configuration authority
   and native mappings before AP startup; no touchpad I2C command
   until the live ACPI snapshot selects its companion. One BSP controller worker
   serializes transfers; the ACPI worker alone executes AML. Keep LPSS reset and
   clock writes with the native PCI owner, without relaxing AML PCI-write policy
   or adding userspace bus access. GPIO MMIO/IRQ preparation must also preserve
   boot/runtime mapping ownership; the native driver cannot borrow the ACPI
   worker's mapping window. Owner acceptance of #897 additionally permits
   fenced unassigned BAR placement and one-use mapping after live root capture.
   If live discovery cannot use safely prepared
   resources, stop with the concrete conflict before extending the contract.
2. **Polling and measurement — accepted:** start qualification at a tunable
   8 ms service interval,
   with finite report/transfer budgets and sleeping between attempts. Prefer a
   qualified GPIO asserted-level read; if that route is unavailable, establish
   safe idle input-read semantics before admitting an alternative. A dependency
   on GPIO status is part of task 3, not an assumption that interrupts already
   work. Capture idle cost before polling; no permanent hot-path diagnostics or
   whole-kernel accounting project. The owner's 2026-10-16 task 3 measurement
   decision uses service time × poll rate with stated limits; scheduler accounting
   waits for task 5's separate decision.
   **Owner's rate requirement:** report rate and latency are goals alongside CPU
   cost. Measure the ELAN rate during real movement and choose the shipping
   interval accordingly, potentially faster than 8 ms; record measured rate,
   coalescing/loss evidence and chosen interval here. All are unmeasured today.
3. **One touchpad pipeline — accepted:** source-owned filter state with explicit
   presence/identity/orientation and optional palm hints. Tip and Confidence are
   not fabricated Synaptics pressure; pressure/W thresholds remain applicable
   only to transports with those scales. Keep existing `CONFIG_TOUCHPAD_*`
   values and shared movement/typing/edge/scroll policy as tunable defaults;
   missing required geometry or incomplete frames refuse/reset rather than
   inventing calibration. No new pointer ABI or second gesture engine.
   Check that higher report rates incur no avoidable filter/smoothing latency.

## Qualification and who does what

Task 1's record includes the pre-code revision/pins, boot controls and unchanged
ordinary launch/IPC controls. Whole-boot wall time remains unmeasured.
Before polling, capture repeated native idle
observations at the prompt, on AC, same display/log/audio/network state, with
the touchpad untouched. Repeat matched A/B/A/B after polling and interrupts;
record interval, wake/service count, report/transfer failures and available CPU
accounting. Distinguish elapsed worker-service bounds from CPU time. Nested QEMU
CPU use is only a proxy; no native polling baseline is captured yet. Task 3
uses the accepted service-time estimate, not scheduler utilization; task 5
needs a separate decision for scheduler accounting.

Ordinary builds and 1/4-CPU QEMU/GDB check absence, refusal and unchanged PS/2/USB
input. QEMU is not qualification of vela's LPSS, GPIO or real ELAN reports; no
new emulator, test infrastructure or injected-device program is proposed.

The owner deploys native packets with
`stick-deploy --initrd`, preserving the installed menu. Use the normal entry so
output saved to `home://` persists for `stick-deploy --mount` collection. Pool
tools are 0.0.5: packet commands use `boot://NAME.pxe`, with no shell quoting.
Owner-attended screen runs use no blanking option. Unattended OLED runs use
**`display.idle_blank=0`**, which blanks immediately; it never means disabled.
Descriptor discovery/reset/idle capture need no fingers; the owner must physically
exercise movement, clicks/drag, two fingers, resting palms, typing and lifts.
Remote over the Valve dock's CDC-NCM Ethernet was qualified during task 2
when attached at boot. The orchestrator can now collect logs and run ordinary
workloads/integrity checks hands-free through that connection. It cannot synthesize
the owner's physical touchpad checks. No Deck deployment is part of this track.

### Native task 2 pass, 2026-10-15

The owner booted clean `60847f3e` (#894 on #897), kernel SHA-256
`8acfef0e6e1a52029a7287ad49c81958c7628b752e1505a9902de7799db1c538`.
BAR0 was assigned `0x10020000000/0x1000`; LPSS identity/reset and 64/64 FIFO
identification passed. Configuration read back CON `0x75` for `0x65` written,
TAR `0x1055`, high 67/low 144/hold `0x10037`, disabled. This confirms the
CON[4] read-only TAR[12] mirror diagnosis: configuration excludes only this
bit, then transfer selects and verifies both mode locations. All other
configuration readbacks remain checked and failures name the step and values.

The native bounded repeated-START read returned a validated HID v1.00
descriptor: vendor `04f3`, product `327e`, version `0004`; report 675 bytes at
register 2, input 31 at 3, output 0 at 4, command 5, data 6. No input is enabled.
The full log was collected through Remote over the Valve dock; see the
[task 2 record](../development/experiments/vela-lpss-descriptor/README.md#native-owner-pass-2026-10-15).
The owner rechecked final heads #894 `4e13c60e` on #897 `d934ffac` on
2026-10-16 at about 08:40: the same BAR0 address and validated descriptor
passed; the log was collected over the dock. See the
[recheck record](../development/experiments/vela-lpss-descriptor/README.md#native-final-head-recheck-2026-10-16).

Client RESET, report/input transport and GPIO behavior remain later gates.

Primary platform reference: [Intel Core Ultra Series3 I/O Registers,
IC_CON, ID872352 revision001 (2026-04-14)](https://edc.intel.com/content/www/kr/ko/design/publications/core-ultra-series-3-processors-i-o-registers/reg-ic-con-ic-con-offset-0/).
Pinned Linux `db6a186505c8` `i2c-designware-master.c` sets both CON and TAR
mode bits because dynamic TAR support is not discoverable. Its LPSS code
confirms function-reset release3, 100 MHz for this identity and no I2C
fractional-divider setup. No upstream implementation is imported.

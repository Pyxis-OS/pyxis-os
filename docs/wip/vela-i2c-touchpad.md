# Vela I2C touchpad

Status: **all three defaults accepted, 2026-10-15; task 1 qualified on vela;
task 2 assigned and authorized.** Later tasks need their own implementation go. The
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

No Pyxis HID descriptor, report descriptor, GPIO level, reset completion or
touch event has been measured. The tables do not settle those questions.

**Native task 1, owner, 2026-10-15:** `7a305bef` captured
`\_SB_.PC00.I2C0.TPD0`, controller `\_SB_.PC00.I2C0`, address `0x15`,
seven-bit, 400 kHz, HID descriptor register `0x0001`. Its interrupt is GPIO
`\_SB_.GPI1` pin 17, level/active-low, exclusive and wake-capable. Discovery
took 297 µs. These are live AML results, not fixed driver constants; the
[record](../development/experiments/vela-hid-i2c-snapshot/README.md#native-vela-2026-10-15)
keeps native evidence separate from descriptor/input work still due.

uACPI already decodes the connection resources, but Caelum has no I2C master,
GPIO driver or ACPI-to-PCI companion handoff. [PCI preparation](../../include/kernel/pci.h)
is boot-only before AP startup, whereas the [ACPI worker](../kernel/acpi.md#ownership)
loads/evaluates AML afterward. The driver must respect both contracts.

The existing [touchpad filter](../devices/pointer-input-quality.md#contact-behavior-and-limits)
is a singleton with Synaptics pressure/W thresholds, inferred contact identity
and orientation assumptions. Its current sample cannot represent HID Tip,
Confidence, Contact ID or absent pressure honestly. The reusable path is
`touchpad_report` → an `INPUT_POINTER_TOUCHPAD` source → `input_pointer_report`,
including shared typing policy, button ownership, focus, scroll direction and
loss handling. The USB report-mouse parser is not a precision-touchpad parser.

Task 2's unassigned-BAR prerequisite is proposed separately as the general
[PCI BAR assignment](pci-bar-assignment.md) capability. It is not a vela address
exception; the task's native D0 transition passed, but BAR0/base zero prevents
the descriptor read until that prerequisite is implemented and qualified.

## Delivery tasks

Each task needs its own implementation go after the decisions are settled.
Task 1's snapshot and owner check are complete. Task 2's code is implemented;
its descriptor read still needs native qualification. See the
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
2. [x] **LPSS preparation and one HID descriptor read.** Prepare the compatible
   PCI controller/MMIO resources before AP startup; after the snapshot, bind its
   exact companion. Audit D0, LPSS reset/clock and DesignWare capability/timing
   setup. Complete claims/BAR preparation before AP startup. Use bounded FIFO
   transfers with repeated START/STOP, clock-stretch
   tolerance and explicit arbitration-loss, address/data-NACK, busy and timeout
   results. No DMA or I2C address scan. Validate the descriptor's length/version,
   register fields and declared limits. Do not reset other clients to hide a
   failure. Separate Intel LPSS attachment from the reusable DesignWare master;
   later AMD AMDI0010 attachment must not depend on Intel registers or PCI.
   **After this task the owner can:** see a validated HID v1.00 descriptor and
   device identity read from vela's touchpad, without input publication.

   - [ ] Owner check: native validated descriptor read; resource discovery
     alone is not this check. See the [task 2 record](../development/experiments/vela-lpss-descriptor/README.md).
3. **HID-over-I2C polling transport.** SET_POWER, bounded RESET/zero-length
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
4. **Precision-touchpad input through the shared filter.** Decode the actual
   descriptor's digitizer collections, complete contact frames, Contact IDs,
   Tip/Confidence, geometry/units and physical buttons. Select descriptor-derived
   [Input Mode](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-configuration-collection)
   3 through a feature report, preserving unrelated feature fields; do not also
   route compatibility-mouse reports. Reapply after reset and rebaseline only
   from fresh, complete frames. Give the existing filter source-owned state and
   explicit contact/palm hints; preserve PS/2 behavior and configuration policy.
   Keep physical switches independent of contact rejection. More-than-two-finger
   gestures and tap-to-click are outside this slice.
   Audit the whole filter's added latency at the measured rate, including
   `TOUCHPAD_STABLE_SAMPLES=2` admission and rebaseline gates; preserve rejection
   semantics without adding an unnecessary smoothing or reporting delay.
   **After this task the owner can:** move, click/drag and two-finger-scroll with
   vela's own touchpad, including palm/typing/lift and loss checks.
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
including distinct report/input/output/command/data register IDs; an index of
zero is not rejected by itself.

Native task 2's first owner run (`a54a7229`, approximately 20:00) retained the
same endpoint but refused before MMIO: COMMAND `0x4`, PMCSR `0xb` (D3hot,
`NoSoftReset`). The generic BME guard caused that refusal; descriptor read is
still unqualified. In vela's supplied tables, conditional I2C0 `_PS0` is empty,
with no controller/ancestor `_PR0`; the optional child power-resource block is
disabled in the captured table. `_PS3` can clear BAR0, so it is not a rollback.
No AML power method is needed for this checked native D0 path. Linux at the
same pinned commit orders platform D0, PCI PM D0/delay/readback and only then
LPSS enable/reset (`drivers/pci/pci.c`, `pci-driver.c`, `mfd/intel-lpss-pci.c`).
This implementation preserves PME policy and refuses reset-causing wake rather
than implementing Linux's general BAR-restoration/platform-power machinery.

**Native attempt 2, `7003c117`, approximately 20:07:** D0 preparation passed:
PMCSR `0xb -> 0x8`, 10 ms wait, BME off, BAR values retained. BAR0 was already
unassigned (`memory64 base=0`), so bootstrap mapping correctly refused. No HID
descriptor was read. Existing PCI discovery neither assigns BARs nor retains
foreign BAR sizes; a mapped address or root aperture is not proof of vacancy.

### Unassigned BAR integration

The owner accepted the general [PCI BAR assignment](pci-bar-assignment.md)
capability on 2026-10-15; #897 precedes this PR. The LPSS unassigned path keeps
IO/MEM/BME off across verified D3hot-to-D0 preparation and reserves its own
4 KiB virtual window before AP startup. Only after the exact live companion
matches does the BSP worker request the generic allocator's checked placement.
It validates the actual sized BAR extent, completes the D3hot ownership fence,
enables/reads back memory decoding and runs the same LPSS reset/identity setup.
No unowned BAR is sized and no address is taken from the Linux snapshot.
Failure retains the native window/claim; no descriptor or pointer success is
published. Native assignment and descriptor read remain pending.

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
   whole-kernel accounting project. If existing tools cannot expose native CPU
   time, stop with a bounded measurement-only proposal rather than label elapsed
   transfer time as CPU utilization.
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
CPU use is only a proxy; no native polling baseline is captured yet.

Ordinary builds and 1/4-CPU QEMU/GDB check absence, refusal and unchanged PS/2/USB
input. QEMU is not qualification of vela's LPSS, GPIO or real ELAN reports; no
new emulator, test infrastructure or injected-device program is proposed.

Until vela has Remote over the dock, the owner deploys native packets with
`stick-deploy --initrd`, preserving the installed menu. Use the normal entry so
output saved to `home://` persists for `stick-deploy --mount` collection. Pool
tools are 0.0.5: packet commands use `boot://NAME.pxe`, with no shell quoting.
Owner-attended screen runs use no blanking option. Unattended OLED runs use
**`display.idle_blank=0`**, which blanks immediately; it never means disabled.
Descriptor discovery/reset/idle capture need no fingers; the owner must physically
exercise movement, clicks/drag, two fingers, resting palms, typing and lifts.
The orchestrator can drive build/staging instructions and, once Remote works,
log collection, ordinary workloads and integrity checks. It cannot synthesize
the owner's physical touchpad checks. No Deck deployment is part of this track.

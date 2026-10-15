# Vela I2C touchpad

Status: **owner-assigned proposal, 2026-10-15; decisions below are proposed.**
No driver implementation is authorized by this document. The
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

## Delivery tasks

Each task needs its own implementation go after the decisions are settled.
Task 1 is the first small step; there are no native results yet.

1. **Live firmware endpoint snapshot.** On the ACPI worker, locate ELAN06FA/PNP0C50,
   evaluate `_STA`, `_CRS` and HID-I2C `_DSM`, and copy owned resource results:
   I2C source/address/speed, descriptor register, GPIO/IRQ source and electrical
   attributes. Reject missing, malformed or ambiguous resources; retain no
   borrowed uACPI buffers.
   **After this task the owner can:** read the actual vela endpoint and interrupt
   route in a bounded diagnostic, including values unavailable in the dump.
2. **LPSS preparation and one HID descriptor read.** Prepare the compatible
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
3. **HID-over-I2C polling transport.** SET_POWER, bounded RESET/zero-length
   completion, report-descriptor fetch and descriptor-bounded input/feature
   transfers. Follow the [published enumeration sequence](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/plug-and-play-support-and-power-management).
   Poll first, with explicit idle/no-report behavior and bounded draining. A
   narrow GPIO input-level reader may be needed here to poll the asserted line;
   enabling/routing GPIO interrupts remains task 5. Never assume unconditional
   input-register reads are safe. Loss or malformed/incomplete reports reset
   assembly and source state; no old-contact replay or permanent busy loop.
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
   **After this task the owner can:** move, click/drag and two-finger-scroll with
   vela's own touchpad, including palm/typing/lift and loss checks.
5. **Intel GPIO interrupt, polling fallback.** From the live resource and proven
   Panther Lake pin-controller layout, own the specific line, acknowledge its
   level correctly and wake the BSP worker; no I2C/AML in interrupt entry.
   Preserve neighboring pins and firmware ownership. Compare delivery/idle cost
   with polling; retain the working polling route on failed interrupt delivery.
   Keep the GPIO attachment separate from later AMD GPIO/FTS3528 work.
   **After this task the owner can:** use the same touchpad behavior with measured
   interrupt-driven idle cost and an honest polling fallback.

## Proposed owner decisions

1. **Split preparation and ownership. Default:** complete checked PCI claims
   and BAR preparation before AP startup; no touchpad I2C command
   until the live ACPI snapshot selects its companion. One BSP controller worker
   serializes transfers; the ACPI worker alone executes AML. Keep LPSS reset and
   clock writes with the native PCI owner, without relaxing AML PCI-write policy
   or adding userspace bus access. GPIO MMIO/IRQ preparation must also preserve
   boot/runtime mapping ownership; the native driver cannot borrow the ACPI
   worker's mapping window. If live discovery cannot use safely prepared
   resources, stop with the concrete conflict before extending the contract.
2. **Polling and measurement. Default:** start at a tunable 8 ms service interval,
   with finite report/transfer budgets and sleeping between attempts. Prefer a
   qualified GPIO asserted-level read; if that route is unavailable, establish
   safe idle input-read semantics before admitting an alternative. A dependency
   on GPIO status is part of task 3, not an assumption that interrupts already
   work. Capture idle cost before polling; no permanent hot-path diagnostics or
   whole-kernel accounting project. If existing tools cannot expose native CPU
   time, stop with a bounded measurement-only proposal rather than label elapsed
   transfer time as CPU utilization.
3. **One touchpad pipeline. Default:** source-owned filter state with explicit
   presence/identity/orientation and optional palm hints. Tip and Confidence are
   not fabricated Synaptics pressure; pressure/W thresholds remain applicable
   only to transports with those scales. Keep existing `CONFIG_TOUCHPAD_*`
   values and shared movement/typing/edge/scroll policy as tunable defaults;
   missing required geometry or incomplete frames refuse/reset rather than
   inventing calibration. No new pointer ABI or second gesture engine.

## Qualification and who does what

Before implementation, capture exact main/pins, boot time and the unchanged
ordinary launch/IPC controls. Before polling, capture repeated native idle
observations at the prompt, on AC, same display/log/audio/network state, with
the touchpad untouched. Repeat matched A/B/A/B after polling and interrupts;
record interval, wake/service count, report/transfer failures and available CPU
accounting. Distinguish elapsed worker-service bounds from CPU time. Nested QEMU
CPU use is only a proxy. No baseline is claimed captured by this proposal.

Ordinary builds and 1/4-CPU QEMU/GDB check absence, refusal and unchanged PS/2/USB
input. QEMU is not qualification of vela's LPSS, GPIO or real ELAN reports; no
new emulator, test infrastructure or injected-device program is proposed.

Until vela has Remote over the dock, the owner must deploy each native stick
packet (lsblk-first, preserve installed data), boot it and collect the log/photo.
Every hands-free vela packet uses **`display.idle_blank=0`** for its OLED.
Descriptor discovery/reset/idle capture need no fingers; the owner must physically
exercise movement, clicks/drag, two fingers, resting palms, typing and lifts.
The orchestrator can drive build/staging instructions and, once Remote works,
log collection, ordinary workloads and integrity checks. It cannot synthesize
the owner's physical touchpad checks. No Deck deployment is part of this track.

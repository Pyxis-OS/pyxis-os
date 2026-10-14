# Vela power button

## Contract and source evidence

Owner event-route decision accepted 2026-10-14: retain existing event sources;
an additional runtime power-button GPE requires AML proof and selective enable
of its existing handler. `_PRW` alone is insufficient. No general runtime-GPE
finalization or guessed GPE. All discovery, AML and retained Notify contexts
belong to the BSP ACPI worker until reboot.

Present `PNP0C0C` devices accept Notify `0x80`. Intel `INTC10CC` devices require
simple mode (HDMM=0), request the advertised array through BTNC/BTNE, then execute
BTNL. Advertised event-filter `_DSM` functions use revision 1, with named-method
fallback. Successful command execution accepts a void or arbitrary return value;
integer-valued queries remain typed. The array request precedes BTNL so firmware
keeps its final policy. No HDSM hotkey enable. Intel Notify `0xce` presses,
`0xcf` releases, and `0xc0` reads HDEM: only power index 9 acts.

The [pinned Linux Intel HID source](https://raw.githubusercontent.com/torvalds/linux/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/platform/x86/intel/hid.c)
uses an integer helper for BTNL, but failure warns without disabling its input
handler. Thus Linux does not literally ignore the method's return; Caelum's
fatal admission check was the incompatible assumption. Linux requests the
advertised button-array mask before loading buttons and recognizes HEBC function
9; Caelum retains the second mask byte for that function. Source inspection only;
no GPL driver code copied.

A press uses existing physical-access clean power-off, storage checks and the
recovery overlay. Startup and operation-time notifications drain while disarmed;
queued presses coalesce. Failure leaves the machine usable, with Force available
only through the explicit overlay. No ABI or dependency change.

## Failed native control and AML route — 2026-10-14

The owner's first Vela boot of the diagnostic packet did not react to a press.
PNP0C0C registered, while BTNL's missing integer return disabled the Intel
handler. The supplied local DSDT/SSDT tables were disassembled with iasl; raw
firmware and identifying data remain outside the repository.

The DSDT establishes the runtime chain:

- `\_SB.PC00.LPCB.Q_EC._Q10` calls `\_SB.PWPR`. On the modern OS path with
  `HIDD.BTLD=1`, PWPR sends HIDD Notify `0xce`; otherwise PWRB receives `0x80`
  when its press route is enabled.
- `_Q11` calls PWRR, which sends HIDD Notify `0xcf` on the same Intel path.
- HIDD's `_HID` is `INTC10CC`. BTNL returns nothing, sets `PWRB.PBST=0` and
  `HIDD.BTLD=1`, and applies the firmware's final array policy. `_DSM` function 1
  calls it, then returns a buffer. The old typed helper rejected that successful
  call, repeated BTNL through fallback, and disabled the now-selected handler.
- Q_EC is the present `PNP0C09` embedded controller. Its `_GPE` is selected by
  firmware, including an ESPI branch; no number is hardcoded. Existing
  `acpi_ec_start()` evaluates it, installs/enables the EC handler and dispatches
  `_Qxx`. No separate `_Lxx`/`_Exx` runtime press route was found. PWRB has no
  `_PRW`.

This is static firmware evidence, not a measured SCI/Notify delivery. It calls
for correcting Intel admission, with **no additional GPE enable**. Preparation's
“additional GPE unqualified … none enabled” describes additional routes; the
existing EC route already has a handler. The supplied failure summary named
INT34C3, but the saved ELF, source and supplied AML identify INTC10CC.

## QEMU qualification

Default/info kernel and image builds passed with LLVM builder
`pyxis-llvm23.1.3-df87791`, using matching prebuilt SDK/userland/ports bundles.
No compiler rebuild. Code follow-up `f0524baa`.

Q35/KVM, host CPU, four CPUs, 512 MiB, VGA/PS2, VirtIO RNG, fresh OVMF variables,
VirtIO-SCSI ISO boot, no disk or passthrough. The unchanged fixed-event control
and initial implementation both powered off cleanly through QMP
`system_powerdown`. GDB observed disarmed startup and armed runtime handling.

A local synthetic SSDT, unrelated to the native firmware, supplied a named
void BTNL and a buffer-returning DSM load, plus Notify methods. The pre-fix
kernel disabled both Intel handlers. After the fix both stayed enabled; GDB
observed one BTNL call each and the advertised masks 5 and 17 delivered to BTNE.
DSM function 9 remained admitted. Manual AML calls on the ACPI worker verified
release `0xcf` and non-power `0xc0` did nothing, while HDEM index 9, Intel press
`0xce` and PNP0C0C `0x80` each reached clean power-off in separate boots. This
qualifies software behavior, not Vela's physical transport. No test framework or
firmware fixture is committed. Local captures remain in `build/power-button/`;
all task-owned QEMU/debuggers are stopped. No performance claim.

## Native retest packet

`/shared/present/vela-power-button/README.md` contains verified ELF/config hashes,
owner **`/dev/sdc1`** copy/restore commands (check `lsblk` first), and one short
press after startup. Never-merged diagnostic branch
`diag/vela-power-on-75a8aaac` at `45c61551`: stick base `75a8aaac`, existing #847
boot fix, #853 NVMe code, then #855 and this follow-up. Kernel-only info build;
keep the stick's initrd/Limine, `nvme.write=0`, and no #848/newer ABI/pins.
Native firmware and disks are untouched by Codex. Physical button delivery is intermittent in the owner retest below; detailed
shutdown logs are not yet supplied.

The same owner boot confirmed Vela NVMe 2.0 namespace readiness; see the
[NVMe record](../nvme-task1/README.md#vela-native-readiness--2026-10-14).


## Intermittent native delivery — 2026-10-14

The owner retested diagnostic kernel `45c61551`. PNP0C0C and HIDD registered;
the Intel array request reported capabilities `0x1f`. Five or six short presses
(0.5–1 s) did nothing at least a minute after the shell appeared; a later short
press did power off. **Delivery is intermittent, not qualified.** Earlier reports
of a native pass or a 30-second startup-only window were corrected; a long press
may have been the firmware forced-off gesture. No post-press log yet identifies
where short presses were lost. NVMe shutdown confirmation is not inferred.
The reported HID spellings differ from the saved ELF/AML; no additional device
match is inferred from that transcription.

The worker arms after namespace/device setup and disarmed startup-work drain;
this cannot explain steady-state lost presses. The never-merged diagnostic kernel
`17acbab9`, on the same `45c61551` retest base, timestamps SCI dispatch, EC GPE
status and query numbers, PWRB/HIDD Notify values, and the power latch/worker
handoff. It does not change query draining, enables, event handling or shutdown
policy. Firmware
prerequisites and missed-edge/queue hypotheses remain under investigation.
Earlier-readiness changes are not part of #855. Keep this PR draft until the
intermittence is resolved and owner-verified.


### Diagnostic packet and prerequisite audit

Staged at `/shared/present/vela-power-button/diag/`, with verified ELF/config
hashes, owner `lsblk`-first `/dev/sdc1` copy/restore commands and a short-press
run sheet. The existing info/trace-only logging system is unchanged: this image
contains targeted `power-debug` records rather than enabling broad trace output.
It reads EC status/GPE info and the IO-APIC SCI entry, counts SCI/GPE hits and
already-queued query work, logs every query number/time and drain stop reason,
and records every device Notify plus press admission/drop state. Five-second
heartbeats expose silence and pending state; they introduce diagnostic wakes,
not query polling/retries. Logging may perturb the intermittent race.

Direct startup readbacks include OSYS, ECAV/OSTP, HIDD BTLD/BTS1 and advertised
flags. `_Q10`/PWPR has no press-duration gate: HIDD is selected for modern OSYS
with BTLD set; PWRB requires PBST otherwise. uACPI supports `_OSI("Windows
2015")` by default, and the supplied `_SB._INI` sets OSYS accordingly. The
firmware's EC `_REG(3,1)` writes OSTP (OS code 6 for that OSYS), but the supplied
EC fields are SystemMemory and no EmbeddedControl OperationRegion was found.
uACPI's handler registration does not appear to invoke that orphan `_REG`.
Whether OSTP is unset, and whether that affects event generation, remain
hypotheses. The diagnostic only reads it; it does not call `_REG`, change `_OSI`
or execute additional enable commands. HDEM/BTNS are not used as readbacks
because they have AML side effects.

Default/info diagnostic build passed. A matching-base75a8aaac archive and local
synthetic button AML in Q35/KVM, four CPUs, 2 GiB, fresh OVMF/VirtIO-SCSI ISO boot
verified timestamped CF/CE Notify, admission and worker dispatch through clean
power-off. No EC hardware/query delivery is emulated by that check. All task-owned
QEMU/GDB stopped. No native data writes or behavior fix is authorized by these
diagnostics; #855 remains draft awaiting the owner capture.


### Diagnostic owner run and GPIO audit — 2026-10-14

Owner screen OCR (best effort, incomplete), diagnostic `17acbab9`, roughly ten
presses of varied lengths: none was seen to cause software power-off. A longer
hold later produced an unidentified line before power-off; without that line,
this is not proof of a Pyxis press or shutdown. Periodic heartbeat output could
also appear during a firmware forced-off gesture.

Preparation recorded BTLD=1, BTS1=0x1f, PBST=0, PWRB Notify 1 and input arming.
Heartbeat EC status 0x08, GPE 0x6e info 0x19 means software/hardware enabled with
a handler, no sampled pending GPE status. IO-APIC entry 0x802a is unmasked,
level/active-high, matching the supplied MADT SCI IRQ9 override flags 0x0d.
The one captured SCI at t_ms=9530 saw EC status 0x28, dispatched query 0x12,
and drained to SCI_EVT-clear. `_Q12` processes battery-temperature/platform
conditions and notifies IETM 0x88; it is not the power path. No query 0x10 or
HIDD power Notify is reported in this capture. This proves at least one EC SCI
worked, not that firmware asserted none during every press; the OCR record is
incomplete and snapshots can miss transient state.

The diagnostic's typed integer evaluation incorrectly rejects Field Units.
Warnings make OSYS/AEAB/AHDB/HEFE/HEB1/S0ID zero readbacks unusable. OSTP was
reported as result=0/value=1, but also appears in the Field Unit warning list;
treat that OCR association as unverified until a proper field read or video
confirms it. ECAV and other plain integer Names remain readable. A reported
OSTP value would weaken the unset-byte hypothesis, but does not establish the
modern OS code without a valid OSYS readback. No manual `_REG` is justified.

Reinspection covers the supplied DSDT plus all 34 SSDTs (raw files match their
local disassembly inputs). No `_AEI` or `_EVT` NameSeg exists in those tables.
GpioInt descriptors are G_IN resource templates consumed by touch and Intel CVS devices,
not an ACPI GPIO event route to HIDD/PWPR/PWRB. The only call to root PWPR is
EC `_Q10`; release is `_Q11`/PWRR. HPEM's other notification path is the HIDW
wake index 0x1b, not the power index 9. Conditional CBTS/RBED have no definition
in the supplied namespace tables. No additional runtime `_Lxx`/`_Exx` power
route is established. This cannot exclude opaque SMM/EC firmware or dynamically
supplied tables, but it does not justify a GPIO-controller driver as this fix.

The checksum-valid supplied FADT (276 bytes) has flags 0x20c4f5: PWR_BUTTON=1,
HW_REDUCED_ACPI=0, SCI IRQ9. Firmware declares a control-method power button,
not the fixed PM1 power event; Caelum deliberately does not enable that fixed
source. Nothing in the visible `_Q10`/PWPR chain imposes a hold threshold.

Next evidence: phone video of Caelum before/during release of a short press,
then a held press only if the owner chooses firmware forced-off. Look for EC
query 0x10, HIDD CE/CF (or PWRB80), admitted latch, power-worker dispatch and
ordinary shutdown lines. A heartbeat alone is not a button event. Do not add a
hold-safe shutdown suppression or a guessed GPIO/GPE enable; neither follows
from this evidence. Correct Field Unit reads and wider read-only pending-GPE
observation are diagnostic candidates if another image is requested. #855
remains draft; native delivery is not qualified.

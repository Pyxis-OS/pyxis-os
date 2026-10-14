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
Native firmware and disks are untouched by Codex. Physical Notify delivery and
clean power-off remain **retest pending**.

The same owner boot confirmed Vela NVMe 2.0 namespace readiness; see the
[NVMe record](../nvme-task1/README.md#vela-native-readiness--2026-10-14).

# Renoir flip task 1: read-only inventory

2026-10-09. [Accepted design, #632](https://git.internal/PyxisOS/pyxis-os/pulls/632).
Diagnostics only; no GPU, PCI configuration or memory-controller writes, ATOM
commands, firmware messages, scanout allocation or flips. Task 2 is not assigned.

## Conclusion and evidence boundary

**No driver-owned spare pool is proved.** The present boot interface supplies no
allocator handoff assigning extra scanout storage to Caelum. Native inventory is
pending; do not confuse that with a negative finding about the actual firmware's
reservations. Keep unsynchronized GOP copying. Missing prerequisites never
justify allocating a BAR remainder or enabling flips.

Task 1 records the GOP route and address correlation, UEFI/boot memory descriptors,
posted ATOM reservation fields and visible DMCUB mappings. The inspected Linux
sources supply no complete read-only pre-OS PSP/SMU reservation map. Those owners
remain unknown, not absent. A disabled DMCUB window does not prove deallocation.

Pool proof needs a firmware/boot contract assigning a bounded range to this
kernel driver, exclusion of all firmware/security/display clients, compatible
linear layout, verified CPU/GPU translation and a lifetime through GPU retirement.
A reserved UEFI descriptor, zero contents, a driver-size ATOM field, Linux's
available VRAM or successful allocation, or an unused-looking remainder is
insufficient. A validated posted VBIOS table establishes reported reservations,
not the completeness of all other owners. Linux also retains a minimum 9 MiB
stolen VGA region on Renoir for external clients; its complement is not a pool.

## What the probe reads

`DISPLAY_INVENTORY=1` adds `display.inventory=1` to normal, rescue and installer
entries. Default `0` emits no inventory lines and skips VFCT parsing and MMIO
inventory. Diagnostics use info-level `renoir-inventory:` lines, once per boot.
The inventory does not change the presenter cadence or timing capability.

Limine remains in its adapter. It copies at most 256 validated EFI descriptors
(version 1, at least 40 bytes, 4 KiB page units) into numeric boot metadata.
Missing/invalid/over-capacity maps are explicitly unavailable evidence. The
bootstrap ACPI lookup checksum/bounds-validates VFCT (1 MiB maximum); the parser
bounds every image/table, accepts at most four VBIOS images, and matches PCI
BDF/vendor/device. Only numeric metadata survives paging: no raw VBIOS, ACPI
UUID/OEM strings, panel EDID, serials or MACs are retained or logged by the probe.

Supported table layouts are ROM 1.1/2.1/2.2, master 2.1,
`vram_usagebyfirmware` 2.1/2.2 and `firmware_info` 3.0–3.5. Versions, sizes,
KB units, operation flags and known/unknown fields print explicitly. Extra
reserved size is read only for firmware-info 3.4/3.5; protected-region size only
for 3.5. Unknown or malformed metadata cannot confer ownership.

After PCI discovery and before AP startup, require one unclaimed Renoir
1002:1636 display endpoint, memory decode, verified D0 and the expected BAR5
assignment. Map only ten audited register pages supervisor RO/NX/UC; reject RAM,
framebuffer/WC or another claimed PCI mapping overlap. No BAR sizing probe or
indirect index/data transaction. Read twice, recheck identity/D0/BAR, then unmap.

Record four HUBPs/OTGs/OPPs and six MPCCs. Identify a stable single-source route:
OTG ODM selectors to OPP, MPC output to MPCC, MPCC top to paired HUBP/DPP, plus
HUBP VTG selector to OTG. Preserve Linux's disconnected-SEG1 stale-count and
self-pointing-bottom conventions. Stereo, combined ODM and multiple-layer routes
are not classified as the bounded mono candidate. Raw surface records include
format/pitch/viewport, address configuration/swizzle, DCC/TMZ/VMID, primary,
metadata, current/earliest in-use addresses, flip controls and color crossbar.
This is inventory, not certification of a future backend's complete prerequisites.

Read DCN/GC/MMHUB framebuffer base/top/offset, NBIF memory size, VMID0 page-table
context and HUBP system-aperture/TLB state. Direct framebuffer correlation uses
`CPU = GPU - FB_BASE + FB_OFFSET`, cross-checks GC/MMHUB and the whole GOP extent,
requires primary/current/earliest agreement and excludes an overlapping known
page-table range. VMID0 alone is insufficient. Record DMCUB control/security,
eight cache windows and uncached regions 4/5; enabled mappings are exclusions,
never an exhaustive reservation or spare-pool declaration.

Source: Linux v6.19.10, commit `271f8eab9590b57a2ff0c8c9eee357723c4a85cb`.
[VFCT](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_bios.c#L374-L423),
[ATOM reservation contract](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/atomfirmware.h#L742-L799),
[DCN translation context](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubbub/dcn20/dcn20_hubbub.c#L394-L425),
[native APU CPU base](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/gmc_v9_0.c#L1710-L1750),
[DMCUB windows](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dmub/src/dmub_dcn20.c#L194-L271),
[Renoir stolen-memory limit](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_gmc.c#L997-L1044).
Adapted MIT definitions/provenance/notices are in
[AMD NOTICE](../../../../arch/x86_64/amd/NOTICE), retained in kernel bundles and images.
No upstream downloads are added to the build.

## Local validation

Baseline: unmodified main `807cba49`, existing LLVM23.1.3 builder, matching verified
SDK/userland/ports payloads. An earlier remote-EOF bundle was rejected because
main's launcher header changed; it was not used for qualification. Kernel ELF
`c17e32899fe3b762c19cc0a57f6e2ff74009a2c7dff2f8e92ac08c1f30888c68`, initrd
`ec6a9aab937af4d12486002069749ff225f2a70faa6b10359f569974ebf3e120`.
QEMU10.2.2, Q35/nested KVM/max, four CPUs/2 GiB, std VGA, fresh OVMF,
relative PS/2, VirtIO-SCSI CD/RNG; no NIC/disk/USB/audio. Baseline reached the
local shell; Super+Right and `ls` worked, caret/I-beam visible.

Code `bd917773`: default and opt-in images built with the same builder and
unchanged verified SDK/userland/ports. Both QEMU boots reached normal userspace.
Default emitted no inventory lines and GDB confirmed both option/capture false.
Opt-in emitted only begin plus Renoir-unavailable; GDB confirmed option/capture
true, timing off, 105 valid EFI descriptors, no VFCT and all register mappings
zero. Super+Right and `ls` completed in the opt-in boot with caret/I-beam visible.
Exact-head CI and sealed artifact checksums are recorded in the PR.
QEMU has no DCN and cannot qualify the native
route, memory ownership or reservation completeness. No new tests/fault injection.

## One native PXE boot

1. Build the submitted revision using the existing builder, `LOG_LEVEL=info`,
   `DISPLAY_INVENTORY=1 DISPLAY_TIMING=off DISPLAY_TIMING_METRICS=0 LOG_UDP=1`.
   Keep the normal GOP mode, boot configuration and devices. No gameplay workload
   or camera batch is needed for this read-only task.
2. Have **Luna** stage the sealed kernel/initrd/`limine.conf` set and checksum
   manifest from this task's `build/native-inventory/`. Alpha does not stage PXE.
   Confirm the command line contains `display.inventory=1 display.timing=off
   log.udp=1`; no global trace flag. The laptop is already in its PXE loop.
3. On horse, retain the existing UDP log for that one boot through normal startup.
   Give alpha the `renoir-inventory:` lines and revision/checksums. Mask unrelated
   MAC/serial fields before sharing; do not commit raw firmware blocks or logs.
4. Record the actual route/surface state, direct-address correlation result,
   EFI/ATOM/DMCUB extents and any unavailable/unstable evidence here. Conclude
   whether the complete pool proof exists. Unknown PSP/SMU ownership prevents
   treating a visible gap as free. Report the blocker to the owner; do not start
   allocation or write implementation.

- [x] Read-only diagnostics and source inventory prepared.
- [ ] Owner's native boot received and evaluated; task 1 closes only with that report.

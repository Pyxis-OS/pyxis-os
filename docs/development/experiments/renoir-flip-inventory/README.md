# Renoir flip task 1: read-only inventory

2026-10-09. [Accepted design, #632](https://git.internal/PyxisOS/pyxis-os/pulls/632).
Diagnostics only; no GPU, PCI configuration or memory-controller writes, ATOM
commands, firmware messages, scanout allocation or flips. Task 2 is not assigned.

## Conclusion and evidence boundary

The first native boot establishes a stable mono route and a 512 MiB UMA
framebuffer range. ATOM reports no firmware or driver VRAM reservation; that
is the table's claim, not proof that all firmware clients are absent. The GOP
address is consistent with a separate BAR0 aperture at VRAM offset zero, but
BAR0 was not captured in that boot. The revised reader records it without
sizing writes. The evidence-standard decision below is **pending**, not accepted.
No spare allocation or flip is authorized by this inventory.

The earlier requirement for an explicit firmware allocator handoff was too
strong as a Linux comparison. Linux initializes a VRAM manager and subtracts
fixed reservations before further driver allocations; it does not receive an
exclusive-pool grant from firmware. Whether Pyxis may adopt that exclusion rule
while preserving the GOP setup is the owner's decision. BAR0 confirmation and
the pitch discrepancy below remain separate backend prerequisites.

## Native ThinkPad evidence, 2026-10-09

Luna's build of `5e342488`, `LOG_LEVEL=info`, PXE with
`display.inventory=1 display.timing=off`. Local masked log:
`/shared/present/batch2/inventory-638.log`; raw logs/firmware are not committed.

| Evidence | Captured value / interpretation |
| --- | --- |
| Route | Stable `OTG0 <- OPP0 <- MPCC0 <- HUBP0`; other OTGs inactive. |
| Format/layout | Config `8`, crossbar `e40000` matches ARGB/GOP shifts `16/8/0`; tiling `80` has linear SW_MODE 0. Viewport `1920x1080`, origin zero; captured DCC/TMZ clear. |
| Addresses | Primary/earliest `0xf400000000`; generic INUSE zero. Flip pending/lock bits clear. VMID selector zero, TLB/control/page-table fields zero. |
| VRAM translation | DCN/MMHUB `f400..f41f`, offset/GC `810`: GPU `[0xf400000000,0xf420000000)`, direct CPU `[0x810000000,0x830000000)`, 512 MiB. |
| UEFI reservation | Type 0, `[0x80f340000,0x830000000)`, **550,240,256 bytes = 524.75 MiB**, covering UMA plus 12.75 MiB below it. It is not a 550 MiB descriptor. |
| GOP | CPU `0x860000000`, pitch 7680, 8,294,400 bytes (`0x7e9000`). Its CPU address is outside direct UMA. |
| ATOM | ROM 2.2, master 2.1, firmware-info 3.2 with capability 1 (posted), MC base `0xf400000000`; VRAM-usage 2.1 has firmware start/size/flags `0/0/0`, driver size 0. No extra-reserved-size field in this firmware-info version. |
| DMCUB | Control `20000`: enable clear, soft reset set; all captured cache/uncached windows disabled and zero. This is not a complete PSP/SMU ownership map. |

The old `direct-GOP-correlation=0` had two causes: it required the generic
INUSE register to equal primary, and it compared only the direct RAM alias.
Linux's [pending predicate](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn20/dcn20_hubp.c#L925-L950)
uses pending plus **earliest** in-use, not generic INUSE. The revised diagnostic
retains generic INUSE as raw evidence without making its zero a contradiction.
Its precise latch semantics were not established; no latch-control write is used.

`VRAM_offset = primary - GPU_FB_base = 0`. Direct RAM correlation gives
`0x810000000 + offset`; the GOP-aperture candidate is `BAR0 + offset`, following
[Linux's BAR0 setup](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/gmc_v9_0.c#L1726-L1750)
and [VRAM CPU mapping](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c#L611-L620).
If the next capture confirms BAR0 `0x860000000`, the observed GOP span correlates
to VRAM `[0,0x7e9000)`. This is a strong candidate, not a measured BAR0 value yet.
The BAR0 extent remains unknown; do not extrapolate the GOP span to a full BAR.

Pitch is unresolved: raw `0x780` is 1920, equal to GOP `7680/4`, while Linux
[programs pitch minus one](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn20/dcn20_hubp.c#L328-L366)
from [framebuffer byte pitch / cpp](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm_plane.c#L860-L870).
That convention would interpret this raw value as 1921. The source establishes
Linux's programming convention, not a resolved hardware stride for inherited
firmware state. Do not certify pitch compatibility or rewrite it on this evidence.

## Linux pre-initialization reservations on this Renoir

Pinned Linux v6.19.10, `1002:1636` is
[Renoir/APU](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_drv.c#L2123).
[TTM setup](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c#L1959-L2039)
creates the VRAM manager, then reserves firmware usage, driver usage, conditional
discovery/TMR, stolen VGA, extended stolen and ASIC-specific stolen regions.
These precede subsequent BO allocations; they are not a firmware allocator handoff.
For `V=0x20000000`, half-open offsets are:

| Reservation | Applicability to captured state |
| --- | --- |
| `fw_vram_usage` | Zero: ATOM 2.1 flags 0 does not populate Linux's SR-IOV reservation, and reported firmware size is zero. |
| `drv_vram_usage` | Zero: 2.1 driver usage feeds host scratch; default 20 KiB is CPU memory, not a VRAM carve-out. |
| Discovery/protected top | If `reserve_tmr`, last 64 KiB: `[0x1fff0000,V)`. Preserve this conservatively. |
| GDDR6 training C2P | None under capability 1: training bit `0x400` is absent. |
| Stolen VGA/pre-OS | `[0,0x900000)`, 9 MiB, retained on Renoir. |
| Stolen extended | Empty here: 1920x1080x4 is smaller than 9 MiB; extended is framebuffer estimate minus prefix, not remaining VRAM. |
| ASIC-specific stolen | None: the special fixed area is for Vega10 Hyper-V VF, not native Renoir. |

[ATOM parsing/scratch](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_atomfirmware.c#L105-L210)
and [extra-reserved-size getter](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_atomfirmware.c#L909-L944)
explain why firmware-info 3.2 supplies no such size. The
[GMC stolen policy](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_gmc.c#L986-L1057)
retains a minimum 9 MiB because another client can write the first 8 MiB on S3
resume. Its [DCN2.1 framebuffer estimate](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/gmc_v9_0.c#L1343-L1382)
uses viewport area times four, so separately preserve the actual GOP allocation.

Do not infer that Renoir skips discovery from the old “NAVI10 and later” comment.
This tag's [default discovery route](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c#L2862-L2873)
has no static Renoir case or
[Renoir fallback filename](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c#L442-L468).
The [memory reader](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c#L296-L325)
sets `reserve_tmr` for valid nonzero memory size; its system-memory alternative
does not. The [top-region helper](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c#L1717-L1778)
uses the firmware-info reserved size or a 64 KiB fallback. This native boot did
not parse the discovery payload, so its actual content is unmeasured.

PSP's later TMR is distinct: [PSP12 clears boot-time TMR](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c#L205-L209),
and Linux [allocates/configures a VRAM/GTT BO](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c#L849-L886),
normally [4 MiB](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/amdgpu/amdgpu_psp.h#L38).
Renoir SMU [driver tables request VRAM](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/pm/swsmu/smu12/renoir_ppt.c#L156-L166),
then Linux [allocates a shared BO](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/pm/swsmu/amdgpu_smu.c#L974-L1029)
and sends its address. The optional logging pool normally uses GTT; its
[debug alternative uses VRAM](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/pm/swsmu/amdgpu_smu.c#L1075-L1113).
GART and DMCUB BOs are later allocations, not additional fixed pre-OS ranges.

Pre-OS PSP/SMU location/completeness remains unknown. They can use system DRAM
or internal memory, and this UEFI reservation includes 12.75 MiB outside the UMA
range, but it is **not** identified as their storage. Linux's later BOs do not
prove that their firmware predecessors were inside or outside UMA. The low
prefix/top tail exclusions, posted zero-usage table and disabled DMCUB reduce
the concern for a low spare; they do not establish absence of an undocumented
client there. Adopting Linux's exclusion rule without reinitializing those
clients is an explicit residual risk for the owner to accept.

## One owner decision — pending

**Adopt a Linux-derived exclusion rule as sufficient ownership evidence for this
bounded inherited-GOP backend? Recommended default: yes**, replacing the explicit
allocator-handoff proof requirement. Treat the validated 512 MiB UMA range as
kernel-driver-owned except actual GOP/VGA storage, all reported firmware/driver
and live-client reservations, applicable discovery/training/stolen reservations,
and an additional conservative last-16-MiB guard `[0x1f000000,V)`. The guard covers
the expected discovery tail and provides distance from the top; it is a proposed
cushion, not evidence that PSP/SMU storage is confined there. Any newly identified
reservation must be excluded in full, even if larger than the guard.

For one spare, use `align_up(max(actual_GOP_end, stolen_prefix_end), 64 KiB)`
provided the entire aligned allocation is below every upper exclusion. With
BAR0/GOP correlation confirmed and this capture's extents, the candidate starts
at offset `0x900000`; 8,294,400 payload bytes occupy `[0x900000,0x10e9000)`, with
64 KiB-rounded backing `[0x900000,0x10f0000)`. Corresponding direct CPU start is
`0x810900000`, GPU start `0xf400900000`; a BAR0 alias would be `0x860900000` if
that base is confirmed. Prefer one WC direct-UMA mapping, not simultaneous
WB/WC aliases. Preserve the original GOP surface and all possible fronts.

Acceptance would adopt an evidence standard, including the PSP/SMU residual
uncertainty. It would not allocate this candidate, resolve pitch or BAR0 extent,
implement flips, or start task 2. Declining keeps GOP copying and requires further
reservation evidence. Nothing in this round is accepted until the owner answers.

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
requires primary/earliest agreement and excludes an overlapping known
page-table range. Generic INUSE remains diagnostic. Read BAR0 low/high/type
twice without sizing; separately report direct-RAM and BAR0-aperture correlation.
Neither a matching base nor the GOP span establishes BAR0 length. VMID0 alone
is insufficient. Record DMCUB control/security,
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
The BAR0 correction was source-reviewed and the full default image rebuilt after
merging main `28051508` (#631/#632); changed SDK/userland/ports inputs were rebuilt,
not substituted by the first-boot bundles. BAR0's positive native path is still
unmeasured. The follow-up head/build/QEMU/CI evidence is recorded in the PR.
QEMU has no DCN and cannot qualify the native
route, memory ownership or reservation completeness. No new tests/fault injection.

## Read-only BAR0 follow-up

1. Build the submitted revision using the existing builder, `LOG_LEVEL=info`,
   `DISPLAY_INVENTORY=1 DISPLAY_TIMING=off DISPLAY_TIMING_METRICS=0 LOG_UDP=1`.
   Keep the normal GOP mode, boot configuration and devices. No gameplay workload
   or camera batch is needed for this read-only task.
2. Have **Luna** stage the sealed kernel/initrd/`limine.conf` set and checksum
   manifest from this task's `build/native-bar0/`. Alpha does not stage PXE;
   retain the original `build/native-inventory/` set as the first-boot record.
   Confirm the command line contains `display.inventory=1 display.timing=off
   log.udp=1`; no global trace flag. The laptop is already in its PXE loop.
3. On horse, retain the existing UDP log for that one boot through normal startup.
   Give alpha the `renoir-inventory:` lines and revision/checksums. Mask unrelated
   MAC/serial fields before sharing; do not commit raw firmware blocks or logs.
4. The first boot is recorded above. A BAR0 follow-up, with the same options,
   must capture stable BAR0 low/high/base plus both correlation results. Confirm
   or reject the aperture candidate; do not infer a BAR length. Keep allocation
   and writes deferred pending the owner decision and backend prerequisites.

- [x] Read-only diagnostics and source inventory prepared.
- [x] First owner native boot received and evaluated (`5e342488`).
- [ ] Native BAR0 follow-up and owner evidence-standard decision; task 1 stays open.

# Hardware-assumption audit

Source audit of `origin/main` at `a3efad31` (2026-10-18). This is a list of
remaining boot and device-coverage risks, not authorization to change drivers.
Severity ranks the consequence on a machine that exercises the condition; it
does not claim that every listed machine has that firmware configuration. All
code paths below were verified by reading. Hardware examples without a native
or QEMU observation are explicitly **suspected**.

| Rank | Source and assumption | Breaking configuration and consequence | Suggested check or fix | Evidence |
| --- | --- | --- | --- | --- |
| High | `arch/x86_64/acpi.c:380-417`, `arch/x86_64/clock.c:116-121`: ACPI must expose a page-aligned, memory-mapped HPET, even when a usable invariant TSC exists. | A machine without the HPET table panics before PCI or userspace. A dormant or badly gated HPET may also fail boot calibration. The table-absent case is reproduced by the recorded QEMU `q35,hpet=off` boot in [hardware recovery](../development/experiments/hardware-recovery/README.md). | Select and validate a clock source without requiring HPET first; keep a bounded calibration fallback and qualify all CPUs before selecting TSC. | source-verified, QEMU table-absent case recorded. |
| High | `arch/x86_64/acpi.c:460-476`, `arch/x86_64/pci.c:26-37`: exactly one MCFG allocation, segment zero, beginning at bus zero. | A valid multi-aperture MCFG or segment with a nonzero first bus disables **all** PCI discovery. NVMe, xHCI, NIC and any assignable GPU then disappear even if one aperture was usable. This is suspected on more complex laptops/workstations; [PCI-SIG explicitly permits multiple bases in one segment](https://pcisig.com/PCIFirmware/ECN/Firmware/EnablingMultipleBaseAddressesperPCISegmentGroup). | Retain separate verified segment/bus apertures and enumerate each; refuse only ambiguous or unsafe ranges, with per-aperture diagnostics. | source-verified, hardware trigger suspected. |
| High | `kernel/display/display.c:78-112`, `boot/limine/entry.c:359-365`: an exposed linear framebuffer or one of the supported GPU drivers is mandatory. | Headless systems and a GOP `PixelBltOnly` mode have no linear framebuffer; without a supported PCI GPU, boot panics. UEFI [defines `PixelBltOnly` as lacking a linear framebuffer](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html). A laptop whose NVIDIA dGPU owns the output may still boot from GOP, but cannot rely on native mode setting here. | Make display optional for boot/service operation, with an explicit console or remote diagnostic path; add GPU support separately. | source-verified, hardware trigger suspected. |
| Medium | `arch/x86_64/acpi.c:105-116,295-305,350-356`: early MADT/FADT/HPET/MCFG scans fully validate **every** XSDT entry before testing its signature. | One unrelated optional table with a bad checksum, truncated extent or bad pointer panics before the relevant table is found. A firmware with a malformed SSDT is a suspected trigger; this is a boot failure rather than only loss of that feature. | For early discovery, validate the root and required target tables strictly, but diagnose and skip an invalid unrelated entry where its identity can be safely established; keep all reads within the firmware map. | source-verified, hardware trigger suspected. |
| Medium | `arch/x86_64/apic.c:116-132`: the loader's x2APIC mode must equal the CPU's **capability**, not merely the hardware mode. | A CPU with x2APIC support whose loader legitimately leaves xAPIC active panics despite the kernel having xAPIC register support. Suspected on a loader/firmware combination that does not enable x2APIC. | Accept either consistent active mode, or explicitly switch after checking the CPU and interrupt-routing contract. | source-verified, hardware trigger suspected. |
| Medium | `arch/x86_64/io_apic.c:160-164`, `arch/x86_64/apic.c:75-89`: physical interrupt delivery assumes the BSP APIC ID fits eight bits. | A valid x2APIC BSP ID above 255 panics when IO-APIC or MSI routing is prepared. This is suspected on large or sparsely numbered systems, not on a four-core system merely because it has x2APIC. | Discover interrupt-remapping capability and choose a routable target; until then fail with the existing explicit diagnostic. | source-verified, hardware trigger suspected. |
| Medium | `kernel/pci/msi.c:28-64`, `kernel/usb/xhci.c:1264-1270`, `kernel/storage/nvme.c:1385-1392`: the common MSI helper accepts only unmasked 64-bit MSI. | An MSI-only xHCI or NVMe device advertising 32-bit or per-vector-mask MSI is refused, and neither driver routes INTx. USB or storage vanishes while the rest of the machine may boot. | Support verified 32-bit message layouts and masking where needed, or route a safe INTx fallback. | source-verified, hardware trigger suspected. |
| Medium | `kernel/virtio/transport.c:344-361`: modern VirtIO PCI devices must offer MSI-X. | A conforming VirtIO device with only INTx cannot be prepared; this affects VM storage, network, RNG, and graphics. The [VirtIO specification calls MSI-X optional](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html). | Add shared INTx handling through the ISR status byte with explicit ownership and masking. | source-verified, device trigger suspected. |
| Medium | `boot/limine/entry.c:369-390`: the boot framebuffer must be 32-bit RGB with three eight-bit channels and four-byte-aligned pitch/address. | A valid GOP bit-mask mode with a different pixel layout, such as ten-bit channels, panics before the first pixel. A 144 Hz panel alone does not establish this condition; firmware mode format must be checked. | Convert from the advertised pixel layout in a bounded early staging path, or negotiate a supported mode before handoff. | source-verified, hardware trigger suspected. |
| Low | `boot/limine/entry.c:88-100`, `kernel/pci/assignment.c:105-116,681-687`: PCI BAR assignment needs a valid EFI map with at most 256 descriptors. | A busy firmware map with more descriptors causes the adapter to discard the map silently; later BAR assignment refuses. Devices already assigned by firmware still work. Suspected for a highly fragmented UEFI map. | Retain a validated dynamic EFI snapshot or report why it was discarded at boot; keep placement fail-closed when reservations are unknown. | source-verified, hardware trigger suspected. |
| Low | `include/kernel/mm/pmm.h:7`, `kernel/mm/pmm.c:55-94`: usable physical RAM is clipped at 64 GiB. | A workstation with RAM above that physical address is deliberately under-provisioned, with one warning; the lower RAM remains safe. A 64 GB installed-memory system can already cross the physical boundary because of address holes. | Size the PMM metadata and heap invariant from the validated map before raising the cap. | source-verified, already owner-accepted debt. |
| Low | `include/kernel/boot.h:7`, `boot/limine/entry.c:176-190,212-215`, `arch/x86_64/smp.c:110-117`: fixed 256-entry memory-map and 256-CPU scratch capacities. | A firmware map at the limit can run out of split slots for boot files; a system reporting over 256 logical CPUs panics explicitly. Neither condition is expected on the 2–4-core target. | Count and allocate exact boot metadata before copying where early memory permits; scale CPU scratch only when a target requires it. | source-verified, target trigger suspected. |

The first Vela failures have already been fixed: framebuffer page rounding and
addresses above 4 GiB, PIT-free local timer calibration, x2APIC handoff, distant
xHCI extended capabilities, MSI fallback, and delayed root-port links. See
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1335). This
audit did not reclassify those as open findings. The SystemCMOS handler now
exists at `kernel/acpi/cmos.c:13-53`. Also, `arch/x86_64/io_apic.c:83-90,160-203`
independently prepares the SCI route; the older statement in technical debt
that SCI requires the keyboard's IO-APIC is stale.

The Renoir 144 Hz display path samples actual timing and refuses unavailable or
incoherent samples rather than requiring a fixed 60 Hz period
(`kernel/display/timing.c:12-19,126-180`). The Synaptics/PS2 path probes the
controller only when firmware advertises 8042 and the route exists
(`arch/x86_64/acpi.c:286-335`, `arch/x86_64/init.c:77-79`). Serial COM1 is
probed and optional; SystemCMOS is handled. NVMe and HDA now use bounded DMA
allocator constraints, and xHCI rejects controllers without 4 KiB page support
explicitly. These checks reduce the chance of a silent address-width failure,
but none qualifies untested native hardware.

There is no Caelum AHCI driver in this tree. A SATA-only machine therefore has
no native AHCI storage path; this is a coverage gap, not an unchecked transfer
assumption. An eventual aarch64 port must provide its own boot, interrupt,
timer, paging, PCI, and physical-device adapters; x86 register use in `arch/`
does not itself imply a kernel-side hardware assumption on today's x86_64
target. No QEMU or native run was performed for this read-only audit; the QEMU
HPET-off result cited above is from the existing repository record.

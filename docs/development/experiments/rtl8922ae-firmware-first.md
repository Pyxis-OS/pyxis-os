# RTL8922AE firmware-first cut

2026-10-22. Implementation and software controls for
[the accepted first cut](../../wip/rtl8922ae-wifi.md#stage-2-firmware-transport-before-scan).
Native firmware execution and reset qualification are pending. No native/PXE
operation was performed for this record.

## Revisions and configuration

Exact pre-code main: `e4699e089aa944136f0ecfb4a7877cd6153c3fa5`.
Candidate code: `ee8dc970b969838624794d6e3c1a499b7de54431` for B1/B2;
`ea13f03f` for B3, adding bounded eFuse yields and refusal of reserved boot
status. Both additions are on the absent device's unexecuted path.
Dependencies for these original measurements: userland `0775149f505b`, ports `f68109ae9768`,
pyxis-fs `f3f22b6e07e7`, lwIP `a1aadb91a503`.
Builder: `pyxis-llvm23.1.3-3d69e83`.

QEMU q35/KVM, four CPUs, 4 GiB, `-cpu max`, modern virtio-net with user
networking, virtio-rng and virtio-scsi CD-ROM. Fedora OVMF pair from the existing
fixture; fresh variables for each boot. The guest selected the KVM clock.
Default Kconfig, info logs, `wifi.rtl8922ae=1`, RTL hardware absent. The same
focused archive and RAM-only Remote profile were used throughout; no games.
Only the kernel, C SDK/runtime, Lua/MbedTLS and required control programs were
rebuilt, rather than the complete port set or compiler container.

## Software checks

Target kernel build passed. Interactive QEMU/Remote and GDB showed absent RTL,
no reserved PCI owner, MMIO mapping, DMA allocation, interrupt or activation
worker; wired DHCP and Remote remained usable. SCAN_START returned
CALL_UNAVAILABLE, with WIFI_READY clear. This checks the absent-device boundary,
not any RTL register, reset, DMA completion, power or RF behavior.

The new PCI helper preserves the existing reset-free probe semantics. Read-only
preparation chooses advertised FLR, otherwise resetting PM with NoSoftReset
clear. Destructive attempts retain ownership; boot and terminal reset each have
a checked completion and finite deadline. Runtime waits sleep with interrupts
enabled; only configuration transactions mask interrupts. Source inspection,
not QEMU hardware execution, checked these paths.

## Interleaved wired controls

The existing stage-1 packet's ttcp/Remote helpers were reused with a localhost
forward. Each row has three TX and three RX samples of 16 MiB. Commands:

```text
ttcp -t -p 19497 -n 2048 -l 8192 10.0.2.2
ttcp -r -p 19497 10.0.2.2
```

The host peer used the existing 16 MiB zero fixture and socat; host TX byte
counts and guest totals/EOF matched. A1 was captured before implementation;
A2/A3 used its frozen kernel and archive. Values are median (min–max), MiB/s.

| Cohort | TX | RX |
| --- | --- | --- |
| A1 | 58.925 (58.216–59.190) | 86.457 (82.110–87.929) |
| B1 | 58.369 (31.811–58.979) | 50.159 (48.707–88.637) |
| A2 | 59.610 (51.585–60.156) | 87.640 (86.226–89.626) |
| B2 | 50.774 (49.850–54.929) | 85.069 (72.764–85.787) |
| A3 | 52.211 (51.318–55.064) | 83.601 (82.383–83.886) |
| B3 | 20.724 (10.723–41.432) | 55.971 (43.104–58.363) |

**Performance remains unqualified.** Candidate medians are lower, with large
variation, including on the unchanged baseline. The cause is not established.
GDB confirms no RTL activation or backing on this emulated machine; that does
not explain or dismiss the difference. Do not claim unchanged performance from
these nested/slirp samples. The separately scheduled native A/B must measure
independent USB-wired controls alongside actual activation before this gate
closes. One additional A3 attempt overlapped packet-tools compilation and is
explicitly void, retained separately and excluded above; its replacement reused
that boot after compilation completed.

Raw configurations, logs, debugger snapshots and samples:
`/shared/present/rtl8922ae-stage2/`. The native packet is in `native/`; preparation
and offline checksum validation grant no permission to stage or boot it.


## Review follow-up

The revised stop path masks delivery, performs the pinned Linux PCI pre-deinit
and MAC power-off sequences, then always attempts terminal PCI reset with a
fresh deadline. Firmware statuses 8/9 remain pending; ring consumers mask the
reserved upper nibble; PM transitions allow resettable PME/Data_Select fields.
Completed readiness and stop actions, plus validity-tagged device facts, are
observed through the appended `WIFI_STATUS.boot` record, replacing five kernel
log lines. Target kernel/SDK and the updated Wi-Fi observer built successfully. At
`2fa5d7d08799`, QEMU/Remote reported absent RTL with `boot valid=0 state=0`;
GDB confirmed no prepared reset, DMA backing or stopping phase. DHCP/Remote
remained usable. This qualifies the absent-device/status path only; the MAC
shutdown and reset paths remain source-reviewed, not hardware-qualified.

The native packet retains the exact-main A kernel and old status observer.
B rebuilds the Wi-Fi observer at userland `9aa9fadc0d66` against the appended
status ABI; after rebasing onto userland main, its executable remains
byte-identical to the `071677d8d42c` observer checked above. Only that executable
and its README differ between archives.
Configuration, firmware and every wired-control binary remain identical.
There is no compatibility reader. The interleaved measurements above predate
these review changes and remain unqualified; the revised native packet needs
separate authorization before staging or running.


## Native admission stop and MSI correction

The orchestrator's first native sequence used frozen candidate `26501203` on
vela. A1 passed its USB-wired controls; B1 returned BAR2/MSI admission refusal
before the activation worker or any DMA backing. Its selected FLR and the
subsequent configuration/claim checks had passed. The sequence stopped and the
menu was restored; firmware readiness and native matched performance remain
unqualified. Raw evidence stays under `native/results/` in the evidence directory.

The retained device profile is mask-capable, 64-bit MSI. The original shared
helper explicitly rejected mask-capable MSI. The combined error did not identify
whether a preceding BAR sizing/map check also failed. The correction admits
standard 32-/64-bit layouts with optional masks, validates the complete span,
keeps delivery off during preparation, and enables only message zero. It also
separates the RTL admission errors. Pending Bits is never written.

QEMU 10.2.2 q35/KVM, four CPUs/4 GiB, with the same clean archive:

- Ordinary HDA MSI prepared, started/stopped the overlay click and delivered
  interrupts without controller failure. MSI-only `qemu-xhci,msi=on,msix=off`
  enumerated the USB keyboard and completed commands/interrupts. NVMe and virtio
  stayed functional on their unchanged MSI-X paths; wired DHCP/Remote worked.
- A private, unmerged boot probe used the real helper against QEMU's mask-capable
  `igb` endpoint, with NIC DMA disabled. Prepare/enable/disable all succeeded;
  control readbacks were `0x180/0x181/0x180`, masks `1/0/1`, pending `0/0/0`.
  This qualifies configuration gates, not NIC interrupt production or a driver.
  The probe is excluded from the production build and native packet.
- No suitable emulated 32-bit mask-capable endpoint was found. Its standard
  offsets and bounds are source-reviewed only. AX200 and the NVMe MSI-only
  route were also source-reviewed, not hardware-exercised here.

Raw debugger snapshots, the private probe and model inventory stay in
`/shared/present/rtl8922ae-stage2/qemu-msi/` and `msi-model-audit.md` beside it.
A new production packet is prepared for a separately scheduled native rerun;
there is no native qualification claim from these QEMU checks.

## DMAC stop and VFIO observation (2026-10-22)

The orchestrator's bare-metal MSI rerun at `727370c5` passed preparation and
FLR, selected cut 2/non-secure firmware 0.35.80.3, then stopped at download-mode
DMAC setup. Allocated backing was 9,154,560 bytes; firmware transfer and IRQ
activity had not started. The menu was restored. Evidence: `native-msi/results/`
in the same private evidence directory. This is not a firmware-readiness pass.

Candidate `bd5e976c` adds permanent status detail for the failing DMAC substep,
register, value, mask and expected bits, without changing programming or waits.
It also clears/checks MSI Extended Message Data Enable. Chronium approved a
VFIO loop on vela Fedora: QEMU 10.2.2, q35/KVM, four CPUs/4 GiB, fresh Fedora
OVMF variables, the clean ISO, virtio/slirp management and only the selected
RTL8922AE passed through. Debugging and Remote forwards were loopback-only.
No host reboot, PXE, BootOrder, host-driver binding or firmware settings changed.

The first guest placed the card directly on the root bus; QEMU exposed PCIe
type 9, which the reset helper refuses. It stopped before radio allocation.
A second guest, after stopping the first, used an explicit PCIe root port and
passed the existing endpoint/reset/MSI checks. Status reported:

```text
RTL8922AE DMAC dle-clock reg=8404 value=ffffffff mask=04800000 want=04800000
boot valid=f state=1d
```

Cut, security, selected firmware and allocated backing matched the bare-metal
stop. The clock-register read is rejected before the write. The preceding DLE
disable succeeded; no quota or readiness test had run. Read-only GDB inspection
after terminal failure confirmed pre-deinit, MAC-off and stop-reset completion,
with transport and interrupt delivery inactive. Both guests were stopped and
the VFIO group released. Evidence, configurations, hashes and status/GDB
snapshots: `/shared/present/rtl8922ae-stage2/vfio-bd5e976c/`.

Source inspection against pinned Linux v6.18 shows the same register/mask and
DLE-disable → clock-enable order (`mac_be.c:191–205`, `mac.c:2152–2153`). Its
`rtw89_write32_set` unconditionally ORs the read value and writes it
(`core.h:6485–6490`); the PCI accessor does not reject all ones
(`pci.c:2036–2043`). Linux would therefore write `ffffffff` at this step and
continue. That establishes a validation mismatch, not that all ones is a valid
reset value or that the clocks are enabled. Proposed next comparison: match
Linux's RMW treatment only for this clock register, preserving the other read
checks and subsequent bounded DLE checks. No such functional change or rerun
has been made. VFIO emulates MSI and changes DMA isolation; final bare-metal
firmware/stop and independent wired-control qualification remain open.

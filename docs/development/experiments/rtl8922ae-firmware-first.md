# RTL8922AE firmware-first cut

2026-10-22. Implementation and software controls for
[the accepted first cut](../../wip/rtl8922ae-wifi.md#stage-2-firmware-transport-before-scan).
The native warm-reboot firmware/stop profile and independent wired controls are
qualified (2026-10-23). RF/scan/data and cold-boot/recovery behavior remain open.
The sections below distinguish original software controls, VFIO diagnosis and
the orchestrator-run bare-metal qualification.

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

**These original nested samples are not a performance qualification.**
Candidate medians are lower, with large
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

### Checked DLE clock RMW comparison

Chronium approved the narrow clock exception: at `0x8404` only, perform the RMW
even after an all-ones pre-read, then require a non-sentinel post-write readback
with both requested clock bits set. All other all-ones guards remain. Candidate
`6f5e8b0a` implements that check; register order and deadlines are unchanged.

One further root-port VFIO guest with the same configuration reached:

```text
RTL8922AE DMAC dle-clock-post reg=8404 value=ffffffff mask=04800000 want=04800000
```

The RMW was issued; the readback remained all ones and the new check refused
further setup. Quotas, readiness polls and firmware transfer were not reached.
Cut/security/image, allocated backing and `boot valid=f state=1d` matched the
prior observation. Post-failure GDB confirmed pre-deinit, MAC-off and terminal
FLR completed with transport/interrupt delivery inactive. The guest was stopped,
its PID verified absent and the VFIO group released. No subsequent workaround
or startup retry was made. The cause of the still-all-ones hardware read remains
unestablished; this is not firmware readiness or a verified clock enable.

Evidence: `/shared/present/rtl8922ae-stage2/vfio-clock-6f5e8b0a/`. The current
userland `099906d` observer was rebuilt against the matching SDK and checked
separately in q35/KVM QEMU without the card: absent status/zero backing and
working DHCP/Remote. Its binary differs from the frozen VFIO observer; native
archives still differ from baseline only at the observer executable and README.
All wired-control inputs remain frozen. That observer guest was also stopped.

Fresh bare-metal packet: `/shared/present/rtl8922ae-stage2/native-clock/`.
It seals the exact VFIO-tested kernel, current observer, unchanged baseline and
clean RAM-only profiles. Offline hash/archive/configuration checks pass. No
native staging or reboot by epsilon. A native repetition determines whether the
post-write refusal also occurs outside VFIO; any refusal stops the sequence,
preserves status and restores the menu, without retry or relaxed checks.

### Bare-metal clock refusal and power-order audit

The orchestrator repeated `6f5e8b0a` on vela: A1 passed; B1 reproduced the exact
`dle-clock-post reg=8404 value=ffffffff` refusal without a hang. The menu was
restored and verified. Evidence remains under `native-clock/results/`. The
failure is therefore not specific to VFIO, though the read value alone does
not establish which power/clock condition caused it.

The exact pinned Linux probe chain is `rtw89_chip_info_setup` →
`rtw89_mac_pwr_on` → chip power-on → security eFuse → scoreboard notification →
firmware recognition → `rtw89_chip_efuse_info_setup` →
`rtw89_mac_partial_init(false)` → HCI enable → BE pre-enable → DLE/HFC → PCI
pre-init → firmware download. The chip's xtal/AFE/power and DMAC/CMAC function
writes match. BE pre-enable's DMA mode, channel stops and table address mode
are also present. `sys_init_be` belongs after download; its DMAC, CMAC0 and chip
function-enable calls are no-ops, so moving it earlier adds no clock setup.
The probe deliberately skips BB-MCU pre-init; full MAC initialization with
`include_bb=true` is a different path.

One concrete omission is the wrapper's byte write of `MAC_AX_NOTIFY_TP_MAJOR`
(`0x81`) to `R_AX_SCOREBOARD + 3` (`0x00af`) after security eFuse
(`mac.c:1494–1501`). Pyxis had only the power-off notification. Candidate
`afdde378` restores that power-on notification as one change, retaining all
clock/readback guards and deadlines. This is source-sequence alignment, not
a demonstrated cause or a firmware-readiness claim. Its prepared VFIO image was superseded by the exact-head run below.

After the orchestrator returned vela to Fedora, the exact `0129a88a` head
(including that scoreboard write) was rebuilt and run in one otherwise matched
root-port VFIO guest. It reproduced `dle-clock-post reg=8404 value=ffffffff`;
the notification did not resolve the refusal. Allocated backing, selected
firmware and `boot valid=f state=1d` matched. GDB after terminal failure confirmed
pre-deinit, MAC-off and final FLR completed, with transport and IRQ delivery
inactive. The guest was stopped and the VFIO group released; vela remains in
Fedora. No host reboot, PXE, BootOrder or driver-binding changes by epsilon.
Evidence: `/shared/present/rtl8922ae-stage2/vfio-scoreboard-0129a88a/`. No additional
startup or functional workaround was attempted. Firmware readiness remains open.


## Traced Linux/Pyxis comparison (2026-10-22)

Chronium approved one Linux guest followed by unchanged Pyxis `0129a88a` on
vela's passed-through card. Both used QEMU 10.2.2, q35/KVM, four CPUs/4 GiB,
fresh Fedora OVMF variables, the explicit PCIe root port and
`vfio-pci,rombar=0,x-no-mmap=on`. Linux used the stock Fedora
`7.2.10-200.fc44.x86_64` kernel/modules in a minimal initramfs: one driver probe,
no network manager, scan or association command. This is a running Linux
comparison, not execution of the pinned v6.18 source tree. Both guests used
`rtw8922a_fw-4.bin` SHA-256
`d11927f593c82879bd0437435475d7915a60932374747984b3dc906a23009dea`.

Fedora QEMU exposes these VFIO events through USDT rather than its log tracing
backend. Approved bpftrace 0.24.2 captured BAR/config accesses and reset events
before guest execution, with a PID filter. Each guest began paused; reset events
were verified before `cont`. Linux recorded 5,905 access events and Pyxis 1,393;
both have start/end markers, empty QEMU stderr and no lost-event report. The
tracer reports parse warnings for unrelated migration-probe operands; the
selected VFIO probes attach and emit checked records. Two earlier paused
calibration launches stopped before guest execution to correct output buffering.
These intrusive traces provide ordering evidence, not performance measurements.

| Observation | Linux guest | Pyxis guest |
| --- | --- | --- |
| DLE disable at `0x8400` | `7fff4f80` → `7b7f4f80` | Same |
| Clock `0x8404` read/write | `ffffffff` / `ffffffff` | Same |
| Additional clock readback | None | `ffffffff`; refuses |
| WDE `0x8d00`, PLE `0x9100` ready bits | Both reach `3` | Not reached |
| WCPU `0x01e0` after download | `0c000002`: raw status `3`, download bit clear | Not reached |
| Driver result | Bound driver and wiphy, firmware 0.35.80.3 | `dle-clock-post` failure |

The write-prefix diff is not identical: Linux has early PCI/indirect setup,
additional eFuse transactions, writes at `0x0190/0x0194` and a repeated HCI-enable
write. Initial power-register values also differ. These differences are retained
for later investigation, not established missing requirements. Both nevertheless
reach the same DLE-disable and clock read/write values. Linux then programs the
quotas, enables DLE and observes readiness before successful firmware download.
Pinned v6.18 `mac_be.c` likewise uses an unconditional clock RMW with no readback;
its firmware-status table maps raw status `3` to firmware-init-ready.

**Finding:** the Pyxis-only post-write sentinel check is the immediate refusal.
Linux's successful DLE/firmware startup disproves the earlier inference that
this all-ones value alone establishes a missing power-on step. It does not
establish the register's undocumented read semantics or prove the remaining
Pyxis download path correct. Chronium subsequently accepted replacing the
post-read requirement with the existing bounded WDE/PLE functional-ready checks,
retaining this register's Linux RMW and every other guard. The resulting VFIO
comparison is below; separate bare-metal qualification remains open.

Redacted ordered accesses, write-prefix diff, serial/status, input commands,
raw hashes and analysis helper are in
`/shared/present/rtl8922ae-stage2/linux-ground-truth/`. Raw traces remain
root-private on vela because eFuse reads can contain identifiers. Both guests
and tracers were stopped, recorded PIDs verified absent and VFIO released;
host remains in Fedora, with no reboot/PXE/BootOrder/driver-binding changes.
Pyxis firmware readiness and independent native wired controls remain open.


### Functional DLE readiness comparison

Accepted (chronium, 2026-10-22): remove only `0x8404`'s extra post-write readback
check. Candidate `9a1c9191033f` retains the pre-read guard, RMW, quota checks,
bounded WDE/PLE readiness checks and every other guard/deadline. The existing
builder completed `make -j16`. Its parent is now based on main `0bf27b3b`; this
is not an isolated whole-kernel performance comparison against `0129a88a`.

One approved VFIO guest used the same root-port/q35/KVM configuration and frozen
clean archive as the traced Pyxis guest. Only the kernel was replaced in that
ISO; extraction confirmed the kernel bytes and unchanged archive SHA-256
`9b65b85a4c03142a3138dd99c4760caf92540edb5907f501eeb90ac2232ffa72`.
Remote/virtio management worked. Status advanced to:

```text
RTL8922AE WCPU H2C download path unavailable
```

The call path establishes that DMAC buffer/quota setup, bounded WDE/PLE ready
polls, HFC setup and PCI transport preparation returned success. The next status
covers both `start_wcpu()` and its H2C-ready poll; this run does not distinguish
which failed. It stopped before the firmware header submission. Live cut 2,
non-secure firmware 0.35.80.3 and retained backing 9,154,560 bytes matched the
prior observation; no firmware-readiness pass is claimed.

Read-only GDB after the terminal result confirmed pre-deinit, MAC-off and final
FLR completed, with transport inactive, interrupt delivery off and stopped=true.
QEMU stderr was empty. Guest PID 6099 was stopped and verified absent, VFIO
released and host left in Fedora. No second startup, host reboot, PXE, BootOrder
or driver rebind. No further guard or programming changes were made. Redacted
Remote/GDB/serial evidence, exact commands, candidate ISO/kernel and hashes:
`/shared/present/rtl8922ae-stage2/vfio-dle-functional-9a1c9191/`.
The next investigation is the WCPU-start/H2C-ready boundary; native firmware
readiness and independent wired controls remain unqualified.


## WCPU/H2C comparison and VFIO readiness (2026-10-23)

One unchanged `9a1c9191` guest, using the prior USDT/VFIO configuration,
reproduced the shared WCPU/H2C refusal. Its 1,659 access events identify the
specific cause: after clearing WCPU control, the read at `0x0888` returns
`deadbeef`; the generic RMW guard refuses before any H2C poll. GDB reported no
transport fault and zero submitted commands. In the saved successful Linux
7.2.10 trace, that register returns the same value and Linux writes it back
with bit zero set, matching pinned v6.18 `mac_be.c`'s unconditional DCPU RMW.

Candidate `3822b586` admits only that observed register/value combination and
performs the reference RMW. `valid_register()` is unchanged: all ones and
`eaeaeaea` still refuse there, and the remaining guards/quotas/deadlines stay.
This corrects a disproven sentinel assumption, without claiming undocumented
register semantics or changing firmware, queue sizes, power order or authority.
The subsequent 9,703-event trace reaches:

| WCPU control `0x01e0` | Checked meaning |
| --- | --- |
| `04000202` | H2C ready with download enabled |
| `04000203` | Firmware header accepted / download ready |
| `0c000002` | Firmware initialized, raw status 3, download-enable clear |

GDB reported 213 submitted commands, including 212 section chunks. Public
status had no failure, firmware 0.35.80.3, RFE 1, xtal 73, two TX/RX streams and
antennas, raw country `ffff`, and retained backing 9,154,560 bytes.
`WIFI_READY` deliberately remains clear: no WLAN, scan or data claim follows.
Both traces have start/end markers, empty QEMU stderr and no lost-event report;
the earlier unrelated USDT parser warnings remain. These are ordering checks,
not performance results; eFuse payloads remain private.

After a linear refresh onto main `59e8d64d`, with unchanged owned Wi-Fi/MSI
source, exact `09fd4910` repeated readiness and checked stop in one untraced
VFIO guest. A further build/run at that revision used `LOG_LEVEL=info`, matching
the frozen pre-code native baseline; it also passed. The same clean archive and
observer were retained throughout, rather than silently replacing controls.
Normal container builds passed. No compiler-container change was needed.

After each run, GDB confirmed pre-deinit, MAC-off and final FLR completion,
inactive transport, disabled interrupt delivery and no transport fault. Guests
and tracers were stopped and VFIO released; vela remains in Fedora, with no host
reboot, PXE/BootOrder change, rebind, scan or bare-metal operation. VFIO emulates
MSI and uses the IOMMU, so that VFIO result alone does not qualify bare-metal
firmware/stop or independent wired controls. The native repetition follows.
Redacted traces/status/GDB, raw hashes and commands:
`/shared/present/rtl8922ae-stage2/wcpu-trace-20261023/`,
`vfio-ready-09fd4910/` and `vfio-native-info-09fd4910/` beside it.


## Native warm-reboot qualification (2026-10-23)

The orchestrator ran the approved `native-ready` sequence on vela, on AC with
the same USB RTL8156 cable/port, idle input and quiet horse without a competing
build or benchmark. All six warm cohorts completed in A1/B1/A2/B2/A3/B3 order.
A is exact pre-code `e4699e08`, RTL opt-out; B is exact `09fd4910`, RTL opt-in.
Both use info-level logging, the same kernel configuration and matched clean
archives/control programs; only their ABI-matched Wi-Fi observer and README
differ. B includes the recorded upstream main refresh, so this is a whole-
candidate comparison, not isolated attribution of cost to Wi-Fi.

B1–B3 all report firmware 0.35.80.3, cut 2/non-secure, boot validity/state `1f`,
FLR, completed pre-deinit/MAC-off/terminal reset and no startup failure. The
board observations agree across all three: RFE 1, xtal 73, two TX/RX streams and
antennas, raw country `ffff`. `WIFI_READY` stays clear; no WLAN is published.

Each cohort transfers three 16 MiB samples per direction through independent
USB-wired management. Values are median (min–max), MiB/s:

| Cohort | TX | RX |
| --- | --- | --- |
| A1 | 77.343 (77.224–78.134) | 52.803 (52.360–56.354) |
| B1 | 77.067 (77.014–77.519) | 56.012 (53.857–56.220) |
| A2 | 77.422 (75.154–77.796) | 52.010 (50.788–55.179) |
| B2 | 76.954 (76.721–77.047) | 52.358 (52.341–54.060) |
| A3 | 77.585 (77.025–78.122) | 56.160 (51.393–56.547) |
| B3 | 76.713 (76.583–77.084) | 54.449 (52.818–55.765) |

Aggregate medians are TX 77.422 → 77.014 MiB/s (−0.53%) and RX
52.803 → 54.060 MiB/s (+2.38%). Sample ranges overlap; these controls show no
material wired regression in this run. They do not establish peak link rate or
idle CPU cost. The first-cut native warm-reboot gate is met.

The orchestrator restored the PXE menu; a subsequent read-only packet check
also reports `RTL8922AE_PXE ORIGINAL VERIFIED`, including protected payloads.
Raw samples, status proofs, conditions and completion markers remain in
`/shared/present/rtl8922ae-stage2/native-ready/results/`.

Limits: no cold-boot, original Linux-driver handoff, PM-fallback or forced-failure
qualification; no firmware readback, transfer payload hash, radio IRQ-production,
RF-silence, scan or data/security claim. Retry/reset recovery still precedes
everyday use. Passive scan requires a separate review of explicit country,
channel rules, board/firmware power limits, platform SAR/TAS and rfkill; raw
country `ffff` supplies no permission.

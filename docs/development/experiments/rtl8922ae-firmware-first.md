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
log lines. These are source-reviewed changes, not hardware qualification.

The native packet retains the exact-main A kernel and old status observer.
B rebuilds the Wi-Fi observer at userland `071677d8d42c` against the appended
status ABI; only that executable and its README differ between archives.
Configuration, firmware and every wired-control binary remain identical.
There is no compatibility reader. The interleaved measurements above predate
these review changes and remain unqualified; the revised native packet needs
separate authorization before staging or running.

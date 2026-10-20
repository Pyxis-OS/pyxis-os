# AX200 Wi-Fi stage 1: power-on evidence and wired baseline

Native stage-1 evidence, 2026-10-18, for the [AX200 controller](../../devices/ax200-wifi.md).
Source `b31389cc8d9932584b18b948c0d814b2e5b7825b` (on main `fed2ce88`),
userland `a6c39a12`, ports `7604c85d`, fs `25337999`, lwIP `a1aadb91`; builder
`pyxis-llvm23.1.3-3d69e83`, -j8. Flags `REMOTE_BEACON=t14 LOG_LEVEL=info
LOG_UDP=1`, default Kconfig, no game assets; the debug image adds
`DEBUG_NET=t14 DEBUG_WAIT=1`, the timing image `DEBUG_NET= DEBUG_WAIT=0`.
Clock source TSC; AC, cable/peer, idle input and a quiet horse.

## Power-on PCI state

The owner powered the ThinkPad off and on (AC, battery attached). The capture
read PCI fields at the network-GDB checkpoint over the built-in RTL8111, with no
PCI writes, BAR sizing, MMIO or firmware commands. This is post-enumeration
evidence, not G3 rail removal.

| AX200 observation | Native result |
| --- | --- |
| Identity / subsystem | `03:00.0 8086:2723 rev 1a`; `8086:0080` |
| COMMAND / power | `0x0000`: Mem/BME off; PM at `0xc8`, PMCSR `0x0008`: D0, NoSoftReset set, PME enable/status clear |
| BAR0 encoding | 64-bit non-prefetchable memory at `0xfd700000`; extent unknown |
| MSI | Capability `0xd0`, control `0x0080`: disabled, one message, 64-bit, no per-vector masking |
| MSI-X | Capability `0x80`, control `0x000f`: 16 entries, disabled, function mask clear; table BAR0+`0x2000`, PBA BAR0+`0x3000` |

## Wired TCP baseline

Existing [ttcp workloads](../network-throughput.md) against horse
`192.168.0.213:18080`, three samples per row, each exactly 64 MiB, after a warm
reboot into the matching ordinary image:

```text
boot://ttcp.pxe -t -p 18080 -n 8192 -l 8192 192.168.0.213
boot://ttcp.pxe -t -p 18080 -n 32768 -l 2048 192.168.0.213
boot://ttcp.pxe -r -p 18080 192.168.0.213
```

| Wired TCP workload | Samples (MiB/s) | Median | Range |
| --- | --- | ---: | --- |
| TX, 8 KiB writes | 112.229, 111.154, 111.080 | 111.154 | 111.080–112.229 |
| TX, 2 KiB writes | 103.038, 102.749, 104.082 | 103.038 | 102.749–104.082 |
| Discard RX | 113.079, 113.072, 113.023 | 113.072 | 113.023–113.079 |

All exits, drains and byte totals passed. RX used a sparse zero fixture and a
byte-count check, not a content check. This is the wired pre-driver baseline,
not Wi-Fi throughput; native idle/BSP utilization was not measurable with
production tools.

Raw packet, hashes and results: `/shared/present/wifi-ax200-stage1/`
(`results/pci-20261018-150306`, `results/tcp-20261018-150604`; the timed-out
`pci-20261018-145654` is excluded). The PXE default entry was restored and its
payload/EFI hashes verified.

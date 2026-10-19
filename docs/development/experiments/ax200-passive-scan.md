# AX200 firmware and passive scan qualification

Stage 2 in progress. No radio readiness or scan result is qualified yet.
Accepted scope: [AX200 passive scan](../../wip/ax200-passive-scan.md).

## Before-code wired baseline (2026-10-19)

Owner/orchestrator warm-rebooted the ThinkPad into current main
`3bb300aea365d5986a068f4753efff0832bd7b6c`. Userland `a9f6c64a`, ports
`7604c85d`, fs `25337999`, lwIP `a1aadb91`; existing builder `3d69e83`, -j8,
default Kconfig, `REMOTE_BEACON=t14 LOG_LEVEL=info LOG_UDP=1`, debug disabled,
no game assets. RTL8111 was prepared/up and bound as net0; TSC selected.
Peer horse `192.168.0.213:18080`, same cable, idle input/quiet host reported.
The owner subsequently confirmed the ThinkPad was on AC for this baseline;
that confirmation, rather than the runner's conditions string, verifies AC.

Existing ttcp, three 64 MiB samples each:

| Workload | Samples (MiB/s) | Median | Range |
| --- | --- | ---: | --- |
| TX 8 KiB | 111.490, 111.595, 111.627 | 111.595 | 111.490–111.627 |
| TX 2 KiB | 101.176, 101.893, 102.969 | 101.893 | 101.176–102.969 |
| Discard RX | 113.111, 113.021, 113.126 | 113.111 | 113.021–113.126 |

```text
boot://ttcp.pxe -t -p 18080 -n 8192 -l 8192 192.168.0.213
boot://ttcp.pxe -t -p 18080 -n 32768 -l 2048 192.168.0.213
boot://ttcp.pxe -r -p 18080 192.168.0.213
```

All nine command exits/drains and guest byte counts passed; TX host counts
matched. RX checks count, not content integrity. Native idle/BSP CPU cost is
unmeasured. Repeat the same workload/configuration after driver work and
record verified power state for the comparison.

Raw results, exact image/tool hashes, pins/configuration/flags and the operator
clarification: `/shared/present/wifi-ax200-stage2/baseline/results/tcp-20261019-080205`.
The original PXE menu was restored and verified after collection; root payloads
and EFI were unchanged. The running image remains the baseline until reboot.

## Implementation checks

The implementation stacks accepted scope #980 and rebases onto main `2b001c6e`
(#975 NPFS writeback and #979 battery docs). The before-code image remains the
recorded `3bb300ae`, not retrospectively relabelled as this newer revision. The
wired workload uses archive/RAM, with no mounted NPFS volume; the intervening
changes do not alter its network API or driver. Repeat the same workload and
report both revisions explicitly.

Existing builder `3d69e83`, `make -j8 kernel sdk` passed. Source/asset review
corrected a legitimate split entry in the pinned firmware command-version table,
initialized RX descriptors before context publication, and bounded the extra
boot-launch Wi-Fi resource slot. Ordinary absent-device QEMU and native ALIVE,
NVM/rfkill, ring counts, scan/abort and wired continuity are still pending. No
container rebuild is needed; no firmware/RF runtime success is claimed.

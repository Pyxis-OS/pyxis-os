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
boot-launch Wi-Fi resource slot. QEMU q35/KVM, four CPUs, 2 GiB and VirtIO Ethernet passed the absent-device
path at code `1afcc19a` / userland `18fceef`: explicit diagnostic READ/SCAN,
`wifi status` reports absent with zero DMA/rings; `wifi scan` returns
UNAVAILABLE / exit 1. An ordinary command and a fresh Remote reconnect still
succeed with complete drains. GDB confirms no preparation/worker DMA/pending
operation. This does not exercise AX200, cancellation or RF. The diagnostic
image uses `LIVE_CONFIG` to select Development/Remote grants; stock configs omit
them. Native ALIVE, NVM/rfkill, ring counts, scan/abort and wired continuity
remain pending. No
container rebuild is needed; no firmware/RF runtime success is claimed.


Native packet: `/shared/present/wifi-ax200-stage2/native`, kernel `f3ada682`,
userland `18fceef`, same builder/flags plus `WIFI_AX200=1` and the explicit
diagnostic config. It carries kernel.config, pins, command flags and hashes;
READ polling precedes exactly one passive scan. Results remain local because
SSID/BSSID are private nearby identifiers. The packet does not retry/reset or
claim that scan completion is established. Source and binary reference notices
are carried with the kernel/archive independently of Intel firmware licensing.


### First native attempt (2026-10-19)

Packet `f3ada682` passed its host hash check. The orchestrator rebooted the
ThinkPad through Remote; the owner observed a Caelum panic before Remote became
available. `radio-20261019-083249` ended with `preflight: remote timeout`;
this is a failed bring-up, not a scan result. No UDP log was collected: the old
host pane was inactive and this runner does not own a UDP collector. Panic text
is awaited; no cause is established. The orchestrator restored and verified the
original PXE menu; no restaging is authorized while that evidence is pending.

The packet now includes the already-built `pyxis-log` tool and a separate
pre-reboot capture command in its README. It listens on UDP2325 and records both
text and collector diagnostics. This is best-effort: early Wi-Fi preparation or
firmware startup can precede usable/selected Ethernet, so the panic photograph
remains necessary. No image, guest code or logging level was changed.


### Firmware DMA reservation correction

Owner panic OCR is retained at `native/panic/ocr-2026-10-19.txt` under the packet.
It shows the 3,649,536-byte DMA preparation summary, then worker creation
failures (MM_NO_MEMORY) and the presentation-task panic. Source inspection
identifies 48 independently allocated firmware sections plus ten fixed DMA
buffers and three MMIO mappings. Each DMA allocation reserves a kernel VM range;
the 512 RX buffers already share one 2 MiB allocation. Adding approximately
61 records to the reported ordinary ThinkPad's 196/256 explains exhaustion;
MM_NO_MEMORY alone does not independently distinguish VM metadata from heap or
physical failure. No injected debugger result is claimed.

Firmware now uses one contiguous 1,372,160-byte owner with the original 48
page-aligned section slices. The eleven DMA owners plus three MMIO mappings
replace 61 reservations with 14, saving 47, without changing DMA bytes, firmware
layout, queue capacities or VM_MAX_RANGES. The separate kernel VM-limit work is
not part of this fix. Failure before publication unwinds every successful DMA
owner and CPU mapping, clearing their resource counts while retaining uncertain
PCI ownership. No runtime/published DMA is released. Optional preparation follows
other boot-device preparations; its worker is created after core service and
presentation stacks so an optional worker failure cannot take their last slot.

Ordinary QEMU q35/KVM, 4 CPUs, 2 GiB and VirtIO Ethernet: before and after
both booted to presentation and Remote with `records=32/256`, allocated=1,441
frames and backed=1,092 pages. The after run also reports AX200 absent with zero
DMA/rings. Reserved pages differ by two as the kernel/archive grew. Raw
`qualification/vm-ranges/{before,after}.serial.log` is retained in the packet.
This check exercises normal boot only: QEMU without AX200 cannot establish the
device's actual allocation count, allocation-failure unwind or firmware behavior.
Those guarantees are source-inspected, with actual radio allocation confirmation
remaining in the next native boot. No limit raise or injected debugger result
was used to obtain this ordinary boot comparison.

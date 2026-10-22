# RTL8922AE stage 1: CPU-only preparation

Software qualification, 2026-10-22, for the
[RTL8922AE plan](../../wip/rtl8922ae-wifi.md). Native vela PCI preparation and
USB-wired comparison remain pending; this record does not qualify RF or firmware.

## Revisions and configuration

- Baseline: exact main `353c3cc4c78d9e59c6c743f005d6a978aba5856f`, before driver code.
- Candidate: `9a6b5b8df5a7407374f85ee63afdf230b21c4b8b`.
- Unchanged userland test payload: `0831b08854e5911fc275c22f9324a77a505e8d88`;
  source archive reused from the Go qualification build. Its ABI hash and the
  baseline/candidate kernel ABI are identical; no userland/SDK/ports rebuild or
  dependency-pin change was needed. The matched archive adds the pinned RTL
  firmware/notices and Wi-Fi READ/SCAN grants in Development and Remote only.
- Builder `pyxis-llvm23.1.3-3d69e83`, `make -j8 LOG_LEVEL=info kernel`, default
  Kconfig. Nested QEMU q35/KVM, four CPUs, 4 GiB, virtio-net/user networking,
  virtio-rng, matching Fedora OVMF pair. KVM clock selected on all four CPUs.
  One QEMU instance at a time; no compilation during the timed samples.
- Same immutable archive, peer, devices and guest commands for the matched
  comparison. The candidate additionally enables `wifi.rtl8922ae=1`.

## Functional and debugger checks

The native target is absent from QEMU. With the RTL option, the existing Wi-Fi
READ operation reports absent/unprepared/not ready, zero DMA/rings/interrupts,
and a clear detail; SCAN returns UNAVAILABLE. Wired DHCP, Remote and the traffic
controls remain usable. With both radio options disabled, the unchanged AX200
absent/default status is returned. Selecting both options reports a refusal
and still has zero device backing and unavailable scan. These test refusal, not
the live Realtek PCI branch.

GDB on the committed candidate enters the CPU-only inspector at an active BSP
stop, with registers/stack and private result restored afterward. The actual
immutable 1,849,226-byte asset passes: version 0.35.80.3; three MFW entries;
normal/WoW/log counts 1/1/1; two BBMCU images; 13 sections, seven security
sections, six formatted MSS pools and 57 elements. Passing one byte less fails
and clears every result field; the subsequent full-length inspection passes.
No firmware or device operation runs. One-CPU preliminary and four-CPU committed
inspection agree. This is not a malformed-input campaign or hardware emulation.

The pinned downloader fetched/verified the asset, complete Realtek licence and
WHENCE through the mirror; its cached invocation also passes. Shell scripts pass
syntax review, and the Lua image plan includes the four RTL asset/notice entries
only when selected. Kernel source builds stay offline. Full image CI remains a
separate check on the submitted head.

## Wired controls

A pre-code control captured three 16 MiB samples: TX 53.507–56.178 MiB/s and RX
81.676–88.092 MiB/s. The matched A/B rounds below use the same archive on both
kernels, with three samples per direction per boot, in order A1/B1/A2/B2/A3/B3.

```text
ttcp -t -p 19494 -n 2048 -l 8192 10.0.2.2
ttcp -r -p 19495 10.0.2.2
```

The host sink counts all bytes to closure; the source supplies exactly 16 MiB
of zeros. These controls check byte totals and EOF, not a payload checksum.

| Boot | TX median (range), MiB/s | RX median (range), MiB/s |
| --- | ---: | ---: |
| A1 | 55.814 (50.799–58.235) | 84.473 (83.896–85.379) |
| B1 | 27.284 (23.839–43.459) | 47.226 (26.483–49.616) |
| A2 | 52.376 (49.296–52.616) | 82.115 (79.638–82.495) |
| B2 | 54.818 (54.339–57.693) | 82.138 (71.936–85.455) |
| A3 | 56.087 (56.018–56.778) | 86.217 (83.196–91.943) |
| B3 | 53.188 (52.814–55.226) | 83.418 (83.283–87.668) |

Every sample transferred the full 16 MiB and exited successfully. B1 was slower
in both directions; that slowdown was not reproduced in B2 or B3, whose ranges
overlap the controls. Its cause is not attributed. B1 remains in the record;
absence of a regression is not established. Nested scheduling/host variation is not controlled here, and the live RTL preparation path is absent.
Native USB-wired controls remain required.

Before user sessions, baseline and candidate both retained 1,433 allocated PMM
frames, 1,092 backed VM pages and two TLSF pools totalling 512 KiB (47 live
allocations). Total admitted PMM frames were 1,014,422 and 1,014,417 respectively;
this difference is not attributed here. CPU-only preparation allocates no device
backing. These are boot snapshots, not session resident usage.

## Limits and evidence

No Pyxis boot on vela, BAR sizing/MMIO, device claim, MSI programming, DMA,
firmware execution, calibration or scan occurred. Inherited warm firmware,
live cut/eFuse/security suitability, rfkill and physical RF silence remain
unknown. Zero Pyxis counters do not prove the device was already stopped.
A standard GDB inferior-call trampoline is incompatible with the kernel's NX
stack; debugger setup failures are void, not driver results.

Raw consoles, machine-client output, manifests, GDB inspection and build logs:
`/shared/present/rtl8922ae-stage1/`. Reference archive/asset audit:
`/shared/build/wifi-investigation/`. No capture contains real MACs or SSIDs.

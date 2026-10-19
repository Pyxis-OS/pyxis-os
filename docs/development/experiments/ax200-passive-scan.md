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

### Native boot after the reservation fix

The owner rebooted into `0d30fc92`. Boot and Remote succeeded, with VM init
`records=214/256`; the earlier core-worker starvation did not recur. Radio
initialization failed closed at the one-second ALIVE deadline, retaining DMA.
READ reported prepared=1, ready=0, failed=1, rfkill=0, interrupts=0 and packets=0.
No NVM, runtime queue or scan result is established. Raw result:
`native/results/radio-20261019-085630` under the packet.

Source comparison against the pinned OpenBSD `iwx` found no incorrect context
layout, section ordering, firmware kick or ALIVE/MSI-X register selection. This
does not prove hardware behavior. Zero interrupts alone cannot distinguish
failed firmware self-load from failed interrupt delivery. The next diagnostic
image records causes/masks, MSI-X enable/entry/route/pending state, APIC pending,
context-base equality, RX completion and secure-CPU status before timeout
cleanup changes them. Reads do not acknowledge causes, publish RX, retry or
reset the radio; optional NIC access is bounded to 5 ms. Only a timeout adds
diagnostic warning lines; success logging is unchanged. Native evidence is
required before choosing a fix.

The native UDP collector received nothing. Source inspection finds no beacon
dependency: logs use Ethernet/IPv4 limited broadcast to UDP2325 after the
selected Ethernet interface has an address. Kernel and collector share the wire
header; normal history is sent once, without listener discovery or replay.
An ordinary four-CPU KVM/QEMU boot of `0d30fc92`, with `log.udp=1`, emitted 68
UDP2325 broadcasts including recognizable boot text. Capture and serial output:
`qualification/udp-log/{ethernet.pcap,serial.log}`. Remote and absent-radio READ
also passed. This validates guest emission, not native LAN delivery or horse's
firewall/listener. The next packet asks for a pre-boot `tcpdump` capture alongside
`pyxis-log`, and collects the Wi-Fi boot log over Remote even on radio failure.
PXE remains on `0d30fc92` until the orchestrator is told before restaging.

### Pre-ALIVE cause evidence

The owner/orchestrator ran `3cd8cb6f`; boot/Remote still succeeded and radio
still failed closed. Raw result: `native/results/radio-20261019-091226`.
HW cause `22040000`, mask `91fffe10`, contain unmasked SW_ERR bit25 and HW_ERR
bit29, with no ALIVE bit0. Bit18 is undefined in the pinned iwx header and is
masked. Legacy INT `20000000` is HW_ERR. MSI-X readbacks show enable, function
and entry unmasked, and the expected BSP route; PBA/IRR and software IRQ count
are zero. This establishes latched error indications and no observed ISR, not
two independent root causes. Automask writing1 is the reference's W1C rearm;
its readback1 does not justify reversing that write. See pinned
`if_iwxreg.h:1174–1208`, `if_iwx.c:11610–11663` in the recorded source closure.
Secure-CPU status `0/a30000` is recorded without an unsupported interpretation.

The next diagnostic retains pre-kick causes and reads LMAC/UMAC error tables
before stop/reset. Their addresses come from the pinned CC77 TLVs55/54 and must
match the two known extents; no runtime pointer is followed. Reads are fixed at
152/60 bytes through the reference's NIC-memory read interface, with bounded
NIC acquisition, no allocation, SRAM writes, recovery or retry. Only validity,
error ID, status/data, firmware version and command/ISR fields are logged;
empty/unavailable tables are explicit. This may identify a firmware assertion;
an empty table is not proof that startup succeeded.

UDP delivery is now confirmed natively: horse's capture contains82 broadcasts
from the ThinkPad, including the diagnostic period. The owner pane used the
older2026-10-09 `~/bin/pyxis-log` (hash prefix `9806b3b1`), which did not decode
the current format. The packet collector has hash prefix `8148c2b3`; subsequent
runs use `images/tools/pyxis-log`. No guest UDP fix is indicated by this evidence.
Capture: `native/logs/udp2325.pcap`. Earlier guest-only emission checks remain
valid, with their native-delivery limitation resolved by this separate capture.

### Post-reset ownership candidate

`72bae745` again booted and failed ALIVE; raw
`native/results/radio-20261019-092257`. Pre-kick HW/FH/INT were all zero:
the error indications arose after the kick. The SRAM words did not form a
plausible initialized firmware error table. They are not accepted as an
assertion, valid record or error ID, and the pre-ALIVE table decoder is removed.
The newer packet collector recorded the full native boot successfully.

An offline audit of the actual archive confirms the pinned firmware SHA-256.
Linux v6.12 `pcie/drv.c:487,593` selects AX200/CC for PCI2723;
`cfg/22000.c:13–16,195–201,347` selects API77, family22000/gen2. The iwx
PCI22500_1 branch selects non-integrated CC; its section-copy and context paths
agree with Pyxis:14 LMAC/15 UMAC/19 paging, payload excluding the four-byte TLV
device offset but retaining internal headers,1,372,160 DMA bytes,448 context
dwords and flags990. NVM commands follow ALIVE; the newer iwx gen3/PNVM/IML
path is not the AX200 branch. No evidence currently justifies changing firmware
family, adding PNVM/IML, or treating this as a scan/NVM failure.

One concrete Linux sequence difference is corrected: after software reset,
retake NIC_READY/OS_ALIVE before APM and context publication. Cached v6.12
`pcie/trans.c:130–144,1839–1867` explicitly resets with `retake_ownership=true`;
the old native path prepared only before reset. iwx does not explicitly repeat
that handshake at the same point, so this is a source-backed candidate, not an
established root cause. Timeout evidence adds post-reset/pre-kick/current
HW_CONFIG, raw chip/radio revision and clock-ready-to-kick elapsed time. Existing
deadlines, DMA retention, MSI-X routing and one-attempt policy are unchanged.

Linux's AX200 config also enables `bisr_workaround`, but its implementation and
the secure-CPU status decoding are not in the previously cached closure.
Additional mirror requests currently return404 and were sent to the
orchestrator; no delay or ROM-status meaning is invented. The original iwx
allocates/copies firmware after reset; Pyxis prepares that memory earlier, so
post-clock startup timing remains a lead to verify against the missing source.

### BISR reference closure

Owner/orchestrator run `0120e356` still failed ALIVE; raw
`native/results/radio-20261019-093428`. HW revision340, RF10a100;
HW_CONFIG18080000 after reset and18489000 before/after kick;
clock-ready-to-kick2,216,268 ns. Causes were clean before the kick and otherwise
matched the prior failure. Retaking ownership alone did not resolve it.

The orchestrator supplied the missing Linux v6.12 references on2026-10-19 from
torvalds/linux/kernel.org into the shared reference directory. The header is
`iwl-context-info.h`, not `pcie/ctxt-info.h`. They are reference material, not
vendored source. `iwl-io.c:446–494` implements the AX200-enabled BISR workaround:
wait2 ms **before** INIT_DONE, poll clock-ready, then wait200 microseconds for
the BISR shift. The observed2.216 ms **after** clock-ready does not establish
the required pre-INIT_DONE interval. Native APM now applies both waits in those
phases using the existing sleep/delay rules. This closes a reference sequence
gap; hardware success is not yet established. No reset/retry or deadline change.

Linux `pcie/ctxt-info.c:12–59` also prevents each bootstrap DMA span from
crossing a4 GiB boundary. Native preparation now checks the context and each
firmware slice before publication, failing closed rather than changing the
allocator, retrying or splitting ownership. No actual native boundary violation
has been established. The single firmware arena and its VM-record savings stay.
The fetched files still do not decode secure-CPU status a30000; no meaning is
invented. The context structures/section path continue to agree.

One further routing omission is corrected: enabled HW bit3 TOP_FATAL now has
its IVAR0x13 assigned, as in Linux `pcie/trans.c:1113`. It was absent from the
native mapping list. This cannot explain the recorded22040000, whose bit3 is
clear; it prevents losing that different fatal indication to default routing.

The relevant Linux source is `GPL-2.0 OR BSD-3-Clause`; the permissive BSD
alternative is selected. Copyright, conditions and disclaimer are retained in
`kernel/wifi/NOTICE` and the installed kernel notice. Native implementations use
Pyxis interfaces; no Linux runtime is introduced. Reference hashes:

| Linux v6.12 iwlwifi file | SHA-256 |
| --- | --- |
| `pcie/trans.c` | `870d62744feaf720548a9d7b81d98ae6e295634d60a3a6921a702e8b347cc5ac` |
| `iwl-io.c` | `65ae268783bb71c6492379e7a3de9bc0017c71eddba4fcd7c39b1cb080eb3ad7` |
| `pcie/ctxt-info.c` | `49cac1d77cfd67f75ff4e15bde815e32907470832e2399768e1e8362a2d923db` |

Complete local manifest: `/shared/build/wifi-ax200-stage2/linux-reference/owner-supplied-manifest.json`.
After the orchestrator handed the ThinkPad back, `1166d8d3` was staged with
verified hashes and a byte-for-byte restorable menu. The native run again failed:
`native/results/radio-20261019-095703`. Clock-to-kick was5,635,526 ns; causes,
zero IRQ/RX, CPU status and readiness failure were unchanged. BISR sequencing
and ownership retake did not resolve the failure. No further image is proposed
or staged pending discriminating evidence.


### Ranked diagnosis and proposed Linux comparison (2026-10-19)

These are working hypotheses, not established causes. After five firmware
startup images with unchanged error evidence, another timing change is not
justified. SW_ERR/HW_ERR appear only after the context kick; no initialized
firmware error table, ALIVE or ISR has been observed. CSR context equality proves
a register readback, not a device DMA fetch. CPU `a30000` has no established
decoder in the inspected reference. Pending/IRR zero is an instant observation,
not proof of a working or broken interrupt route.

| Rank | Hypothesis and current evidence | Evidence that would distinguish it |
| --- | --- | --- |
| 1 | Bootstrap DMA is inaccessible or interpreted incorrectly: address-width/alignment, visibility, or requester-specific IOMMU state. Context/span checks and barriers reduce some risks but do not prove device access; working Ethernet does not establish AX200's DMA view. | Working Fedora context-base writes, actual DMA/IOMMU mode and firmware identity first. If needed next, propose a bounded read-only Pyxis context/section-address and AMD-Vi fault-state capture. Linux IOVAs must not be mistaken for physical addresses. No IOMMU-disable experiment is proposed. |
| 2 | Context/RFH/command-ring fields or their programming differ from a working startup on this revision. Pinned layouts/section counts agree on inspection, but have no native successful control. | Compare Fedora CSR/PRPH ordering, values and address encodings through ALIVE, including HW_CONFIG, RFH, LTR, context base and CPU_RUN. MMIO trace cannot show the pointed-to context bytes; an unresolved memory-field difference needs a separately proposed snapshot. |
| 3 | Firmware selection or ROM interpretation differs despite the correct CC77 PCI selection and verified packaged hash. | Capture the firmware version/file hash Fedora actually uses on this exact chip and its successful startup outcome. Do not replace its working image to force a match; API differences limit the comparison. NVM/PNVM/IML changes are not justified by current evidence. |
| 4 | A reset, power or kick handshake remains wrong. Retake and correctly phased BISR waits changed elapsed time without changing failure, weakening delay alone as the explanation. | Compare the successful Linux reset/APM/CPU-run sequence rather than adding another delay. Distinguish an actual register/order difference from normal scheduling variation. |
| 5 | Interrupt setup alone is wrong. No ISR was seen, but that alone does not explain the post-kick SW/HW error causes. DMA and interrupt translation could share a problem. | Compare Linux IVAR/masks, cause/acknowledgement order and interrupt-count changes. Device I/O tracing omits PCI-core table writes, so it cannot independently certify MSI-X delivery. |

**Proposed next step: one working Fedora startup capture, not another Pyxis
candidate.** The owner packet is
`/shared/present/wifi-ax200-stage2/fedora-ground-truth/README.md`, with
`capture.py`. It records iwlwifi I/O tracepoints around PCI reprobe/link-up,
keeps modules resident, temporarily suppresses Wi-Fi auto-association and
restores NetworkManager handling. The helper is syntax/source-reviewed only;
no Fedora capture has run. Kernel/firmware versions, PCI, IOMMU metadata,
dmesg and IRQ counts accompany the trace. Missing tracepoints stop before the
cycle. A successful warm reprobe is not power-on evidence or a DMA-memory dump.

Use [driver tracing](https://wireless.docs.kernel.org/en/latest/en/users/drivers/iwlwifi/debugging.html)
first. [mmiotrace](https://docs.kernel.org/trace/mmiotrace.html) is a separately
reviewed fallback: it normally offlines other CPUs and only captures mappings
created after activation. Neither trace establishes pointed-to host RAM contents.
Read the resulting trace before proposing a specific Pyxis evidence image or fix;
keep failed readiness and DMA retention unchanged meanwhile.


### Stock Fedora capture packet (2026-10-19)

Gamma's read-only inventory found Fedora `6.19.10-300.fc44` has
`CONFIG_IWLWIFI_DEVICE_TRACING` disabled, so the tracepoint preflight correctly
stopped before any PCI cycle. It has mmiotrace available. Installed firmware is
reported as `77.f39cc7f9.0`, matching the pinned Pyxis CC77 FW_VERSION TLV and
header string. Pyxis's uncompressed file is1,368,096 bytes, SHA-256
`d23a382871e6e638957845b47424ab98dca5e68e022ab73eeeebb0b6f751d7e9`.
Byte equality with Fedora's installed compressed file remains to be checked;
the revised helper decompresses and hashes it without changing firmware.

The owner requested a stock-kernel mmiotrace packet for gamma. Same shared
packet README/helper now supersede the tracepoint procedure. It starts global
mmiotrace before one PCI reprobe so the new BAR mapping is covered; modules
remain loaded, Wi-Fi is unmanaged/unassociated during capture, Ethernet stays
managed. It requires single-CPU tracing, drains the final marker, checks BAR
mapping/context-base writes and reported losses, and verifies tracer/CPU/NM
cleanup. A failed bind is not automatically retried. Trace overhead can itself
change startup timing; CAPTURED is not a hardware-success verdict. No debug mask
change is needed for this first MMIO comparison, and host DMA contents remain
outside the trace. No new Pyxis candidate or PXE action accompanies this update.

Packet: `fedora-ground-truth/fedora-mmiotrace-packet.tar.gz`, containing
README, helper and SHA256SUMS under the existing shared stage-2 directory.
Source/syntax review only; no mmiotrace run yet. Inventory and preflight results
remain in `fedora-ground-truth/results/fedora-ax200-{tracing-findings,preflight-lines}.txt`.

### Fedora probe-time ALIVE and a concrete register mismatch

The first mmiotrace result (`fedora-ground-truth/results/ax200-fedora-mmiotrace-20261019-104006.tar.gz`)
is incomplete overall but contains the useful probe-time self-load/ALIVE path:

| Raw mmiotrace.txt lines | Observed operation |
| --- | --- |
| 255–256 | Context base `0x00000000fedbf000` |
| 257 | LTR `0xd4 = 0x88fa88fa` |
| 261–262 | PRPH CPU_RUN `0xa05c44 = 1` |
| 267,269–270 | HW cause1 (ALIVE), acknowledge1, publish RX `0x7f8` |

Five command doorbells follow. Later HW cause4 is RESET_DONE, not FH_ERROR.
This establishes hardware ALIVE handling, not decoded DMA payloads. No capture
retry is needed for this comparison. The firmware's decompressed SHA matches
Pyxis exactly. Linux group13 is DMA-FQ; its context address is an IOVA, not a
comparable physical allocation. Host DMA contents/reachability remain unknown.

**Proven encoding defect:** Pyxis's `AX_LTR_250_US = 0x82fa82fa` sets scale0,
value`0x2fa`. Fedora and both pinned references encode scale2,value250:
`0x88fa88fa` (Linux `iwl-csr.h:97–103`, `trans-gen2.c:376–394`; OpenBSD
`if_iwx.c:885–903`). This is the host boot-latency workaround. Correcting it is
the next proposed narrow candidate; it is not yet proven to cause the native
HW/SW errors. No driver change or staging accompanies this analysis.

The helper cached the sysfs-discovered name before udev renamed it, then failed
interface-up. Cleanup also selected nop before closing trace_pipe, causing
EBUSY and leaving one CPU online. Corrected helpers resolve each netdev command
through PCI sysfs, close/reap the drained reader before nop, and accept overlapping
BAR/MSI-X mappings (46 of351 valid records were previously misclassified).
`recover.py` restores the recorded tracer/CPU state without a device cycle;
the README has gamma's command. Fixes have syntax/source review only; await
recovery verification, not another capture. Raw status remains INCOMPLETE.


### LTR-only candidate and VFIO feasibility (2026-10-19)

Owner authorized the next candidate: code `2ef4ef09` changes only
`AX_LTR_250_US` to`0x88fa88fa`. Existing builder/flags/configuration/pins are
unchanged. `make -j8 image PREBUILT="sdk userspace ports"` passed. Prepared,
**not staged**, at `native/next-ltr-2ef4ef09`; SHA256SUMS passes and archive is
byte-identical to1166d8d3. Kernel SHA-256:
`8605a5d8560ca1d6f83c63ff6b8ca26d480f0187284d56fc910ad8b165fe4e5b`.
No native pass or renewed RF qualification is claimed. Wait until ThinkPad
returns from Fedora and epsilon finishes its DNS window; tell the orchestrator
before any PXE change. Current exact-code CI3210 is not green (pins/build);
the deliberately retained diagnostic base still needs restacking before merge.

Gamma's read-only VFIO inventory confirms sole group13 memberAX200, reset
methodsflr/bus, signed VFIO modules, KVM, QEMU10.2.2 and ample RAM. A trial is
viable subject to actual assignment/reset/remapping success, after recovering
mmiotrace/CPUs. The requested draft lives at
`/shared/present/wifi-ax200-stage2/vfio-draft/README.md`; it has the candidate ISO,
ELF, Remote tool and hashes. No binding/guest run is authorized by that draft.
Before use, wrap temporary binding, bounded collection and verified cleanup in
one owned runner and check exact Fedora OVMF paths/runtime trace events.

QEMU's checked vfio-pci properties provide`x-no-mmap` for MMIO tracing and
`x-no-kvm-msix` for interrupt tracing. Direct mmap/IRQ routing can bypass those
hooks. VFIO supplies host mapping/remapping infrastructure, not proof of guest
DMA content/addresses or MSI-X programming. Guest2GiB placement, FLR, virtual
platform and instrumentation also change; pass/fail is differential evidence,
not a clean binary separation of DMA versus context-layout hypotheses.

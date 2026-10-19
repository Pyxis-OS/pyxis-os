# AX200 open-AP qualification

Stage 3 implementation in progress; no association or data path qualified yet.
Accepted [scope](../../wip/ax200-open-ap.md); stage-2 limits remain explicit.

## Before-code native baseline, 2026-10-19

ThinkPad warm-rebooted by the orchestrator through wired Remote into main
`504821bfabc7646fd600b9f854afde636159c87d`. Userland `aca3e42f`, ports
`422883c6`, fs `f3f22b6e`, lwIP `a1aadb91`; existing builder `3d69e83`, j8,
default Kconfig, INFO, UDP log, `REMOTE_BEACON=t14 WIFI_AX200=1`, diagnostic
READ/SCAN in Development/Remote. Country unset; no CONNECT implementation.
Guest test AP OFF. Conditions reported: AC, same RTL8111 cable, quiet horse.
No nearby identity or firmware country value is recorded here.

Existing ttcp, three 64 MiB samples each, with matching host/guest byte counts
and complete command/drain outcomes:

| Native workload | Median MiB/s | Range MiB/s |
| --- | ---: | --- |
| TX 8 KiB | 111.529 | 110.949–111.554 |
| TX 2 KiB | 100.955 | 99.442–101.081 |
| Discard RX | 112.905 | 112.407–112.922 |

Existing capability-baseline controls, three cohorts of ten samples (warmup
excluded). Times include ordinary syscall/clock overhead, not subtracted:

| Control | Median ns | Range ns |
| --- | ---: | --- |
| open | 12904.5 | 5740–15219 |
| launch | 824012.5 | 691488–1144840 |
| file open | 5701.0 | 5611–6392 |
| file payload | 206062.0 | 203683–213401 |
| file complete | 213190.0 | 211377–221126 |
| clock wrapper | 1352.891 | 960.995–1498.474 |

The Lua OPEN cohort medians vary; retain the full range for matched comparison.
FILE uses the archive 1 MiB fixture and 4,088-byte reads. Launch measures a small
`echo -n` through exit. The clock wrapper is a Lua/native call loop, not isolated
hardware-counter cost. Native idle/BSP CPU accounting remains unavailable.

Private packet: `/shared/present/wifi-ax200-stage3/baseline`, result
`results/baseline-20261019-144045` (raw commands, image/tool hashes, samples and
`controls-summary.json`). Existing command sheet is `controls.txt`; one-command
runner is `run.py baseline`. Passive scan/terminal READY also passes. The packet
was staged after prior notice; after collection the pre-stage-3 default was
restored byte-for-byte and root payloads/EFI/other entries verified unchanged.

Current-main full image and ordinary four-CPU q35/KVM 2 GiB functional controls
pass: boot, absent-radio status and existing FILE/open/launch/clock commands.
Those nested checks are not native timings or RF qualification. No own jobs
remain. The implementation repeat must retain workloads/flags and state any
intervening revision/configuration changes; no measured stage-3 result yet.

## Implementation and functional checks in progress

The implementation integrates bounded ACPI RF-policy capture (BSD-3-Clause
Intel references), legacy station/PHY/MAC/TX/data handling (pinned iwx/net80211
and Linux's BSD-3-Clause alternative), and native kernel connection leases.
Source manifests remain private at `/shared/build/wifi-ax200-stage3/`; retained
notices are in `kernel/wifi/NOTICE`. The additional provisioned DMA closure is
679,936 bytes (155,648 control, 524,288 payload); actual native counters remain
to be checked. Frame queues are bounded and copied between the two BSP workers.

Ordinary kernel/SDK/ports/userland/image builds pass with builder `3d69e83`, j8.
Four-CPU q35/nested KVM, 2 GiB, VirtIO network+entropy, no AX200: controller
absence, optional unbound `wlan0`, Ethernet DHCP and wired Remote pass. A finite
native profile reaches CONNECT and returns UNAVAILABLE; capture binds a failed
value, its reader returns EIO, and a subsequent command/Remote connection remains
usable. Plain console output refuses before association. GDB observes
`ACPI_WIFI_ABSENT`, zero radio/network generation and no pending connection
attempt after refusal. These checks do not qualify RF, successful leases, DHCP
retirement races or allocation-failure cleanup; lifetime repros go through the
owner-routed review. QEMU logs/GDB are under the stage-3 private `qemu/` directory.

The initial private QEMU fixture was rejected for supplying two controller
selectors; it was corrected to MAC-only. An empty capture's append marker also
exposed an overstrict CLI precheck; it now checks the real native collector and
empty mode. One host assembly process segfaulted; an identical assembly rerun
passed. No kernel/runtime fault was observed from that host failure.

Initial station maintenance uses a 20 ms bounded wake and configured Wi-Fi DHCP
maintenance polls at 100 ms. These are tuning, not latency promises; native idle/
BSP cost remains unmeasured. Source review checked authority election before
DISCONNECT and cleanup attribution through final-owner drain, not a runtime
proof of successful-link lifetime.

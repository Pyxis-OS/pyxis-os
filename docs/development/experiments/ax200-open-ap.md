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

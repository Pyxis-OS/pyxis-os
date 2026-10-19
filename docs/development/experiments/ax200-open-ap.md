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

A subsequent build at parent `1e16835b` / userland `1b37eab` also exercises private
bundles around unchanged ttcp/ping with declared `interface=wlan0`. The ttcp call
returns NO_ROUTE while Ethernet Remote stays active; GDB at native route selection
observes exactly one factory interface, retained `wlan0` identity3, incarnation0.
There is no runtime-selected broad grant or Ethernet fallback. The later numeric
validation change bounds nonfinite input before conversion; ordinary compilation
passes. Final native candidate is kernel `e4511822` / userland `2094b9b`, same
builder/INFO/Kconfig and wired selections. Its private country policy and MAC
selector are explicit. Packet `native/` passes hashes,
ELF-revision/pin checks and Python syntax checks. Native RF/data qualification and
matched after-code measurements remain open.


## First native open-link smoke: policy discovery refusal

ThinkPad warm wired-Remote reboot, kernel `e4511822`, userland `2094b9b`,
explicit owner-confirmed country and owner-enabled isolated guest AP. Reboot and
passive target resolution pass (seven scan records). CONNECT returns DENIED (2),
with zero data TX/RX, no wlan0 link/address, controller still ready/not failed,
and Ethernet retaining all management/default/DNS roles. Private result:
`native/results/radio-20261019-154408`. No association or payload pass is claimed.

The boot log reports ACPI PCI resource capture refused for a translated, routed
or malformed address resource. Source inspection establishes the immediate
blocker: `kernel/acpi/wifi.c:find_companion` requires that entire allocation
snapshot to be READY. Refusal therefore publishes ACPI_WIFI_FAILED before any
WRDS/EWRD/WGDS/PPAG/_DSM evaluation. `platform_policy` refuses this snapshot;
the short-circuit in station setup skips the explicit-country MCC request. The
run does **not** establish missing SAR/PPAG tables, an MCC refusal or an NVM
transmit restriction. The combined terminal detail hid this distinction.

Linux v6.12 `iwl_mvm_get_bios_tables` does not fail firmware startup when optional
WRDS/EWRD/WGDS tables are unavailable; SAR initialization skips a disabled profile,
and geo setup depends on basic SAR. Its absent-table path leaves firmware limits
in effect, rather than inventing host power limits. Pyxis already accepts genuinely
ABSENT/DISABLED optional tables (subject to basic/geo consistency); it must not
reinterpret failed discovery, malformed policy or unread locked UEFI tables as
absence. Reference: [iwlwifi mvm/fw.c](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/fw.c).

The owner accepted independent bounded companion lookup as a routine fix. The
implementation resolves unique enabled PCI roots by _HID/_CID, _SEG and _BBN,
then immediate _ADR children along retained PCI ancestry. It does not consult or
weaken the refused BAR-allocation snapshot. Absent optional policy and failed
discovery remain distinct. Terminal status now identifies platform sources and
country-MCC failures separately. Build and QEMU/native validation follow below.

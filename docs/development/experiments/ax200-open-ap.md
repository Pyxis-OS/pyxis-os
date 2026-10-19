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


The companion fix was first built and QEMU-checked at `c30b14c7`, then linearly
restacked on main `840f80a1` (package-runtime changes), userland `30d9775f` based on
`f2264a8`, and main's ports `c8e0efd`. Final code/image is `0d79898f`; no compiler
container change. The boot-schema conflict preserves both `ram_npfs_bytes` and
`wifi_policy`. Full kernel/SDK/ports/userland and image builds pass, INFO/default
Kconfig, existing builder `3d69e83`, j8. All four parent CI jobs pass at that head.

Four-CPU q35/nested KVM, 2 GiB, VirtIO network+entropy and no AX200 again boots
with Ethernet DHCP and direct Remote working. Absent-radio CONNECT returns
UNAVAILABLE and a failed captured value; reading it fails with EIO; subsequent
commands pass. GDB observes ACPI_WIFI_ABSENT and zero radio/network incarnation.
This does not execute the successful AX200 companion or RF path. An initial
Remote connection reset was a host-fixture mismatch: native reverse-beacon boot
configuration with a direct-connect client. The QEMU fixture was corrected to
omit the beacon; the native packet retains `REMOTE_BEACON=t14`.

Replacement packet `next-companion-0d79898f/` passes image/tool/root hashes,
advertised ELF revision and private configuration checks. It is **not staged**;
the ThinkPad is with another owner's task. Guest AP is disabled between attempts.
After the window returns, repeat the same `run.py reboot` and `run.py radio`.
No Wi-Fi association, DHCP or payload result is claimed yet. Owned QEMU/GDB jobs
are stopped; retained raw functional evidence is in private `qemu/companion-*`.


## Companion-alias refusal

Native `0d79898f` / userland `30d9775f` warm boot completes; passive target
resolution passes, then CONNECT again returns DENIED (2), now with
`ACPI companion identity malformed`. The controller remains ready/not failed,
with no data TX and Ethernet policy intact. Private result:
`next-companion-0d79898f/results/radio-20261019-162715`.

The previously captured ThinkPad DSDT (checksum valid) supplies a concrete
explanation: PCI0 has _HID PNP0A08, _CID PNP0A03, _UID 1 and _BBN 0. _UID is
not a bus selector. GPP7 has _ADR 0x00020003 (the retained 00:02.3 port).
Its immediate PXSX, DEV0 and WLAN children all have _ADR 0, describing 03:00.0.
None has a device _STA; no SSDT augments these names. PXSX's nested WRST power
resource _STA is not a device status. Thus all three receive the specified
missing-_STA default and the current unique-child check necessarily refuses.
Moving _STA before duplicate detection cannot resolve this case.

This is cached native AML plus source inspection, not a live per-node dump from
this boot. The next admitted diagnostic will identify the root, hop and matching
node values. The PCI address-range check also inappropriately covers unrelated
non-PCI siblings, but no out-of-range immediate sibling on this path was found;
it is not established as the cause here. The owner accepted bounded capture across enabled endpoint aliases in the
stage plan; no first/last-node heuristic or TX bypass is applied.


The alias correction keeps unique root/intermediate bridge matches and collects
at most 16 enabled final endpoint nodes. It captures GLAI on every alias before
ordinary policy reads, then reconciles each optional table and supported Intel
_DSM function. Proved absence does not replace a present value; duplicate state/
value conflicts latch MALFORMED. Comparisons use scalar members and byte arrays,
not structure padding. Query support unions disjoint functions; conflicting
function values still refuse. All-disabled aliases report DISABLED. One bounded
requested native diagnostic identifies root/bus, endpoint _ADR and alias names;
no nearby network identity is logged. Source review found no concrete issue;
build/QEMU checks do not establish successful native policy or radio operation.


## MCC provenance parser correction

The `ee59b4ba` native repeat confirms root PCI0, port 00:02.3 and three endpoint
aliases. Companion reconciliation succeeds; CONNECT refuses at MCC reply
admission, without data TX or controller failure. Private actual radio result:
`next-aliases-ee59b4ba/results/radio-20261019-165051` (the preceding reboot result
is timestamped 164955). No reply header was captured by that image, so the
specific native failing predicate is not yet known.

The pinned CC77 advertises MCC notification version 5 and MCC_11AX capability89.
Linux v6.12 `iwl_mvm_update_mcc` therefore selects the 20-byte v4 response
(status0, MCC4, cap6, source12, count16, channels20), not v3's 16-byte or v8's
24-byte header. The existing offsets are correct. Its returned source is profile
provenance, not an echo of requested MCC_API9: Linux retains the returned source
for subsequent updates. Requiring source9 was an independently proven overstrict
check. The correction accepts known non-test profile sources while keeping
status0/1, exact owner-country, counted extent and NVM/firmware/host channel
intersection requirements. Reserved, uninitialized and test sources still refuse.
A bounded requested header diagnostic records status/MCC/cap/channel count/source
in private native logs; no actual returned country or header dump is committed.
New refusal details identify extent, status, country mismatch or source separately.
The `7df24da5` native repeat (`next-mcc-7df24da5/results/radio-20261019-171348`;
171250 is the reboot record) returned a 20-byte reply with status4
(`MCC_RESP_ILLEGAL`) and no channel map. Source inspection confirms the request
uses the Linux encoding: alpha2[0]<<8|alpha2[1], little-endian, source MCC_API9,
28 bytes with zero key/reserved fields. The transport matches the reply opcode,
group, command index and queue before copying its payload. There is no evidence
here of reversed country bytes or a v3/v8 offset error. The failed reply's other
fields do not establish an applied country or profile.

The status check now precedes channel-map validation so this refusal reports
ILLEGAL rather than an extent error. Kernel build passes; this diagnostic-order
change is not native-qualified. No alternative source, country or firmware-limit
fallback was attempted. Why this correctly encoded request is rejected remains
unresolved; the header's OEM-testing key comment alone does not establish a key
requirement. Actual command-buffer/reply-envelope evidence or a stock-driver MCC
exchange is needed before proposing a different request provenance.


The next evidence candidate logs the command's actual DMA-buffer bytes (wide
header plus payload) immediately before its doorbell, and the matched RX packet
(envelope plus payload) after opcode/group/index/queue and extent checks. Each
capture is bounded to 64 bytes; full lengths are logged. It keeps the same
country, source, key and reserved fields. Byte-for-byte source comparison against
Linux's packed `iwl_mcc_update_cmd` found no request-encoding difference for the
same country/source. No replacement country/provenance or RF fallback is used.
These temporary diagnostics and raw bytes belong only in private native results;
they are not network or connection qualification.

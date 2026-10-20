# AX200 open-AP qualification

The initial stage-3 native qualification (2026-10-20) covers: an explicit legacy 2.4 GHz open link,
independent DHCP and verified data while wired management stays primary. See the
[controller](../../devices/ax200-wifi.md) and [interface](../../interfaces/wifi.md).

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


## Final native configuration

Owner/orchestrator-run ThinkPad T14 Gen 1 AMD, AX200/CC77, kernel
`05d912e26b56b1abbd3164c94e7377395b03d047`, userland
`e84ad5354c3d57a70c99ce1bcb29b9572f3e70f6`, ports `053d5714`, fs `f3f22b6e`,
lwIP `a1aadb91`, builder `3d69e83`, DEBUG ceiling / INFO console and UDP log.
RTL8111 cable goes directly to the router LAN; Ethernet retains default, DNS,
Remote, UDP log and debugger roles. Test AP is open, broadcast, 2.4 GHz, allowing
a legacy 20 MHz station. SSID/BSSID/MAC and profile bytes stay private.

The owner allowed guest↔LAN access for the controlled payload test, then disabled
LAN access again; the guest network is isolated between sessions. All final RF
checks used channel 6. Firmware passive mask `0x1fff` admits channels 1–13 under
the current profile; boot mask `0x3fff` is only the supported-domain ceiling.
This proves native map handling, not RF qualification on channels 12/13.
Native status reports 4,329,472 DMA bytes, 512 RX entries/buffers, 256 command
entries, one command queue and a 256-entry data ring.

## Functional results

| Check | Evidence/result |
| --- | --- |
| Final reboot and link/DHCP | `reboot-20261020-122202`, `radio-20261020-122313`: complete, new exact image, independent wlan0 lease; clean explicit disconnect |
| Scoped payload | `payload-20261020-122318`: 256 KiB upload/echo and download hashes match; both host peers match wlan0's address, not eth0's |
| No Ethernet fallback | After disconnect, the same wlan0-only TCP grant refuses CONNECT; the host fixture sees no extra accept |
| AP loss | `apoff-20261020-122349`: held link becomes unassociated/terminal, TIMED_OUT17; WLAN address/admission clears and wired management remains |
| Explicit same-boot recovery | `radio-20261020-122559`: fresh selection, association/DHCP and disconnect complete after the owner restores the AP; no reboot |
| Repeated links / owning aliases | Earlier kernel `4d351d3e`, `batch-20261020-113637`: three link/DHCP/disconnect cycles; remaining alias survives first close, final close retires; fresh recovery passes |
| Foreground cancellation | Earlier kernel `4d351d3e`, `cancel-20261020-113657`: capture terminates, setup-incomplete ABANDONED28 retirement and complete session drain; explicit recovery passes |

The AP-off runner completed loss, then observed 16 target-absent scan attempts
without a controller fault before the orchestrator stopped it. The owner observed
that this Archer takes minutes to resume guest broadcasts after guest re-enable;
the main 2.4 GHz network stayed up. Earlier main-network disappearances occurred
with channel/setting changes, not this simple guest toggle. The 120-second
selection budget does not guarantee router recovery. The subsequent explicit
CONNECT on the same boot qualifies Pyxis's clean-loss/recovery contract; the
interrupted combined run is not labelled COMPLETE. No automatic reconnect occurs.

The fixture is the existing `tcp.pxe` in a private RAM bundle with a declared
wlan0-only TCP CONNECT grant. It sends a finite request, half-closes, then reads
the response. The host on allowed TCP 5001/5002 receives/validates the upload before
replying. Guest native FILE size/SHA-256, host byte/hash/source observations and
scoped disconnected refusal establish payload integrity and route isolation.

Commands: `run.py radio`, `qualify.py payload`, `qualify.py apoff` in
`/shared/present/wifi-ax200-stage3/next-lar24-05d912e2/`; earlier ownership/cancel
records are under `qualification-4d351d3e/`. Raw results and image/config/tool
hashes stay there; no nearby identities are copied into this record.

## Build, QEMU and limits

Full kernel/SDK/ports/userland build passes. Exact code-head CI3458 passes changes,
pins, filesystem and build; plan CI3459 passes. Exact-code q35/KVM/hostCPU/4vCPU/
2 GiB QEMU with VirtIO net/RNG passes Ethernet/Remote, absent AX200, channel-12/13
profile parsing followed by CONNECT→UNAVAILABLE6, failed-capture status1, a later
command and full Remote drain. GDB reads no pending attempt/connection/regulatory
admission and fourteen NVM entries. QEMU is not RF evidence; no synthetic radio
or firmware-response injection was used. Kernel and SDK changes are signed;
final documentation-only closure does not change the native-tested code.

Direct native CANCEL, setup deadline, read-only observer and initiating-root CUT,
RF on 12/13 and quantitative Wi-Fi/associated wired-load costs remain
[qualification debt](../../technical-debt.md#ax200-open-link-qualification-follow-ups).
Hardware rfkill remains untestable with this ThinkPad's F8/Fn+F8. No matched
post-code performance run was taken under the horse compiler-load hold; functional
payload/counter success does not establish throughput or CPU cost. No jobs remain.

PXE was restored through both owned staging layers to the pre-stage-3 menu,
byte-for-byte SHA-256 `3c8c58913dfbb98bbafafdd12170f5284c78c05375929bf9eee6ef3faaea9a2e`;
root kernel/archive/config and EFI payload hashes remained unchanged.

## Security-review correction qualification

The 2026-10-20 review corrections add a Wi-Fi lease-overlap guard and 127/8 DNS
refusal, bounded live regulatory refresh, deferred ERP/slot/preamble updates,
target-only scan collection, Retry-aware duplicate caches, passive-only channel
14, and no-op/late TX completion handling. Raw MCC byte dumps are removed and
station/context diagnostics use ktrace. These paths are source-reviewed; the
native radio/payload requalification is pending on the corrected image.

The earlier eth0/wlan0 test leases shared a subnet. That setup is now deliberately
refused with `CALL_ALREADY_EXISTS`; rerunning successful DHCP/payload needs a
non-overlapping guest subnet while wired management remains primary. The router
must route the controlled guest transfer to horse during that test. Earlier
native results above remain evidence for their stated revisions, not these fixes.

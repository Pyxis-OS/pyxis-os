# RTL8922AE one-PHY preparation qualification

## Revisions and configuration

Baseline: main `dc8319ea` and userland `05215476`, captured before implementation.
Candidate: [Pyxis #1174](https://git.internal/PyxisOS/pyxis-os/pulls/1174),
[helper #367](https://git.internal/PyxisOS/pyxis-userland/pulls/367).
The initial QEMU candidate was `b809d3ba` / `fc4bc24`, rebased on main
`e334f159`; subsequent signing/documentation and helper-reporting changes do not
change the exercised kernel paths. The updated helper and complete image were
also rebuilt. The native packet manifest records its exact independent heads
and hashes; native execution is pending.

Build: existing `pyxis-llvm23.1.3-3d69e83`, `make -j16 image LOG_LEVEL=info
DISTRIBUTION=full LIVE_CONFIG=<private-read-scan-profile> REMOTE_BEACON=
WIFI_RTL8922AE=1`; no private game overrides. QEMU: q35/KVM, host CPU, four CPUs,
4 GiB, Fedora OVMF raw pair, modern VirtIO-net with slirp and Remote.

## Source and host checks

The pinned Linux reference and firmware are unchanged from the
[input record](rtl8922ae-scan-inputs.md). Independent source review corrected
post-download BB/RF enable, firmware-table RF upload filtering and execution
order, OUTSRC command category and byte-width MAC controls. The pinned radio
image executes B then A; only 226 B / 225 A digital words are uploaded, while
all selected records are written to their proper RF paths.

CPU-only power-image construction covered world, RO and US channel profiles.
World NO_IR channels 12–13 cannot become calibration channels. Independent
raw-table comparison covered the constructed power bytes. The MMIO fixture
checked that post-RFK verification accepts changed calibration coefficients,
rejects altered hard limits/force/offset controls, performs no writes and obeys
interruption. These are software checks, not measured RF or proof of device
programming. Modified units passed freestanding warnings-as-errors compilation;
the integrated full image built successfully.

## QEMU controls

READ decoded the 600-byte status; absent-radio SCAN returned UNAVAILABLE without
startup. GDB confirmed no pending work, no attempt and zero PHY observations.
A separate one-CPU AX200-absent boot decoded the zero extension and refused SCAN.
Neither check simulates Realtek hardware or qualifies RF, hardware cancellation
or physical rfkill.

Interleaved A1/B1/A2/B2 used the existing command:

```sh
iobench read boot://share/iobench.bin --rounds 3 --buffer 4096
```

All 1 MiB reads verified. Nested payload times in milliseconds:

| Boot | Median | Range |
| --- | ---: | ---: |
| A1 | 0.265 | 0.259–0.268 |
| B1 | 0.274 | 0.268–0.288 |
| A2 | 0.279 | 0.261–0.309 |
| B2 | 0.261 | 0.256–0.268 |

The ranges overlap and the direction reverses in the second pair. These short
archive-read controls include the intervening main/userland changes; they do
not isolate a radio cost or establish native performance.

## Native gate

Packet and raw evidence: `/shared/present/rtl8922ae-phy-init/`.
Chronium's first scheduled request on `bd89b5d3` / `c460e428` stopped inside full
DMAC initialization, before BB/RF tables, channel tuning, power programming or
any explicit RFK command. Boot valid/state `1f`, PHY state `91`, RFK completed
zero and 14 valid/unblocked rfkill checks record the progress and confirmed
MAC-off/FLR. Wired management survived; PXE restoration was byte-identical.
Combined firmware loading and BB pre-initialization/enable did run; omission of
explicit calibration does not establish physical silence from firmware.

That image retained one detail for the entire DMAC initializer; the exact
register failure cannot be reconstructed after reset. The follow-up retains
substep/width/register/expected/observed/mask in status only, including the poll's
actual last sample, without changing hardware operations or adding logs.
The original packet/results are frozen. A separate private MAC-only VFIO image
stops before BB/RF tables, power programming and RFK even if MAC setup succeeds.
Its Linux comparison requires a reviewed matching-module guard before opening
the interface.

The orchestrator's MAC-only VFIO first phase on the status-diagnostic image
(`70050b8d` / `c460e428`) retained `mpdu-amsdu`: write `0x010e05f0`, read
`0x000e05f0` at `0x9c94`, differing only in bit 24 (`MPDU_CUT_CTRL_EN`).
RFK remained zero, checked stop passed and the retired guest left the VFIO group
unowned. This is a precise diagnostic outcome, not full MAC/PHY qualification.
Evidence: `vfio-mac/evidence-first/` under the packet directory.

Accepted correction (chronium, 2026-10-23): retain the pinned v6.18 write and
remove only Pyxis's added immediate equality refusal for this register. The
reference's `mpdu_proc_init_be` has no cut-specific branch or readback check
there. No inspected source establishes that bit as write-only or self-clearing.
All other register guards, bounded readiness checks and checked stop remain.
The frozen Fedora comparator instead writes `0x210e05f0` (additional ADDRCAM
check bit 29); that newer-source difference is not imported by this correction.

Exact-module data/relocation and firmware review qualified the separate Linux
comparison's four entry guards. Its earlier AFE sequence is absent from this
fixed firmware, and successful MAC initialization precedes the primary BB-table
guard. The review is bound to the prepared hashes and does not establish
physical RF silence during firmware/bootstrap startup. The original completed
packet/images stay frozen. The scheduled Linux phase subsequently booted and
loaded firmware/board metadata, but did not reach its serial guard checkpoint.
No guard was armed and no interface-up token was sent. Read-only inspection
confirmed the recorded guest terminated and VFIO group 18 was unowned. The
saved output cannot distinguish a blocked module probe from the following
symbol enumeration; it supplies no Linux full-MAC comparison result.

For the accepted `0x9c94` correction, the kernel and full info image rebuilt,
the changed unit compiled with warnings as errors, and disassembly preserved
the exact direct write followed by the existing guarded header-control change.
Four-CPU q35/KVM/OVMF/modern-VirtIO QEMU with no radio reported absent/idle;
SCAN refused at start with status 6 and no DMA/firmware backing. GDB confirmed
zero PHY observations, no pending work and no MAC failure. The initial control
used the result-failure wording expected after a present-device request; the
absent-device path instead refuses at start. The corrected control recorded
that actual terminal result. Neither control exercises the physical register;
progress past the former refusal remains unqualified.

The host staging helper was exercised on a disposable local directory: menu
restoration was byte-identical and protected root payloads unchanged. Actual
PHY/RF programming, calibration outcome, enforced power behaviour and physical
rfkill remain unqualified. Both PRs remain draft.


## Current-main preparation (2026-10-26)

The draft rebases onto main `182c58ba` and helper #367 onto userland main
`fdfa26cb`. The ABI-prefix helper update is `035b143a`; other dependency pins
are inherited from main. This track targets 0.0.11 (chronium, 2026-10-26),
not 0.0.10. The hardware sequence and frozen diagnostic packet/evidence are
unchanged. Kernel/runtime/SDK and the Wi-Fi helper rebuild with the current
XSAVE, VM and allocation changes. One shadow warning is in unchanged
`kernel/user/spawn.c`.

The released `wifi_status_reply` remains exactly 544 bytes; the composed
`wifi_status_full_reply` adds 56 bytes of PHY facts. The object path accepts
the old minimum and copies/reports only the prefix fitting the caller's
capacity, capped at 600 bytes. The helper's old query keeps the prefix; its
full query requires a complete validated snapshot. Both 0.0.8 and 0.0.9 ABI
checks pass without changing the supported-release list.

The private four-CPU q35/KVM/OVMF absent-radio check exercises capacities 0,
1, 543, 544, 545, 599, 600 and 608. Undersized requests return
BUFFER_TOO_SMALL with no changed bytes; successful requests report the
copied count and leave the tail sentinel intact. A consumer compiled with
the actual 0.0.9 header works, as do current prefix/full helpers and the
rebuilt command. GDB checks no startup attempt and zero PHY observations.
An initial private-program launch omitted its required MEMORY grant and was
refused before it ran; the corrected declared profile supplies that grant.
Evidence and commands: `prefix-check/` under the existing record directory.
This exercises reply delivery and absence behavior, not Realtek hardware.

Hardware progress remains the recorded MAC-only refusal at `0x9c94` above.
The accepted removal of its extra equality check has not yet been exercised on
hardware. The Linux phase never armed its four terminal guards or sent the
interface-up token. Its firmware/board output and clocksource timeout do not
establish whether module probing or subsequent symbol enumeration stalled.
Fresh diagnostic preparation separates those phases and retains all-vCPU
stacks on timeout, rehearsing checkpoint handling without VFIO first. A new
exclusive VFIO window goes through the orchestrator; never resume the failed
invocation or reuse its consumed result directories. BB/RF programming,
calibration, power enforcement and scan remain unqualified. Both PRs stay draft.


### Checkpoint controller rehearsal

The fresh packet is under `vfio-mac-checkpoints/`, preserving `vfio-mac/` and
its consumed results. Two local q35/KVM guests (Linux 6.19.10, four vCPUs,
no VFIO or network device) exercise synthetic module-phase and symbol-phase
timeouts. In each case the controller records all four PCs/registers,
GDB backtraces and one raw stack page per vCPU after QMP confirms a halt,
then retires the guest without sending an interface-up token or resuming it.
These PCs/backtraces have no kernel debug symbols: raw data is retained, but
successful unwinding is not claimed. The rehearsal does not test the frozen
7.2.10 module probe, the four real entry guards or any Realtek hardware.

The actual comparator still requires the exact reviewed 7.2.10 kernel,
modules, firmware and guard ownership before its one link-up release.
Module-begin/done and symbol-walk-begin/done markers distinguish the old
ambiguous checkpoint failure; bounded-memory awk validates the four unique,
nonzero symbols and their modules. Timeout capture precedes checked retirement;
there is no resume/retry permission. Raw stacks stay private on vela. The next
hardware window is this one fresh Linux diagnostic invocation, not a new
Pyxis calibration or passive-scan request.


### Linux comparator completion (2026-10-26)

The fresh `vfio-mac-checkpoints-v2` invocation on vela passed its frozen
Fedora 7.2.10 review, all module/symbol checkpoints and four hardware guards.
One interface-up token reached the primary `rtw89_phy_init_bb_reg` entry;
there was no resume after it. The reviewed callgraph places that entry after
MAC initialization and before BB-table programming. QEMU retired and VFIO
was unowned; no timeout-stack capture was needed. Evidence is in
`vfio-mac-checkpoints-v2/evidence-checkpoints/` and its read-only
`linux-control-readonly.json` supplement.

The captured MAC sequence writes `0x210e05f0` to `0x9c94`, then continues
with `0x9b00`, `0x9cc0`, `0x8938` and `0x9d00`. This agrees with the reviewed
Fedora module, which lacks Pyxis's former immediate equality check. Its extra
ADDRCAM bit 29 is a newer-source difference; retain the pinned v6.18 Pyxis
value `0x010e05f0`. No bit-24 write-only/self-clearing semantics are established.
The trace has BEGIN/END and 7,654 parsed access records, but still reports USDT
operand-parser warnings: do not claim complete MMIO capture. MAC completion
is established by the reviewed live primary guard, not trace completeness.

Next qualify the already accepted Pyxis equality-check removal against the
same MAC-only boundary before advancing to full PHY/RFK qualification. No
additional register relaxation, newer-source bit or reordered hardware step is
justified by this result. It is not BB/RF, calibration, physical silence or
passive-scan qualification.


### Current-main rebase and MAC-only preparation (2026-10-27)

Parent #1174 rebases onto main `297e48a4`, helper #367 onto userland
`fd53bce4` as `83b06154`. The earlier `20820a68` CI build did not compile:
its prerequisite pin check rejected helper `035b143a`, which no longer
contained userland main. Repinning the rebased helper resolves that gate;
no source/compiler fix was needed. Rebased code head `634c2169` passes all
five CI checks and the local kernel/runtime/SDK/helper rebuild.

Fresh absent-radio four-CPU q35/KVM checks repeat the released-header,
544/partial/full-capacity and untouched-tail cases with the current kernel
and helper. GDB still observes no attempt and zero PHY facts. Sources/logs:
`prefix-current-20261027/` and `ci-20261027/` under the same evidence root.
There is no new timing or hardware-performance claim.

The private MAC-only probe `09a397b5a501` retained native admission and a
return after MAC_CONFIGURED, before downstream BB/radio/channel/power/RFK.
The native attempt verified that running identity before and after its one
request, but failed earlier at the initial power-state guard. It did not
qualify MAC completion.

### Register refusal audit (2026-10-27)

The native probe reported PHY state `0x20` (STOP_UNCERTAIN), boot state `0x19`
(ATTEMPTED, MAC_OFF, PCI_STOP), and OUTCOME_UNKNOWN/status 27. Firmware readiness
and PRE_DEINIT were absent. The raw `0x03f0` value was not retained, so this
cannot distinguish a nonzero inherited state from an inaccessible read. No
firmware download or downstream host RFK was reached. The sealed firmware has
no optional AFE element; cleanup still performed power-off/analog operations.
This is not evidence of physical RF silence. The orchestrator verified menu
restoration and booted the standard default afterward. That attempt is frozen.

**Direction (chronium, 2026-10-27):** remove register refusals that the pinned
Linux path does not make, except checks protecting an identified hardware or
native-authority hazard. Audited all RTL8922AE hardware/transport, MAC/PHY,
power/RFK and supervisor paths against Linux v6.18 commit
`7d0a66e4bb9081d75c82ec4957c50034cb0ea449`; the hash-checked reference archive is
recorded in [firmware provenance](../../../firmware/rtl8922ae/UPSTREAM.md).
Linux paths below are relative to `drivers/net/wireless/realtek/rtw89/`.

| Register or group | Added refusal | Reference behavior and disposition |
| --- | --- | --- |
| Ordinary 8/16/32-bit RMW and MAC/table writes | Sentinel-value or write-equality rejection | `core.h` write helpers post the operation; remove blanket checks and infallible boolean wrappers. |
| `0x03f0` WLMAC admission | Require state zero | `mac.c:1484–1488` rejects active state one only; match it. |
| `0x03f0` after MAC-off | Extra state-zero poll | `rtw8922a.c:413–500` uses off handshakes, no final state-zero poll; remove. |
| `0xb030`/`0xb050`, `0xb200`/`0xb300` ring groups | Count/base equality | `pci.c` ring initialization writes without equality; remove. DMA address/index bounds remain. |
| `0xb000` pre-deinit | Stop-master equality poll | `pci_be.c:392–406` posts stop/index updates without that poll; remove. PCI reset remains mandatory. |
| `0xb01c` DMA idle | Extra busy bits 15/31; combined 1 ms | `pci_be.c:185–201` checks TX channels then RX, 1 ms each; match masks and separate waits. |
| Shared completion polling | Early timeout refusal before the final sample | Linux `include/linux/iopoll.h` samples the condition at expiry; match, with native request/cancellation limits retained and a 1 µs default poll interval. |
| `0x8404`, `0x0888` | Sentinel exceptions | Plain reference RMW, with functional WDE/PLE and firmware waits; remove value classification. |
| WDE/PLE quota groups | Min/max equality | `mac_be.c:298–334` writes quotas without comparison; remove. Keep WDE/PLE-ready waits. |
| `0x0270` XTAL | Post-read sentinel and crystal XI/XO equality | Reference checks serial completion, not XI/XO equality; remove added refusals and warning-only caller aborts. |
| `0x9c94` and MAC enable words | Extra enable/readback equality | Reference ordinary writes/software-state checks; no hardware enable equality. |
| `0x7164` PHY mailbox | Require bit 0 within 1 ms | `fw.c:6965–6987`, `fw.h:117`: any nonzero byte within 1 s; match. |
| `0x0068` eFuse-state conversion | Extra idle readback; abort on converter return | `efuse_be.c:143–192`, `mac.c:2950–2992`: idle write only; dump/PHY-cap callers ignore converter return. Match. |
| RF table, crystal and PLL callers | Abort outer initialization on ignored helper results | Reference warning-only/void callers continue; match, retaining native cancellation and actual DMA/transport failure gates. |
| RFK reports/acknowledgments | Global DONE_ACK refusal, duplicate equality, START suppression | Reference assigns latest RFK report/completes; unrelated ack errors do not globally fail RFK. Match, preserving buffer bounds and truthful success facts. |
| RFK timeout/non-OK | Skip remaining helpers | `phy.c:3631–3659` checks helper completion; `rtw8922a.c` continues callers. Continue reference order, retain actual failures for qualification. |
| Broad power image | By-rate/reference/shape/TSSI/MACID identity comparisons | `phy_be.c`/`rtw8922a.c` write without those comparisons; remove. |

Retained exceptions are narrow: exact PCI/BAR/MSI and DMA ownership protect
mapped memory and interrupt delivery; scheduler/ACK/BA/null/CTS gates prevent
forbidden autonomous TX. SAR/normal/RU cap pages and force/additive selectors
protect the admitted RF envelope during TX-capable calibration (`reg.h`
TX-power fields, `phy_be.c` power-limit writers, `fw.c` RFK command semantics).
Unrelated power-selector bit 16 is no longer compared. Factory eFuse validity,
real board/antenna capabilities and malformed/unknown regulatory/SAR inputs
remain admission requirements without invented defaults. These exceptions are
native policy and safety checks, not claims that Linux validates register
identity. Complete per-register/callsite inventories and source evidence are
under `/shared/present/rtl8922ae-readback-audit/`.

No new native or VFIO run accompanies this audit. The consumed MAC-only packet
is unchanged; a fresh packet and scheduled native check are required before
claiming MAC completion or proceeding to full PHY/RFK.

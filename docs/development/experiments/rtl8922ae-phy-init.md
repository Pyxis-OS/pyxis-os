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

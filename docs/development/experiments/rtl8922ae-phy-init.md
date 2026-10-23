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
the interface. No subsequent hardware execution has run yet.

The host staging helper was exercised on a disposable local directory: menu
restoration was byte-identical and protected root payloads unchanged. Actual
PHY/RF programming, calibration outcome, enforced power behaviour and physical
rfkill remain unqualified. Both PRs remain draft.

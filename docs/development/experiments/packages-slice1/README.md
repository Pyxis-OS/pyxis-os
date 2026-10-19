# Packages slice 1 qualification

2026-10-19, task 7 in progress. Source `880ff590`; userland `d5f90549`, ports
`e9b0eb4a`, fs `f3f22b6e`, lwIP `a1aadb91`. Existing builder
`pyxis-llvm23.1.3-3d69e83`; `make -j8 image LOG_LEVEL=info DEVILUTIONX=1
EDUKE32=1 PREBUILT="sdk userspace ports"`. Dependencies are the already qualified,
unchanged task-5 products; the kernel is rebuilt at this source revision.

The packet includes the two personal-use programs but no game data/font files;
it is not for distribution. Its only configuration adaptation copies the stock
live profile to `config/installed.lua` too. The existing rescue/default-config
selection therefore uses RAM npfs packages/system/HTTP backing and RAM home,
without applying the installed pool override. No kernel, ABI or source edit.

## Real repository

Tests use the real LAN service at `192.168.0.213:5245`, not a local mirror:
`/public/` offers fonts, and `/personal/` offers Doom 1.9 and Quake 1.06 shareware.
The client explicitly records those URLs. Hostname `horse` resolves through
this VM's separate overlay DNS, whereas the service is LAN-bound; selecting the
known LAN URL is authored configuration, not client-side address substitution.
No repository upload, mutation probe or service/firewall change by alpha.

QEMU: Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA 1280×800,
VirtIO network/RNG and a read-only disposable GPT disk. Boot selects the
installed/rescue path with that scratch disk GUID and `boot.default_config=1`.
Read-only GDB inspection found one npfs pool, with `ram_capacity=2147483648`
and no next pool; package changes therefore use RAM, not the scratch disk.

- Empty set refuses fonts, Doom, Quake, DevilutionX and EDuke32 with the named
  missing required package. Both real indexes load and list the three offered
  packages.
- Confirmed install selects set 1 with three packages. `pkg verify` validates
  selected files and the command index.
- All nine styles/gear render in font-min. Removing fonts selects set 2 and
  refuses a new font-min launch; the existing renderer remains usable and
  updates its pointer text. Predecessor rollback creates set 3, restoring fonts;
  a fresh font-min launch renders, and full verification passes again.
- Doom and Chocolate Doom render their shareware level/demo and return normally.
  Quake and Chocolate Quake render the demo and explicit `map e1m1`, then quit
  back to the shell. This is basic
  launch/data/render/exit coverage, not save/load, pointer-lock or audio quality.
  No audio device was emulated.
- Requesting `clang-hosted` explicitly reports no available package version;
  it does not silently launch an archive toolchain or switch the selected set.

Raw logs, GDB output and screenshots remain at
`/shared/present/packages/task7-20261019/`. No timing/performance claim or native
result is inferred from these nested-QEMU observations.

After an orderly QEMU reboot, `pkg status` returned set 0 with zero packages,
and `pkg repository list` reported no configured repositories. Both commands
completed successfully. The read-only scratch disk was unchanged by construction;
this establishes the packet's boot-lifetime RAM behavior, not installed-pool
persistence or native storage qualification.

## Owner-run vela results

2026-10-19, the owner deployed and booted the packet below on vela. Both
repository additions and `pkg install fonts doom-data quake-data` worked;
`pkg verify` passed. font-min worked, with the owner reporting excellent TTF
rendering on the panel; Doom and Quake also worked. These are owner-reported
native results, separate from the QEMU observations above. Chocolate variants,
native removal/rollback and reboot lifetime have not been reported.

## Package confirmation follow-up

At the owner's request, pkg keeps the same Review content but replaces typed
`apply` with `[y/N]`, default No. Only exactly `y` or `Y` accepts; empty or any
other input cancels without switching the active set. Already prepared trees
can remain inactive, as before. No non-interactive flag existed and none was
added. Installer wipe/update confirmations are unchanged. The deployed native
packet predates this prompt change.

Follow-up source: main `11376a8e` plus userland `6d15281e` (PR #311).
`make -j8 image LOG_LEVEL=info DEVILUTIONX=1 EDUKE32=1
PREBUILT="kernel sdk ports"` passed with verified unchanged component bundles.
Q35/KVM, four vCPUs, 2 GiB, OVMF, standard VGA and VirtIO network/RNG,
ordinary live boot without a disk: empty install confirmation cancelled at
set 0; `y` installed Doom shareware as set 1. Removal answers `yes`, `n` and
`apply` each cancelled, retaining set 1; `Y` removed it into set 2. Rollback
with `y` selected fresh set 3 and `pkg verify` passed. Remote exited normally.
Evidence: `/shared/present/packages/task7-20261019/confirmation/`, with the
image build log alongside it. No native restaging or deployment by alpha.

## Native packet and remaining gates

`/shared/present/vela-packages-slice1/` contains the matched kernel/live archive,
kernel.config, SHA256SUMS, revision/profile record and short numbered run sheet.
The orchestrator schedules it; alpha does not deploy or run native checks.
Owner uses `stick-deploy ... --initrd` and the existing rescue entry, without
manual Limine edits. Actual deployment establishes stick fit; the pair is
90,906,848 bytes. Three equal new pairs would occupy 272,720,544 bytes, but that
is not a claim about free space beside existing baselines or foreign files.

Owner deployment and boot establish that this packet fit the stick; no free-space
margin was measured. Remaining native checks are listed above. DevilutionX/EDuke32
play need their private packages; toolchain/clangd need a fresh hosted build with
current exports. The existing horse payload was deliberately not packed.
No private data was used or logged, and task 7/the slice is not complete.

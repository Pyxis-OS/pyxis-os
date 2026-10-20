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
rendering on the panel; Doom, Chocolate Doom, Quake and Chocolate Quake all
worked with the same packet. These are owner-reported native results, separate
from the QEMU observations above. Native removal, rollback and reboot lifetime
are recorded [below](#thinkpad-removal-rollback-and-reboot-lifetime).

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

## ThinkPad removal, rollback and reboot lifetime

2026-10-20, hands-free on the ThinkPad: the orchestrator staged the packet and
started the runner; nobody handled media or the keyboard. Source main `d3a65acd`
(userland `c3be6fe6`, ports `e9b0eb4a`, fs `f3f22b6e`, lwIP `a1aadb91`), built
with `make -j16 image REMOTE_BEACON=t14 LOG_UDP=1` on the existing builder. It
was a PXE live boot with `init=boot://boot-init.pxe remote.beacon=t14 log.udp=1`,
so package sets and repository configuration lived in RAM npfs. The runner
used the real horse repositories above.

The runner (`run.py`, on horse) drove a reverse Remote machine session. Each
step waited for the shell's typed completion event, and each `[y/N]` answer
was sent only after the prompt appeared. Every step passed:

1. Both boots reported `Caelum d3a65acd4206`.
2. From the empty set 0 with no repositories, the runner added both
   repositories. `pkg install fonts doom-data` with `y` selected set 1.
3. `pkg remove doom-data` answered `n` cancelled with exit status 1, and set 1
   was retained. Answered `y`, it selected set 2 with fonts only.
4. `pkg rollback` selected set 3, restoring doom-data. `pkg rollback 2`
   selected set 4 with fonts only.
5. Each `pkg status` listed the exact set and entries, and each `pkg verify`
   reported the selected files and command index verified.
6. The runner then restarted the machine with `reboot` over Remote.
7. The next boot reported set 0 with zero packages and no repositories. A fresh
   repository configuration and `pkg install doom-data` selected set 1, and
   verify passed.

The pre-reboot session ended with a transport reset at `reboot`, the expected
cut without a final event, as in QEMU. The post-reboot session ended with FINAL
status 0 and a complete drain. The same packet and runner passed twice in
nested QEMU first; those runs only show that the runner and the build agree.

This qualifies RAM-backed package lifetime on a live boot. It does not cover
installed-pool persistence across a reboot. Packet, runner and checksums:
`/shared/present/packages-lifetime-t14/`. The native transcript and raw events
are in `native-20261020a/` inside it. Only public fonts and Doom shareware were
fetched.

## Private game packages

2026-10-20. The personal repository on horse is for the owner's retail game data:
LAN-only and never mirrored. Private recipes and a space-free copy of the Duke
Nukem 3D GRP live in a mode-700 directory on horse, outside any checkout. They
were packed into the personal tree with `tools/pxpkg.py`, and `pxpkg.py check`
passed for all six versions. No game data, recipe or archive is in this
repository, a packet or a log; the hashes below identify them.

| Package | Version | Archive SHA-256 | Identified as |
| --- | --- | --- | --- |
| `doom-data` | `ultimate-1.9` | `140135d7d47980e449e11cf73f8bf3459f5637f9d9965f7558aa1e4e0d76adfb` | The Ultimate Doom 1.9 (`DOOM.WAD` MD5 `c4fe9fd9…`) |
| `quake-data` | `registered-1.06` | `ce6c15c18e8c71e4e2dcc537c44536d3f0778cdf6110dcf7a83327e9f6bb02d8` | Registered `pak0.pak` (MD5 `5906e599…`) and `pak1.pak` (MD5 `d76b3e56…`) |
| `duke3d-data` | `megaton` | `01d6f2e53cb5e7a43f06c1eb3ea85c87c8cff76aed3941a2e77a40e138e0a187` | Megaton Edition `duke3d.grp` (MD5 `22b6938f…`): episodes 1–4, no episode-5 maps |
| `diablo-data` | `shareware` | `f116db0ab9b92e2dbd3b6333244b2f3153c054566fe88d993793aa6394ec8a29` | `spawn.mpq` (MD5 `9d9a4841…`); exact shareware release not identified |

The shareware Doom and Quake versions remain in the same tree. The packer
appends to the index and `pkg install ID` takes the last listed version, so the
retail versions are the defaults. DevilutionX does not yet accept the retail
`DIABDAT.MPQ`, so Diablo stays shareware.

QEMU: Q35/KVM, nested, 4 vCPUs, 4 GiB, VirtIO network/RNG, no audio device. The
image was main `5d13f5d6` with `DEVILUTIONX=1 EDUKE32=1`, on a live boot against
the real personal repository.
- Before installation, `eduke32` and `devilutionx` refused with
  `Missing required resource duke3d-data` and `diablo-data`.
- `pkg install duke3d-data diablo-data doom-data quake-data` selected the four
  versions above, and `pkg verify` passed.
- All six games then started from their packages and returned to the shell:
  - EDuke32 identified the GRP and reached E1L1;
  - DevilutionX showed Diablo Shareware and reached Tristram with a new hero;
  - Doom and Chocolate Doom showed The Ultimate Doom with four episodes and
    reached E1M1;
  - Quake and Chocolate Quake loaded E2M1 from `pak1.pak`.
- The audio initialisation errors were expected without an audio device.
- EDuke32 warns `Invalid user directory specified in cfg file: /`; this was
  noted, not investigated.
- These runs cover start and first-level coverage only, not sound or play
  quality.

The owner-attended vela run sheet is at `/shared/present/vela-game-packages/`.
It is a personal-use program build with no data. Its `install.py` installs the
packages hands-free over Remote and passed against that exact image in QEMU.
Native gameplay results are pending.

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
native play is pending the [private game packages](#private-game-packages) vela
run. The existing horse payload was deliberately not
packed; the fresh package is recorded below. No private data was used or logged,
and task 7/the slice is not complete.

## Hosted toolchain package (2026-10-20)

Producer: main `05efd5b2`, userland `c3be6fe6`, ports `e9b0eb4a`, fs `f3f22b6e`,
lwIP `a1aadb91`; fork `3d69e83`, builder `pyxis-llvm23.1.3-3d69e83`. The owner had
`toolchain/build-hosted.sh` run on horse in that container (command in the
[toolchain README](../../../../toolchain/README.md#native-hosted-payloads)); it took
17 min 30 s and produced `hosted-57dfcf9a33cc…15e8` with a 371,527,680-byte
`payload.tar` (sha256 `ad8344d1…9b43`, verified). Serialized images: Clang
90,410,926, LLD 55,802,178, clangd 44,431,818 bytes.

That build ran the producer without the root `LICENSE`. At pack time the LLVM
license copied from `metadata/LLVM-LICENSE.TXT` and a recipe copy with the
`LICENSE` entry and the payload tree by path were used; the producer now writes
both itself. Packing gave 8,774 files and a 372,762,112-byte archive (sha256
`ef776f7d…7c2e`); `pxpkg.py check` passed, and it was published to horse's
public tree beside `fonts` (index and a range request answered over HTTP,
206). The archive is within the 370 MB the owner accepted.

QEMU: Q35/KVM host CPU, four vCPUs, 4 GiB, OVMF, no GUI, VirtIO network/RNG,
no disk, in the shared nested VM; a locally built `make image` of `05efd5b2`
(unchanged sources), live boot. Driven through `pyxis-remote --machine`.

- `pkg repository add http://192.168.0.213:5245/public/` and `pkg list --available`
  listed `fonts` and `clang-hosted` (372,762,112 bytes). Install, with `y`, made
  set 1. `pkg verify` passed. The install, including download and extraction,
  took 17 min 49 s once; this is a nested VM over user-mode networking and is
  not a rate or native result.
- `which` resolved `clang`, `clang++`, `clang-cpp`, `lld`, `ld.lld` and `clangd`
  to the package's set-1 bundles. `clang`, `lld -flavor gnu` and `clangd` reported
  `23.1.3` at the fork commit.
- `clang -c` plus standalone `lld` produced a P1F that printed its line and
  exited 0; the single-command `clang hello.c -o hello.pxe` did the same. A
  `clang++` `<cstdio>` program ran. A C file with an undeclared name printed the
  diagnostic and the shell reported status 1.
- Headless Neovim with only `vim.lsp.enable("clangd")` and `compile_flags.txt`
  (`-std=c23`, `-Wall`) launched bare `clangd` from the package. First
  diagnostics came 982 ms after the file opened (one sample) and named the
  undeclared identifier at its line and column. Hover on a call showed the
  function signature, go-to-definition gave the right line, and member
  completion after `p.` offered the struct's two fields.
- The VM was shut down with `poweroff`.

Owner-observed on vela, 2026-10-20 about 10:40, live boot with `home://` and
packages on RAM backing (not the installed stick): `pkg install clang-hosted`
from the same public repository (version `57dfcf9a…`, 372,762,112 bytes) took
about 30–45 s of wall clock for download and extraction, by the owner's estimate,
not a timed measurement. The owner then reported on the same boot that clangd
from the package works in Neovim: completion and semantic highlighting were fine.
A live boot has no user `init.lua`, so the owner ran `vim.lsp.enable('clangd')` by
hand first. At about 11:35 the owner also reported that `clang` from the package
compiled a hello-world C program on that boot and the program ran. No latencies
were reported.

Limits: one sample of each time; no interactive Neovim display, semantic-token
or key checks; no removal, rollback or reboot behavior for this package; the only
native results are the owner's install estimate and the clangd and C compile/run
reports above; a native C++ build was not reported. Logs and the driver script are in
`/shared/present/packages/task7-20261020-toolchain/`.

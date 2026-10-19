# Package-consumer qualification

2026-10-19. Nested Q35/KVM host CPU, OVMF, standard VGA 1280×800,
2 GiB, VirtIO network/rng and private virtio-fs. Four vCPUs for live consumers;
two for scratch installed-profile and small-pool checks. Builder
`pyxis-llvm23.1.3-3d69e83`, `make -j8 image BOOT_MENU_TIMEOUT=5`.
No native/PXE action, performance claim, power cut or licensed-data fixture.

## Revisions and baseline

Baseline `2b001c6e` / userland `a9f6c64a` booted and displayed Doom using its
archive data; source inspection at task-4 base `10ee4a78` confirmed those paths.
The attempted baseline Quake launch was not admitted while Doom was foreground,
so no Quake baseline pass is claimed.

The resumed implementation is on main `504821bf`, userland `17a2d2c`, ports
`c8e0efd`, fs `f3f22b6e`; parent `15970ad3` records the published pins. Full
integration builds passed. Kernel-only refresh used verified unchanged SDK,
userland and ports bundles. Earlier optional DevilutionX/EDuke32 recipe builds
applied the unchanged task-4 path patches and built without licensed data;
the resumed build covers the default image. No compiler rebuild or new source
archive was needed.

Rebase corrections gave the new repository-view operation its own number,
retained its confinement across package-command operations, and used the renamed
inode wrapper. The obsolete parked alias runtime was dropped. Quake's new bundle
needed its existing random/read authority for atomic saves; the corrected exact
bundle was rechecked after the image build.

## Observed

- No-disk live startup created empty RAM npfs packages and repository config.
  Add/list/duplicate-add worked; malformed URLs and absent removal refused
  without changing the list. A private DNS fixture mapped `horse` to the test
  repository: both shareware archives (4,200,448 / 18,694,144 bytes) fetched by
  hostname, installed together and passed client verification.
- The Read-only tab listed those repositories, refused addition without writable
  management authority and retained the original list.
- A native caller exercised the actual confined writer: allowed FILE read,
  create/write/sync/remove succeeded; another name, a directory child, an
  out-of-view rename and package-command derivation refused. The existing config
  remained byte-identical; no raw RAM-pool creator was delegated.
- Doom, Chocolate Doom, Quake and Chocolate Quake refused missing required data
  before installation. After installation all four rendered playable shareware
  E1M1 and exited normally. Logs identified `doom_data://` / `quake_data://`.
  Quake initially refused its atomic config save because the bundle omitted
  random/read; the corrected bundle quit without that warning and a normal
  read confirmed `home://quake/id1/config.cfg` was created.
- A small native toolchain-shaped package exported all six declared commands;
  `which` reported the package entry, version, set and real bundle target, and
  each command launched. An already running headless Neovim used its bare
  `clangd` default before and after a package switch. Active conflicting resource
  roots refused; the old buffer stayed pinned after server exit, and wiping it
  allowed a fresh buffer to use the new selection. This used a generated native
  fixture, not the real LLVM payload or an LSP protocol qualification. One first
  fixture used buffer deletion instead of wiping; retained-buffer behavior was
  correct, and the corrected check passed.
- A disposable installed-profile GPT/npfs image retained repository edits over
  an orderly reboot. Its confined-writer checks passed too. Read-only GDB showed
  separate RAM (2 GiB logical) and physical-pool entries. After shutdown the
  extracted scratch pool passed `fsck.npfs`.
- A fresh boot with an 8 MiB logical RAM pool had empty repositories/set 0.
  Installing Doom failed before Review with cleanup unconfirmed; set 0 remained
  active and subsequent status/echo completed. No writable recovery was claimed.

## Limits

The small-pool failure conservatively reported an unconfirmed writer-lock
cleanup. Recovery was not attempted; a live reboot discards the pool. RAM backing
can retain its allocation high-water until reboot and is not a reservation.
Actual ~370 MB LLVM package installation, sustained game/save/load behaviour,
licensed games and native slice qualification remain task 7. Archive removal and
repository deployment remain tasks 5/6. General native build chains retain their
separate nested-execution gate.

Raw logs, private recipes, caller sources and screenshots stay under
`/shared/present/packages/task4-20261019/resumed/`, including `ui/RESULTS.md`,
`install.txt`, `confined-final.txt`, `commands.txt`, `editor-corrected.jsonl`,
`installed/` and `low/RESULTS.md`. Existing pre-resume captures remain separate.

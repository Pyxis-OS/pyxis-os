# Package consumers: task 4 checkpoint

2026-10-19, nested Q35/KVM host CPU, four vCPUs, 2 GiB, OVMF, VirtIO
network/rng, no disk. Existing builder `pyxis-llvm23.1.3-3d69e83`, `make -j8
image BOOT_MENU_TIMEOUT=5`. No native/PXE action, performance claim or power-cut
experiment. Data fixtures contain only the existing Doom/Quake shareware.

## Build and baseline

The saved task-3 baseline (`2b001c6e` plus docs / userland `a9f6c64a`) booted
and displayed Doom from its archive data. Source inspection at task-4 base
`10ee4a78` confirmed those consumer paths remained unchanged; this was a
functional baseline, not a matched timing comparison.

An initial full integration image built, followed by a full image at Pyxis
`73e77846`, userland `fc40d58`, ports `e996dbc`, fs `d2fb97c5`. Both optional
ports, DevilutionX and EDuke32, compiled separately against that SDK using their
ordinary recipe targets, without licensed data inputs or image staging. Their
changed patches applied and DevilutionX assets appeared inside its bundle.
A kernel-only refresh correctly refused mismatched SDK interface provenance;
the subsequent full image build passed instead of bypassing that check.

The source was subsequently rebased onto Pyxis `76094b18` / userland `4e9770b`.
Runtime results below precede that rebase; no exact-rebased runtime pass is
claimed yet. All implementation PRs remain draft for the remaining qualification and the
Neovim package-selection authority decision. The owner accepted package-bundle
aliases on 2026-10-19. The runtime checkpoint at userland `e5965ce` compiles
both changed bundle runtime objects with ordinary compiler warnings enabled;
no alias QEMU result is claimed yet. It captures one selected set, resolves only
the selected catalog alias and adds no source forwarding or new grants.

## Observed QEMU behaviour

- The no-disk live pool initialized and the ordinary shell/network started.
  `pkg status` reported empty set 0. Adding/listing a repository and duplicate-add
  succeeded; a small package installed, selected and verified in RAM.
- After orderly shutdown and another live boot, both the repository list and
  package selection were empty again. The same session then added a repository;
  invalid URL addition and removal of an absent URL refused without changing it.
- In the Read-only tab, listing the shared repository worked and addition
  refused missing writable package-management authority.
- Doom, Chocolate Doom, Quake and Chocolate Quake refused missing required data
  packages, even though the archive still contains its old data until task 5.
- Over the configured network repository, Doom and Quake shareware archives
  (4,200,448 and 18,694,144 bytes) installed together and all selected files
  verified. A new Doom launch reported `doom_data://DOOM.WAD`, kept its existing
  home save location, and rendered its title/demo. This establishes the declared
  package path rather than an archive fallback.

## Remaining coverage

DNS-name fetch, installed persistence, direct negative-name writer-view calls,
capacity/low-memory refusal, the other consumer launches and the rebased final
image are still pending. Two attempted private shell helpers did not establish
a valid caller (stored PXA has no native launch path; the unpacked helper lacked
startup roles), so their subsequent missing-root errors are not confinement
passes. Repository edits did exercise allowed operations through the confined
view; source review separately covered its name/type and asynchronous checks.

Static space roots cannot expose a toolchain installed after live startup.
The owner accepted [per-launch package aliases](../../../wip/packages.md#package-bundle-aliases-accepted-owner-2026-10-19).
Neovim's lack of package selection authority is a separate
[proposed explicit grant](../../../wip/packages.md#neovim-package-selection-authority-proposed),
not an implicit consequence of alias acceptance. The current checkpoint leaves
that grant, shipping alias manifests and Neovim integration unimplemented.
Real compiler/clangd/native slice qualification remains task 7. Raw logs, recipes
and screenshots stay at `/shared/present/packages/task4-20261019/`.

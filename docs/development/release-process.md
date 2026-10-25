# Release process

How a Pyxis release is prepared and checked. The user-facing notes for each
version are in the [release history](../releases.md). chronium signs and pushes
every tag himself; agents prepare the pull request, the candidate checks and the
runbook, and never sign or tag.

## Policy

- **No release branches.** A release ships whatever is on `main` at its tag
  (chronium, 2026-10-23). The tag is on the merge commit of the pull request that
  adds the version's entry to `docs/releases.md` and its name to
  `boot/os-release.lua`.
- **The ABI is fixed at the tag, not before** (chronium, 2026-10-23). Until the
  tag, `include/abi/` changes in place; at the tag it becomes the release's
  contract, and later releases only add to it unless a break is deliberate and
  listed in their notes ([ABI](../interfaces/abi.md)).
- **A tagged fix release** is used only for a blocker in a major release feature
  (chronium, 2026-10-20). A letter release such as 0.0.8a keeps its base
  release's ABI name.

## Preparing the pull request

1. **Write the entry.** Add the new version at the top of
   `docs/releases.md`, in the style of the earlier entries: what changed for a
   user, grouped by theme, then release notes for anything an owner must do.
   Take the content from the pull requests merged since the previous tag in
   pyxis-os, userland and ports; group them rather than listing each.
2. **Check the ABI.** Compare `include/abi/` and the SDK's exported headers with
   the previous tag. Run `scripts/check-abi.sh`: it must report that
   `include/abi/` keeps every line of each listed release. If the public ABI
   changed, say exactly what in the entry and decide the `abi` list with
   chronium; if it did not, the entry says the ABI is unchanged.
3. **Add the ABI name.** Add the version to `boot/os-release.lua` (for example
   `abi = { "0.0.8", "0.0.9" }`), in the same pull request as the entry.
4. **Update the site inputs.** The site renders `docs/releases.md` and
   `docs/manual/` from the selected ref at build time; nothing is copied by
   hand ([site build](../../site/README.md)).

## Candidate check in QEMU

Each release candidate is checked in QEMU first: install the previous release,
keep data in `home://` and a `system://config/boot.lua` override, update, and
boot the result. Launch `nvim` there and quit with `:q`; it exercises child
spawn with extra resources, which the 0.0.6 check missed. A fresh install of the
candidate ISO is checked as well, from the ISO exactly as the
[manual](../manual/install.md) describes.

## Release-tied packages

Before the owner's native update, pack the release-tied packages (the game
packages and `clang-hosted`) from the tagged checkout into horse's trees and
prune other revisions, following
[packing a revision](../../tools/package-repository/README.md#packing-a-revision).
A package built at another revision refuses to install or launch on the release.
A build deployed to the owner's stick between releases is packed the same way,
from that build's checkout, before the owner updates. The owner's stick then
gets the update natively. The stick usually carries a personal build with opt-in
programs such as [DevilutionX](../userland/devilutionx.md); releases describe the
shareable build.

## Signing and publication

The release is signed with the intermediate CA that chronium issued from his
homelab root; the keys stay with him
([signing](../../packages/signing/README.md)). The signed ISO, `SHA256SUMS` and
the repository indexes are uploaded only after his OK for that step; the site is
deployed only after the files it links are live.

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

## Checking the repository trees

Before building a signed release, chronium freezes the last published signed
index from his controlled `public` and `releases` signing trees into the source
snapshot. From the release checkout, run `tools/freeze-repository-floors.py`
with both `--repository NAME https://packages.pyxis.nexus/NAME/ TREE` arguments
and `--write`, review the resulting
`packages/signing/repository-floors.lua`, then repeat with `--check` on the
exact candidate. The tool compares `current`, index, signature and signer bytes
with the published endpoint, checks the signer and signature against the pinned
Software Signing CA, and refuses any disagreement. A network response alone
cannot advance the floor. An ordinary development build uses the last checked-in
snapshot without fetching anything. The source snapshot must be frozen before
the release tag; a missing or failed check stops the signed release build.

Record the absolute `TREE` paths in each release's signing/publish record when
chronium runs `tools/sign-repository.sh` and the upload. For 0.0.9, chronium
confirmed that the signed and uploaded trees are
`~/shared/present/release-0.0.9/staging/{public,releases}` on horse
(`/shared/present/release-0.0.9/staging/{public,releases}` on the VMs), with
published `current` serial 2. This location is evidence for that publish, not
a default location to infer for later releases.

Before signing, check each signing tree (seeded from the published tree, then
packed) in three ways. `check --abi` alone is not enough, and `check --abi
<previous>` must not be run: a release adds packages that have no build for the
previous ABI (25 in 0.0.9), and clients refuse those per package.

1. **The new ABI is covered.** Every bound package ID has a build for the release
   being cut, so nothing release-tied was left unpacked:

   ```sh
   python3 tools/pxpkg.py check "$OUT/public" --abi 0.0.9
   ```

2. **The previous release's builds are preserved.** Systems still running the
   previous release keep reading their own builds from the same tree, so every
   entry already published must still be in the working index, byte for byte
   (the packer never replaces a published ID and version, and `prune` is not run
   for a release). Run this from the repository root; it compares the published
   snapshot, `index/N.lua` for the serial in `current`, with the working
   `index.lua`:

   ```sh
   python3 - "$OUT/public" <<'EOF'
   import sys
   from pathlib import Path
   sys.path.insert(0, "tools")
   import pxpkg

   root = Path(sys.argv[1])
   serial = pxpkg.read_current(root)
   if not serial:
       sys.exit("check-preserved: no published serial in " + str(root))
   _, published = pxpkg.load_snapshot(root, serial)
   working = {(e["id"], e["version"], e.get("abi")): e for e in pxpkg.load_index(root)}
   problems = []
   for entry in published:
       key = (entry["id"], entry["version"], entry.get("abi"))
       if working.get(key) != entry:
           problems.append("%s@%s %s: %s" % (key + ("missing" if key not in working else "changed",)))
   for line in problems:
       print("check-preserved:", line, file=sys.stderr)
   print("check-preserved: serial %d lists %d entries; %d preserved, %d added, %d problems"
         % (serial, len(published), len(published) - len(problems),
            len(working) - len(published) + len(problems), len(problems)))
   sys.exit(1 if problems else 0)
   EOF
   ```

   It covers every published entry, which includes the previous ABI's builds. A
   `missing` or `changed` line stops the release: something pruned or rebuilt a
   published entry.
3. **The tree is sound.** Every archive matches the index and nothing unlisted is
   present:

   ```sh
   python3 tools/pxpkg.py check "$OUT/public"
   ```

Run the same three on the `releases` tree, where the new release's package is
the one added entry and `--abi` does not apply (a `release` entry has no `abi`).

## Signing and publication

The release is signed with the intermediate CA that chronium issued from his
homelab root; the keys stay with him
([signing](../../packages/signing/README.md)). The signed ISO, `SHA256SUMS` and
the repository indexes are uploaded only after his OK for that step; the site is
deployed only after the files it links are live.

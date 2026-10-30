-- The release package (docs/wip/pkg-upgrade.md, decision 73): a tagged build's
-- kernel, core archive and os-release, byte-for-byte. make release-package packs
-- it; its version is the release tag and its index entry provides the build's
-- ABI list. This first release with checkpoint routing stays "pyxis": the
-- shipped 0.0.10 client must always land here. After this bootstrap ships,
-- ordinary releases use "pyxis-release", required checkpoints "pyxis-checkpoint".
-- Publication order defines the route; never append another "pyxis" release.
return {
  id = "pyxis",
  kind = "release",
  depends = {},
  entries = {
    { build = "caelum.elf", at = "caelum.elf" },
    { build = "initrd.cpio", at = "initrd.cpio" },
    { build = "os-release", at = "os-release" },
  },
}

-- Optional native Rust byte viewer; not part of the base or archive.
return {
  id = "hexyl",
  variant = "0.17.0",
  kind = "program",
  depends = {},
  commands = { hexyl = { path = "hexyl.pxe" } },
  entries = {
    { build = "ports/hexyl/stage/bin/hexyl.pxe", at = "hexyl.pxe" },
    { build = "ports/hexyl/stage/share/hexyl", at = "share/hexyl" },
    { build = "ports/hexyl/stage/share/licenses/hexyl", at = "licenses/hexyl" },
    { tree = "../ports/hexyl", at = "share/sources/pyxis-hexyl" },
    { file = "../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}

-- screenshot-saver from the Pyxis userland.
return {
  id = "screenshot-saver",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["screenshot-saver"] = { path = "screenshot-saver.pxe" } },
  entries = {
    { build = "userspace-root/screenshot-saver.pxe", at = "screenshot-saver.pxe" },
    { build = "ports/libpng/stage/share/licenses/libpng", at = "licenses/libpng" },
    { build = "ports/zlib/stage/share/licenses/zlib", at = "licenses/zlib" },
  },
}
